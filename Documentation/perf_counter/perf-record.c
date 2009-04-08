

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <getopt.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sched.h>
#include <pthread.h>

#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/mman.h>

#include <linux/unistd.h>
#include <linux/types.h>

#include "../../include/linux/perf_counter.h"


/*
 * prctl(PR_TASK_PERF_COUNTERS_DISABLE) will (cheaply) disable all
 * counters in the current task.
 */
#define PR_TASK_PERF_COUNTERS_DISABLE   31
#define PR_TASK_PERF_COUNTERS_ENABLE    32

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define rdclock()                                       \
({                                                      \
        struct timespec ts;                             \
                                                        \
        clock_gettime(CLOCK_MONOTONIC, &ts);            \
        ts.tv_sec * 1000000000ULL + ts.tv_nsec;         \
})

/*
 * Pick up some kernel type conventions:
 */
#define __user
#define asmlinkage

#ifdef __x86_64__
#define __NR_perf_counter_open 295
#define rmb()		asm volatile("lfence" ::: "memory")
#define cpu_relax()	asm volatile("rep; nop" ::: "memory");
#endif

#ifdef __i386__
#define __NR_perf_counter_open 333
#define rmb()		asm volatile("lfence" ::: "memory")
#define cpu_relax()	asm volatile("rep; nop" ::: "memory");
#endif

#ifdef __powerpc__
#define __NR_perf_counter_open 319
#define rmb() 		asm volatile ("sync" ::: "memory")
#define cpu_relax()	asm volatile ("" ::: "memory");
#endif

#define unlikely(x)	__builtin_expect(!!(x), 0)
#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })

asmlinkage int sys_perf_counter_open(
        struct perf_counter_hw_event    *hw_event_uptr          __user,
        pid_t                           pid,
        int                             cpu,
        int                             group_fd,
        unsigned long                   flags)
{
        return syscall(
                __NR_perf_counter_open, hw_event_uptr, pid, cpu, group_fd, flags);
}

#define MAX_COUNTERS			64
#define MAX_NR_CPUS			256

#define EID(type, id) (((__u64)(type) << PERF_COUNTER_TYPE_SHIFT) | (id))

static int			nr_counters			=  0;
static __u64			event_id[MAX_COUNTERS]		= { };
static int			default_interval = 100000;
static int			event_count[MAX_COUNTERS];
static int			fd[MAX_NR_CPUS][MAX_COUNTERS];
static int			nr_cpus				=  0;
static unsigned int		page_size;
static unsigned int		mmap_pages			= 16;
static int			output;
static char 			*output_name			= "output.perf";
static int			group				= 0;
static unsigned int		realtime_prio			=  0;

const unsigned int default_count[] = {
	1000000,
	1000000,
	  10000,
	  10000,
	1000000,
	  10000,
};

static char *hw_event_names[] = {
	"CPU cycles",
	"instructions",
	"cache references",
	"cache misses",
	"branches",
	"branch misses",
	"bus cycles",
};

static char *sw_event_names[] = {
	"cpu clock ticks",
	"task clock ticks",
	"pagefaults",
	"context switches",
	"CPU migrations",
	"minor faults",
	"major faults",
};

struct event_symbol {
	__u64 event;
	char *symbol;
};

static struct event_symbol event_symbols[] = {
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_CPU_CYCLES),		"cpu-cycles",		},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_CPU_CYCLES),		"cycles",		},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_INSTRUCTIONS),		"instructions",		},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_CACHE_REFERENCES),		"cache-references",	},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_CACHE_MISSES),		"cache-misses",		},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_BRANCH_INSTRUCTIONS),	"branch-instructions",	},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_BRANCH_INSTRUCTIONS),	"branches",		},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_BRANCH_MISSES),		"branch-misses",	},
	{EID(PERF_TYPE_HARDWARE, PERF_COUNT_BUS_CYCLES),		"bus-cycles",		},

	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_CPU_CLOCK),			"cpu-clock",		},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_TASK_CLOCK),		"task-clock",		},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_PAGE_FAULTS),		"page-faults",		},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_PAGE_FAULTS),		"faults",		},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_PAGE_FAULTS_MIN),		"minor-faults",		},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_PAGE_FAULTS_MAJ),		"major-faults",		},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_CONTEXT_SWITCHES),		"context-switches",	},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_CONTEXT_SWITCHES),		"cs",			},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_CPU_MIGRATIONS),		"cpu-migrations",	},
	{EID(PERF_TYPE_SOFTWARE, PERF_COUNT_CPU_MIGRATIONS),		"migrations",		},
};

/*
 * Each event can have multiple symbolic names.
 * Symbolic names are (almost) exactly matched.
 */
static __u64 match_event_symbols(char *str)
{
	__u64 config, id;
	int type;
	unsigned int i;

	if (sscanf(str, "r%llx", &config) == 1)
		return config | PERF_COUNTER_RAW_MASK;

	if (sscanf(str, "%d:%llu", &type, &id) == 2)
		return EID(type, id);

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++) {
		if (!strncmp(str, event_symbols[i].symbol,
			     strlen(event_symbols[i].symbol)))
			return event_symbols[i].event;
	}

	return ~0ULL;
}

static int parse_events(char *str)
{
	__u64 config;

again:
	if (nr_counters == MAX_COUNTERS)
		return -1;

	config = match_event_symbols(str);
	if (config == ~0ULL)
		return -1;

	event_id[nr_counters] = config;
	nr_counters++;

	str = strstr(str, ",");
	if (str) {
		str++;
		goto again;
	}

	return 0;
}

#define __PERF_COUNTER_FIELD(config, name) \
	((config & PERF_COUNTER_##name##_MASK) >> PERF_COUNTER_##name##_SHIFT)

#define PERF_COUNTER_RAW(config)	__PERF_COUNTER_FIELD(config, RAW)
#define PERF_COUNTER_CONFIG(config)	__PERF_COUNTER_FIELD(config, CONFIG)
#define PERF_COUNTER_TYPE(config)	__PERF_COUNTER_FIELD(config, TYPE)
#define PERF_COUNTER_ID(config)		__PERF_COUNTER_FIELD(config, EVENT)

static void display_events_help(void)
{
	unsigned int i;
	__u64 e;

	printf(
	" -e EVENT     --event=EVENT   #  symbolic-name        abbreviations");

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++) {
		int type, id;

		e = event_symbols[i].event;
		type = PERF_COUNTER_TYPE(e);
		id = PERF_COUNTER_ID(e);

		printf("\n                             %d:%d: %-20s",
				type, id, event_symbols[i].symbol);
	}

	printf("\n"
	"                           rNNN: raw PMU events (eventsel+umask)\n\n");
}

static void display_help(void)
{
	printf(
	"Usage: perf-record [<options>]\n"
	"perf-record Options (up to %d event types can be specified at once):\n\n",
		 MAX_COUNTERS);

	display_events_help();

	printf(
	" -c CNT    --count=CNT          # event period to sample\n"
	" -m pages  --mmap_pages=<pages> # number of mmap data pages\n"
	" -o file   --output=<file>      # output file\n"
	" -r prio   --realtime=<prio>    # use RT prio\n"
	);

	exit(0);
}

static void process_options(int argc, char *argv[])
{
	int error = 0, counter;

	for (;;) {
		int option_index = 0;
		/** Options for getopt */
		static struct option long_options[] = {
			{"count",	required_argument,	NULL, 'c'},
			{"event",	required_argument,	NULL, 'e'},
			{"mmap_pages",	required_argument,	NULL, 'm'},
			{"output",	required_argument,	NULL, 'o'},
			{"realtime",	required_argument,	NULL, 'r'},
			{NULL,		0,			NULL,  0 }
		};
		int c = getopt_long(argc, argv, "+:c:e:m:o:r:",
				    long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'c': default_interval		=   atoi(optarg); break;
		case 'e': error				= parse_events(optarg); break;
		case 'm': mmap_pages			=   atoi(optarg); break;
		case 'o': output_name			= strdup(optarg); break;
		case 'r': realtime_prio			=   atoi(optarg); break;
		default: error = 1; break;
		}
	}
	if (error)
		display_help();

	if (!nr_counters) {
		nr_counters = 1;
		event_id[0] = 0;
	}

	for (counter = 0; counter < nr_counters; counter++) {
		if (event_count[counter])
			continue;

		event_count[counter] = default_interval;
	}
}

struct mmap_data {
	int counter;
	void *base;
	unsigned int mask;
	unsigned int prev;
};

static unsigned int mmap_read_head(struct mmap_data *md)
{
	struct perf_counter_mmap_page *pc = md->base;
	int head;

	head = pc->data_head;
	rmb();

	return head;
}

static long events;
static struct timeval last_read, this_read;

static void mmap_read(struct mmap_data *md)
{
	unsigned int head = mmap_read_head(md);
	unsigned int old = md->prev;
	unsigned char *data = md->base + page_size;
	unsigned long size;
	void *buf;
	int diff;

	gettimeofday(&this_read, NULL);

	/*
	 * If we're further behind than half the buffer, there's a chance
	 * the writer will bite our tail and screw up the events under us.
	 *
	 * If we somehow ended up ahead of the head, we got messed up.
	 *
	 * In either case, truncate and restart at head.
	 */
	diff = head - old;
	if (diff > md->mask / 2 || diff < 0) {
		struct timeval iv;
		unsigned long msecs;

		timersub(&this_read, &last_read, &iv);
		msecs = iv.tv_sec*1000 + iv.tv_usec/1000;

		fprintf(stderr, "WARNING: failed to keep up with mmap data."
				"  Last read %lu msecs ago.\n", msecs);

		/*
		 * head points to a known good entry, start there.
		 */
		old = head;
	}

	last_read = this_read;

	if (old != head)
		events++;

	size = head - old;

	if ((old & md->mask) + size != (head & md->mask)) {
		buf = &data[old & md->mask];
		size = md->mask + 1 - (old & md->mask);
		old += size;
		while (size) {
			int ret = write(output, buf, size);
			if (ret < 0) {
				perror("failed to write");
				exit(-1);
			}
			size -= ret;
			buf += ret;
		}
	}

	buf = &data[old & md->mask];
	size = head - old;
	old += size;
	while (size) {
		int ret = write(output, buf, size);
		if (ret < 0) {
			perror("failed to write");
			exit(-1);
		}
		size -= ret;
		buf += ret;
	}

	md->prev = old;
}

static volatile int done = 0;

static void sigchld_handler(int sig)
{
	if (sig == SIGCHLD)
		done = 1;
}

int main(int argc, char *argv[])
{
	struct pollfd event_array[MAX_NR_CPUS * MAX_COUNTERS];
	struct mmap_data mmap_array[MAX_NR_CPUS][MAX_COUNTERS];
	struct perf_counter_hw_event hw_event;
	int i, counter, group_fd, nr_poll = 0;
	pid_t pid;
	int ret;

	page_size = sysconf(_SC_PAGE_SIZE);

	process_options(argc, argv);

	nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	assert(nr_cpus <= MAX_NR_CPUS);
	assert(nr_cpus >= 0);

	output = open(output_name, O_CREAT|O_RDWR, S_IRWXU);
	if (output < 0) {
		perror("failed to create output file");
		exit(-1);
	}

	argc -= optind;
	argv += optind;

	for (i = 0; i < nr_cpus; i++) {
		group_fd = -1;
		for (counter = 0; counter < nr_counters; counter++) {

			memset(&hw_event, 0, sizeof(hw_event));
			hw_event.config		= event_id[counter];
			hw_event.irq_period	= event_count[counter];
			hw_event.record_type	= PERF_RECORD_IP | PERF_RECORD_TID;
			hw_event.nmi		= 1;
			hw_event.mmap		= 1;
			hw_event.comm		= 1;

			fd[i][counter] = sys_perf_counter_open(&hw_event, -1, i, group_fd, 0);
			if (fd[i][counter] < 0) {
				int err = errno;
				printf("kerneltop error: syscall returned with %d (%s)\n",
					fd[i][counter], strerror(err));
				if (err == EPERM)
					printf("Are you root?\n");
				exit(-1);
			}
			assert(fd[i][counter] >= 0);
			fcntl(fd[i][counter], F_SETFL, O_NONBLOCK);

			/*
			 * First counter acts as the group leader:
			 */
			if (group && group_fd == -1)
				group_fd = fd[i][counter];

			event_array[nr_poll].fd = fd[i][counter];
			event_array[nr_poll].events = POLLIN;
			nr_poll++;

			mmap_array[i][counter].counter = counter;
			mmap_array[i][counter].prev = 0;
			mmap_array[i][counter].mask = mmap_pages*page_size - 1;
			mmap_array[i][counter].base = mmap(NULL, (mmap_pages+1)*page_size,
					PROT_READ, MAP_SHARED, fd[i][counter], 0);
			if (mmap_array[i][counter].base == MAP_FAILED) {
				printf("kerneltop error: failed to mmap with %d (%s)\n",
						errno, strerror(errno));
				exit(-1);
			}
		}
	}

	signal(SIGCHLD, sigchld_handler);

	pid = fork();
	if (pid < 0)
		perror("failed to fork");

	if (!pid) {
		if (execvp(argv[0], argv)) {
			perror(argv[0]);
			exit(-1);
		}
	}

	if (realtime_prio) {
		struct sched_param param;

		param.sched_priority = realtime_prio;
		if (sched_setscheduler(0, SCHED_FIFO, &param)) {
			printf("Could not set realtime priority.\n");
			exit(-1);
		}
	}

	/*
	 * TODO: store the current /proc/$/maps information somewhere
	 */

	while (!done) {
		int hits = events;

		for (i = 0; i < nr_cpus; i++) {
			for (counter = 0; counter < nr_counters; counter++)
				mmap_read(&mmap_array[i][counter]);
		}

		if (hits == events)
			ret = poll(event_array, nr_poll, 100);
	}

	return 0;
}
