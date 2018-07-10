/* vi: set sw=4 ts=4: */
/*
 * Mini touch implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* Mar 16, 2003      Manuel Novoa III   (mjn3@codepoet.org)
 *
 * Previous version called open() and then utime().  While this will be
 * be necessary to implement -r and -t, it currently only makes things bigger.
 * Also, exiting on a failure was a bug.  All args should be processed.
 */
//config:config TOUCH
//config:	bool "touch (5.8 kb)"
//config:	default y
//config:	help
//config:	touch is used to create or change the access and/or
//config:	modification timestamp of specified files.
//config:
//config:config FEATURE_TOUCH_NODEREF
//config:	bool "Add support for -h"
//config:	default y
//config:	depends on TOUCH
//config:	help
//config:	Enable touch to have the -h option.
//config:	This requires libc support for lutimes() function.
//config:
//config:config FEATURE_TOUCH_SUSV3
//config:	bool "Add support for SUSV3 features (-d -t -r)"
//config:	default y
//config:	depends on TOUCH
//config:	help
//config:	Enable touch to use a reference file or a given date/time argument.

//applet:IF_TOUCH(APPLET_NOFORK(touch, touch, BB_DIR_BIN, BB_SUID_DROP, touch))

//kbuild:lib-$(CONFIG_TOUCH) += touch.o

/* BB_AUDIT SUSv3 _NOT_ compliant -- options -a, -m not supported. */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/touch.html */

//usage:#define touch_trivial_usage
//usage:       "[-c]" IF_FEATURE_TOUCH_SUSV3(" [-d DATE] [-t DATE] [-r FILE]") " FILE..."
//usage:#define touch_full_usage "\n\n"
//usage:       "Update the last-modified date on the given FILE[s]\n"
//usage:     "\n	-c	Don't create files"
//usage:	IF_FEATURE_TOUCH_NODEREF(
//usage:     "\n	-h	Don't follow links"
//usage:	)
//usage:	IF_FEATURE_TOUCH_SUSV3(
//usage:     "\n	-d DT	Date/time to use"
//usage:     "\n	-t DT	Date/time to use"
//usage:     "\n	-r FILE	Use FILE's date/time"
//usage:	)
//usage:
//usage:#define touch_example_usage
//usage:       "$ ls -l /tmp/foo\n"
//usage:       "/bin/ls: /tmp/foo: No such file or directory\n"
//usage:       "$ touch /tmp/foo\n"
//usage:       "$ ls -l /tmp/foo\n"
//usage:       "-rw-rw-r--    1 andersen andersen        0 Apr 15 01:11 /tmp/foo\n"

/* coreutils implements:
 * -a   change only the access time
 * -c, --no-create
 *      do not create any files
 * -d, --date=STRING
 *      parse STRING and use it instead of current time
 * -f   (ignored, BSD compat)
 * -m   change only the modification time
 * -h, --no-dereference
 * -r, --reference=FILE
 *      use this file's times instead of current time
 * -t STAMP
 *      use [[CC]YY]MMDDhhmm[.ss] instead of current time
 * --time=WORD
 *      change the specified time: WORD is access, atime, or use
 */

#include "libbb.h"

int touch_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int touch_main(int argc UNUSED_PARAM, char **argv)
{
	int fd;
	int status = EXIT_SUCCESS;
	int opts;
	enum {
		OPT_c = (1 << 0),
		OPT_r = (1 << 1) * ENABLE_FEATURE_TOUCH_SUSV3,
		OPT_d = (1 << 2) * ENABLE_FEATURE_TOUCH_SUSV3,
		OPT_t = (1 << 3) * ENABLE_FEATURE_TOUCH_SUSV3,
		OPT_h = (1 << 4) * ENABLE_FEATURE_TOUCH_NODEREF,
	};
#if ENABLE_FEATURE_TOUCH_SUSV3
# if ENABLE_LONG_OPTS
	static const char touch_longopts[] ALIGN1 =
		/* name, has_arg, val */
		"no-create\0"         No_argument       "c"
		"reference\0"         Required_argument "r"
		"date\0"              Required_argument "d"
		IF_FEATURE_TOUCH_NODEREF("no-dereference\0" No_argument "h")
	;
#  define GETOPT32 getopt32long
#  define LONGOPTS ,touch_longopts
# else
#  define GETOPT32 getopt32
#  define LONGOPTS
# endif
	char *reference_file = NULL;
	char *date_str = NULL;
	struct timeval timebuf[2];
	timebuf[1].tv_usec = timebuf[0].tv_usec = 0;
#else
# define reference_file NULL
# define date_str       NULL
# define timebuf        ((struct timeval*)NULL)
# define GETOPT32 getopt32
# define LONGOPTS
#endif

	/* -d and -t both set time. In coreutils,
	 * accepted data format differs a bit between -d and -t.
	 * We accept the same formats for both */
	opts = GETOPT32(argv, "c" IF_FEATURE_TOUCH_SUSV3("r:d:t:")
				IF_FEATURE_TOUCH_NODEREF("h")
				/*ignored:*/ "fma"
				LONGOPTS
				IF_FEATURE_TOUCH_SUSV3(, &reference_file)
				IF_FEATURE_TOUCH_SUSV3(, &date_str)
				IF_FEATURE_TOUCH_SUSV3(, &date_str)
	);

	argv += optind;
	if (!*argv) {
		bb_show_usage();
	}

	if (reference_file) {
		struct stat stbuf;
		xstat(reference_file, &stbuf);
		timebuf[1].tv_sec = timebuf[0].tv_sec = stbuf.st_mtime;
		/* Can use .st_mtim.tv_nsec
		 * (or is it .st_mtimensec?? see date.c)
		 * to set microseconds too.
		 */
	}

	if (date_str) {
		struct tm tm_time;
		time_t t;

		//memset(&tm_time, 0, sizeof(tm_time));
		/* Better than memset: makes "HH:MM" dates meaningful */
		time(&t);
		localtime_r(&t, &tm_time);
		parse_datestr(date_str, &tm_time);

		/* Correct any day of week and day of year etc. fields */
		tm_time.tm_isdst = -1;  /* Be sure to recheck dst */
		t = validate_tm_time(date_str, &tm_time);

		timebuf[1].tv_sec = timebuf[0].tv_sec = t;
	}

	do {
		int result;
		result = (
#if ENABLE_FEATURE_TOUCH_NODEREF
			(opts & OPT_h) ? lutimes :
#endif
			utimes)(*argv, (reference_file || date_str) ? timebuf : NULL);
		if (result != 0) {
			if (errno == ENOENT) { /* no such file? */
				if (opts & OPT_c) {
					/* Creation is disabled, so ignore */
					continue;
				}
				/* Try to create the file */
				fd = open(*argv, O_RDWR | O_CREAT, 0666);
				if (fd >= 0) {
					xclose(fd);
					if (reference_file || date_str)
						utimes(*argv, timebuf);
					continue;
				}
			}
			status = EXIT_FAILURE;
			bb_simple_perror_msg(*argv);
		}
	} while (*++argv);

	return status;
}
