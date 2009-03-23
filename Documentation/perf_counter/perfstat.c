/*
 * perfstat:  /usr/bin/time -alike performance counter statistics utility
 *
 *        It summarizes the counter events of all tasks (and child tasks),
 *        covering all CPUs that the command (or workload) executes on.
 *        It only counts the per-task events of the workload started,
 *        independent of how many other tasks run on those CPUs.
 *
 * Build with:       cc -O2 -g -lrt -Wall -W -o perfstat perfstat.c
 *
 * Sample output:
 *

   $ ./perfstat -e 1 -e 3 -e 5 ls -lR /usr/include/ >/dev/null

   Performance counter stats for 'ls':

           163516953 instructions
                2295 cache-misses
             2855182 branch-misses

 *
 * Copyright (C) 2008, Red Hat Inc, Ingo Molnar <mingo@redhat.com>
 *
 * Released under the GPLv2 (not later).
 *
 * Percpu counter support by: Yanmin Zhang <yanmin_zhang@linux.intel.com>
 * Symbolic event options by: Wu Fengguang <fengguang.wu@intel.com>
 */
#define _GNU_SOURCE

#include <assert.h>
#include <getopt.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>

#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <linux/unistd.h>

#ifdef __x86_64__
# define __NR_perf_counter_open	295
#endif

#ifdef __i386__
# define __NR_perf_counter_open 333
#endif

#ifdef __powerpc__
#define __NR_perf_counter_open 319
#endif

/*
 * Pick up some kernel type conventions:
 */
#define __user
#define asmlinkage

typedef unsigned int		__u32;
typedef unsigned long long	__u64;
typedef long long		__s64;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/*
 * User-space ABI bits:
 */

/*
 * Generalized performance counter event types, used by the hw_event.type
 * parameter of the sys_perf_counter_open() syscall:
 */
enum hw_event_types {
	/*
	 * Common hardware events, generalized by the kernel:
	 */
	PERF_COUNT_CPU_CYCLES		=  0,
	PERF_COUNT_INSTRUCTIONS		=  1,
	PERF_COUNT_CACHE_REFERENCES	=  2,
	PERF_COUNT_CACHE_MISSES		=  3,
	PERF_COUNT_BRANCH_INSTRUCTIONS	=  4,
	PERF_COUNT_BRANCH_MISSES	=  5,
	PERF_COUNT_BUS_CYCLES		=  6,

	PERF_HW_EVENTS_MAX		=  7,

	/*
	 * Special "software" counters provided by the kernel, even if
	 * the hardware does not support performance counters. These
	 * counters measure various physical and sw events of the
	 * kernel (and allow the profiling of them as well):
	 */
	PERF_COUNT_CPU_CLOCK		= -1,
	PERF_COUNT_TASK_CLOCK		= -2,
	PERF_COUNT_PAGE_FAULTS		= -3,
	PERF_COUNT_CONTEXT_SWITCHES	= -4,
	PERF_COUNT_CPU_MIGRATIONS	= -5,

	PERF_SW_EVENTS_MIN		= -6,
};

/*
 * IRQ-notification data record type:
 */
enum perf_counter_record_type {
	PERF_RECORD_SIMPLE		=  0,
	PERF_RECORD_IRQ			=  1,
	PERF_RECORD_GROUP		=  2,
};

/*
 * Hardware event to monitor via a performance monitoring counter:
 */
struct perf_counter_hw_event {
	__s64			type;

	__u64			irq_period;
	__u64			record_type;
	__u64			read_format;

	__u64			disabled       :  1, /* off by default        */
				nmi	       :  1, /* NMI sampling          */
				raw	       :  1, /* raw event type        */
				inherit	       :  1, /* children inherit it   */
				pinned	       :  1, /* must always be on PMU */
				exclusive      :  1, /* only group on PMU     */
				exclude_user   :  1, /* don't count user      */
				exclude_kernel :  1, /* ditto kernel          */
				exclude_hv     :  1, /* ditto hypervisor      */
				exclude_idle   :  1, /* don't count when idle */

				__reserved_1   : 54;

	__u32			extra_config_len;
	__u32			__reserved_4;

	__u64			__reserved_2;
	__u64			__reserved_3;
};

/*
 * Ioctls that can be done on a perf counter fd:
 */
#define PERF_COUNTER_IOC_ENABLE		_IO('$', 0)
#define PERF_COUNTER_IOC_DISABLE	_IO('$', 1)

asmlinkage int sys_perf_counter_open(

	struct perf_counter_hw_event	*hw_event_uptr		__user,
	pid_t				pid,
	int				cpu,
	int				group_fd,
	unsigned long			flags)
{
	int ret;

	ret = syscall(
		__NR_perf_counter_open, hw_event_uptr, pid, cpu, group_fd, flags);
#if defined(__x86_64__) || defined(__i386__)
	if (ret < 0 && ret > -4096) {
		errno = -ret;
		ret = -1;
	}
#endif
	return ret;
}


static char *hw_event_names [] = {
	"CPU cycles",
	"instructions",
	"cache references",
	"cache misses",
	"branches",
	"branch misses",
	"bus cycles",
};

static char *sw_event_names [] = {
	"cpu clock ticks",
	"task clock ticks",
	"pagefaults",
	"context switches",
	"CPU migrations",
};

struct event_symbol {
	int event;
	char *symbol;
};

static struct event_symbol event_symbols [] = {
	{PERF_COUNT_CPU_CYCLES,			"cpu-cycles",		},
	{PERF_COUNT_CPU_CYCLES,			"cycles",		},
	{PERF_COUNT_INSTRUCTIONS,		"instructions",		},
	{PERF_COUNT_CACHE_REFERENCES,		"cache-references",	},
	{PERF_COUNT_CACHE_MISSES,		"cache-misses",		},
	{PERF_COUNT_BRANCH_INSTRUCTIONS,	"branch-instructions",	},
	{PERF_COUNT_BRANCH_INSTRUCTIONS,	"branches",		},
	{PERF_COUNT_BRANCH_MISSES,		"branch-misses",	},
	{PERF_COUNT_BUS_CYCLES,			"bus-cycles",		},
	{PERF_COUNT_CPU_CLOCK,			"cpu-ticks",		},
	{PERF_COUNT_CPU_CLOCK,			"ticks",		},
	{PERF_COUNT_TASK_CLOCK,			"task-ticks",		},
	{PERF_COUNT_PAGE_FAULTS,		"page-faults",		},
	{PERF_COUNT_PAGE_FAULTS,		"faults",		},
	{PERF_COUNT_CONTEXT_SWITCHES,		"context-switches",	},
	{PERF_COUNT_CONTEXT_SWITCHES,		"cs",			},
	{PERF_COUNT_CPU_MIGRATIONS,		"cpu-migrations",	},
	{PERF_COUNT_CPU_MIGRATIONS,		"migrations",		},
};

#define MAX_COUNTERS					64
#define MAX_NR_CPUS					256

static int			nr_counters		= 0;
static int			nr_cpus			= 0;

static int			event_id[MAX_COUNTERS]	=
					 { -2, -5, -4, -3, 0, 1, 2, 3};

static int			event_raw[MAX_COUNTERS];

static int			system_wide		= 0;

static void display_help(void)
{
	unsigned int i;
	int e;

	printf(
	"Usage: perfstat [<events...>] <cmd...>\n\n"
	"PerfStat Options (up to %d event types can be specified):\n\n",
		 MAX_COUNTERS);
	printf(
	" -e EVENT     --event=EVENT        #  symbolic-name        abbreviations");

	for (i = 0, e = PERF_HW_EVENTS_MAX; i < ARRAY_SIZE(event_symbols); i++) {
		if (e != event_symbols[i].event) {
			e = event_symbols[i].event;
			printf(
	"\n                                  %2d: %-20s", e, event_symbols[i].symbol);
		} else
			printf(" %s", event_symbols[i].symbol);
	}

	printf("\n"
	"                                rNNN: raw event type\n\n"
	" -s                                # system-wide collection\n\n"
	" -c <cmd..>   --command=<cmd..>    # command+arguments to be timed.\n"
	"\n");
	exit(0);
}

static int type_valid(int type)
{
	if (type >= PERF_HW_EVENTS_MAX)
		return 0;
	if (type <= PERF_SW_EVENTS_MIN)
		return 0;

	return 1;
}

static char *event_name(int ctr)
{
	int type = event_id[ctr];
	static char buf[32];

	if (event_raw[ctr]) {
		sprintf(buf, "raw 0x%x", type);
		return buf;
	}
	if (!type_valid(type))
		return "unknown";

	if (type >= 0)
		return hw_event_names[type];

	return sw_event_names[-type-1];
}

/*
 * Each event can have multiple symbolic names.
 * Symbolic names are (almost) exactly matched.
 */
static int match_event_symbols(char *str)
{
	unsigned int i;

	if (isdigit(str[0]) || str[0] == '-')
		return atoi(str);

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++) {
		if (!strncmp(str, event_symbols[i].symbol,
			     strlen(event_symbols[i].symbol)))
			return event_symbols[i].event;
	}

	return PERF_HW_EVENTS_MAX;
}

static void parse_events(char *str)
{
	int type, raw;

again:
	nr_counters++;
	if (nr_counters == MAX_COUNTERS)
		display_help();

	raw = 0;
	if (*str == 'r') {
		raw = 1;
		++str;
		type = strtol(str, NULL, 16);
	} else {
		type = match_event_symbols(str);
		if (!type_valid(type))
			display_help();
	}

	event_id[nr_counters] = type;
	event_raw[nr_counters] = raw;

	str = strstr(str, ",");
	if (str) {
		str++;
		goto again;
	}
}

static void process_options(int argc, char *argv[])
{
	for (;;) {
		int option_index = 0;
		/** Options for getopt */
		static struct option long_options[] = {
			{"event",	required_argument,	NULL, 'e'},
			{"help",	no_argument,		NULL, 'h'},
			{"command",	no_argument,		NULL, 'c'},
			{NULL,		0,			NULL,  0 }
		};
		int c = getopt_long(argc, argv, "+:e:c:s",
				    long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'c':
			break;
		case 's':
			system_wide = 1;
			break;
		case 'e':
			parse_events(optarg);
			break;
		default:
			break;
		}
	}
	if (optind == argc)
		goto err;

	if (!nr_counters)
		nr_counters = 8;
	else
		nr_counters++;
	return;

err:
	display_help();
}

char fault_here[1000000];

#define PR_TASK_PERF_COUNTERS_DISABLE           31
#define PR_TASK_PERF_COUNTERS_ENABLE            32

static int fd[MAX_NR_CPUS][MAX_COUNTERS];

static void create_counter(int counter)
{
	struct perf_counter_hw_event hw_event;

	memset(&hw_event, 0, sizeof(hw_event));
	hw_event.type		= event_id[counter];
	hw_event.raw		= event_raw[counter];
	hw_event.record_type	= PERF_RECORD_SIMPLE;
	hw_event.nmi		= 0;

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


#define rdclock()					\
({							\
	struct timespec ts;				\
							\
	clock_gettime(CLOCK_MONOTONIC, &ts);		\
	ts.tv_sec * 1000000000ULL + ts.tv_nsec;		\
})

int main(int argc, char *argv[])
{
	unsigned long long t0, t1;
	int counter;
	ssize_t res;
	int status;
	int pid;

	process_options(argc, argv);

	if (system_wide) {
		nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
		assert(nr_cpus <= MAX_NR_CPUS);
		assert(nr_cpus >= 0);
	} else
		nr_cpus = 1;

	for (counter = 0; counter < nr_counters; counter++)
		create_counter(counter);

	argc -= optind;
	argv += optind;

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
		int cpu;
		__u64 count, single_count;

		count = 0;
		for (cpu = 0; cpu < nr_cpus; cpu ++) {
			res = read(fd[cpu][counter],
					(char *) &single_count, sizeof(single_count));
			assert(res == sizeof(single_count));
			count += single_count;
		}

		if (!event_raw[counter] &&
		    (event_id[counter] == PERF_COUNT_CPU_CLOCK ||
		     event_id[counter] == PERF_COUNT_TASK_CLOCK)) {

			double msecs = (double)count / 1000000;

			fprintf(stderr, " %14.6f  %-20s (msecs)\n",
				msecs, event_name(counter));
		} else {
			fprintf(stderr, " %14Ld  %-20s (events)\n",
				count, event_name(counter));
		}
		if (!counter)
			fprintf(stderr, "\n");
	}
	fprintf(stderr, "\n");
	fprintf(stderr, " Wall-clock time elapsed: %12.6f msecs\n",
			(double)(t1-t0)/1e6);
	fprintf(stderr, "\n");

	return 0;
}
