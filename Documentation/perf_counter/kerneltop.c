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

static int			run_perfstat			=  0;
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

static __u64			count_filter		       = 100;

static int			tid				= -1;
static int			profile_cpu			= -1;
static int			nr_cpus				=  0;
static int			nmi				=  1;
static unsigned int		realtime_prio			=  0;
static int			group				=  0;
static unsigned int		page_size;
static unsigned int		mmap_pages			=  16;
static int			use_mmap			= 0;
static int			use_munmap			= 0;

static char			*vmlinux;

static char			*sym_filter;
static unsigned long		filter_start;
static unsigned long		filter_end;

static int			delay_secs			=  2;
static int			zero;
static int			dump_symtab;

static int			scale;

struct source_line {
	uint64_t		EIP;
	unsigned long		count;
	char			*line;
	struct source_line	*next;
};

static struct source_line	*lines;
static struct source_line	**lines_tail;

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

static void display_perfstat_help(void)
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

static void display_help(void)
{
	if (run_perfstat)
		return display_perfstat_help();

	printf(
	"Usage: kerneltop [<options>]\n"
	"   Or: kerneltop -S [<options>] COMMAND [ARGS]\n\n"
	"KernelTop Options (up to %d event types can be specified at once):\n\n",
		 MAX_COUNTERS);

	display_events_help();

	printf(
	" -S        --stat             # perfstat COMMAND\n"
	" -a                           # system-wide collection (for perfstat)\n\n"
	" -c CNT    --count=CNT        # event period to sample\n\n"
	" -C CPU    --cpu=CPU          # CPU (-1 for all)                 [default: -1]\n"
	" -p PID    --pid=PID          # PID of sampled task (-1 for all) [default: -1]\n\n"
	" -l                           # show scale factor for RR events\n"
	" -d delay  --delay=<seconds>  # sampling/display delay           [default:  2]\n"
	" -f CNT    --filter=CNT       # min-event-count filter          [default: 100]\n\n"
	" -r prio   --realtime=<prio>  # event acquisition runs with SCHED_FIFO policy\n"
	" -s symbol --symbol=<symbol>  # function to be showed annotated one-shot\n"
	" -x path   --vmlinux=<path>   # the vmlinux binary, required for -s use\n"
	" -z        --zero             # zero counts after display\n"
	" -D        --dump_symtab      # dump symbol table to stderr on startup\n"
	" -m pages  --mmap_pages=<pages> # number of mmap data pages\n"
	" -M        --mmap_info        # print mmap info stream\n"
	" -U        --munmap_info      # print munmap info stream\n"
	);

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

/*
 * Symbols
 */

static uint64_t			min_ip;
static uint64_t			max_ip = -1ll;

struct sym_entry {
	unsigned long long	addr;
	char			*sym;
	unsigned long		count[MAX_COUNTERS];
	int			skip;
	struct source_line	*source;
};

#define MAX_SYMS		100000

static int sym_table_count;

struct sym_entry		*sym_filter_entry;

static struct sym_entry		sym_table[MAX_SYMS];

static void show_details(struct sym_entry *sym);

/*
 * Ordering weight: count-1 * count-2 * ... / count-n
 */
static double sym_weight(const struct sym_entry *sym)
{
	double weight;
	int counter;

	weight = sym->count[0];

	for (counter = 1; counter < nr_counters-1; counter++)
		weight *= sym->count[counter];

	weight /= (sym->count[counter] + 1);

	return weight;
}

static int compare(const void *__sym1, const void *__sym2)
{
	const struct sym_entry *sym1 = __sym1, *sym2 = __sym2;

	return sym_weight(sym1) < sym_weight(sym2);
}

static long			events;
static long			userspace_events;
static const char		CONSOLE_CLEAR[] = "[H[2J";

static struct sym_entry		tmp[MAX_SYMS];

static void print_sym_table(void)
{
	int i, printed;
	int counter;
	float events_per_sec = events/delay_secs;
	float kevents_per_sec = (events-userspace_events)/delay_secs;
	float sum_kevents = 0.0;

	events = userspace_events = 0;
	memcpy(tmp, sym_table, sizeof(sym_table[0])*sym_table_count);
	qsort(tmp, sym_table_count, sizeof(tmp[0]), compare);

	for (i = 0; i < sym_table_count && tmp[i].count[0]; i++)
		sum_kevents += tmp[i].count[0];

	write(1, CONSOLE_CLEAR, strlen(CONSOLE_CLEAR));

	printf(
"------------------------------------------------------------------------------\n");
	printf( " KernelTop:%8.0f irqs/sec  kernel:%4.1f%% [%s, ",
		events_per_sec,
		100.0 - (100.0*((events_per_sec-kevents_per_sec)/events_per_sec)),
		nmi ? "NMI" : "IRQ");

	if (nr_counters == 1)
		printf("%d ", event_count[0]);

	for (counter = 0; counter < nr_counters; counter++) {
		if (counter)
			printf("/");

		printf("%s", event_name(counter));
	}

	printf( "], ");

	if (tid != -1)
		printf(" (tid: %d", tid);
	else
		printf(" (all");

	if (profile_cpu != -1)
		printf(", cpu: %d)\n", profile_cpu);
	else {
		if (tid != -1)
			printf(")\n");
		else
			printf(", %d CPUs)\n", nr_cpus);
	}

	printf("------------------------------------------------------------------------------\n\n");

	if (nr_counters == 1)
		printf("             events    pcnt");
	else
		printf("  weight     events    pcnt");

	printf("         RIP          kernel function\n"
	       	       "  ______     ______   _____   ________________   _______________\n\n"
	);

	for (i = 0, printed = 0; i < sym_table_count; i++) {
		float pcnt;
		int count;

		if (printed <= 18 && tmp[i].count[0] >= count_filter) {
			pcnt = 100.0 - (100.0*((sum_kevents-tmp[i].count[0])/sum_kevents));

			if (nr_counters == 1)
				printf("%19.2f - %4.1f%% - %016llx : %s\n",
					sym_weight(tmp + i),
					pcnt, tmp[i].addr, tmp[i].sym);
			else
				printf("%8.1f %10ld - %4.1f%% - %016llx : %s\n",
					sym_weight(tmp + i),
					tmp[i].count[0],
					pcnt, tmp[i].addr, tmp[i].sym);
			printed++;
		}
		/*
		 * Add decay to the counts:
		 */
		for (count = 0; count < nr_counters; count++)
			sym_table[i].count[count] = zero ? 0 : sym_table[i].count[count] * 7 / 8;
	}

	if (sym_filter_entry)
		show_details(sym_filter_entry);

	{
		struct pollfd stdin_poll = { .fd = 0, .events = POLLIN };

		if (poll(&stdin_poll, 1, 0) == 1) {
			printf("key pressed - exiting.\n");
			exit(0);
		}
	}
}

static void *display_thread(void *arg)
{
	printf("KernelTop refresh period: %d seconds\n", delay_secs);

	while (!sleep(delay_secs))
		print_sym_table();

	return NULL;
}

static int read_symbol(FILE *in, struct sym_entry *s)
{
	static int filter_match = 0;
	char *sym, stype;
	char str[500];
	int rc, pos;

	rc = fscanf(in, "%llx %c %499s", &s->addr, &stype, str);
	if (rc == EOF)
		return -1;

	assert(rc == 3);

	/* skip until end of line: */
	pos = strlen(str);
	do {
		rc = fgetc(in);
		if (rc == '\n' || rc == EOF || pos >= 499)
			break;
		str[pos] = rc;
		pos++;
	} while (1);
	str[pos] = 0;

	sym = str;

	/* Filter out known duplicates and non-text symbols. */
	if (!strcmp(sym, "_text"))
		return 1;
	if (!min_ip && !strcmp(sym, "_stext"))
		return 1;
	if (!strcmp(sym, "_etext") || !strcmp(sym, "_sinittext"))
		return 1;
	if (stype != 'T' && stype != 't')
		return 1;
	if (!strncmp("init_module", sym, 11) || !strncmp("cleanup_module", sym, 14))
		return 1;
	if (strstr(sym, "_text_start") || strstr(sym, "_text_end"))
		return 1;

	s->sym = malloc(strlen(str));
	assert(s->sym);

	strcpy((char *)s->sym, str);
	s->skip = 0;

	/* Tag events to be skipped. */
	if (!strcmp("default_idle", s->sym) || !strcmp("cpu_idle", s->sym))
		s->skip = 1;
	else if (!strcmp("enter_idle", s->sym) || !strcmp("exit_idle", s->sym))
		s->skip = 1;
	else if (!strcmp("mwait_idle", s->sym))
		s->skip = 1;

	if (filter_match == 1) {
		filter_end = s->addr;
		filter_match = -1;
		if (filter_end - filter_start > 10000) {
			printf("hm, too large filter symbol <%s> - skipping.\n",
				sym_filter);
			printf("symbol filter start: %016lx\n", filter_start);
			printf("                end: %016lx\n", filter_end);
			filter_end = filter_start = 0;
			sym_filter = NULL;
			sleep(1);
		}
	}
	if (filter_match == 0 && sym_filter && !strcmp(s->sym, sym_filter)) {
		filter_match = 1;
		filter_start = s->addr;
	}

	return 0;
}

int compare_addr(const void *__sym1, const void *__sym2)
{
	const struct sym_entry *sym1 = __sym1, *sym2 = __sym2;

	return sym1->addr > sym2->addr;
}

static void sort_symbol_table(void)
{
	int i, dups;

	do {
		qsort(sym_table, sym_table_count, sizeof(sym_table[0]), compare_addr);
		for (i = 0, dups = 0; i < sym_table_count; i++) {
			if (sym_table[i].addr == sym_table[i+1].addr) {
				sym_table[i+1].addr = -1ll;
				dups++;
			}
		}
		sym_table_count -= dups;
	} while(dups);
}

static void parse_symbols(void)
{
	struct sym_entry *last;

	FILE *kallsyms = fopen("/proc/kallsyms", "r");

	if (!kallsyms) {
		printf("Could not open /proc/kallsyms - no CONFIG_KALLSYMS_ALL=y?\n");
		exit(-1);
	}

	while (!feof(kallsyms)) {
		if (read_symbol(kallsyms, &sym_table[sym_table_count]) == 0) {
			sym_table_count++;
			assert(sym_table_count <= MAX_SYMS);
		}
	}

	sort_symbol_table();
	min_ip = sym_table[0].addr;
	max_ip = sym_table[sym_table_count-1].addr;
	last = sym_table + sym_table_count++;

	last->addr = -1ll;
	last->sym = "<end>";

	if (filter_end) {
		int count;
		for (count=0; count < sym_table_count; count ++) {
			if (!strcmp(sym_table[count].sym, sym_filter)) {
				sym_filter_entry = &sym_table[count];
				break;
			}
		}
	}
	if (dump_symtab) {
		int i;

		for (i = 0; i < sym_table_count; i++)
			fprintf(stderr, "%llx %s\n",
				sym_table[i].addr, sym_table[i].sym);
	}
}

/*
 * Source lines
 */

static void parse_vmlinux(char *filename)
{
	FILE *file;
	char command[PATH_MAX*2];
	if (!filename)
		return;

	sprintf(command, "objdump --start-address=0x%016lx --stop-address=0x%016lx -dS %s", filter_start, filter_end, filename);

	file = popen(command, "r");
	if (!file)
		return;

	lines_tail = &lines;
	while (!feof(file)) {
		struct source_line *src;
		size_t dummy = 0;
		char *c;

		src = malloc(sizeof(struct source_line));
		assert(src != NULL);
		memset(src, 0, sizeof(struct source_line));

		if (getline(&src->line, &dummy, file) < 0)
			break;
		if (!src->line)
			break;

		c = strchr(src->line, '\n');
		if (c)
			*c = 0;

		src->next = NULL;
		*lines_tail = src;
		lines_tail = &src->next;

		if (strlen(src->line)>8 && src->line[8] == ':')
			src->EIP = strtoull(src->line, NULL, 16);
		if (strlen(src->line)>8 && src->line[16] == ':')
			src->EIP = strtoull(src->line, NULL, 16);
	}
	pclose(file);
}

static void record_precise_ip(uint64_t ip)
{
	struct source_line *line;

	for (line = lines; line; line = line->next) {
		if (line->EIP == ip)
			line->count++;
		if (line->EIP > ip)
			break;
	}
}

static void lookup_sym_in_vmlinux(struct sym_entry *sym)
{
	struct source_line *line;
	char pattern[PATH_MAX];
	sprintf(pattern, "<%s>:", sym->sym);

	for (line = lines; line; line = line->next) {
		if (strstr(line->line, pattern)) {
			sym->source = line;
			break;
		}
	}
}

static void show_lines(struct source_line *line_queue, int line_queue_count)
{
	int i;
	struct source_line *line;

	line = line_queue;
	for (i = 0; i < line_queue_count; i++) {
		printf("%8li\t%s\n", line->count, line->line);
		line = line->next;
	}
}

#define TRACE_COUNT     3

static void show_details(struct sym_entry *sym)
{
	struct source_line *line;
	struct source_line *line_queue = NULL;
	int displayed = 0;
	int line_queue_count = 0;

	if (!sym->source)
		lookup_sym_in_vmlinux(sym);
	if (!sym->source)
		return;

	printf("Showing details for %s\n", sym->sym);

	line = sym->source;
	while (line) {
		if (displayed && strstr(line->line, ">:"))
			break;

		if (!line_queue_count)
			line_queue = line;
		line_queue_count ++;

		if (line->count >= count_filter) {
			show_lines(line_queue, line_queue_count);
			line_queue_count = 0;
			line_queue = NULL;
		} else if (line_queue_count > TRACE_COUNT) {
			line_queue = line_queue->next;
			line_queue_count --;
		}

		line->count = 0;
		displayed++;
		if (displayed > 300)
			break;
		line = line->next;
	}
}

/*
 * Binary search in the histogram table and record the hit:
 */
static void record_ip(uint64_t ip, int counter)
{
	int left_idx, middle_idx, right_idx, idx;
	unsigned long left, middle, right;

	record_precise_ip(ip);

	left_idx = 0;
	right_idx = sym_table_count-1;
	assert(ip <= max_ip && ip >= min_ip);

	while (left_idx + 1 < right_idx) {
		middle_idx = (left_idx + right_idx) / 2;

		left   = sym_table[  left_idx].addr;
		middle = sym_table[middle_idx].addr;
		right  = sym_table[ right_idx].addr;

		if (!(left <= middle && middle <= right)) {
			printf("%016lx...\n%016lx...\n%016lx\n", left, middle, right);
			printf("%d %d %d\n", left_idx, middle_idx, right_idx);
		}
		assert(left <= middle && middle <= right);
		if (!(left <= ip && ip <= right)) {
			printf(" left: %016lx\n", left);
			printf("   ip: %016lx\n", (unsigned long)ip);
			printf("right: %016lx\n", right);
		}
		assert(left <= ip && ip <= right);
		/*
		 * [ left .... target .... middle .... right ]
		 *   => right := middle
		 */
		if (ip < middle) {
			right_idx = middle_idx;
			continue;
		}
		/*
		 * [ left .... middle ... target ... right ]
		 *   => left := middle
		 */
		left_idx = middle_idx;
	}

	idx = left_idx;

	if (!sym_table[idx].skip)
		sym_table[idx].count[counter]++;
	else events--;
}

static void process_event(uint64_t ip, int counter)
{
	events++;

	if (ip < min_ip || ip > max_ip) {
		userspace_events++;
		return;
	}

	record_ip(ip, counter);
}

static void process_options(int argc, char *argv[])
{
	int error = 0, counter;

	if (strstr(argv[0], "perfstat"))
		run_perfstat = 1;

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
			{"mmap_info",	no_argument,		NULL, 'M'},
			{"mmap_pages",	required_argument,	NULL, 'm'},
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
		case 'd': delay_secs			=   atoi(optarg); break;
		case 'D': dump_symtab			=              1; break;

		case 'e': error				= parse_events(optarg); break;

		case 'f': count_filter			=   atoi(optarg); break;
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
		case 'r': realtime_prio			=   atoi(optarg); break;
		case 's': sym_filter			= strdup(optarg); break;
		case 'S': run_perfstat			=	       1; break;
		case 'x': vmlinux			= strdup(optarg); break;
		case 'z': zero				=              1; break;
		case 'm': mmap_pages			=   atoi(optarg); break;
		case 'M': use_mmap			=              1; break;
		case 'U': use_munmap			=              1; break;
		default: error = 1; break;
		}
	}
	if (error)
		display_help();

	if (!nr_counters) {
		if (run_perfstat)
			nr_counters = 8;
		else {
			nr_counters = 1;
			event_id[0] = 0;
		}
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

struct timeval last_read, this_read;

static void mmap_read(struct mmap_data *md)
{
	unsigned int head = mmap_read_head(md);
	unsigned int old = md->prev;
	unsigned char *data = md->base + page_size;
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

	for (; old != head;) {
		struct ip_event {
			struct perf_event_header header;
			__u64 ip;
			__u32 pid, tid;
		};
		struct mmap_event {
			struct perf_event_header header;
			__u32 pid, tid;
			__u64 start;
			__u64 len;
			__u64 pgoff;
			char filename[PATH_MAX];
		};

		typedef union event_union {
			struct perf_event_header header;
			struct ip_event ip;
			struct mmap_event mmap;
		} event_t;

		event_t *event = (event_t *)&data[old & md->mask];

		event_t event_copy;

		unsigned int size = event->header.size;

		/*
		 * Event straddles the mmap boundary -- header should always
		 * be inside due to u64 alignment of output.
		 */
		if ((old & md->mask) + size != ((old + size) & md->mask)) {
			unsigned int offset = old;
			unsigned int len = min(sizeof(*event), size), cpy;
			void *dst = &event_copy;

			do {
				cpy = min(md->mask + 1 - (offset & md->mask), len);
				memcpy(dst, &data[offset & md->mask], cpy);
				offset += cpy;
				dst += cpy;
				len -= cpy;
			} while (len);

			event = &event_copy;
		}

		old += size;

		if (event->header.misc & PERF_EVENT_MISC_OVERFLOW) {
			if (event->header.type & PERF_RECORD_IP)
				process_event(event->ip.ip, md->counter);
		} else {
			switch (event->header.type) {
				case PERF_EVENT_MMAP:
				case PERF_EVENT_MUNMAP:
					printf("%s: %Lu %Lu %Lu %s\n",
							event->header.type == PERF_EVENT_MMAP
							? "mmap" : "munmap",
							event->mmap.start,
							event->mmap.len,
							event->mmap.pgoff,
							event->mmap.filename);
					break;
			}
		}
	}

	md->prev = old;
}

int main(int argc, char *argv[])
{
	struct pollfd event_array[MAX_NR_CPUS * MAX_COUNTERS];
	struct mmap_data mmap_array[MAX_NR_CPUS][MAX_COUNTERS];
	struct perf_counter_hw_event hw_event;
	pthread_t thread;
	int i, counter, group_fd, nr_poll = 0;
	unsigned int cpu;
	int ret;

	page_size = sysconf(_SC_PAGE_SIZE);

	process_options(argc, argv);

	nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	assert(nr_cpus <= MAX_NR_CPUS);
	assert(nr_cpus >= 0);

	if (run_perfstat)
		return do_perfstat(argc, argv);

	if (tid != -1 || profile_cpu != -1)
		nr_cpus = 1;

	parse_symbols();
	if (vmlinux && sym_filter_entry)
		parse_vmlinux(vmlinux);

	for (i = 0; i < nr_cpus; i++) {
		group_fd = -1;
		for (counter = 0; counter < nr_counters; counter++) {

			cpu	= profile_cpu;
			if (tid == -1 && profile_cpu == -1)
				cpu = i;

			memset(&hw_event, 0, sizeof(hw_event));
			hw_event.config		= event_id[counter];
			hw_event.irq_period	= event_count[counter];
			hw_event.record_type	= PERF_RECORD_IP | PERF_RECORD_TID;
			hw_event.nmi		= nmi;
			hw_event.mmap		= use_mmap;
			hw_event.munmap		= use_munmap;

			fd[i][counter] = sys_perf_counter_open(&hw_event, tid, cpu, group_fd, 0);
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

	if (pthread_create(&thread, NULL, display_thread, NULL)) {
		printf("Could not create display thread.\n");
		exit(-1);
	}

	if (realtime_prio) {
		struct sched_param param;

		param.sched_priority = realtime_prio;
		if (sched_setscheduler(0, SCHED_FIFO, &param)) {
			printf("Could not set realtime priority.\n");
			exit(-1);
		}
	}

	while (1) {
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
