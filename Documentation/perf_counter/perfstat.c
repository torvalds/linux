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

#include "perfcounters.h"

static int			nr_cpus			= 0;

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
	return;

err:
	display_help();
}

char fault_here[1000000];

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
