#include<stdio.h>
#include<string.h>
#include<stdlib.h>

int events = 0;
int read_disk = 0;
int write_disk = 0;

FILE *trace = NULL;   //trace file FILE pointer
int pagesize = 0;	  
int numpages = 0;
char *algorithm = NULL;
int interval = 0;	 //for ARB

struct Trace{
	enum {R, W}operation;
	unsigned long long addr;
};

struct PageFrame{
	int valid;				//Is this page a valid page(in memory)
	int pageno;				//If valid, this is the vitual page number.
	int reference;			//reference bit
	unsigned char r;		//reference byte for ARB
	int dirty;				//modified bit
	int time;				//age, use this to implemented the FIFO for ARB
};

void simulate();
int replacement(struct PageFrame *);
int SC(struct PageFrame *);
int ESC(struct PageFrame *);
int ARB(struct PageFrame *);

int main(int argc, char **argv)
{
	trace = fopen(argv[1], "rt");
	pagesize = atoi(argv[2]);
	numpages = atoi(argv[3]);
	algorithm = argv[4];
	if(strcmp(argv[4], "ARB") == 0)
		interval = atoi(argv[5]);
	simulate();
	printf("events in trace:     %d\n", events);
	printf("total disk reads:    %d\n", read_disk);
	printf("total disk writes:   %d\n", write_disk);
}

//read next trace record from trace file, and parse it's operation and address, 
//return a Trace struct pointer. If no more(end of file occured), return NULL
struct Trace* nextLine()
{
	char buf[40];
	while(fgets(buf, 40, trace) != NULL)
	{
		if(strlen(buf) == 0 || buf[0] == '#')
			continue;
		struct Trace* t = (struct Trace*)malloc(sizeof(struct Trace));
		t->operation = (buf[0] == 'R') ? R: W;
		char *p = strchr(buf, '\n');
		if(p == NULL)
			p = buf + strlen(buf) - 1;
		else
			p = buf + strlen(buf) - 2;
		t->addr = strtoull(buf+2, &p, 16);
		return t;
	}
	return NULL;
}

//allocate space for page frame, and initialise all the page frame to unused state.
struct PageFrame* initPageFrames()
{
	struct PageFrame* frames = (struct PageFrame*)malloc(numpages * sizeof(struct PageFrame));
	for(int i = 0; i < numpages; i++){
		frames[i].valid = 0;
		frames[i].dirty = 0;
		frames[i].r = 0;
		frames[i].reference = 0;
		frames[i].time = 1;
	}
	return frames;
}

void simulate()
{
	struct Trace *t;
	struct PageFrame* frames = initPageFrames(numpages);
	int next = 0;
	int count = 0;
	while((t = nextLine()) != NULL)
	{
		//for ARB, if the clock tick is the start of one interval, then right shift
		//the reference byte for all page frames.
		if(strcmp(algorithm, "ARB") == 0)
		{
			if(events % interval == 0)
			{
				for(int i = 0; i < numpages; i++)
					frames[i].r = (frames[i].r >> 1);
			}
		}
		events++;

		//calculate the page number
		int page = t->addr / pagesize;
		int index = 0;

		//The age for all page frames should increment by one
		for(int i = 0; i < numpages; i++)
			frames[i].time++;
		//check whether this page already in memory, if does, update it's reference bit
		//and reference byte(for ARB)
		for(; index < numpages; index++)
			if(frames[index].valid == 1 && frames[index].pageno == page){
				frames[index].reference = 1;
				frames[index].r |= (1 << 7);
				break;
			}
		if(index >= numpages){
			//If it's not in memory, means we need to read it from disk, no matter 
			//whether need to replace another page
			read_disk++;
			//find a page frame which is not used(valid bit is zero), and update it's state
			for(index = 0; index < numpages; index++)
				if(frames[index].valid == 0)
				{
					frames[index].valid = 1;
					frames[index].reference = 1;
					frames[index].time = 1;
					frames[index].r = (1 << 7);
					frames[index].pageno = page;
					break;
				}
			if(index >= numpages){
				//use diffrent replacement algorithm to select a frame to replace.
				index = replacement(frames);

				//If the modified bit is 1, then we need write it back to disk
				if(frames[index].dirty == 1)
				{
					frames[index].dirty = 0;
					write_disk++;
				}
				frames[index].time = 1;
				frames[index].reference = 1;
				frames[index].r = (1 << 7);
				frames[index].pageno = page;
			}
		}
		if(t->operation == W)
			frames[index].dirty = 1;
	}
}

int replacement(struct PageFrame* frames)
{
	if(strcmp(algorithm, "SC") == 0)
		return SC(frames);
	else if(strcmp(algorithm, "ESC") == 0)
		return ESC(frames);
	else if(strcmp(algorithm, "ARB") == 0)
		return ARB(frames);
}

//Just decides which page will be exchanged.
int SC(struct PageFrame* frames)
{
	static int next = 0;
	int index = next;
	for(int i = 0; i < numpages; i++)
	{
		if(frames[index].reference == 1)
			frames[index].reference = 0;
		else
			break;
		index = (index + 1) % numpages;
	}
	if(frames[index].dirty == 1){
		write_disk++;
		frames[index].dirty = 0;
	}
	next = (index+1)%numpages;
	return index;
}

int ESC(struct PageFrame* f)
{
	static int next = 0;
	int index = next;
	for(int i = 0; i < numpages; i++){
		if(f[index].reference == 0 && f[index].dirty == 0){
			next = (index + 1) % numpages;
			return index;
		}
		index = (index + 1)%numpages;
	}
	for(int i = 0; i < numpages; i++){
		if(f[index].reference == 0 && f[index].dirty == 1){
			next = (index + 1)%numpages;
			return index;
		}
		f[index].reference = 0;
		index = (index + 1) % numpages;
	}
	for(int i = 0; i < numpages; i++){
		if(f[index].reference == 0 && f[index].dirty == 0){
			next = (index + 1) % numpages;
			return index;
		}
		index = (index + 1) % numpages;
	}
	return index;
}

//find a page frame with the minimum reference byte. If the reference byte is 
//same, then choose one with the maximum time
int ARB(struct PageFrame* f)
{
	int min = f[0].r;
	int index = 0;
	for(int i = 1; i < numpages; i++)
	{
		if(f[i].r < min)
		{
			min = f[i].r;
			index = i;
		}
		else if(f[i].r == min)
		{
			if(f[i].time > f[index].time)
			{
				min = f[i].r;
				index = i;
			}
		}
	}
	return index;
}
