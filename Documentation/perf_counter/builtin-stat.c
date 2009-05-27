/*
 * perf stat:  /usr/bin/time -alike performance counter statistics utility

          It summarizes the counter events of all tasks (and child tasks),
          covering all CPUs that the command (or workload) executes on.
          It only counts the per-task events of the workload started,
          independent of how many other tasks run on those CPUs.

   Sample output:

   $ perf stat -e 1 -e 3 -e 5 ls -lR /usr/include/ >/dev/null

   Performance counter stats for 'ls':

           163516953 instructions
                2295 cache-misses
             2855182 branch-misses
 *
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

#include "perf.h"
#include "builtin.h"
#include "util/util.h"
#include "util/parse-options.h"
#include "util/parse-events.h"

#include <sys/prctl.h>

static int			system_wide			=  0;
static int			inherit				=  1;

static __u64			default_event_id[MAX_COUNTERS]	= {
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

static int			target_pid			= -1;
static int			nr_cpus				=  0;
static unsigned int		page_size;

static int			scale				=  1;

static const unsigned int default_count[] = {
	1000000,
	1000000,
	  10000,
	  10000,
	1000000,
	  10000,
};

static void create_perfstat_counter(int counter)
{
	struct perf_counter_hw_event hw_event;

	memset(&hw_event, 0, sizeof(hw_event));
	hw_event.config		= event_id[counter];
	hw_event.record_type	= 0;
	hw_event.nmi		= 1;
	hw_event.exclude_kernel = event_mask[counter] & EVENT_MASK_KERNEL;
	hw_event.exclude_user   = event_mask[counter] & EVENT_MASK_USER;

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
		hw_event.inherit	= inherit;
		hw_event.disabled	= 1;

		fd[0][counter] = sys_perf_counter_open(&hw_event, 0, -1, -1, 0);
		if (fd[0][counter] < 0) {
			printf("perfstat error: syscall returned with %d (%s)\n",
					fd[0][counter], strerror(errno));
			exit(-1);
		}
	}
}

static int do_perfstat(int argc, const char **argv)
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

	/*
	 * Enable counters and exec the command:
	 */
	t0 = rdclock();
	prctl(PR_TASK_PERF_COUNTERS_ENABLE);

	if ((pid = fork()) < 0)
		perror("failed to fork");
	if (!pid) {
		if (execvp(argv[0], (char **)argv)) {
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

static void skip_signal(int signo)
{
}

static const char * const stat_usage[] = {
	"perf stat [<options>] <command>",
	NULL
};

static char events_help_msg[EVENTS_HELP_MAX];

static const struct option options[] = {
	OPT_CALLBACK('e', "event", NULL, "event",
		     events_help_msg, parse_events),
	OPT_INTEGER('c', "count", &default_interval,
		    "event period to sample"),
	OPT_BOOLEAN('i', "inherit", &inherit,
		    "child tasks inherit counters"),
	OPT_INTEGER('p', "pid", &target_pid,
		    "stat events on existing pid"),
	OPT_BOOLEAN('a', "all-cpus", &system_wide,
			    "system-wide collection from all CPUs"),
	OPT_BOOLEAN('l', "scale", &scale,
			    "scale/normalize counters"),
	OPT_END()
};

int cmd_stat(int argc, const char **argv, const char *prefix)
{
	int counter;

	page_size = sysconf(_SC_PAGE_SIZE);

	create_events_help(events_help_msg);
	memcpy(event_id, default_event_id, sizeof(default_event_id));

	argc = parse_options(argc, argv, options, stat_usage, 0);
	if (!argc)
		usage_with_options(stat_usage, options);

	if (!nr_counters) {
		nr_counters = 8;
	}

	for (counter = 0; counter < nr_counters; counter++) {
		if (event_count[counter])
			continue;

		event_count[counter] = default_interval;
	}
	nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	assert(nr_cpus <= MAX_NR_CPUS);
	assert(nr_cpus >= 0);

	/*
	 * We dont want to block the signals - that would cause
	 * child tasks to inherit that and Ctrl-C would not work.
	 * What we want is for Ctrl-C to work in the exec()-ed
	 * task, but being ignored by perf stat itself:
	 */
	signal(SIGINT,  skip_signal);
	signal(SIGALRM, skip_signal);
	signal(SIGABRT, skip_signal);

	return do_perfstat(argc, argv);
}
