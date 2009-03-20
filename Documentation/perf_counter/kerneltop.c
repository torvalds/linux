/*
 * kerneltop.c: show top kernel functions - performance counters showcase

   Build with:

     cc -O6 -Wall -lrt `pkg-config --cflags --libs glib-2.0` -o kerneltop kerneltop.c

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
#include <getopt.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>

#include <glib.h>

#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <linux/unistd.h>

#include "perfcounters.h"


#define MAX_COUNTERS			64
#define MAX_NR_CPUS			256

#define DEF_PERFSTAT_EVENTS		{ -2, -5, -4, -3, 0, 1, 2, 3}

static int			run_perfstat			=  0;
static int			system_wide			=  0;

static int			nr_counters			=  0;
static __s64			event_id[MAX_COUNTERS]		= DEF_PERFSTAT_EVENTS;
static int			event_raw[MAX_COUNTERS];
static int			event_count[MAX_COUNTERS];
static int			fd[MAX_NR_CPUS][MAX_COUNTERS];

static __u64			count_filter		       = 100;

static int			tid				= -1;
static int			profile_cpu			= -1;
static int			nr_cpus				=  0;
static int			nmi				=  1;
static int			group				=  0;

static char			*vmlinux;

static char			*sym_filter;
static unsigned long		filter_start;
static unsigned long		filter_end;

static int			delay_secs			=  2;
static int			zero;
static int			dump_symtab;

static GList			*lines;

struct source_line {
	uint64_t		EIP;
	unsigned long		count;
	char			*line;
};


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
};

struct event_symbol {
	int event;
	char *symbol;
};

static struct event_symbol event_symbols[] = {
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

static void display_events_help(void)
{
	unsigned int i;
	int e;

	printf(
	" -e EVENT     --event=EVENT   #  symbolic-name        abbreviations");

	for (i = 0, e = PERF_HW_EVENTS_MAX; i < ARRAY_SIZE(event_symbols); i++) {
		if (e != event_symbols[i].event) {
			e = event_symbols[i].event;
			printf(
	"\n                             %2d: %-20s", e, event_symbols[i].symbol);
		} else
			printf(" %s", event_symbols[i].symbol);
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
	" -d delay  --delay=<seconds>  # sampling/display delay           [default:  2]\n"
	" -f CNT    --filter=CNT       # min-event-count filter          [default: 100]\n\n"
	" -s symbol --symbol=<symbol>  # function to be showed annotated one-shot\n"
	" -x path   --vmlinux=<path>   # the vmlinux binary, required for -s use\n"
	" -z        --zero             # zero counts after display\n"
	" -D        --dump_symtab      # dump symbol table to stderr on startup\n"
	);

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
	__s64 type = event_id[ctr];
	static char buf[32];

	if (event_raw[ctr]) {
		sprintf(buf, "raw 0x%llx", (long long)type);
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

static int parse_events(char *str)
{
	__s64 type;
	int raw;

again:
	if (nr_counters == MAX_COUNTERS)
		return -1;

	raw = 0;
	if (*str == 'r') {
		raw = 1;
		++str;
		type = strtol(str, NULL, 16);
	} else {
		type = match_event_symbols(str);
		if (!type_valid(type))
			return -1;
	}

	event_id[nr_counters] = type;
	event_raw[nr_counters] = raw;
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
	GList			*source;
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

static time_t			last_refresh;
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

	memcpy(tmp, sym_table, sizeof(sym_table[0])*sym_table_count);
	qsort(tmp, sym_table_count, sizeof(tmp[0]), compare);

	write(1, CONSOLE_CLEAR, strlen(CONSOLE_CLEAR));

	printf(
"------------------------------------------------------------------------------\n");
	printf( " KernelTop:%8.0f irqs/sec  kernel:%3.1f%% [%s, ",
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
		printf("             events");
	else
		printf("  weight     events");

	printf("         RIP          kernel function\n"
	       	       "  ______     ______   ________________   _______________\n\n"
	);

	printed = 0;
	for (i = 0; i < sym_table_count; i++) {
		int count;

		if (nr_counters == 1) {
			if (printed <= 18 &&
					tmp[i].count[0] >= count_filter) {
				printf("%19.2f - %016llx : %s\n",
				  sym_weight(tmp + i), tmp[i].addr, tmp[i].sym);
				printed++;
			}
		} else {
			if (printed <= 18 &&
					tmp[i].count[0] >= count_filter) {
				printf("%8.1f %10ld - %016llx : %s\n",
				  sym_weight(tmp + i),
				  tmp[i].count[0],
				  tmp[i].addr, tmp[i].sym);
				printed++;
			}
		}
		/*
		 * Add decay to the counts:
		 */
		for (count = 0; count < nr_counters; count++)
			sym_table[i].count[count] = zero ? 0 : sym_table[i].count[count] * 7 / 8;
	}

	if (sym_filter_entry)
		show_details(sym_filter_entry);

	last_refresh = time(NULL);

	{
		struct pollfd stdin_poll = { .fd = 0, .events = POLLIN };

		if (poll(&stdin_poll, 1, 0) == 1) {
			printf("key pressed - exiting.\n");
			exit(0);
		}
	}
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
	if (!strcmp("enter_idle", s->sym) || !strcmp("exit_idle", s->sym))
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

		lines = g_list_prepend(lines, src);

		if (strlen(src->line)>8 && src->line[8] == ':')
			src->EIP = strtoull(src->line, NULL, 16);
		if (strlen(src->line)>8 && src->line[16] == ':')
			src->EIP = strtoull(src->line, NULL, 16);
	}
	pclose(file);
	lines = g_list_reverse(lines);
}

static void record_precise_ip(uint64_t ip)
{
	struct source_line *line;
	GList *item;

	item = g_list_first(lines);
	while (item) {
		line = item->data;
		if (line->EIP == ip)
			line->count++;
		if (line->EIP > ip)
			break;
		item = g_list_next(item);
	}
}

static void lookup_sym_in_vmlinux(struct sym_entry *sym)
{
	struct source_line *line;
	GList *item;
	char pattern[PATH_MAX];
	sprintf(pattern, "<%s>:", sym->sym);

	item = g_list_first(lines);
	while (item) {
		line = item->data;
		if (strstr(line->line, pattern)) {
			sym->source = item;
			break;
		}
		item = g_list_next(item);
	}
}

void show_lines(GList *item_queue, int item_queue_count)
{
	int i;
	struct source_line *line;

	for (i = 0; i < item_queue_count; i++) {
		line = item_queue->data;
		printf("%8li\t%s\n", line->count, line->line);
		item_queue = g_list_next(item_queue);
	}
}

#define TRACE_COUNT     3

static void show_details(struct sym_entry *sym)
{
	struct source_line *line;
	GList *item;
	int displayed = 0;
	GList *item_queue = NULL;
	int item_queue_count = 0;

	if (!sym->source)
		lookup_sym_in_vmlinux(sym);
	if (!sym->source)
		return;

	printf("Showing details for %s\n", sym->sym);

	item = sym->source;
	while (item) {
		line = item->data;
		if (displayed && strstr(line->line, ">:"))
			break;

		if (!item_queue_count)
			item_queue = item;
		item_queue_count ++;

		if (line->count >= count_filter) {
			show_lines(item_queue, item_queue_count);
			item_queue_count = 0;
			item_queue = NULL;
		} else if (item_queue_count > TRACE_COUNT) {
			item_queue = g_list_next(item_queue);
			item_queue_count --;
		}

		line->count = 0;
		displayed++;
		if (displayed > 300)
			break;
		item = g_list_next(item);
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
			printf("   ip: %016lx\n", ip);
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
			{"pid",		required_argument,	NULL, 'p'},
			{"vmlinux",	required_argument,	NULL, 'x'},
			{"symbol",	required_argument,	NULL, 's'},
			{"stat",	no_argument,		NULL, 'S'},
			{"zero",	no_argument,		NULL, 'z'},
			{NULL,		0,			NULL,  0 }
		};
		int c = getopt_long(argc, argv, "+:ac:C:d:De:f:g:hn:p:s:Sx:z",
				    long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'a': system_wide			=	       1; break;
		case 'c': event_count[nr_counters]	=   atoi(optarg); break;
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
		case 'n': nmi				=   atoi(optarg); break;
		case 'p':
			/* CPU and PID are mutually exclusive */
			if (profile_cpu != -1) {
				printf("WARNING: PID switch overriding CPU\n");
				sleep(1);
				profile_cpu = -1;
			}
			tid				=   atoi(optarg); break;
		case 's': sym_filter			= strdup(optarg); break;
		case 'S': run_perfstat			=	       1; break;
		case 'x': vmlinux			= strdup(optarg); break;
		case 'z': zero				=              1; break;
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

		if (event_id[counter] < PERF_HW_EVENTS_MAX)
			event_count[counter] = default_count[event_id[counter]];
		else
			event_count[counter] = 100000;
	}
}

int main(int argc, char *argv[])
{
	struct pollfd event_array[MAX_NR_CPUS][MAX_COUNTERS];
	struct perf_counter_hw_event hw_event;
	int i, counter, group_fd;
	unsigned int cpu;
	uint64_t ip;
	ssize_t res;
	int ret;

	process_options(argc, argv);

	nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	assert(nr_cpus <= MAX_NR_CPUS);
	assert(nr_cpus >= 0);

	if (run_perfstat)
		return do_perfstat(argc, argv);

	if (tid != -1 || profile_cpu != -1)
		nr_cpus = 1;

	for (i = 0; i < nr_cpus; i++) {
		group_fd = -1;
		for (counter = 0; counter < nr_counters; counter++) {

			cpu	= profile_cpu;
			if (tid == -1 && profile_cpu == -1)
				cpu = i;

			memset(&hw_event, 0, sizeof(hw_event));
			hw_event.type		= event_id[counter];
			hw_event.raw		= event_raw[counter];
			hw_event.irq_period	= event_count[counter];
			hw_event.record_type	= PERF_RECORD_IRQ;
			hw_event.nmi		= nmi;

			fd[i][counter] = sys_perf_counter_open(&hw_event, tid, cpu, group_fd, 0);
			fcntl(fd[i][counter], F_SETFL, O_NONBLOCK);
			if (fd[i][counter] < 0) {
				printf("kerneltop error: syscall returned with %d (%s)\n",
					fd[i][counter], strerror(-fd[i][counter]));
				if (fd[i][counter] == -1)
					printf("Are you root?\n");
				exit(-1);
			}
			assert(fd[i][counter] >= 0);

			/*
			 * First counter acts as the group leader:
			 */
			if (group && group_fd == -1)
				group_fd = fd[i][counter];

			event_array[i][counter].fd = fd[i][counter];
			event_array[i][counter].events = POLLIN;
		}
	}

	parse_symbols();
	if (vmlinux && sym_filter_entry)
		parse_vmlinux(vmlinux);

	printf("KernelTop refresh period: %d seconds\n", delay_secs);
	last_refresh = time(NULL);

	while (1) {
		int hits = events;

		for (i = 0; i < nr_cpus; i++) {
			for (counter = 0; counter < nr_counters; counter++) {
				res = read(fd[i][counter], (char *) &ip, sizeof(ip));
				if (res > 0) {
					assert(res == sizeof(ip));

					process_event(ip, counter);
				}
			}
		}

		if (time(NULL) >= last_refresh + delay_secs) {
			print_sym_table();
			events = userspace_events = 0;
		}

		if (hits == events)
			ret = poll(event_array[0], nr_cpus, 1000);
		hits = events;
	}

	return 0;
}
