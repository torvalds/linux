/* vi: set sw=4 ts=4: */
/*
 * Mini chmod implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Reworked by (C) 2002 Vladimir Oleynik <dzo@simtreas.ru>
 *  to correctly parse '-rwxgoa'
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config CHMOD
//config:	bool "chmod (5.1 kb)"
//config:	default y
//config:	help
//config:	chmod is used to change the access permission of files.

//applet:IF_CHMOD(APPLET_NOEXEC(chmod, chmod, BB_DIR_BIN, BB_SUID_DROP, chmod))

//kbuild:lib-$(CONFIG_CHMOD) += chmod.o

/* BB_AUDIT SUSv3 compliant */
/* BB_AUDIT GNU defects - unsupported long options. */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/chmod.html */

//usage:#define chmod_trivial_usage
//usage:       "[-R"IF_DESKTOP("cvf")"] MODE[,MODE]... FILE..."
//usage:#define chmod_full_usage "\n\n"
//usage:       "Each MODE is one or more of the letters ugoa, one of the\n"
//usage:       "symbols +-= and one or more of the letters rwxst\n"
//usage:     "\n	-R	Recurse"
//usage:	IF_DESKTOP(
//usage:     "\n	-c	List changed files"
//usage:     "\n	-v	List all files"
//usage:     "\n	-f	Hide errors"
//usage:	)
//usage:
//usage:#define chmod_example_usage
//usage:       "$ ls -l /tmp/foo\n"
//usage:       "-rw-rw-r--    1 root     root            0 Apr 12 18:25 /tmp/foo\n"
//usage:       "$ chmod u+x /tmp/foo\n"
//usage:       "$ ls -l /tmp/foo\n"
//usage:       "-rwxrw-r--    1 root     root            0 Apr 12 18:25 /tmp/foo*\n"
//usage:       "$ chmod 444 /tmp/foo\n"
//usage:       "$ ls -l /tmp/foo\n"
//usage:       "-r--r--r--    1 root     root            0 Apr 12 18:25 /tmp/foo\n"

#include "libbb.h"

/* This is a NOEXEC applet. Be very careful! */


#define OPT_RECURSE (option_mask32 & 1)
#define OPT_VERBOSE (IF_DESKTOP(option_mask32 & 2) IF_NOT_DESKTOP(0))
#define OPT_CHANGED (IF_DESKTOP(option_mask32 & 4) IF_NOT_DESKTOP(0))
#define OPT_QUIET   (IF_DESKTOP(option_mask32 & 8) IF_NOT_DESKTOP(0))
#define OPT_STR     "R" IF_DESKTOP("vcf")

/* coreutils:
 * chmod never changes the permissions of symbolic links; the chmod
 * system call cannot change their permissions. This is not a problem
 * since the permissions of symbolic links are never used.
 * However, for each symbolic link listed on the command line, chmod changes
 * the permissions of the pointed-to file. In contrast, chmod ignores
 * symbolic links encountered during recursive directory traversals.
 */

static int FAST_FUNC fileAction(const char *fileName, struct stat *statbuf, void* param, int depth)
{
	mode_t newmode;

	/* match coreutils behavior */
	if (depth == 0) {
		/* statbuf holds lstat result, but we need stat (follow link) */
		if (stat(fileName, statbuf))
			goto err;
	} else { /* depth > 0: skip links */
		if (S_ISLNK(statbuf->st_mode))
			return TRUE;
	}

	newmode = bb_parse_mode((char *)param, statbuf->st_mode);
	if (newmode == (mode_t)-1)
		bb_error_msg_and_die("invalid mode '%s'", (char *)param);

	if (chmod(fileName, newmode) == 0) {
		if (OPT_VERBOSE
		 || (OPT_CHANGED && statbuf->st_mode != newmode)
		) {
			printf("mode of '%s' changed to %04o (%s)\n", fileName,
				newmode & 07777, bb_mode_string(newmode)+1);
		}
		return TRUE;
	}
 err:
	if (!OPT_QUIET)
		bb_simple_perror_msg(fileName);
	return FALSE;
}

int chmod_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int chmod_main(int argc UNUSED_PARAM, char **argv)
{
	int retval = EXIT_SUCCESS;
	char *arg, **argp;
	char *smode;

	/* Convert first encountered -r into ar, -w into aw etc
	 * so that getopt would not eat it */
	argp = argv;
	while ((arg = *++argp)) {
		/* Mode spec must be the first arg (sans -R etc) */
		/* (protect against mishandling e.g. "chmod 644 -r") */
		if (arg[0] != '-') {
			arg = NULL;
			break;
		}
		/* An option. Not a -- or valid option? */
		if (arg[1] && !strchr("-"OPT_STR, arg[1])) {
			arg[0] = 'a';
			break;
		}
	}

	/* Parse options */
	getopt32(argv, "^" OPT_STR "\0" "-2");
	argv += optind;

	/* Restore option-like mode if needed */
	if (arg) arg[0] = '-';

	/* Ok, ready to do the deed now */
	smode = *argv++;
	do {
		if (!recursive_action(*argv,
			OPT_RECURSE,    // recurse
			fileAction,     // file action
			fileAction,     // dir action
			smode,          // user data
			0)              // depth
		) {
			retval = EXIT_FAILURE;
		}
	} while (*++argv);

	return retval;
}

/*
Security: chmod is too important and too subtle.
This is a test script (busybox chmod versus coreutils).
Run it in empty directory.

#!/bin/sh
t1="/tmp/busybox chmod"
t2="/usr/bin/chmod"
create() {
    rm -rf $1; mkdir $1
    (
    cd $1 || exit 1
    mkdir dir
    >up
    >file
    >dir/file
    ln -s dir linkdir
    ln -s file linkfile
    ln -s ../up dir/up
    )
}
tst() {
    (cd test1; $t1 $1)
    (cd test2; $t2 $1)
    (cd test1; ls -lR) >out1
    (cd test2; ls -lR) >out2
    echo "chmod $1" >out.diff
    if ! diff -u out1 out2 >>out.diff; then exit 1; fi
    rm out.diff
}
echo "If script produced 'out.diff' file, then at least one testcase failed"
create test1; create test2
tst "a+w file"
tst "a-w dir"
tst "a+w linkfile"
tst "a-w linkdir"
tst "-R a+w file"
tst "-R a-w dir"
tst "-R a+w linkfile"
tst "-R a-w linkdir"
tst "a-r,a+x linkfile"
*/
