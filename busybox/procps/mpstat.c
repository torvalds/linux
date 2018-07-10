/* vi: set sw=4 ts=4: */
/*
 * Per-processor statistics, based on sysstat version 9.1.2 by Sebastien Godard
 *
 * Copyright (C) 2010 Marek Polacek <mmpolacek@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config MPSTAT
//config:	bool "mpstat (10 kb)"
//config:	default y
//config:	help
//config:	Per-processor statistics

//applet:IF_MPSTAT(APPLET(mpstat, BB_DIR_BIN, BB_SUID_DROP))
/* shouldn't be noexec: "mpstat INTERVAL" runs indefinitely */

//kbuild:lib-$(CONFIG_MPSTAT) += mpstat.o

#include "libbb.h"
#include <sys/utsname.h>  /* struct utsname */

//#define debug(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#define debug(fmt, ...) ((void)0)

/* Size of /proc/interrupts line, CPU data excluded */
#define INTERRUPTS_LINE    64
/* Maximum number of interrupts */
#define NR_IRQS            256
#define NR_IRQCPU_PREALLOC 3
#define MAX_IRQNAME_LEN    16
#define MAX_PF_NAME        512
/* sysstat 9.0.6 uses width 8, but newer code which also prints /proc/softirqs
 * data needs more: "interrupts" in /proc/softirqs have longer names,
 * most are up to 8 chars, one (BLOCK_IOPOLL) is even longer.
 * We are printing headers in the " IRQNAME/s" form, experimentally
 * anything smaller than 10 chars looks ugly for /proc/softirqs stats.
 */
#define INTRATE_SCRWIDTH      10
#define INTRATE_SCRWIDTH_STR "10"

/* System files */
#define PROCFS_STAT       "/proc/stat"
#define PROCFS_INTERRUPTS "/proc/interrupts"
#define PROCFS_SOFTIRQS   "/proc/softirqs"
#define PROCFS_UPTIME     "/proc/uptime"


#if 1
typedef unsigned long long data_t;
typedef long long idata_t;
#define FMT_DATA "ll"
#define DATA_MAX ULLONG_MAX
#else
typedef unsigned long data_t;
typedef long idata_t;
#define FMT_DATA "l"
#define DATA_MAX ULONG_MAX
#endif


struct stats_irqcpu {
	unsigned interrupts;
	char irq_name[MAX_IRQNAME_LEN];
};

struct stats_cpu {
	data_t cpu_user;
	data_t cpu_nice;
	data_t cpu_system;
	data_t cpu_idle;
	data_t cpu_iowait;
	data_t cpu_steal;
	data_t cpu_irq;
	data_t cpu_softirq;
	data_t cpu_guest;
};

struct stats_irq {
	data_t irq_nr;
};


/* Globals. Sort by size and access frequency. */
struct globals {
	int interval;
	int count;
	unsigned cpu_nr;                /* Number of CPUs */
	unsigned irqcpu_nr;             /* Number of interrupts per CPU */
	unsigned softirqcpu_nr;         /* Number of soft interrupts per CPU */
	unsigned options;
	unsigned hz;
	unsigned cpu_bitmap_len;
	smallint p_option;
	// 9.0.6 does not do it. Try "mpstat -A 1 2" - headers are repeated!
	//smallint header_done;
	//smallint avg_header_done;
	unsigned char *cpu_bitmap;      /* Bit 0: global, bit 1: 1st proc... */
	data_t global_uptime[3];
	data_t per_cpu_uptime[3];
	struct stats_cpu *st_cpu[3];
	struct stats_irq *st_irq[3];
	struct stats_irqcpu *st_irqcpu[3];
	struct stats_irqcpu *st_softirqcpu[3];
	struct tm timestamp[3];
};
#define G (*ptr_to_globals)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)

/* The selected interrupts statistics (bits in G.options) */
enum {
	D_CPU      = 1 << 0,
	D_IRQ_SUM  = 1 << 1,
	D_IRQ_CPU  = 1 << 2,
	D_SOFTIRQS = 1 << 3,
};


/* Is option on? */
static ALWAYS_INLINE int display_opt(int opt)
{
	return (opt & G.options);
}

#if DATA_MAX > 0xffffffff
/*
 * Handle overflow conditions properly for counters which can have
 * less bits than data_t, depending on the kernel version.
 */
/* Surprisingly, on 32bit inlining is a size win */
static ALWAYS_INLINE data_t overflow_safe_sub(data_t prev, data_t curr)
{
	data_t v = curr - prev;

	if ((idata_t)v < 0     /* curr < prev - counter overflow? */
	 && prev <= 0xffffffff /* kernel uses 32bit value for the counter? */
	) {
		/* Add 33th bit set to 1 to curr, compensating for the overflow */
		/* double shift defeats "warning: left shift count >= width of type" */
		v += ((data_t)1 << 16) << 16;
	}
	return v;
}
#else
static ALWAYS_INLINE data_t overflow_safe_sub(data_t prev, data_t curr)
{
	return curr - prev;
}
#endif

static double percent_value(data_t prev, data_t curr, data_t itv)
{
	return ((double)overflow_safe_sub(prev, curr)) / itv * 100;
}

static double hz_value(data_t prev, data_t curr, data_t itv)
{
	//bb_error_msg("curr:%lld prev:%lld G.hz:%u", curr, prev, G.hz);
	return ((double)overflow_safe_sub(prev, curr)) / itv * G.hz;
}

static ALWAYS_INLINE data_t jiffies_diff(data_t old, data_t new)
{
	data_t diff = new - old;
	return (diff == 0) ? 1 : diff;
}

static int is_cpu_in_bitmap(unsigned cpu)
{
	return G.cpu_bitmap[cpu >> 3] & (1 << (cpu & 7));
}

static void write_irqcpu_stats(struct stats_irqcpu *per_cpu_stats[],
		int total_irqs,
		data_t itv,
		int prev, int current,
		const char *prev_str, const char *current_str)
{
	int j;
	int offset, cpu;
	struct stats_irqcpu *p0, *q0;

	/* Check if number of IRQs has changed */
	if (G.interval != 0) {
		for (j = 0; j <= total_irqs; j++) {
			p0 = &per_cpu_stats[current][j];
			if (p0->irq_name[0] != '\0') {
				q0 = &per_cpu_stats[prev][j];
				if (strcmp(p0->irq_name, q0->irq_name) != 0) {
					/* Strings are different */
					break;
				}
			}
		}
	}

	/* Print header */
	printf("\n%-11s  CPU", prev_str);
	{
		/* A bit complex code to "buy back" space if one header is too wide.
		 * Here's how it looks like. BLOCK_IOPOLL eats too much space,
		 * and latter headers use smaller width to compensate:
		 * ...BLOCK/s BLOCK_IOPOLL/s TASKLET/s SCHED/s HRTIMER/s  RCU/s
		 * ...   2.32      0.00      0.01     17.58      0.14    141.96
		 */
		int expected_len = 0;
		int printed_len = 0;
		for (j = 0; j < total_irqs; j++) {
			p0 = &per_cpu_stats[current][j];
			if (p0->irq_name[0] != '\0') {
				int n = (INTRATE_SCRWIDTH-3) - (printed_len - expected_len);
				printed_len += printf(" %*s/s", n > 0 ? n : 0, skip_whitespace(p0->irq_name));
				expected_len += INTRATE_SCRWIDTH;
			}
		}
	}
	bb_putchar('\n');

	for (cpu = 1; cpu <= G.cpu_nr; cpu++) {
		/* Check if we want stats about this CPU */
		if (!is_cpu_in_bitmap(cpu) && G.p_option) {
			continue;
		}

		printf("%-11s %4u", current_str, cpu - 1);

		for (j = 0; j < total_irqs; j++) {
			/* IRQ field set only for proc 0 */
			p0 = &per_cpu_stats[current][j];

			/*
			 * An empty string for irq name means that
			 * interrupt is no longer used.
			 */
			if (p0->irq_name[0] != '\0') {
				offset = j;
				q0 = &per_cpu_stats[prev][offset];

				/*
				 * If we want stats for the time since boot
				 * we have p0->irq != q0->irq.
				 */
				if (strcmp(p0->irq_name, q0->irq_name) != 0
				 && G.interval != 0
				) {
					if (j) {
						offset = j - 1;
						q0 = &per_cpu_stats[prev][offset];
					}
					if (strcmp(p0->irq_name, q0->irq_name) != 0
					 && (j + 1 < total_irqs)
					) {
						offset = j + 1;
						q0 = &per_cpu_stats[prev][offset];
					}
				}

				if (strcmp(p0->irq_name, q0->irq_name) == 0
				 || G.interval == 0
				) {
					struct stats_irqcpu *p, *q;
					p = &per_cpu_stats[current][(cpu - 1) * total_irqs + j];
					q = &per_cpu_stats[prev][(cpu - 1) * total_irqs + offset];
					printf("%"INTRATE_SCRWIDTH_STR".2f",
						(double)(p->interrupts - q->interrupts) / itv * G.hz);
				} else {
					printf("        N/A");
				}
			}
		}
		bb_putchar('\n');
	}
}

static data_t get_per_cpu_interval(const struct stats_cpu *scc,
		const struct stats_cpu *scp)
{
	return ((scc->cpu_user + scc->cpu_nice +
		 scc->cpu_system + scc->cpu_iowait +
		 scc->cpu_idle + scc->cpu_steal +
		 scc->cpu_irq + scc->cpu_softirq) -
		(scp->cpu_user + scp->cpu_nice +
		 scp->cpu_system + scp->cpu_iowait +
		 scp->cpu_idle + scp->cpu_steal +
		 scp->cpu_irq + scp->cpu_softirq));
}

static void print_stats_cpu_struct(const struct stats_cpu *p,
		const struct stats_cpu *c,
		data_t itv)
{
	printf(" %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f\n",
		percent_value(p->cpu_user - p->cpu_guest,
		/**/                          c->cpu_user - c->cpu_guest, itv),
		percent_value(p->cpu_nice   , c->cpu_nice   , itv),
		percent_value(p->cpu_system , c->cpu_system , itv),
		percent_value(p->cpu_iowait , c->cpu_iowait , itv),
		percent_value(p->cpu_irq    , c->cpu_irq    , itv),
		percent_value(p->cpu_softirq, c->cpu_softirq, itv),
		percent_value(p->cpu_steal  , c->cpu_steal  , itv),
		percent_value(p->cpu_guest  , c->cpu_guest  , itv),
		percent_value(p->cpu_idle   , c->cpu_idle   , itv)
	);
}

static void write_stats_core(int prev, int current,
		const char *prev_str, const char *current_str)
{
	struct stats_cpu *scc, *scp;
	data_t itv, global_itv;
	int cpu;

	/* Compute time interval */
	itv = global_itv = jiffies_diff(G.global_uptime[prev], G.global_uptime[current]);

	/* Reduce interval to one CPU */
	if (G.cpu_nr > 1)
		itv = jiffies_diff(G.per_cpu_uptime[prev], G.per_cpu_uptime[current]);

	/* Print CPU stats */
	if (display_opt(D_CPU)) {

		///* This is done exactly once */
		//if (!G.header_done) {
			printf("\n%-11s  CPU    %%usr   %%nice    %%sys %%iowait    %%irq   %%soft  %%steal  %%guest   %%idle\n",
				prev_str
			);
		//	G.header_done = 1;
		//}

		for (cpu = 0; cpu <= G.cpu_nr; cpu++) {
			data_t per_cpu_itv;

			/* Print stats about this particular CPU? */
			if (!is_cpu_in_bitmap(cpu))
				continue;

			scc = &G.st_cpu[current][cpu];
			scp = &G.st_cpu[prev][cpu];
			per_cpu_itv = global_itv;

			printf((cpu ? "%-11s %4u" : "%-11s  all"), current_str, cpu - 1);
			if (cpu) {
				double idle;
				/*
				 * If the CPU is offline, then it isn't in /proc/stat,
				 * so all values are 0.
				 * NB: Guest time is already included in user time.
				 */
				if ((scc->cpu_user | scc->cpu_nice | scc->cpu_system |
				     scc->cpu_iowait | scc->cpu_idle | scc->cpu_steal |
				     scc->cpu_irq | scc->cpu_softirq) == 0
				) {
					/*
					 * Set current struct fields to values from prev.
					 * iteration. Then their values won't jump from
					 * zero, when the CPU comes back online.
					 */
					*scc = *scp;
					idle = 0.0;
					goto print_zeros;
				}
				/* Compute interval again for current proc */
				per_cpu_itv = get_per_cpu_interval(scc, scp);
				if (per_cpu_itv == 0) {
					/*
					 * If the CPU is tickless then there is no change in CPU values
					 * but the sum of values is not zero.
					 */
					idle = 100.0;
 print_zeros:
					printf(" %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f\n",
						0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, idle);
					continue;
				}
			}
			print_stats_cpu_struct(scp, scc, per_cpu_itv);
		}
	}

	/* Print total number of IRQs per CPU */
	if (display_opt(D_IRQ_SUM)) {

		///* Print average header, this is done exactly once */
		//if (!G.avg_header_done) {
			printf("\n%-11s  CPU    intr/s\n", prev_str);
		//	G.avg_header_done = 1;
		//}

		for (cpu = 0; cpu <= G.cpu_nr; cpu++) {
			data_t per_cpu_itv;

			/* Print stats about this CPU? */
			if (!is_cpu_in_bitmap(cpu))
				continue;

			per_cpu_itv = itv;
			printf((cpu ? "%-11s %4u" : "%-11s  all"), current_str, cpu - 1);
			if (cpu) {
				scc = &G.st_cpu[current][cpu];
				scp = &G.st_cpu[prev][cpu];
				/* Compute interval again for current proc */
				per_cpu_itv = get_per_cpu_interval(scc, scp);
				if (per_cpu_itv == 0) {
					printf(" %9.2f\n", 0.0);
					continue;
				}
			}
			//bb_error_msg("G.st_irq[%u][%u].irq_nr:%lld - G.st_irq[%u][%u].irq_nr:%lld",
			// current, cpu, G.st_irq[prev][cpu].irq_nr, prev, cpu, G.st_irq[current][cpu].irq_nr);
			printf(" %9.2f\n", hz_value(G.st_irq[prev][cpu].irq_nr, G.st_irq[current][cpu].irq_nr, per_cpu_itv));
		}
	}

	if (display_opt(D_IRQ_CPU)) {
		write_irqcpu_stats(G.st_irqcpu, G.irqcpu_nr,
				itv,
				prev, current,
				prev_str, current_str
		);
	}

	if (display_opt(D_SOFTIRQS)) {
		write_irqcpu_stats(G.st_softirqcpu, G.softirqcpu_nr,
				itv,
				prev, current,
				prev_str, current_str
		);
	}
}

/*
 * Print the statistics
 */
static void write_stats(int current)
{
	char prev_time[16];
	char curr_time[16];

	strftime(prev_time, sizeof(prev_time), "%X", &G.timestamp[!current]);
	strftime(curr_time, sizeof(curr_time), "%X", &G.timestamp[current]);

	write_stats_core(!current, current, prev_time, curr_time);
}

static void write_stats_avg(int current)
{
	write_stats_core(2, current, "Average:", "Average:");
}

/*
 * Read CPU statistics
 */
static void get_cpu_statistics(struct stats_cpu *cpu, data_t *up, data_t *up0)
{
	FILE *fp;
	char buf[1024];

	fp = xfopen_for_read(PROCFS_STAT);

	while (fgets(buf, sizeof(buf), fp)) {
		data_t sum;
		unsigned cpu_number;
		struct stats_cpu *cp;

		if (!starts_with_cpu(buf))
			continue; /* not "cpu" */

		cp = cpu; /* for "cpu " case */
		if (buf[3] != ' ') {
			/* "cpuN " */
			if (G.cpu_nr == 0
			 || sscanf(buf + 3, "%u ", &cpu_number) != 1
			 || cpu_number >= G.cpu_nr
			) {
				continue;
			}
			cp = &cpu[cpu_number + 1];
		}

		/* Read the counters, save them */
		/* Not all fields have to be present */
		memset(cp, 0, sizeof(*cp));
		sscanf(buf, "%*s"
			" %"FMT_DATA"u %"FMT_DATA"u %"FMT_DATA"u"
			" %"FMT_DATA"u %"FMT_DATA"u %"FMT_DATA"u"
			" %"FMT_DATA"u %"FMT_DATA"u %"FMT_DATA"u",
			&cp->cpu_user, &cp->cpu_nice, &cp->cpu_system,
			&cp->cpu_idle, &cp->cpu_iowait, &cp->cpu_irq,
			&cp->cpu_softirq, &cp->cpu_steal, &cp->cpu_guest
		);
		/*
		 * Compute uptime in jiffies (1/HZ), it'll be the sum of
		 * individual CPU's uptimes.
		 * NB: We have to omit cpu_guest, because cpu_user includes it.
		 */
		sum = cp->cpu_user + cp->cpu_nice + cp->cpu_system +
			cp->cpu_idle + cp->cpu_iowait + cp->cpu_irq +
			cp->cpu_softirq + cp->cpu_steal;

		if (buf[3] == ' ') {
			/* "cpu " */
			*up = sum;
		} else {
			/* "cpuN " */
			if (cpu_number == 0 && *up0 != 0) {
				/* Compute uptime of single CPU */
				*up0 = sum;
			}
		}
	}
	fclose(fp);
}

/*
 * Read IRQs from /proc/stat
 */
static void get_irqs_from_stat(struct stats_irq *irq)
{
	FILE *fp;
	char buf[1024];

	fp = xfopen_for_read(PROCFS_STAT);

	while (fgets(buf, sizeof(buf), fp)) {
		//bb_error_msg("/proc/stat:'%s'", buf);
		if (is_prefixed_with(buf, "intr ")) {
			/* Read total number of IRQs since system boot */
			sscanf(buf + 5, "%"FMT_DATA"u", &irq->irq_nr);
		}
	}

	fclose(fp);
}

/*
 * Read stats from /proc/interrupts or /proc/softirqs
 */
static void get_irqs_from_interrupts(const char *fname,
		struct stats_irqcpu *per_cpu_stats[],
		int irqs_per_cpu, int current)
{
	FILE *fp;
	struct stats_irq *irq_i;
	struct stats_irqcpu *ic;
	char *buf;
	unsigned buflen;
	unsigned cpu;
	unsigned irq;
	int cpu_index[G.cpu_nr];
	int iindex;

// Moved to caller.
// Otherwise reading of /proc/softirqs
// was resetting counts to 0 after we painstakingly collected them from
// /proc/interrupts. Which resulted in:
// 01:32:47 PM  CPU    intr/s
// 01:32:47 PM  all    591.47
// 01:32:47 PM    0      0.00 <= ???
// 01:32:47 PM    1      0.00 <= ???
//	for (cpu = 1; cpu <= G.cpu_nr; cpu++) {
//		G.st_irq[current][cpu].irq_nr = 0;
//		//bb_error_msg("G.st_irq[%u][%u].irq_nr=0", current, cpu);
//	}

	fp = fopen_for_read(fname);
	if (!fp)
		return;

	buflen = INTERRUPTS_LINE + 16 * G.cpu_nr;
	buf = xmalloc(buflen);

	/* Parse header and determine, which CPUs are online */
	iindex = 0;
	while (fgets(buf, buflen, fp)) {
		char *cp, *next;
		next = buf;
		while ((cp = strstr(next, "CPU")) != NULL
		 && iindex < G.cpu_nr
		) {
			cpu = strtoul(cp + 3, &next, 10);
			cpu_index[iindex++] = cpu;
		}
		if (iindex) /* We found header */
			break;
	}

	irq = 0;
	while (fgets(buf, buflen, fp)
	 && irq < irqs_per_cpu
	) {
		int len;
		char last_char;
		char *cp;

		/* Skip over "IRQNAME:" */
		cp = strchr(buf, ':');
		if (!cp)
			continue;
		last_char = cp[-1];

		ic = &per_cpu_stats[current][irq];
		len = cp - buf;
		if (len >= sizeof(ic->irq_name)) {
			len = sizeof(ic->irq_name) - 1;
		}
		safe_strncpy(ic->irq_name, buf, len + 1);
		//bb_error_msg("%s: irq%d:'%s' buf:'%s'", fname, irq, ic->irq_name, buf);
		cp++;

		for (cpu = 0; cpu < iindex; cpu++) {
			char *next;
			ic = &per_cpu_stats[current][cpu_index[cpu] * irqs_per_cpu + irq];
			irq_i = &G.st_irq[current][cpu_index[cpu] + 1];
			ic->interrupts = strtoul(cp, &next, 10);
			/* Count only numerical IRQs */
			if (isdigit(last_char)) {
				irq_i->irq_nr += ic->interrupts;
				//bb_error_msg("G.st_irq[%u][%u].irq_nr + %u = %lld",
				// current, cpu_index[cpu] + 1, ic->interrupts, irq_i->irq_nr);
			}
			cp = next;
		}
		irq++;
	}
	fclose(fp);
	free(buf);

	while (irq < irqs_per_cpu) {
		/* Number of interrupts per CPU has changed */
		ic = &per_cpu_stats[current][irq];
		ic->irq_name[0] = '\0'; /* False interrupt */
		irq++;
	}
}

static void get_uptime(data_t *uptime)
{
	FILE *fp;
	char buf[sizeof(long)*3 * 2 + 4]; /* enough for long.long */
	unsigned long uptime_sec, decimal;

	fp = xfopen_for_read(PROCFS_UPTIME);
	if (fgets(buf, sizeof(buf), fp)) {
		if (sscanf(buf, "%lu.%lu", &uptime_sec, &decimal) == 2) {
			*uptime = (data_t)uptime_sec * G.hz + decimal * G.hz / 100;
		}
	}

	fclose(fp);
}

static void get_localtime(struct tm *tm)
{
	time_t timer;
	time(&timer);
	localtime_r(&timer, tm);
}

static void alarm_handler(int sig UNUSED_PARAM)
{
	signal(SIGALRM, alarm_handler);
	alarm(G.interval);
}

static void main_loop(void)
{
	unsigned current;
	unsigned cpus;

	/* Read the stats */
	if (G.cpu_nr > 1) {
		G.per_cpu_uptime[0] = 0;
		get_uptime(&G.per_cpu_uptime[0]);
	}

	get_cpu_statistics(G.st_cpu[0], &G.global_uptime[0], &G.per_cpu_uptime[0]);

	if (display_opt(D_IRQ_SUM))
		get_irqs_from_stat(G.st_irq[0]);

	if (display_opt(D_IRQ_SUM | D_IRQ_CPU))
		get_irqs_from_interrupts(PROCFS_INTERRUPTS, G.st_irqcpu,
					G.irqcpu_nr, 0);

	if (display_opt(D_SOFTIRQS))
		get_irqs_from_interrupts(PROCFS_SOFTIRQS, G.st_softirqcpu,
					G.softirqcpu_nr, 0);

	if (G.interval == 0) {
		/* Display since boot time */
		cpus = G.cpu_nr + 1;
		G.timestamp[1] = G.timestamp[0];
		memset(G.st_cpu[1], 0, sizeof(G.st_cpu[1][0]) * cpus);
		memset(G.st_irq[1], 0, sizeof(G.st_irq[1][0]) * cpus);
		memset(G.st_irqcpu[1], 0, sizeof(G.st_irqcpu[1][0]) * cpus * G.irqcpu_nr);
		memset(G.st_softirqcpu[1], 0, sizeof(G.st_softirqcpu[1][0]) * cpus * G.softirqcpu_nr);

		write_stats(0);

		/* And we're done */
		return;
	}

	/* Set a handler for SIGALRM */
	alarm_handler(0);

	/* Save the stats we already have. We need them to compute the average */
	G.timestamp[2] = G.timestamp[0];
	G.global_uptime[2] = G.global_uptime[0];
	G.per_cpu_uptime[2] = G.per_cpu_uptime[0];
	cpus = G.cpu_nr + 1;
	memcpy(G.st_cpu[2], G.st_cpu[0], sizeof(G.st_cpu[0][0]) * cpus);
	memcpy(G.st_irq[2], G.st_irq[0], sizeof(G.st_irq[0][0]) * cpus);
	memcpy(G.st_irqcpu[2], G.st_irqcpu[0], sizeof(G.st_irqcpu[0][0]) * cpus * G.irqcpu_nr);
	if (display_opt(D_SOFTIRQS)) {
		memcpy(G.st_softirqcpu[2], G.st_softirqcpu[0],
			sizeof(G.st_softirqcpu[0][0]) * cpus * G.softirqcpu_nr);
	}

	current = 1;
	while (1) {
		/* Suspend until a signal is received */
		pause();

		/* Set structures to 0 to distinguish off/online CPUs */
		memset(&G.st_cpu[current][/*cpu:*/ 1], 0, sizeof(G.st_cpu[0][0]) * G.cpu_nr);

		get_localtime(&G.timestamp[current]);

		/* Read stats */
		if (G.cpu_nr > 1) {
			G.per_cpu_uptime[current] = 0;
			get_uptime(&G.per_cpu_uptime[current]);
		}
		get_cpu_statistics(G.st_cpu[current], &G.global_uptime[current], &G.per_cpu_uptime[current]);

		if (display_opt(D_IRQ_SUM))
			get_irqs_from_stat(G.st_irq[current]);

		if (display_opt(D_IRQ_SUM | D_IRQ_CPU)) {
			int cpu;
			for (cpu = 1; cpu <= G.cpu_nr; cpu++) {
				G.st_irq[current][cpu].irq_nr = 0;
			}
			/* accumulates .irq_nr */
			get_irqs_from_interrupts(PROCFS_INTERRUPTS, G.st_irqcpu,
					G.irqcpu_nr, current);
		}

		if (display_opt(D_SOFTIRQS))
			get_irqs_from_interrupts(PROCFS_SOFTIRQS,
					G.st_softirqcpu,
					G.softirqcpu_nr, current);

		write_stats(current);

		if (G.count > 0) {
			if (--G.count == 0)
				break;
		}

		current ^= 1;
	}

	/* Print average statistics */
	write_stats_avg(current);
}

/* Initialization */

static void alloc_struct(int cpus)
{
	int i;
	for (i = 0; i < 3; i++) {
		G.st_cpu[i] = xzalloc(sizeof(G.st_cpu[i][0]) * cpus);
		G.st_irq[i] = xzalloc(sizeof(G.st_irq[i][0]) * cpus);
		G.st_irqcpu[i] = xzalloc(sizeof(G.st_irqcpu[i][0]) * cpus * G.irqcpu_nr);
		G.st_softirqcpu[i] = xzalloc(sizeof(G.st_softirqcpu[i][0]) * cpus * G.softirqcpu_nr);
	}
	G.cpu_bitmap_len = (cpus >> 3) + 1;
	G.cpu_bitmap = xzalloc(G.cpu_bitmap_len);
}

static void print_header(struct tm *t)
{
	char cur_date[16];
	struct utsname uts;

	/* Get system name, release number and hostname */
	uname(&uts);

	strftime(cur_date, sizeof(cur_date), "%x", t);

	printf("%s %s (%s)\t%s\t_%s_\t(%u CPU)\n",
		uts.sysname, uts.release, uts.nodename, cur_date, uts.machine, G.cpu_nr);
}

/*
 * Get number of interrupts available per processor
 */
static int get_irqcpu_nr(const char *f, int max_irqs)
{
	FILE *fp;
	char *line;
	unsigned linelen;
	unsigned irq;

	fp = fopen_for_read(f);
	if (!fp)  /* No interrupts file */
		return 0;

	linelen = INTERRUPTS_LINE + 16 * G.cpu_nr;
	line = xmalloc(linelen);

	irq = 0;
	while (fgets(line, linelen, fp)
	 && irq < max_irqs
	) {
		int p = strcspn(line, ":");
		if ((p > 0) && (p < 16))
			irq++;
	}

	fclose(fp);
	free(line);

	return irq;
}

//usage:#define mpstat_trivial_usage
//usage:       "[-A] [-I SUM|CPU|ALL|SCPU] [-u] [-P num|ALL] [INTERVAL [COUNT]]"
//usage:#define mpstat_full_usage "\n\n"
//usage:       "Per-processor statistics\n"
//usage:     "\n	-A			Same as -I ALL -u -P ALL"
//usage:     "\n	-I SUM|CPU|ALL|SCPU	Report interrupt statistics"
//usage:     "\n	-P num|ALL		Processor to monitor"
//usage:     "\n	-u			Report CPU utilization"

int mpstat_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int mpstat_main(int argc UNUSED_PARAM, char **argv)
{
	char *opt_irq_fmt;
	char *opt_set_cpu;
	int i, opt;
	enum {
		OPT_ALL    = 1 << 0, /* -A */
		OPT_INTS   = 1 << 1, /* -I */
		OPT_SETCPU = 1 << 2, /* -P */
		OPT_UTIL   = 1 << 3, /* -u */
	};

	/* Dont buffer data if redirected to a pipe */
	setbuf(stdout, NULL);

	INIT_G();

	G.interval = -1;

	/* Get number of processors */
	G.cpu_nr = get_cpu_count();

	/* Get number of clock ticks per sec */
	G.hz = bb_clk_tck();

	/* Calculate number of interrupts per processor */
	G.irqcpu_nr = get_irqcpu_nr(PROCFS_INTERRUPTS, NR_IRQS) + NR_IRQCPU_PREALLOC;

	/* Calculate number of soft interrupts per processor */
	G.softirqcpu_nr = get_irqcpu_nr(PROCFS_SOFTIRQS, NR_IRQS) + NR_IRQCPU_PREALLOC;

	/* Allocate space for structures. + 1 for global structure. */
	alloc_struct(G.cpu_nr + 1);

	/* Parse and process arguments */
	opt = getopt32(argv, "AI:P:u", &opt_irq_fmt, &opt_set_cpu);
	argv += optind;

	if (*argv) {
		/* Get interval */
		G.interval = xatoi_positive(*argv);
		G.count = -1;
		argv++;
		if (*argv) {
			/* Get count value */
			if (G.interval == 0)
				bb_show_usage();
			G.count = xatoi_positive(*argv);
			//if (*++argv)
			//	bb_show_usage();
		}
	}
	if (G.interval < 0)
		G.interval = 0;

	if (opt & OPT_ALL) {
		G.p_option = 1;
		G.options |= D_CPU + D_IRQ_SUM + D_IRQ_CPU + D_SOFTIRQS;
		/* Select every CPU */
		memset(G.cpu_bitmap, 0xff, G.cpu_bitmap_len);
	}

	if (opt & OPT_INTS) {
		static const char v[] = {
			D_IRQ_CPU, D_IRQ_SUM, D_SOFTIRQS,
			D_IRQ_SUM + D_IRQ_CPU + D_SOFTIRQS
		};
		i = index_in_strings("CPU\0SUM\0SCPU\0ALL\0", opt_irq_fmt);
		if (i == -1)
			bb_show_usage();
		G.options |= v[i];
	}

	if ((opt & OPT_UTIL) /* -u? */
	 || G.options == 0  /* nothing? (use default then) */
	) {
		G.options |= D_CPU;
	}

	if (opt & OPT_SETCPU) {
		char *t;
		G.p_option = 1;

		for (t = strtok(opt_set_cpu, ","); t; t = strtok(NULL, ",")) {
			if (strcmp(t, "ALL") == 0) {
				/* Select every CPU */
				memset(G.cpu_bitmap, 0xff, G.cpu_bitmap_len);
			} else {
				/* Get CPU number */
				unsigned n = xatoi_positive(t);
				if (n >= G.cpu_nr)
					bb_error_msg_and_die("not that many processors");
				n++;
				G.cpu_bitmap[n >> 3] |= 1 << (n & 7);
			}
		}
	}

	if (!G.p_option)
		/* Display global stats */
		G.cpu_bitmap[0] = 1;

	/* Get time */
	get_localtime(&G.timestamp[0]);

	/* Display header */
	print_header(&G.timestamp[0]);

	/* The main loop */
	main_loop();

	if (ENABLE_FEATURE_CLEAN_UP) {
		/* Clean up */
		for (i = 0; i < 3; i++) {
			free(G.st_cpu[i]);
			free(G.st_irq[i]);
			free(G.st_irqcpu[i]);
			free(G.st_softirqcpu[i]);
		}
		free(G.cpu_bitmap);
		free(&G);
	}

	return EXIT_SUCCESS;
}
