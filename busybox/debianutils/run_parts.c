/* vi: set sw=4 ts=4: */
/*
 * Mini run-parts implementation for busybox
 *
 * Copyright (C) 2007 Bernhard Reutner-Fischer
 *
 * Based on a older version that was in busybox which was 1k big.
 *   Copyright (C) 2001 by Emanuele Aina <emanuele.aina@tiscali.it>
 *
 * Based on the Debian run-parts program, version 1.15
 *   Copyright (C) 1996 Jeff Noxon <jeff@router.patch.net>,
 *   Copyright (C) 1996-1999 Guy Maor <maor@debian.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

/* This is my first attempt to write a program in C (well, this is my first
 * attempt to write a program! :-) . */

/* This piece of code is heavily based on the original version of run-parts,
 * taken from debian-utils. I've only removed the long options and the
 * report mode. As the original run-parts support only long options, I've
 * broken compatibility because the BusyBox policy doesn't allow them.
 */
//config:config RUN_PARTS
//config:	bool "run-parts (5.6 kb)"
//config:	default y
//config:	help
//config:	run-parts is a utility designed to run all the scripts in a directory.
//config:
//config:	It is useful to set up a directory like cron.daily, where you need to
//config:	execute all the scripts in that directory.
//config:
//config:	In this implementation of run-parts some features (such as report
//config:	mode) are not implemented.
//config:
//config:	Unless you know that run-parts is used in some of your scripts
//config:	you can safely say N here.
//config:
//config:config FEATURE_RUN_PARTS_LONG_OPTIONS
//config:	bool "Enable long options"
//config:	default y
//config:	depends on RUN_PARTS && LONG_OPTS
//config:
//config:config FEATURE_RUN_PARTS_FANCY
//config:	bool "Support additional arguments"
//config:	default y
//config:	depends on RUN_PARTS
//config:	help
//config:	Support additional options:
//config:	-l --list print the names of the all matching files (not
//config:	limited to executables), but don't actually run them.

//applet:IF_RUN_PARTS(APPLET_ODDNAME(run-parts, run_parts, BB_DIR_BIN, BB_SUID_DROP, run_parts))

//kbuild:lib-$(CONFIG_RUN_PARTS) += run_parts.o

//usage:#define run_parts_trivial_usage
//usage:       "[-a ARG]... [-u UMASK] "
//usage:       IF_FEATURE_RUN_PARTS_LONG_OPTIONS("[--reverse] [--test] [--exit-on-error] "IF_FEATURE_RUN_PARTS_FANCY("[--list] "))
//usage:       "DIRECTORY"
//usage:#define run_parts_full_usage "\n\n"
//usage:       "Run a bunch of scripts in DIRECTORY\n"
//usage:     "\n	-a ARG		Pass ARG as argument to scripts"
//usage:     "\n	-u UMASK	Set UMASK before running scripts"
//usage:	IF_FEATURE_RUN_PARTS_LONG_OPTIONS(
//usage:     "\n	--reverse	Reverse execution order"
//usage:     "\n	--test		Dry run"
//usage:     "\n	--exit-on-error	Exit if a script exits with non-zero"
//usage:	IF_FEATURE_RUN_PARTS_FANCY(
//usage:     "\n	--list		Print names of matching files even if they are not executable"
//usage:	)
//usage:	)
//usage:
//usage:#define run_parts_example_usage
//usage:       "$ run-parts -a start /etc/init.d\n"
//usage:       "$ run-parts -a stop=now /etc/init.d\n\n"
//usage:       "Let's assume you have a script foo/dosomething:\n"
//usage:       "#!/bin/sh\n"
//usage:       "for i in $*; do eval $i; done; unset i\n"
//usage:       "case \"$1\" in\n"
//usage:       "start*) echo starting something;;\n"
//usage:       "stop*) set -x; shutdown -h $stop;;\n"
//usage:       "esac\n\n"
//usage:       "Running this yields:\n"
//usage:       "$run-parts -a stop=+4m foo/\n"
//usage:       "+ shutdown -h +4m"

#include "libbb.h"
#include "common_bufsiz.h"

struct globals {
	char **names;
	int    cur;
	char  *cmd[2 /* using 1 provokes compiler warning */];
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define names (G.names)
#define cur   (G.cur  )
#define cmd   (G.cmd  )
#define INIT_G() do { setup_common_bufsiz(); } while (0)

enum { NUM_CMD = (COMMON_BUFSIZE - sizeof(G)) / sizeof(cmd[0]) - 1 };

enum {
	OPT_a = (1 << 0),
	OPT_u = (1 << 1),
	OPT_r = (1 << 2) * ENABLE_FEATURE_RUN_PARTS_LONG_OPTIONS,
	OPT_t = (1 << 3) * ENABLE_FEATURE_RUN_PARTS_LONG_OPTIONS,
	OPT_e = (1 << 4) * ENABLE_FEATURE_RUN_PARTS_LONG_OPTIONS,
	OPT_l = (1 << 5) * ENABLE_FEATURE_RUN_PARTS_LONG_OPTIONS
			* ENABLE_FEATURE_RUN_PARTS_FANCY,
};

/* Is this a valid filename (upper/lower alpha, digits,
 * underscores, and hyphens only?)
 */
static bool invalid_name(const char *c)
{
	c = bb_basename(c);

	while (*c && (isalnum(*c) || *c == '_' || *c == '-'))
		c++;

	return *c; /* TRUE (!0) if terminating NUL is not reached */
}

static int bb_alphasort(const void *p1, const void *p2)
{
	int r = strcmp(*(char **) p1, *(char **) p2);
	return (option_mask32 & OPT_r) ? -r : r;
}

static int FAST_FUNC act(const char *file, struct stat *statbuf, void *args UNUSED_PARAM, int depth)
{
	if (depth == 1)
		return TRUE;

	if (depth == 2
	 && (  !(statbuf->st_mode & (S_IFREG | S_IFLNK))
	    || invalid_name(file)
	    || (!(option_mask32 & OPT_l) && access(file, X_OK) != 0))
	) {
		return SKIP;
	}

	names = xrealloc_vector(names, 4, cur);
	names[cur++] = xstrdup(file);
	/*names[cur] = NULL; - xrealloc_vector did it */

	return TRUE;
}

#if ENABLE_FEATURE_RUN_PARTS_LONG_OPTIONS
static const char runparts_longopts[] ALIGN1 =
	"arg\0"     Required_argument "a"
	"umask\0"   Required_argument "u"
//TODO: "verbose\0" No_argument       "v"
	"reverse\0" No_argument       "\xf0"
	"test\0"    No_argument       "\xf1"
	"exit-on-error\0" No_argument "\xf2"
# if ENABLE_FEATURE_RUN_PARTS_FANCY
	"list\0"    No_argument       "\xf3"
# endif
	;
# define GETOPT32 getopt32long
# define LONGOPTS ,runparts_longopts
#else
# define GETOPT32 getopt32
# define LONGOPTS
#endif

int run_parts_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int run_parts_main(int argc UNUSED_PARAM, char **argv)
{
	const char *umask_p = "22";
	llist_t *arg_list = NULL;
	unsigned n;
	int ret;

	INIT_G();

	/* We require exactly one argument: the directory name */
	GETOPT32(argv, "^" "a:*u:" "\0" "=1" LONGOPTS,
			&arg_list, &umask_p
	);

	umask(xstrtou_range(umask_p, 8, 0, 07777));

	n = 1;
	while (arg_list && n < NUM_CMD) {
		cmd[n++] = llist_pop(&arg_list);
	}
	/* cmd[n] = NULL; - is already zeroed out */

	/* run-parts has to sort executables by name before running them */

	recursive_action(argv[optind],
			ACTION_RECURSE|ACTION_FOLLOWLINKS,
			act,            /* file action */
			act,            /* dir action */
			NULL,           /* user data */
			1               /* depth */
		);

	if (!names)
		return 0;

	qsort(names, cur, sizeof(char *), bb_alphasort);

	n = 0;
	while (1) {
		char *name = *names++;
		if (!name)
			break;
		if (option_mask32 & (OPT_t | OPT_l)) {
			puts(name);
			continue;
		}
		cmd[0] = name;
		ret = spawn_and_wait(cmd);
		if (ret == 0)
			continue;
		n = 1;
		if (ret < 0)
			bb_perror_msg("can't execute '%s'", name);
		else /* ret > 0 */
			bb_error_msg("%s: exit status %u", name, ret & 0xff);

		if (option_mask32 & OPT_e)
			xfunc_die();
	}

	return n;
}
