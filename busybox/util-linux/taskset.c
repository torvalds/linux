/* vi: set sw=4 ts=4: */
/*
 * taskset - retrieve or set a processes' CPU affinity
 * Copyright (c) 2006 Bernhard Reutner-Fischer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config TASKSET
//config:	bool "taskset (4.1 kb)"
//config:	default y
//config:	help
//config:	Retrieve or set a processes's CPU affinity.
//config:	This requires sched_{g,s}etaffinity support in your libc.
//config:
//config:config FEATURE_TASKSET_FANCY
//config:	bool "Fancy output"
//config:	default y
//config:	depends on TASKSET
//config:	help
//config:	Needed for machines with more than 32-64 CPUs:
//config:	affinity parameter 0xHHHHHHHHHHHHHHHHHHHH can be arbitrarily long
//config:	in this case. Otherwise, it is limited to sizeof(long).

//applet:IF_TASKSET(APPLET_NOEXEC(taskset, taskset, BB_DIR_USR_BIN, BB_SUID_DROP, taskset))

//kbuild:lib-$(CONFIG_TASKSET) += taskset.o

//usage:#define taskset_trivial_usage
//usage:       "[-p] [HEXMASK] PID | PROG ARGS"
//usage:#define taskset_full_usage "\n\n"
//usage:       "Set or get CPU affinity\n"
//usage:     "\n	-p	Operate on an existing PID"
//usage:
//usage:#define taskset_example_usage
//usage:       "$ taskset 0x7 ./dgemm_test&\n"
//usage:       "$ taskset -p 0x1 $!\n"
//usage:       "pid 4790's current affinity mask: 7\n"
//usage:       "pid 4790's new affinity mask: 1\n"
//usage:       "$ taskset 0x7 /bin/sh -c './taskset -p 0x1 $$'\n"
//usage:       "pid 6671's current affinity mask: 1\n"
//usage:       "pid 6671's new affinity mask: 1\n"
//usage:       "$ taskset -p 1\n"
//usage:       "pid 1's current affinity mask: 3\n"
/*
 * Not yet implemented:
 * -a/--all-tasks (affect all threads)
 *	needs to get TIDs from /proc/PID/task/ and use _them_ as "pid" in sched_setaffinity(pid)
 * -c/--cpu-list  (specify CPUs via "1,3,5-7")
 */

#include <sched.h>
#include "libbb.h"

typedef unsigned long ul;
#define SZOF_UL (unsigned)(sizeof(ul))
#define BITS_UL (unsigned)(sizeof(ul)*8)
#define MASK_UL (unsigned)(sizeof(ul)*8 - 1)

#if ENABLE_FEATURE_TASKSET_FANCY
#define TASKSET_PRINTF_MASK "%s"
/* craft a string from the mask */
static char *from_mask(const ul *mask, unsigned sz_in_bytes)
{
	char *str = xzalloc((sz_in_bytes+1) * 2); /* we will leak it */
	char *p = str;
	for (;;) {
		ul v = *mask++;
		if (SZOF_UL == 4)
			p += sprintf(p, "%08lx", v);
		if (SZOF_UL == 8)
			p += sprintf(p, "%016lx", v);
		if (SZOF_UL == 16)
			p += sprintf(p, "%032lx", v); /* :) */
		sz_in_bytes -= SZOF_UL;
		if ((int)sz_in_bytes <= 0)
			break;
	}
	while (str[0] == '0' && str[1])
		str++;
	return str;
}
#else
#define TASKSET_PRINTF_MASK "%lx"
static unsigned long long from_mask(ul *mask, unsigned sz_in_bytes UNUSED_PARAM)
{
	return *mask;
}
#endif

static unsigned long *get_aff(int pid, unsigned *sz)
{
	int r;
	unsigned long *mask = NULL;
	unsigned sz_in_bytes = *sz;

	for (;;) {
		mask = xrealloc(mask, sz_in_bytes);
		r = sched_getaffinity(pid, sz_in_bytes, (void*)mask);
		if (r == 0)
			break;
		sz_in_bytes *= 2;
		if (errno == EINVAL && (int)sz_in_bytes > 0)
			continue;
		bb_perror_msg_and_die("can't %cet pid %d's affinity", 'g', pid);
	}
	//bb_error_msg("get mask[0]:%lx sz_in_bytes:%d", mask[0], sz_in_bytes);
	*sz = sz_in_bytes;
	return mask;
}

int taskset_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int taskset_main(int argc UNUSED_PARAM, char **argv)
{
	ul *mask;
	unsigned mask_size_in_bytes;
	pid_t pid = 0;
	unsigned opt_p;
	const char *current_new;
	char *aff;

	/* NB: we mimic util-linux's taskset: -p does not take
	 * an argument, i.e., "-pN" is NOT valid, only "-p N"!
	 * Indeed, util-linux-2.13-pre7 uses:
	 * getopt_long(argc, argv, "+pchV", ...), not "...p:..." */

	opt_p = getopt32(argv, "^+" "p" "\0" "-1" /* at least 1 arg */);
	argv += optind;

	aff = *argv++;
	if (opt_p) {
		char *pid_str = aff;
		if (*argv) { /* "-p <aff> <pid> ...rest.is.ignored..." */
			pid_str = *argv; /* NB: *argv != NULL in this case */
		}
		/* else it was just "-p <pid>", and *argv == NULL */
		pid = xatoul_range(pid_str, 1, ((unsigned)(pid_t)ULONG_MAX) >> 1);
	} else {
		/* <aff> <cmd...> */
		if (!*argv)
			bb_show_usage();
	}

	mask_size_in_bytes = SZOF_UL;
	current_new = "current";
 print_aff:
	mask = get_aff(pid, &mask_size_in_bytes);
	if (opt_p) {
		printf("pid %d's %s affinity mask: "TASKSET_PRINTF_MASK"\n",
				pid, current_new, from_mask(mask, mask_size_in_bytes));
		if (*argv == NULL) {
			/* Either it was just "-p <pid>",
			 * or it was "-p <aff> <pid>" and we came here
			 * for the second time (see goto below) */
			return EXIT_SUCCESS;
		}
		*argv = NULL;
		current_new = "new";
	}
	memset(mask, 0, mask_size_in_bytes);

	/* Affinity was specified, translate it into mask */
	/* it is always in hex, skip "0x" if it exists */
	if (aff[0] == '0' && (aff[1]|0x20) == 'x')
		aff += 2;

	if (!ENABLE_FEATURE_TASKSET_FANCY) {
		mask[0] = xstrtoul(aff, 16);
	} else {
		unsigned i;
		char *last_char;

		i = 0; /* bit pos in mask[] */

		/* aff is ASCII hex string, accept very long masks in this form.
		 * Process hex string AABBCCDD... to ulong mask[]
		 * from the rightmost nibble, which is least-significant.
		 * Bits not fitting into mask[] are ignored: (example: 1234
		 * in 12340000000000000000000000000000000000000ff)
		 */
		last_char = strchrnul(aff, '\0');
		while (last_char > aff) {
			char c;
			ul val;

			last_char--;
			c = *last_char;
			if (isdigit(c))
				val = c - '0';
			else if ((c|0x20) >= 'a' && (c|0x20) <= 'f')
				val = (c|0x20) - ('a' - 10);
			else
				bb_error_msg_and_die("bad affinity '%s'", aff);

			if (i < mask_size_in_bytes * 8) {
				mask[i / BITS_UL] |= val << (i & MASK_UL);
				//bb_error_msg("bit %d set", i);
			}
			/* else:
			 * We can error out here, but we don't.
			 * For one, kernel itself ignores bits in mask[]
			 * which do not map to any CPUs:
			 * if mask[] has one 32-bit long element,
			 * but you have only 8 CPUs, all bits beyond first 8
			 * are ignored, silently.
			 * No point in making bits past 31th to be errors.
			 */
			i += 4;
		}
	}

	/* Set pid's or our own (pid==0) affinity */
	if (sched_setaffinity(pid, mask_size_in_bytes, (void*)mask))
		bb_perror_msg_and_die("can't %cet pid %d's affinity", 's', pid);
	//bb_error_msg("set mask[0]:%lx", mask[0]);

	if (!argv[0]) /* "-p <aff> <pid> [...ignored...]" */
		goto print_aff; /* print new affinity and exit */

	BB_EXECVP_or_die(argv);
}
