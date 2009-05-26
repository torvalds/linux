/*
 * kerneltop.c: show top kernel functions - performance counters showcase

   Build with:

     make -C Documentation/perf_counter/

   Sample output:

------------------------------------------------------------------------------
 KernelTop:    2669 irqs/sec  [cache-misses/cache-refs],  (all, cpu: 2)
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
#include "util/util.h"
#include "util/util.h"
#include "util/parse-options.h"
#include "util/parse-events.h"

#include <assert.h>
#include <fcntl.h>

#include <stdio.h>

#include <errno.h>
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

static int			system_wide			=  0;

static __u64			default_event_id[MAX_COUNTERS]		= {
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

static int			target_pid				= -1;
static int			profile_cpu			= -1;
static int			nr_cpus				=  0;
static unsigned int		realtime_prio			=  0;
static int			group				=  0;
static unsigned int		page_size;
static unsigned int		mmap_pages			=  16;
static int			use_mmap			= 0;
static int			use_munmap			= 0;
static int			freq				= 0;

static char			*sym_filter;
static unsigned long		filter_start;
static unsigned long		filter_end;

static int			delay_secs			=  2;
static int			zero;
static int			dump_symtab;

static const unsigned int default_count[] = {
	1000000,
	1000000,
	  10000,
	  10000,
	1000000,
	  10000,
};

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
};

#define MAX_SYMS		100000

static int sym_table_count;

struct sym_entry		*sym_filter_entry;

static struct sym_entry		sym_table[MAX_SYMS];

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
	int i, j, active_count, printed;
	int counter;
	float events_per_sec = events/delay_secs;
	float kevents_per_sec = (events-userspace_events)/delay_secs;
	float sum_kevents = 0.0;

	events = userspace_events = 0;

	/* Iterate over symbol table and copy/tally/decay active symbols. */
	for (i = 0, active_count = 0; i < sym_table_count; i++) {
		if (sym_table[i].count[0]) {
			tmp[active_count++] = sym_table[i];
			sum_kevents += sym_table[i].count[0];

			for (j = 0; j < nr_counters; j++)
				sym_table[i].count[j] = zero ? 0 : sym_table[i].count[j] * 7 / 8;
		}
	}

	qsort(tmp, active_count + 1, sizeof(tmp[0]), compare);

	write(1, CONSOLE_CLEAR, strlen(CONSOLE_CLEAR));

	printf(
"------------------------------------------------------------------------------\n");
	printf( " KernelTop:%8.0f irqs/sec  kernel:%4.1f%% [",
		events_per_sec,
		100.0 - (100.0*((events_per_sec-kevents_per_sec)/events_per_sec)));

	if (nr_counters == 1)
		printf("%d ", event_count[0]);

	for (counter = 0; counter < nr_counters; counter++) {
		if (counter)
			printf("/");

		printf("%s", event_name(counter));
	}

	printf( "], ");

	if (target_pid != -1)
		printf(" (target_pid: %d", target_pid);
	else
		printf(" (all");

	if (profile_cpu != -1)
		printf(", cpu: %d)\n", profile_cpu);
	else {
		if (target_pid != -1)
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

	for (i = 0, printed = 0; i < active_count; i++) {
		float pcnt;

		if (++printed > 18 || tmp[i].count[0] < count_filter)
			break;

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
	}

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

	s->sym = malloc(strlen(str)+1);
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

static int compare_addr(const void *__sym1, const void *__sym2)
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

#define TRACE_COUNT     3

/*
 * Binary search in the histogram table and record the hit:
 */
static void record_ip(uint64_t ip, int counter)
{
	int left_idx, middle_idx, right_idx, idx;
	unsigned long left, middle, right;

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
			__u32 pid, target_pid;
		};
		struct mmap_event {
			struct perf_event_header header;
			__u32 pid, target_pid;
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

		size_t size = event->header.size;

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

static struct pollfd event_array[MAX_NR_CPUS * MAX_COUNTERS];
static struct mmap_data mmap_array[MAX_NR_CPUS][MAX_COUNTERS];

static int __cmd_top(void)
{
	struct perf_counter_hw_event hw_event;
	pthread_t thread;
	int i, counter, group_fd, nr_poll = 0;
	unsigned int cpu;
	int ret;

	for (i = 0; i < nr_cpus; i++) {
		group_fd = -1;
		for (counter = 0; counter < nr_counters; counter++) {

			cpu	= profile_cpu;
			if (target_pid == -1 && profile_cpu == -1)
				cpu = i;

			memset(&hw_event, 0, sizeof(hw_event));
			hw_event.config		= event_id[counter];
			hw_event.irq_period	= event_count[counter];
			hw_event.record_type	= PERF_RECORD_IP | PERF_RECORD_TID;
			hw_event.nmi		= 1;
			hw_event.mmap		= use_mmap;
			hw_event.munmap		= use_munmap;
			hw_event.freq		= freq;

			fd[i][counter] = sys_perf_counter_open(&hw_event, target_pid, cpu, group_fd, 0);
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

static const char * const top_usage[] = {
	"perf top [<options>]",
	NULL
};

static char events_help_msg[EVENTS_HELP_MAX];

static const struct option options[] = {
	OPT_CALLBACK('e', "event", NULL, "event",
		     events_help_msg, parse_events),
	OPT_INTEGER('c', "count", &default_interval,
		    "event period to sample"),
	OPT_INTEGER('p', "pid", &target_pid,
		    "profile events on existing pid"),
	OPT_BOOLEAN('a', "all-cpus", &system_wide,
			    "system-wide collection from all CPUs"),
	OPT_INTEGER('C', "CPU", &profile_cpu,
		    "CPU to profile on"),
	OPT_INTEGER('m', "mmap-pages", &mmap_pages,
		    "number of mmap data pages"),
	OPT_INTEGER('r', "realtime", &realtime_prio,
		    "collect data with this RT SCHED_FIFO priority"),
	OPT_INTEGER('d', "delay", &delay_secs,
		    "number of seconds to delay between refreshes"),
	OPT_BOOLEAN('D', "dump-symtab", &dump_symtab,
			    "dump the symbol table used for profiling"),
	OPT_INTEGER('f', "--count-filter", &count_filter,
		    "only display functions with more events than this"),
	OPT_BOOLEAN('g', "group", &group,
			    "put the counters into a counter group"),
	OPT_STRING('s', "sym-filter", &sym_filter, "pattern",
		    "only display symbols matchig this pattern"),
	OPT_BOOLEAN('z', "zero", &group,
		    "zero history across updates"),
	OPT_BOOLEAN('M', "use-mmap", &use_mmap,
		    "track mmap events"),
	OPT_BOOLEAN('U', "use-munmap", &use_munmap,
		    "track munmap events"),
	OPT_INTEGER('F', "--freq", &freq,
		    "profile at this frequency"),
	OPT_END()
};

int cmd_top(int argc, const char **argv, const char *prefix)
{
	int counter;

	page_size = sysconf(_SC_PAGE_SIZE);

	create_events_help(events_help_msg);
	memcpy(event_id, default_event_id, sizeof(default_event_id));

	argc = parse_options(argc, argv, options, top_usage, 0);
	if (argc)
		usage_with_options(top_usage, options);

	if (freq) {
		default_interval = freq;
		freq = 1;
	}

	/* CPU and PID are mutually exclusive */
	if (target_pid != -1 && profile_cpu != -1) {
		printf("WARNING: PID switch overriding CPU\n");
		sleep(1);
		profile_cpu = -1;
	}

	if (!nr_counters) {
		nr_counters = 1;
		event_id[0] = 0;
	}

	for (counter = 0; counter < nr_counters; counter++) {
		if (event_count[counter])
			continue;

		event_count[counter] = default_interval;
	}

	nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	assert(nr_cpus <= MAX_NR_CPUS);
	assert(nr_cpus >= 0);

	if (target_pid != -1 || profile_cpu != -1)
		nr_cpus = 1;

	parse_symbols();

	return __cmd_top();
}
