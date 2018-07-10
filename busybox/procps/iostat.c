/* vi: set sw=4 ts=4: */
/*
 * Report CPU and I/O stats, based on sysstat version 9.1.2 by Sebastien Godard
 *
 * Copyright (C) 2010 Marek Polacek <mmpolacek@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config IOSTAT
//config:	bool "iostat (7.4 kb)"
//config:	default y
//config:	help
//config:	Report CPU and I/O statistics

//applet:IF_IOSTAT(APPLET(iostat, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_IOSTAT) += iostat.o

#include "libbb.h"
#include <sys/utsname.h>  /* struct utsname */

//#define debug(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#define debug(fmt, ...) ((void)0)

#define MAX_DEVICE_NAME 12
#define MAX_DEVICE_NAME_STR "12"

#if 1
typedef unsigned long long cputime_t;
typedef long long icputime_t;
# define FMT_DATA "ll"
# define CPUTIME_MAX (~0ULL)
#else
typedef unsigned long cputime_t;
typedef long icputime_t;
# define FMT_DATA "l"
# define CPUTIME_MAX (~0UL)
#endif

enum {
	STATS_CPU_USER,
	STATS_CPU_NICE,
	STATS_CPU_SYSTEM,
	STATS_CPU_IDLE,
	STATS_CPU_IOWAIT,
	STATS_CPU_IRQ,
	STATS_CPU_SOFTIRQ,
	STATS_CPU_STEAL,
	STATS_CPU_GUEST,

	GLOBAL_UPTIME,
	SMP_UPTIME,

	N_STATS_CPU,
};

typedef struct {
	cputime_t vector[N_STATS_CPU];
} stats_cpu_t;

typedef struct {
	stats_cpu_t *prev;
	stats_cpu_t *curr;
	cputime_t itv;
} stats_cpu_pair_t;

typedef struct {
	unsigned long long rd_sectors;
	unsigned long long wr_sectors;
	unsigned long rd_ops;
	unsigned long wr_ops;
} stats_dev_data_t;

typedef struct stats_dev {
	struct stats_dev *next;
	char dname[MAX_DEVICE_NAME + 1];
	stats_dev_data_t prev_data;
	stats_dev_data_t curr_data;
} stats_dev_t;

/* Globals. Sort by size and access frequency. */
struct globals {
	smallint show_all;
	unsigned total_cpus;            /* Number of CPUs */
	unsigned clk_tck;               /* Number of clock ticks per second */
	llist_t *dev_name_list;         /* List of devices entered on the command line */
	stats_dev_t *stats_dev_list;
	struct tm tmtime;
	struct {
		const char *str;
		unsigned div;
	} unit;
};
#define G (*ptr_to_globals)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
	G.unit.str = "Blk"; \
	G.unit.div = 1; \
} while (0)

/* Must match option string! */
enum {
	OPT_c = 1 << 0,
	OPT_d = 1 << 1,
	OPT_t = 1 << 2,
	OPT_z = 1 << 3,
	OPT_k = 1 << 4,
	OPT_m = 1 << 5,
};

static ALWAYS_INLINE int this_is_smp(void)
{
	return (G.total_cpus > 1);
}

static void print_header(void)
{
	char buf[32];
	struct utsname uts;

	uname(&uts); /* never fails */

	/* Date representation for the current locale */
	strftime(buf, sizeof(buf), "%x", &G.tmtime);

	printf("%s %s (%s) \t%s \t_%s_\t(%u CPU)\n\n",
			uts.sysname, uts.release, uts.nodename,
			buf, uts.machine, G.total_cpus);
}

static void get_localtime(struct tm *ptm)
{
	time_t timer;
	time(&timer);
	localtime_r(&timer, ptm);
}

static void print_timestamp(void)
{
	char buf[64];
	/* %x: date representation for the current locale */
	/* %X: time representation for the current locale */
	strftime(buf, sizeof(buf), "%x %X", &G.tmtime);
	puts(buf);
}

static cputime_t get_smp_uptime(void)
{
	FILE *fp;
	unsigned long sec, dec;

	fp = xfopen_for_read("/proc/uptime");

	if (fscanf(fp, "%lu.%lu", &sec, &dec) != 2)
		bb_error_msg_and_die("can't read '%s'", "/proc/uptime");

	fclose(fp);

	return (cputime_t)sec * G.clk_tck + dec * G.clk_tck / 100;
}

/* Fetch CPU statistics from /proc/stat */
static void get_cpu_statistics(stats_cpu_t *sc)
{
	FILE *fp;
	char buf[1024];

	fp = xfopen_for_read("/proc/stat");

	memset(sc, 0, sizeof(*sc));

	while (fgets(buf, sizeof(buf), fp)) {
		int i;
		char *ibuf;

		/* Does the line start with "cpu "? */
		if (!starts_with_cpu(buf) || buf[3] != ' ') {
			continue;
		}
		ibuf = buf + 4;
		for (i = STATS_CPU_USER; i <= STATS_CPU_GUEST; i++) {
			ibuf = skip_whitespace(ibuf);
			sscanf(ibuf, "%"FMT_DATA"u", &sc->vector[i]);
			if (i != STATS_CPU_GUEST) {
				sc->vector[GLOBAL_UPTIME] += sc->vector[i];
			}
			ibuf = skip_non_whitespace(ibuf);
		}
		break;
	}

	if (this_is_smp()) {
		sc->vector[SMP_UPTIME] = get_smp_uptime();
	}

	fclose(fp);
}

static ALWAYS_INLINE cputime_t get_interval(cputime_t old, cputime_t new)
{
	cputime_t itv = new - old;

	return (itv == 0) ? 1 : itv;
}

#if CPUTIME_MAX > 0xffffffff
/*
 * Handle overflow conditions properly for counters which can have
 * less bits than cputime_t, depending on the kernel version.
 */
/* Surprisingly, on 32bit inlining is a size win */
static ALWAYS_INLINE cputime_t overflow_safe_sub(cputime_t prev, cputime_t curr)
{
	cputime_t v = curr - prev;

	if ((icputime_t)v < 0     /* curr < prev - counter overflow? */
	 && prev <= 0xffffffff /* kernel uses 32bit value for the counter? */
	) {
		/* Add 33th bit set to 1 to curr, compensating for the overflow */
		/* double shift defeats "warning: left shift count >= width of type" */
		v += ((cputime_t)1 << 16) << 16;
	}
	return v;
}
#else
static ALWAYS_INLINE cputime_t overflow_safe_sub(cputime_t prev, cputime_t curr)
{
	return curr - prev;
}
#endif

static double percent_value(cputime_t prev, cputime_t curr, cputime_t itv)
{
	return ((double)overflow_safe_sub(prev, curr)) / itv * 100;
}

static void print_stats_cpu_struct(stats_cpu_pair_t *stats)
{
	cputime_t *p = stats->prev->vector;
	cputime_t *c = stats->curr->vector;
	printf("        %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f\n",
		percent_value(p[STATS_CPU_USER]  , c[STATS_CPU_USER]  , stats->itv),
		percent_value(p[STATS_CPU_NICE]  , c[STATS_CPU_NICE]  , stats->itv),
		percent_value(p[STATS_CPU_SYSTEM] + p[STATS_CPU_SOFTIRQ] + p[STATS_CPU_IRQ],
			c[STATS_CPU_SYSTEM] + c[STATS_CPU_SOFTIRQ] + c[STATS_CPU_IRQ], stats->itv),
		percent_value(p[STATS_CPU_IOWAIT], c[STATS_CPU_IOWAIT], stats->itv),
		percent_value(p[STATS_CPU_STEAL] , c[STATS_CPU_STEAL] , stats->itv),
		percent_value(p[STATS_CPU_IDLE]  , c[STATS_CPU_IDLE]  , stats->itv)
	);
}

static void cpu_report(stats_cpu_pair_t *stats)
{
	/* Always print a header */
	puts("avg-cpu:  %user   %nice %system %iowait  %steal   %idle");

	/* Print current statistics */
	print_stats_cpu_struct(stats);
}

static void print_stats_dev_struct(stats_dev_t *stats_dev, cputime_t itv)
{
	stats_dev_data_t *p = &stats_dev->prev_data;
	stats_dev_data_t *c = &stats_dev->curr_data;
	if (option_mask32 & OPT_z)
		if (p->rd_ops == c->rd_ops && p->wr_ops == c->wr_ops)
			return;

	printf("%-13s %8.2f %12.2f %12.2f %10llu %10llu\n",
		stats_dev->dname,
		percent_value(p->rd_ops + p->wr_ops, c->rd_ops + c->wr_ops, itv),
		percent_value(p->rd_sectors, c->rd_sectors, itv) / G.unit.div,
		percent_value(p->wr_sectors, c->wr_sectors, itv) / G.unit.div,
		(c->rd_sectors - p->rd_sectors) / G.unit.div,
		(c->wr_sectors - p->wr_sectors) / G.unit.div
	);
}

static void print_devstat_header(void)
{
	printf("Device:%15s%6s%s/s%6s%s/s%6s%s%6s%s\n",
		"tps",
		G.unit.str, "_read", G.unit.str, "_wrtn",
		G.unit.str, "_read", G.unit.str, "_wrtn"
	);
}

/*
 * Is input partition of format [sdaN]?
 */
static int is_partition(const char *dev)
{
	/* Ok, this is naive... */
	return ((dev[0] - 's') | (dev[1] - 'd') | (dev[2] - 'a')) == 0 && isdigit(dev[3]);
}

static stats_dev_t *stats_dev_find_or_new(const char *dev_name)
{
	stats_dev_t **curr = &G.stats_dev_list;

	while (*curr != NULL) {
		if (strcmp((*curr)->dname, dev_name) == 0)
			return *curr;
		curr = &(*curr)->next;
	}

	*curr = xzalloc(sizeof(stats_dev_t));
	strncpy((*curr)->dname, dev_name, MAX_DEVICE_NAME);
	return *curr;
}

static void stats_dev_free(stats_dev_t *stats_dev)
{
	if (stats_dev) {
		stats_dev_free(stats_dev->next);
		free(stats_dev);
	}
}

static void do_disk_statistics(cputime_t itv)
{
	char buf[128];
	char dev_name[MAX_DEVICE_NAME + 1];
	unsigned long long rd_sec_or_dummy;
	unsigned long long wr_sec_or_dummy;
	stats_dev_data_t *curr_data;
	stats_dev_t *stats_dev;
	FILE *fp;
	int rc;

	fp = xfopen_for_read("/proc/diskstats");
	/* Read and possibly print stats from /proc/diskstats */
	while (fgets(buf, sizeof(buf), fp)) {
		sscanf(buf, "%*s %*s %"MAX_DEVICE_NAME_STR"s", dev_name);
		if (G.dev_name_list) {
			/* Is device name in list? */
			if (!llist_find_str(G.dev_name_list, dev_name))
				continue;
		} else if (is_partition(dev_name)) {
			continue;
		}

		stats_dev = stats_dev_find_or_new(dev_name);
		curr_data = &stats_dev->curr_data;

		rc = sscanf(buf, "%*s %*s %*s %lu %llu %llu %llu %lu %*s %llu",
			&curr_data->rd_ops,
			&rd_sec_or_dummy,
			&curr_data->rd_sectors,
			&wr_sec_or_dummy,
			&curr_data->wr_ops,
			&curr_data->wr_sectors);
		if (rc != 6) {
			curr_data->rd_sectors = rd_sec_or_dummy;
			curr_data->wr_sectors = wr_sec_or_dummy;
			//curr_data->rd_ops = ;
			curr_data->wr_ops = (unsigned long)curr_data->rd_sectors;
		}

		if (!G.dev_name_list /* User didn't specify device */
		 && !G.show_all
		 && curr_data->rd_ops == 0
		 && curr_data->wr_ops == 0
		) {
			/* Don't print unused device */
			continue;
		}

		/* Print current statistics */
		print_stats_dev_struct(stats_dev, itv);
		stats_dev->prev_data = *curr_data;
	}

	fclose(fp);
}

static void dev_report(cputime_t itv)
{
	/* Always print a header */
	print_devstat_header();

	/* Fetch current disk statistics */
	do_disk_statistics(itv);
}

//usage:#define iostat_trivial_usage
//usage:       "[-c] [-d] [-t] [-z] [-k|-m] [ALL|BLOCKDEV...] [INTERVAL [COUNT]]"
//usage:#define iostat_full_usage "\n\n"
//usage:       "Report CPU and I/O statistics\n"
//usage:     "\n	-c	Show CPU utilization"
//usage:     "\n	-d	Show device utilization"
//usage:     "\n	-t	Print current time"
//usage:     "\n	-z	Omit devices with no activity"
//usage:     "\n	-k	Use kb/s"
//usage:     "\n	-m	Use Mb/s"

int iostat_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int iostat_main(int argc UNUSED_PARAM, char **argv)
{
	int opt;
	unsigned interval;
	int count;
	stats_cpu_t stats_data[2];
	smallint current_stats;

	INIT_G();

	memset(&stats_data, 0, sizeof(stats_data));

	/* Get number of clock ticks per sec */
	G.clk_tck = bb_clk_tck();

	/* Determine number of CPUs */
	G.total_cpus = get_cpu_count();
	if (G.total_cpus == 0)
		G.total_cpus = 1;

	/* Parse and process arguments */
	/* -k and -m are mutually exclusive */
	opt = getopt32(argv, "^" "cdtzkm" "\0" "k--m:m--k");
	if (!(opt & (OPT_c + OPT_d)))
		/* Default is -cd */
		opt |= OPT_c + OPT_d;

	argv += optind;

	/* Store device names into device list */
	while (*argv && !isdigit(*argv[0])) {
		if (strcmp(*argv, "ALL") != 0) {
			/* If not ALL, save device name */
			char *dev_name = skip_dev_pfx(*argv);
			if (!llist_find_str(G.dev_name_list, dev_name)) {
				llist_add_to(&G.dev_name_list, dev_name);
			}
		} else {
			G.show_all = 1;
		}
		argv++;
	}

	interval = 0;
	count = 1;
	if (*argv) {
		/* Get interval */
		interval = xatoi_positive(*argv);
		count = (interval != 0 ? -1 : 1);
		argv++;
		if (*argv)
			/* Get count value */
			count = xatoi_positive(*argv);
	}

	if (opt & OPT_m) {
		G.unit.str = " MB";
		G.unit.div = 2048;
	}

	if (opt & OPT_k) {
		G.unit.str = " kB";
		G.unit.div = 2;
	}

	get_localtime(&G.tmtime);

	/* Display header */
	print_header();

	current_stats = 0;
	/* Main loop */
	for (;;) {
		stats_cpu_pair_t stats;

		stats.prev = &stats_data[current_stats ^ 1];
		stats.curr = &stats_data[current_stats];

		/* Fill the time structure */
		get_localtime(&G.tmtime);

		/* Fetch current CPU statistics */
		get_cpu_statistics(stats.curr);

		/* Get interval */
		stats.itv = get_interval(
			stats.prev->vector[GLOBAL_UPTIME],
			stats.curr->vector[GLOBAL_UPTIME]
		);

		if (opt & OPT_t)
			print_timestamp();

		if (opt & OPT_c) {
			cpu_report(&stats);
			if (opt & OPT_d)
				/* Separate outputs by a newline */
				bb_putchar('\n');
		}

		if (opt & OPT_d) {
			if (this_is_smp()) {
				stats.itv = get_interval(
					stats.prev->vector[SMP_UPTIME],
					stats.curr->vector[SMP_UPTIME]
				);
			}
			dev_report(stats.itv);
		}

		bb_putchar('\n');

		if (count > 0) {
			if (--count == 0)
				break;
		}

		/* Swap stats */
		current_stats ^= 1;

		sleep(interval);
	}

	if (ENABLE_FEATURE_CLEAN_UP) {
		llist_free(G.dev_name_list, NULL);
		stats_dev_free(G.stats_dev_list);
		free(&G);
	}

	return EXIT_SUCCESS;
}
