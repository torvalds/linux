/* vi: set sw=4 ts=4: */
/*
 * chrt - manipulate real-time attributes of a process
 * Copyright (c) 2006-2007 Bernhard Reutner-Fischer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config CHRT
//config:	bool "chrt (4.4 kb)"
//config:	default y
//config:	help
//config:	Manipulate real-time attributes of a process.
//config:	This requires sched_{g,s}etparam support in your libc.

//applet:IF_CHRT(APPLET_NOEXEC(chrt, chrt, BB_DIR_USR_BIN, BB_SUID_DROP, chrt))

//kbuild:lib-$(CONFIG_CHRT) += chrt.o

//usage:#define chrt_trivial_usage
//usage:       "[-prfombi] [PRIO] [PID | PROG ARGS]"
//usage:#define chrt_full_usage "\n\n"
//usage:       "Change scheduling priority and class for a process\n"
//usage:     "\n	-p	Operate on PID"
//usage:     "\n	-r	Set SCHED_RR class"
//usage:     "\n	-f	Set SCHED_FIFO class"
//usage:     "\n	-o	Set SCHED_OTHER class"
//usage:     "\n	-b	Set SCHED_BATCH class"
//usage:     "\n	-i	Set SCHED_IDLE class"
//usage:     "\n	-m	Show min/max priorities"
//usage:
//usage:#define chrt_example_usage
//usage:       "$ chrt -r 4 sleep 900; x=$!\n"
//usage:       "$ chrt -f -p 3 $x\n"
//usage:       "You need CAP_SYS_NICE privileges to set scheduling attributes of a process"

#include <sched.h>
#include "libbb.h"
#ifndef SCHED_IDLE
# define SCHED_IDLE 5
#endif

static const struct {
	char name[sizeof("SCHED_OTHER")];
} policies[] = {
	{ "SCHED_OTHER" }, /* 0:SCHED_OTHER */
	{ "SCHED_FIFO" },  /* 1:SCHED_FIFO */
	{ "SCHED_RR" },    /* 2:SCHED_RR */
	{ "SCHED_BATCH" }, /* 3:SCHED_BATCH */
	{ "" },            /* 4:SCHED_ISO */
	{ "SCHED_IDLE" },  /* 5:SCHED_IDLE */
	/* 6:SCHED_DEADLINE */
};

static void show_min_max(int pol)
{
	const char *fmt = "%s min/max priority\t: %u/%u\n";
	int max, min;

	max = sched_get_priority_max(pol);
	min = sched_get_priority_min(pol);
	if ((max|min) < 0)
		fmt = "%s not supported\n";
	printf(fmt, policies[pol].name, min, max);
}

#define OPT_m (1<<0)
#define OPT_p (1<<1)
#define OPT_r (1<<2)
#define OPT_f (1<<3)
#define OPT_o (1<<4)
#define OPT_b (1<<5)
#define OPT_i (1<<6)

int chrt_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int chrt_main(int argc UNUSED_PARAM, char **argv)
{
	pid_t pid = 0;
	unsigned opt;
	struct sched_param sp;
	char *pid_str;
	char *priority = priority; /* for compiler */
	const char *current_new;
	int policy = SCHED_RR;

	opt = getopt32(argv, "^"
			"+" "mprfobi"
			"\0"
			/* only one policy accepted: */
			"r--fobi:f--robi:o--rfbi:b--rfoi:i--rfob"
	);
	if (opt & OPT_m) { /* print min/max and exit */
		show_min_max(SCHED_OTHER);
		show_min_max(SCHED_FIFO);
		show_min_max(SCHED_RR);
		show_min_max(SCHED_BATCH);
		show_min_max(SCHED_IDLE);
		fflush_stdout_and_exit(EXIT_SUCCESS);
	}
	//if (opt & OPT_r)
	//	policy = SCHED_RR; - default, already set
	if (opt & OPT_f)
		policy = SCHED_FIFO;
	if (opt & OPT_o)
		policy = SCHED_OTHER;
	if (opt & OPT_b)
		policy = SCHED_BATCH;
	if (opt & OPT_i)
		policy = SCHED_IDLE;

	argv += optind;
	if (!argv[0])
		bb_show_usage();
	if (opt & OPT_p) {
		pid_str = *argv++;
		if (*argv) { /* "-p <priority> <pid> [...]" */
			priority = pid_str;
			pid_str = *argv;
		}
		/* else "-p <pid>", and *argv == NULL */
		pid = xatoul_range(pid_str, 1, ((unsigned)(pid_t)ULONG_MAX) >> 1);
	} else {
		priority = *argv++;
		if (!*argv)
			bb_show_usage();
	}

	current_new = "current\0new";
	if (opt & OPT_p) {
		int pol;
 print_rt_info:
		pol = sched_getscheduler(pid);
		if (pol < 0)
			bb_perror_msg_and_die("can't %cet pid %d's policy", 'g', (int)pid);
		printf("pid %d's %s scheduling policy: %s\n",
				pid, current_new, policies[pol].name);
		if (sched_getparam(pid, &sp))
			bb_perror_msg_and_die("can't get pid %d's attributes", (int)pid);
		printf("pid %d's %s scheduling priority: %d\n",
				(int)pid, current_new, sp.sched_priority);
		if (!*argv) {
			/* Either it was just "-p <pid>",
			 * or it was "-p <priority> <pid>" and we came here
			 * for the second time (see goto below) */
			return EXIT_SUCCESS;
		}
		*argv = NULL;
		current_new += 8;
	}

	sp.sched_priority = xstrtou_range(priority, 0,
		sched_get_priority_min(policy), sched_get_priority_max(policy)
	);

	if (sched_setscheduler(pid, policy, &sp) < 0)
		bb_perror_msg_and_die("can't %cet pid %d's policy", 's', (int)pid);

	if (!argv[0]) /* "-p <priority> <pid> [...]" */
		goto print_rt_info;

	BB_EXECVP_or_die(argv);
}
