/*
 * kerneltop.c: show top kernel functions - performance counters showcase

   Build with:

     cc -O6 -Wall -c -o kerneltop.o kerneltop.c -lrt

   Sample output:

------------------------------------------------------------------------------
 KernelTop:    2669 irqs/sec  [NMI, cache-misses/cache-refs],  (all, cpu: 2)
------------------------------------------------------------------------------

             weight         RIP          kernel function
             ______   ________________   _______________

              35.20 - ffffffff804ce74b : skb_copy_and_csum_dev
              33.00 - ffffffff804cb740 : sock_alloc_send_skb
              31.26 - ffffffff804ce808 : skb_push
              22.43 - ffffffff80510004 : tcp_established_options
              19.00 - ffffffff8027d250 : find_get_page
              15.76 - ffffffff804e4fc9 : eth_type_trans
              15.20 - ffffffff804d8baa : dst_release
              14.86 - ffffffff804cf5d8 : skb_release_head_state
              14.00 - ffffffff802217d5 : read_hpet
              12.00 - ffffffff804ffb7f : __ip_local_out
              11.97 - ffffffff804fc0c8 : ip_local_deliver_finish
               8.54 - ffffffff805001a3 : ip_queue_xmit
 */

/*
 * perfstat:  /usr/bin/time -alike performance counter statistics utility

          It summarizes the counter events of all tasks (and child tasks),
          covering all CPUs that the command (or workload) executes on.
          It only counts the per-task events of the workload started,
          independent of how many other tasks run on those CPUs.

   Sample output:

   $ ./perfstat -e 1 -e 3 -e 5 ls -lR /usr/include/ >/dev/null

   Performance counter stats for 'ls':

           163516953 instructions
                2295 cache-misses
             2855182 branch-misses
 */

 /*
  * Copyright (C) 2008, Red Hat Inc, Ingo Molnar <mingo@redhat.com>
  *
  * Improvements and fixes by:
  *
  *   Arjan van de Ven <arjan@linux.intel.com>
  *   Yanmin Zhang <yanmin.zhang@intel.com>
  *   Wu Fengguang <fengguang.wu@intel.com>
  *   Mike Galbraith <efault@gmx.de>
  *   Paul Mackerras <paulus@samba.org>
  *
  * Released under the GPL v2. (and only v2, not any later version)
  */

#include "util.h"

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

extern asmlinkage int sys_perf_counter_open(
        struct perf_counter_hw_event    *hw_event_uptr          __user,
        pid_t                           pid,
        int                             cpu,
        int                             group_fd,
        unsigned long                   flags);

#define MAX_COUNTERS			64
#define MAX_NR_CPUS			256

#define EID(type, id) (((__u64)(type) << PERF_COUNTER_TYPE_SHIFT) | (id))

static int			system_wide			=  0;

static int			nr_counters			=  0;
static __u64			event_id[MAX_COUNTERS]		= {
	EID(PERF_TYPE_SOFTWARE, PERF_COUNT_TASK_CLOCK),
	EID(PERF_TYPE_SOFTWARE, PERF_COUNT_CONTEXT_SWITCHES),
	EID(PERF_TYPE_SOFTWARE, PERF_COUNT_CPU_MIGRATIONS),
	EID(PERF_TYPE_SOFTWARE, PERF_COUNT_PAGE_FAULTS),

	EID(PERF_TYPE_HARDWARE, PERF_COUNT_CPU_CYCLES),
	EID(PERF_TYPE_HARDWARE, PERF_COUNT_INSTRUCTIONS),
	EID(PERF_TYPE_HARDWARE, PERF_COUNT_CACHE_REFERENCES),
	EID(PERF_TYPE_HARDWARE, PERF_COUNT_CACHE_MISSES),
};
static int			default_interval = 100000;
static int			event_count[MAX_COUNTERS];
static int			fd[MAX_NR_CPUS][MAX_COUNTERS];

static int			tid				= -1;
static int			profile_cpu			= -1;
static int			nr_cpus				=  0;
static int			nmi				=  1;
static int			group				=  0;
static unsigned int		page_size;

static int			zero;

static int			scale;

static const unsigned int default_count[] = {
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
	"Usage: perfstat [<events...>] <cmd...>\n\n"
	"PerfStat Options (up to %d event types can be specified):\n\n",
		 MAX_COUNTERS);

	display_events_help();

	printf(
	" -l                           # scale counter values\n"
	" -a                           # system-wide collection\n");
	exit(0);
}

static char *event_name(int ctr)
{
	__u64 config = event_id[ctr];
	int type = PERF_COUNTER_TYPE(config);
	int id = PERF_COUNTER_ID(config);
	static char buf[32];

	if (PERF_COUNTER_RAW(config)) {
		sprintf(buf, "raw 0x%llx", PERF_COUNTER_CONFIG(config));
		return buf;
	}

	switch (type) {
	case PERF_TYPE_HARDWARE:
		if (id < PERF_HW_EVENTS_MAX)
			return hw_event_names[id];
		return "unknown-hardware";

	case PERF_TYPE_SOFTWARE:
		if (id < PERF_SW_EVENTS_MAX)
			return sw_event_names[id];
		return "unknown-software";

	default:
		break;
	}

	return "unknown";
}

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


/*
 * perfstat
 */

char fault_here[1000000];

static void create_perfstat_counter(int counter)
{
	struct perf_counter_hw_event hw_event;

	memset(&hw_event, 0, sizeof(hw_event));
	hw_event.config		= event_id[counter];
	hw_event.record_type	= 0;
	hw_event.nmi		= 0;
	if (scale)
		hw_event.read_format	= PERF_FORMAT_TOTAL_TIME_ENABLED |
					  PERF_FORMAT_TOTAL_TIME_RUNNING;

	if (system_wide) {
		int cpu;
		for (cpu = 0; cpu < nr_cpus; cpu ++) {
			fd[cpu][counter] = sys_perf_counter_open(&hw_event, -1, cpu, -1, 0);
			if (fd[cpu][counter] < 0) {
				printf("perfstat error: syscall returned with %d (%s)\n",
						fd[cpu][counter], strerror(errno));
				exit(-1);
			}
		}
	} else {
		hw_event.inherit	= 1;
		hw_event.disabled	= 1;

		fd[0][counter] = sys_perf_counter_open(&hw_event, 0, -1, -1, 0);
		if (fd[0][counter] < 0) {
			printf("perfstat error: syscall returned with %d (%s)\n",
					fd[0][counter], strerror(errno));
			exit(-1);
		}
	}
}

int do_perfstat(int argc, char *argv[])
{
	unsigned long long t0, t1;
	int counter;
	ssize_t res;
	int status;
	int pid;

	if (!system_wide)
		nr_cpus = 1;

	for (counter = 0; counter < nr_counters; counter++)
		create_perfstat_counter(counter);

	argc -= optind;
	argv += optind;

	if (!argc)
		display_help();

	/*
	 * Enable counters and exec the command:
	 */
	t0 = rdclock();
	prctl(PR_TASK_PERF_COUNTERS_ENABLE);

	if ((pid = fork()) < 0)
		perror("failed to fork");
	if (!pid) {
		if (execvp(argv[0], argv)) {
			perror(argv[0]);
			exit(-1);
		}
	}
	while (wait(&status) >= 0)
		;
	prctl(PR_TASK_PERF_COUNTERS_DISABLE);
	t1 = rdclock();

	fflush(stdout);

	fprintf(stderr, "\n");
	fprintf(stderr, " Performance counter stats for \'%s\':\n",
		argv[0]);
	fprintf(stderr, "\n");

	for (counter = 0; counter < nr_counters; counter++) {
		int cpu, nv;
		__u64 count[3], single_count[3];
		int scaled;

		count[0] = count[1] = count[2] = 0;
		nv = scale ? 3 : 1;
		for (cpu = 0; cpu < nr_cpus; cpu ++) {
			res = read(fd[cpu][counter],
				   single_count, nv * sizeof(__u64));
			assert(res == nv * sizeof(__u64));

			count[0] += single_count[0];
			if (scale) {
				count[1] += single_count[1];
				count[2] += single_count[2];
			}
		}

		scaled = 0;
		if (scale) {
			if (count[2] == 0) {
				fprintf(stderr, " %14s  %-20s\n",
					"<not counted>", event_name(counter));
				continue;
			}
			if (count[2] < count[1]) {
				scaled = 1;
				count[0] = (unsigned long long)
					((double)count[0] * count[1] / count[2] + 0.5);
			}
		}

		if (event_id[counter] == EID(PERF_TYPE_SOFTWARE, PERF_COUNT_CPU_CLOCK) ||
		    event_id[counter] == EID(PERF_TYPE_SOFTWARE, PERF_COUNT_TASK_CLOCK)) {

			double msecs = (double)count[0] / 1000000;

			fprintf(stderr, " %14.6f  %-20s (msecs)",
				msecs, event_name(counter));
		} else {
			fprintf(stderr, " %14Ld  %-20s (events)",
				count[0], event_name(counter));
		}
		if (scaled)
			fprintf(stderr, "  (scaled from %.2f%%)",
				(double) count[2] / count[1] * 100);
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "\n");
	fprintf(stderr, " Wall-clock time elapsed: %12.6f msecs\n",
			(double)(t1-t0)/1e6);
	fprintf(stderr, "\n");

	return 0;
}

static void process_options(int argc, char **argv)
{
	int error = 0, counter;

	for (;;) {
		int option_index = 0;
		/** Options for getopt */
		static struct option long_options[] = {
			{"count",	required_argument,	NULL, 'c'},
			{"cpu",		required_argument,	NULL, 'C'},
			{"delay",	required_argument,	NULL, 'd'},
			{"dump_symtab",	no_argument,		NULL, 'D'},
			{"event",	required_argument,	NULL, 'e'},
			{"filter",	required_argument,	NULL, 'f'},
			{"group",	required_argument,	NULL, 'g'},
			{"help",	no_argument,		NULL, 'h'},
			{"nmi",		required_argument,	NULL, 'n'},
			{"munmap_info",	no_argument,		NULL, 'U'},
			{"pid",		required_argument,	NULL, 'p'},
			{"realtime",	required_argument,	NULL, 'r'},
			{"scale",	no_argument,		NULL, 'l'},
			{"symbol",	required_argument,	NULL, 's'},
			{"stat",	no_argument,		NULL, 'S'},
			{"vmlinux",	required_argument,	NULL, 'x'},
			{"zero",	no_argument,		NULL, 'z'},
			{NULL,		0,			NULL,  0 }
		};
		int c = getopt_long(argc, argv, "+:ac:C:d:De:f:g:hln:m:p:r:s:Sx:zMU",
				    long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'a': system_wide			=	       1; break;
		case 'c': default_interval		=   atoi(optarg); break;
		case 'C':
			/* CPU and PID are mutually exclusive */
			if (tid != -1) {
				printf("WARNING: CPU switch overriding PID\n");
				sleep(1);
				tid = -1;
			}
			profile_cpu			=   atoi(optarg); break;

		case 'e': error				= parse_events(optarg); break;

		case 'g': group				=   atoi(optarg); break;
		case 'h':      				  display_help(); break;
		case 'l': scale				=	       1; break;
		case 'n': nmi				=   atoi(optarg); break;
		case 'p':
			/* CPU and PID are mutually exclusive */
			if (profile_cpu != -1) {
				printf("WARNING: PID switch overriding CPU\n");
				sleep(1);
				profile_cpu = -1;
			}
			tid				=   atoi(optarg); break;
		case 'z': zero				=              1; break;
		default: error = 1; break;
		}
	}
	if (error)
		display_help();

	if (!nr_counters) {
		nr_counters = 8;
	}

	for (counter = 0; counter < nr_counters; counter++) {
		if (event_count[counter])
			continue;

		event_count[counter] = default_interval;
	}
}

int cmd_stat(int argc, char **argv, const char *prefix)
{
	page_size = sysconf(_SC_PAGE_SIZE);

	process_options(argc, argv);

	nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	assert(nr_cpus <= MAX_NR_CPUS);
	assert(nr_cpus >= 0);

	return do_perfstat(argc, argv);
}
