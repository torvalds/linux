/* vi: set sw=4 ts=4: */
/*
 * A mini 'powertop' utility:
 *   Analyze power consumption on Intel-based laptops.
 * Based on powertop 1.11.
 *
 * Copyright (C) 2010 Marek Polacek <mmpolacek@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config POWERTOP
//config:	bool "powertop (9.1 kb)"
//config:	default y
//config:	help
//config:	Analyze power consumption on Intel-based laptops
//config:
//config:config FEATURE_POWERTOP_INTERACTIVE
//config:	bool "Accept keyboard commands"
//config:	default y
//config:	depends on POWERTOP
//config:	help
//config:	Without this, powertop will only refresh display every 10 seconds.
//config:	No keyboard commands will work, only ^C to terminate.

//applet:IF_POWERTOP(APPLET(powertop, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_POWERTOP) += powertop.o

// XXX This should be configurable
#define ENABLE_FEATURE_POWERTOP_PROCIRQ 1

#include "libbb.h"


//#define debug(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#define debug(fmt, ...) ((void)0)


#define BLOATY_HPET_IRQ_NUM_DETECTION 0
#define MAX_CSTATE_COUNT   8
#define IRQCOUNT           40


#define DEFAULT_SLEEP      10
#define DEFAULT_SLEEP_STR "10"

/* Frequency of the ACPI timer */
#define FREQ_ACPI          3579.545
#define FREQ_ACPI_1000     3579545

/* Max filename length of entry in /sys/devices subsystem */
#define BIG_SYSNAME_LEN    16

#define ESC "\033"

typedef unsigned long long ullong;

struct line {
	char *string;
	int count;
	/*int disk_count;*/
};

#if ENABLE_FEATURE_POWERTOP_PROCIRQ
struct irqdata {
	smallint active;
	int number;
	ullong count;
	char irq_desc[32];
};
#endif

struct globals {
	struct line *lines; /* the most often used member */
	int lines_cnt;
	int lines_cumulative_count;
	int maxcstate;
	unsigned total_cpus;
	smallint cant_enable_timer_stats;
#if ENABLE_FEATURE_POWERTOP_PROCIRQ
# if BLOATY_HPET_IRQ_NUM_DETECTION
	smallint scanned_timer_list;
	int percpu_hpet_start;
	int percpu_hpet_end;
# endif
	int interrupt_0;
	int total_interrupt;
	struct irqdata interrupts[IRQCOUNT];
#endif
	ullong start_usage[MAX_CSTATE_COUNT];
	ullong last_usage[MAX_CSTATE_COUNT];
	ullong start_duration[MAX_CSTATE_COUNT];
	ullong last_duration[MAX_CSTATE_COUNT];
#if ENABLE_FEATURE_POWERTOP_INTERACTIVE
	struct termios init_settings;
#endif
};
#define G (*ptr_to_globals)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)

#if ENABLE_FEATURE_POWERTOP_INTERACTIVE
static void reset_term(void)
{
	tcsetattr_stdin_TCSANOW(&G.init_settings);
}

static void sig_handler(int signo UNUSED_PARAM)
{
	reset_term();
	_exit(EXIT_FAILURE);
}
#endif

static int write_str_to_file(const char *fname, const char *str)
{
	FILE *fp = fopen_for_write(fname);
	if (!fp)
		return 1;
	fputs(str, fp);
	fclose(fp);
	return 0;
}

/* Make it more readable */
#define start_timer() write_str_to_file("/proc/timer_stats", "1\n")
#define stop_timer()  write_str_to_file("/proc/timer_stats", "0\n")

static NOINLINE void clear_lines(void)
{
	int i;
	if (G.lines) {
		for (i = 0; i < G.lines_cnt; i++)
			free(G.lines[i].string);
		free(G.lines);
		G.lines_cnt = 0;
		G.lines = NULL;
	}
}

static void update_lines_cumulative_count(void)
{
	int i;
	for (i = 0; i < G.lines_cnt; i++)
		G.lines_cumulative_count += G.lines[i].count;
}

static int line_compare(const void *p1, const void *p2)
{
	const struct line *a = p1;
	const struct line *b = p2;
	return (b->count /*+ 50 * b->disk_count*/) - (a->count /*+ 50 * a->disk_count*/);
}

static void sort_lines(void)
{
	qsort(G.lines, G.lines_cnt, sizeof(G.lines[0]), line_compare);
}

/* Save C-state usage and duration. Also update maxcstate. */
static void read_cstate_counts(ullong *usage, ullong *duration)
{
	DIR *dir;
	struct dirent *d;

	dir = opendir("/proc/acpi/processor");
	if (!dir)
		return;

	while ((d = readdir(dir)) != NULL) {
		FILE *fp;
		char buf[192];
		int level;
		int len;

		len = strlen(d->d_name); /* "CPUnn" */
		if (len < 3 || len > BIG_SYSNAME_LEN)
			continue;

		sprintf(buf, "%s/%s/power", "/proc/acpi/processor", d->d_name);
		fp = fopen_for_read(buf);
		if (!fp)
			continue;

// Example file contents:
// active state:            C0
// max_cstate:              C8
// maximum allowed latency: 2000000000 usec
// states:
//     C1:                  type[C1] promotion[--] demotion[--] latency[001] usage[00006173] duration[00000000000000000000]
//     C2:                  type[C2] promotion[--] demotion[--] latency[001] usage[00085191] duration[00000000000083024907]
//     C3:                  type[C3] promotion[--] demotion[--] latency[017] usage[01017622] duration[00000000017921327182]
		level = 0;
		while (fgets(buf, sizeof(buf), fp)) {
			char *p = strstr(buf, "age[");
			if (!p)
				continue;
			p += 4;
			usage[level] += bb_strtoull(p, NULL, 10) + 1;
			p = strstr(buf, "ation[");
			if (!p)
				continue;
			p += 6;
			duration[level] += bb_strtoull(p, NULL, 10);

			if (level >= MAX_CSTATE_COUNT-1)
				break;
			level++;
			if (level > G.maxcstate)  /* update maxcstate */
				G.maxcstate = level;
		}
		fclose(fp);
	}
	closedir(dir);
}

/* Add line and/or update count */
static void save_line(const char *string, int count)
{
	int i;
	for (i = 0; i < G.lines_cnt; i++) {
		if (strcmp(string, G.lines[i].string) == 0) {
			/* It's already there, only update count */
			G.lines[i].count += count;
			return;
		}
	}

	/* Add new line */
	G.lines = xrealloc_vector(G.lines, 4, G.lines_cnt);
	G.lines[G.lines_cnt].string = xstrdup(string);
	G.lines[G.lines_cnt].count = count;
	/*G.lines[G.lines_cnt].disk_count = 0;*/
	G.lines_cnt++;
}

#if ENABLE_FEATURE_POWERTOP_PROCIRQ
static int is_hpet_irq(const char *name)
{
	char *p;
# if BLOATY_HPET_IRQ_NUM_DETECTION
	long hpet_chan;

	/* Learn the range of existing hpet timers. This is done once */
	if (!G.scanned_timer_list) {
		FILE *fp;
		char buf[80];

		G.scanned_timer_list = true;
		fp = fopen_for_read("/proc/timer_list");
		if (!fp)
			return 0;

		while (fgets(buf, sizeof(buf), fp)) {
			p = strstr(buf, "Clock Event Device: hpet");
			if (!p)
				continue;
			p += sizeof("Clock Event Device: hpet")-1;
			if (!isdigit(*p))
				continue;
			hpet_chan = xatoi_positive(p);
			if (hpet_chan < G.percpu_hpet_start)
				G.percpu_hpet_start = hpet_chan;
			if (hpet_chan > G.percpu_hpet_end)
				G.percpu_hpet_end = hpet_chan;
		}
		fclose(fp);
	}
# endif
//TODO: optimize
	p = strstr(name, "hpet");
	if (!p)
		return 0;
	p += 4;
	if (!isdigit(*p))
		return 0;
# if BLOATY_HPET_IRQ_NUM_DETECTION
	hpet_chan = xatoi_positive(p);
	if (hpet_chan < G.percpu_hpet_start || hpet_chan > G.percpu_hpet_end)
		return 0;
# endif
	return 1;
}

/* Save new IRQ count, return delta from old one */
static int save_irq_count(int irq, ullong count)
{
	int unused = IRQCOUNT;
	int i;
	for (i = 0; i < IRQCOUNT; i++) {
		if (G.interrupts[i].active && G.interrupts[i].number == irq) {
			ullong old = G.interrupts[i].count;
			G.interrupts[i].count = count;
			return count - old;
		}
		if (!G.interrupts[i].active && unused > i)
			unused = i;
	}
	if (unused < IRQCOUNT) {
		G.interrupts[unused].active = 1;
		G.interrupts[unused].count = count;
		G.interrupts[unused].number = irq;
	}
	return count;
}

/* Read /proc/interrupts, save IRQ counts and IRQ description */
static void process_irq_counts(void)
{
	FILE *fp;
	char buf[128];

	/* Reset values */
	G.interrupt_0 = 0;
	G.total_interrupt = 0;

	fp = xfopen_for_read("/proc/interrupts");
	while (fgets(buf, sizeof(buf), fp)) {
		char irq_desc[sizeof("   <kernel IPI> : ") + sizeof(buf)];
		char *p;
		const char *name;
		int nr;
		ullong count;
		ullong delta;

		p = strchr(buf, ':');
		if (!p)
			continue;
		/*  0:  143646045  153901007   IO-APIC-edge      timer
		 *   ^
		 */
		*p = '\0';
		/* Deal with non-maskable interrupts -- make up fake numbers */
		nr = index_in_strings("NMI\0RES\0CAL\0TLB\0TRM\0THR\0SPU\0", buf);
		if (nr >= 0) {
			nr += 20000;
		} else {
			/* bb_strtou doesn't eat leading spaces, using strtoul */
			errno = 0;
			nr = strtoul(buf, NULL, 10);
			if (errno)
				continue;
		}
		p++;
		/*  0:  143646045  153901007   IO-APIC-edge      timer
		 *    ^
		 */
		/* Sum counts for this IRQ */
		count = 0;
		while (1) {
			char *tmp;
			p = skip_whitespace(p);
			if (!isdigit(*p))
				break;
			count += bb_strtoull(p, &tmp, 10);
			p = tmp;
		}
		/*   0:  143646045  153901007   IO-APIC-edge      timer
		 * NMI:          1          2   Non-maskable interrupts
		 *                              ^
		 */
		if (nr < 20000) {
			/* Skip to the interrupt name, e.g. 'timer' */
			p = strchr(p, ' ');
			if (!p)
				continue;
			p = skip_whitespace(p);
		}

		name = p;
		chomp(p);
		/* Save description of the interrupt */
		if (nr >= 20000)
			sprintf(irq_desc, "   <kernel IPI> : %s", name);
		else
			sprintf(irq_desc, "    <interrupt> : %s", name);

		delta = save_irq_count(nr, count);

		/* Skip per CPU timer interrupts */
		if (is_hpet_irq(name))
			continue;

		if (nr != 0 && delta != 0)
			save_line(irq_desc, delta);

		if (nr == 0)
			G.interrupt_0 = delta;
		else
			G.total_interrupt += delta;
	}

	fclose(fp);
}
#else /* !ENABLE_FEATURE_POWERTOP_PROCIRQ */
# define process_irq_counts()  ((void)0)
#endif

static NOINLINE int process_timer_stats(void)
{
	char buf[128];
	char line[15 + 3 + 128];
	int n;
	FILE *fp;

	buf[0] = '\0';

	n = 0;
	fp = NULL;
	if (!G.cant_enable_timer_stats)
		fp = fopen_for_read("/proc/timer_stats");
	if (fp) {
// Example file contents:
// Timer Stats Version: v0.2
// Sample period: 1.329 s
//    76,     0 swapper          hrtimer_start_range_ns (tick_sched_timer)
//    88,     0 swapper          hrtimer_start_range_ns (tick_sched_timer)
//    24,  3787 firefox          hrtimer_start_range_ns (hrtimer_wakeup)
//   46D,  1136 kondemand/1      do_dbs_timer (delayed_work_timer_fn)
// ...
//     1,  1656 Xorg             hrtimer_start_range_ns (hrtimer_wakeup)
//     1,  2159 udisks-daemon    hrtimer_start_range_ns (hrtimer_wakeup)
// 331 total events, 249.059 events/sec
		while (fgets(buf, sizeof(buf), fp)) {
			const char *count, *process, *func;
			char *p;
			int idx;
			unsigned cnt;

			count = skip_whitespace(buf);
			p = strchr(count, ',');
			if (!p)
				continue;
			*p++ = '\0';
			cnt = bb_strtou(count, NULL, 10);
			if (strcmp(skip_non_whitespace(count), " total events") == 0) {
#if ENABLE_FEATURE_POWERTOP_PROCIRQ
				n = cnt / G.total_cpus;
				if (n > 0 && n < G.interrupt_0) {
					sprintf(line, "    <interrupt> : %s", "extra timer interrupt");
					save_line(line, G.interrupt_0 - n);
				}
#endif
				break;
			}
			if (strchr(count, 'D'))
				continue; /* deferred */
			p = skip_whitespace(p); /* points to pid now */
			process = NULL;
 get_func_name:
			p = strchr(p, ' ');
			if (!p)
				continue;
			*p++ = '\0';
			p = skip_whitespace(p);
			if (process == NULL) {
				process = p;
				goto get_func_name;
			}
			func = p;

			//if (strcmp(process, "swapper") == 0
			// && strcmp(func, "hrtimer_start_range_ns (tick_sched_timer)\n") == 0
			//) {
			//	process = "[kernel scheduler]";
			//	func = "Load balancing tick";
			//}

			if (is_prefixed_with(func, "tick_nohz_"))
				continue;
			if (is_prefixed_with(func, "tick_setup_sched_timer"))
				continue;
			//if (strcmp(process, "powertop") == 0)
			//	continue;

			idx = index_in_strings("insmod\0modprobe\0swapper\0", process);
			if (idx != -1) {
				process = idx < 2 ? "[kernel module]" : "<kernel core>";
			}

			chomp(p);

			// 46D\01136\0kondemand/1\0do_dbs_timer (delayed_work_timer_fn)
			// ^          ^            ^
			// count      process      func

			//if (strchr(process, '['))
				sprintf(line, "%15.15s : %s", process, func);
			//else
			//	sprintf(line, "%s", process);
			save_line(line, cnt);
		}
		fclose(fp);
	}

	return n;
}

#ifdef __i386__
/*
 * Get information about CPU using CPUID opcode.
 */
static void cpuid(unsigned int *eax, unsigned int *ebx, unsigned int *ecx,
				unsigned int *edx)
{
	/* EAX value specifies what information to return */
	__asm__(
		"	pushl %%ebx\n"     /* Save EBX */
		"	cpuid\n"
		"	movl %%ebx, %1\n"  /* Save content of EBX */
		"	popl %%ebx\n"      /* Restore EBX */
		: "=a"(*eax), /* Output */
		  "=r"(*ebx),
		  "=c"(*ecx),
		  "=d"(*edx)
		: "0"(*eax),  /* Input */
		  "1"(*ebx),
		  "2"(*ecx),
		  "3"(*edx)
		/* No clobbered registers */
	);
}
#endif

#ifdef __i386__
static NOINLINE void print_intel_cstates(void)
{
	int bios_table[8] = { 0 };
	int nbios = 0;
	DIR *cpudir;
	struct dirent *d;
	int i;
	unsigned eax, ebx, ecx, edx;

	cpudir = opendir("/sys/devices/system/cpu");
	if (!cpudir)
		return;

	/* Loop over cpuN entries */
	while ((d = readdir(cpudir)) != NULL) {
		DIR *dir;
		int len;
		char fname[sizeof("/sys/devices/system/cpu//cpuidle//desc") + 2*BIG_SYSNAME_LEN];

		len = strlen(d->d_name);
		if (len < 3 || len > BIG_SYSNAME_LEN)
			continue;

		if (!isdigit(d->d_name[3]))
			continue;

		len = sprintf(fname, "%s/%s/cpuidle", "/sys/devices/system/cpu", d->d_name);
		dir = opendir(fname);
		if (!dir)
			continue;

		/*
		 * Every C-state has its own stateN directory, that
		 * contains a 'time' and a 'usage' file.
		 */
		while ((d = readdir(dir)) != NULL) {
			FILE *fp;
			char buf[64];
			int n;

			n = strlen(d->d_name);
			if (n < 3 || n > BIG_SYSNAME_LEN)
				continue;

			sprintf(fname + len, "/%s/desc", d->d_name);
			fp = fopen_for_read(fname);
			if (fp) {
				char *p = fgets(buf, sizeof(buf), fp);
				fclose(fp);
				if (!p)
					break;
				p = strstr(p, "MWAIT ");
				if (p) {
					int pos;
					p += sizeof("MWAIT ") - 1;
					pos = (bb_strtoull(p, NULL, 16) >> 4) + 1;
					if (pos >= ARRAY_SIZE(bios_table))
						continue;
					bios_table[pos]++;
					nbios++;
				}
			}
		}
		closedir(dir);
	}
	closedir(cpudir);

	if (!nbios)
		return;

	eax = 5;
	ebx = ecx = edx = 0;
	cpuid(&eax, &ebx, &ecx, &edx);
	if (!edx || !(ecx & 1))
		return;

	printf("Your %s the following C-states: ", "CPU supports");
	i = 0;
	while (edx) {
		if (edx & 7)
			printf("C%u ", i);
		edx >>= 4;
		i++;
	}
	bb_putchar('\n');

	/* Print BIOS C-States */
	printf("Your %s the following C-states: ", "BIOS reports");
	for (i = 0; i < ARRAY_SIZE(bios_table); i++)
		if (bios_table[i])
			printf("C%u ", i);

	bb_putchar('\n');
}
#else
# define print_intel_cstates() ((void)0)
#endif

static void show_timerstats(void)
{
	unsigned lines;

	/* Get terminal height */
	get_terminal_width_height(STDOUT_FILENO, NULL, &lines);

	/* We don't have whole terminal just for timerstats */
	lines -= 12;

	if (!G.cant_enable_timer_stats) {
		int i, n = 0;
		char strbuf6[6];

		puts("\nTop causes for wakeups:");
		for (i = 0; i < G.lines_cnt; i++) {
			if ((G.lines[i].count > 0 /*|| G.lines[i].disk_count > 0*/)
			 && n++ < lines
			) {
				/* NB: upstream powertop prints "(wakeups/sec)",
				 * we print just "(wakeup counts)".
				 */
				/*char c = ' ';
				if (G.lines[i].disk_count)
					c = 'D';*/
				smart_ulltoa5(G.lines[i].count, strbuf6, " KMGTPEZY")[0] = '\0';
				printf(/*" %5.1f%% (%s)%c  %s\n"*/
					" %5.1f%% (%s)   %s\n",
					G.lines[i].count * 100.0 / G.lines_cumulative_count,
					strbuf6, /*c,*/
					G.lines[i].string);
			}
		}
	} else {
		bb_putchar('\n');
		bb_error_msg("no stats available; run as root or"
				" enable the timer_stats module");
	}
}

// Example display from powertop version 1.11
// Cn                Avg residency       P-states (frequencies)
// C0 (cpu running)        ( 0.5%)         2.00 Ghz     0.0%
// polling           0.0ms ( 0.0%)         1.67 Ghz     0.0%
// C1 mwait          0.0ms ( 0.0%)         1333 Mhz     0.1%
// C2 mwait          0.1ms ( 0.1%)         1000 Mhz    99.9%
// C3 mwait         12.1ms (99.4%)
//
// Wakeups-from-idle per second : 93.6     interval: 15.0s
// no ACPI power usage estimate available
//
// Top causes for wakeups:
//   32.4% ( 26.7)       <interrupt> : extra timer interrupt
//   29.0% ( 23.9)     <kernel core> : hrtimer_start_range_ns (tick_sched_timer)
//    9.0% (  7.5)     <kernel core> : hrtimer_start (tick_sched_timer)
//    6.5% (  5.3)       <interrupt> : ata_piix
//    5.0% (  4.1)             inetd : hrtimer_start_range_ns (hrtimer_wakeup)

//usage:#define powertop_trivial_usage
//usage:       ""
//usage:#define powertop_full_usage "\n\n"
//usage:       "Analyze power consumption on Intel-based laptops"

int powertop_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int powertop_main(int argc UNUSED_PARAM, char UNUSED_PARAM **argv)
{
	ullong cur_usage[MAX_CSTATE_COUNT];
	ullong cur_duration[MAX_CSTATE_COUNT];
	char cstate_lines[MAX_CSTATE_COUNT + 2][64];
#if ENABLE_FEATURE_POWERTOP_INTERACTIVE
	struct pollfd pfd[1];

	pfd[0].fd = 0;
	pfd[0].events = POLLIN;
#endif

	INIT_G();

#if ENABLE_FEATURE_POWERTOP_PROCIRQ && BLOATY_HPET_IRQ_NUM_DETECTION
	G.percpu_hpet_start = INT_MAX;
	G.percpu_hpet_end = INT_MIN;
#endif

	/* Print warning when we don't have superuser privileges */
	if (geteuid() != 0)
		bb_error_msg("run as root to collect enough information");

	/* Get number of CPUs */
	G.total_cpus = get_cpu_count();

	puts("Collecting data for "DEFAULT_SLEEP_STR" seconds");

#if ENABLE_FEATURE_POWERTOP_INTERACTIVE
	/* Turn on unbuffered input; turn off echoing, ^C ^Z etc */
	set_termios_to_raw(STDIN_FILENO, &G.init_settings, TERMIOS_CLEAR_ISIG);
	bb_signals(BB_FATAL_SIGS, sig_handler);
	/* So we don't forget to reset term settings */
	die_func = reset_term;
#endif

	/* Collect initial data */
	process_irq_counts();

	/* Read initial usage and duration */
	read_cstate_counts(G.start_usage, G.start_duration);

	/* Copy them to "last" */
	memcpy(G.last_usage, G.start_usage, sizeof(G.last_usage));
	memcpy(G.last_duration, G.start_duration, sizeof(G.last_duration));

	/* Display C-states */
	print_intel_cstates();

	G.cant_enable_timer_stats |= stop_timer(); /* 1 on error */

	/* The main loop */
	for (;;) {
		//double maxsleep = 0.0;
		ullong totalticks, totalevents;
		int i;

		G.cant_enable_timer_stats |= start_timer(); /* 1 on error */
#if !ENABLE_FEATURE_POWERTOP_INTERACTIVE
		sleep(DEFAULT_SLEEP);
#else
		if (safe_poll(pfd, 1, DEFAULT_SLEEP * 1000) > 0) {
			unsigned char c;
			if (safe_read(STDIN_FILENO, &c, 1) != 1)
				break; /* EOF/error */
			if (c == G.init_settings.c_cc[VINTR])
				break; /* ^C */
			if ((c | 0x20) == 'q')
				break;
		}
#endif
		G.cant_enable_timer_stats |= stop_timer(); /* 1 on error */

		clear_lines();
		process_irq_counts();

		/* Clear the stats */
		memset(cur_duration, 0, sizeof(cur_duration));
		memset(cur_usage, 0, sizeof(cur_usage));

		/* Read them */
		read_cstate_counts(cur_usage, cur_duration);

		/* Count totalticks and totalevents */
		totalticks = totalevents = 0;
		for (i = 0; i < MAX_CSTATE_COUNT; i++) {
			if (cur_usage[i] != 0) {
				totalticks += cur_duration[i] - G.last_duration[i];
				totalevents += cur_usage[i] - G.last_usage[i];
			}
		}

		/* Home; clear screen */
		printf(ESC"[H" ESC"[J");

		/* Clear C-state lines */
		memset(&cstate_lines, 0, sizeof(cstate_lines));

		if (totalevents == 0 && G.maxcstate <= 1) {
			/* This should not happen */
			strcpy(cstate_lines[0], "C-state information is not available\n");
		} else {
			double percentage;
			unsigned newticks;

			newticks = G.total_cpus * DEFAULT_SLEEP * FREQ_ACPI_1000 - totalticks;
			/* Handle rounding errors: do not display negative values */
			if ((int)newticks < 0)
				newticks = 0;

			sprintf(cstate_lines[0], "Cn\t\t  Avg residency\n");
			percentage = newticks * 100.0 / (G.total_cpus * DEFAULT_SLEEP * FREQ_ACPI_1000);
			sprintf(cstate_lines[1], "C0 (cpu running)        (%4.1f%%)\n", percentage);

			/* Compute values for individual C-states */
			for (i = 0; i < MAX_CSTATE_COUNT; i++) {
				if (cur_usage[i] != 0) {
					double slept;
					slept = (cur_duration[i] - G.last_duration[i])
						/ (cur_usage[i] - G.last_usage[i] + 0.1) / FREQ_ACPI;
					percentage = (cur_duration[i] - G.last_duration[i]) * 100
						/ (G.total_cpus * DEFAULT_SLEEP * FREQ_ACPI_1000);
					sprintf(cstate_lines[i + 2], "C%u\t\t%5.1fms (%4.1f%%)\n",
						i + 1, slept, percentage);
					//if (maxsleep < slept)
					//	maxsleep = slept;
				}
			}
		}

		for (i = 0; i < MAX_CSTATE_COUNT + 2; i++)
			if (cstate_lines[i][0])
				fputs(cstate_lines[i], stdout);

		i = process_timer_stats();
#if ENABLE_FEATURE_POWERTOP_PROCIRQ
		if (totalevents == 0) {
			/* No C-state info available, use timerstats */
			totalevents = i * G.total_cpus + G.total_interrupt;
			if (i < 0)
				totalevents += G.interrupt_0 - i;
		}
#endif
		/* Upstream powertop prints wakeups per sec per CPU,
		 * we print just raw wakeup counts.
		 */
//TODO: show real seconds (think about manual refresh)
		printf("\nWakeups-from-idle in %u seconds: %llu\n",
			DEFAULT_SLEEP,
			totalevents
		);

		update_lines_cumulative_count();
		sort_lines();
		show_timerstats();
		fflush(stdout);

		/* Clear the stats */
		memset(cur_duration, 0, sizeof(cur_duration));
		memset(cur_usage, 0, sizeof(cur_usage));

		/* Get new values */
		read_cstate_counts(cur_usage, cur_duration);

		/* Save them */
		memcpy(G.last_usage, cur_usage, sizeof(G.last_usage));
		memcpy(G.last_duration, cur_duration, sizeof(G.last_duration));
	} /* for (;;) */

	bb_putchar('\n');
#if ENABLE_FEATURE_POWERTOP_INTERACTIVE
	reset_term();
#endif

	return EXIT_SUCCESS;
}
