/* vi: set sw=4 ts=4: */
/*
 * Mini date implementation for busybox
 *
 * by Matthew Grant <grantma@anathoth.gen.nz>
 *
 * iso-format handling added by Robert Griebl <griebl@gmx.de>
 * bugfixes and cleanup by Bernhard Reutner-Fischer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* This 'date' command supports only 2 time setting formats,
   all the GNU strftime stuff (its in libc, lets use it),
   setting time using UTC and displaying it, as well as
   an RFC 2822 compliant date output for shell scripting
   mail commands */

/* Input parsing code is always bulky - used heavy duty libc stuff as
   much as possible, missed out a lot of bounds checking */

//config:config DATE
//config:	bool "date (7.1 kb)"
//config:	default y
//config:	help
//config:	date is used to set the system date or display the
//config:	current time in the given format.
//config:
//config:config FEATURE_DATE_ISOFMT
//config:	bool "Enable ISO date format output (-I)"
//config:	default y
//config:	depends on DATE
//config:	help
//config:	Enable option (-I) to output an ISO-8601 compliant
//config:	date/time string.
//config:
//config:# defaults to "no": stat's nanosecond field is a bit non-portable
//config:config FEATURE_DATE_NANO
//config:	bool "Support %[num]N nanosecond format specifier"
//config:	default n  # syscall(__NR_clock_gettime)
//config:	depends on DATE
//config:	select PLATFORM_LINUX
//config:	help
//config:	Support %[num]N format specifier. Adds ~250 bytes of code.
//config:
//config:config FEATURE_DATE_COMPAT
//config:	bool "Support weird 'date MMDDhhmm[[YY]YY][.ss]' format"
//config:	default y
//config:	depends on DATE
//config:	help
//config:	System time can be set by 'date -s DATE' and simply 'date DATE',
//config:	but formats of DATE string are different. 'date DATE' accepts
//config:	a rather weird MMDDhhmm[[YY]YY][.ss] format with completely
//config:	unnatural placement of year between minutes and seconds.
//config:	date -s (and other commands like touch -d) use more sensible
//config:	formats (for one, ISO format YYYY-MM-DD hh:mm:ss.ssssss).
//config:
//config:	With this option off, 'date DATE' is 'date -s DATE' support
//config:	the same format. With it on, 'date DATE' additionally supports
//config:	MMDDhhmm[[YY]YY][.ss] format.

//applet:IF_DATE(APPLET_NOEXEC(date, date, BB_DIR_BIN, BB_SUID_DROP, date))
/* bb_common_bufsiz1 usage here is safe wrt NOEXEC: not expecting it to be zeroed. */

//kbuild:lib-$(CONFIG_DATE) += date.o

/* GNU coreutils 6.9 man page:
 * date [OPTION]... [+FORMAT]
 * date [-u|--utc|--universal] [MMDDhhmm[[CC]YY][.ss]]
 * -d, --date=STRING
 *      display time described by STRING, not 'now'
 * -f, --file=DATEFILE
 *      like --date once for each line of DATEFILE
 * -r, --reference=FILE
 *      display the last modification time of FILE
 * -R, --rfc-2822
 *      output date and time in RFC 2822 format.
 *      Example: Mon, 07 Aug 2006 12:34:56 -0600
 * --rfc-3339=TIMESPEC
 *      output date and time in RFC 3339 format.
 *      TIMESPEC='date', 'seconds', or 'ns'
 *      Date and time components are separated by a single space:
 *      2006-08-07 12:34:56-06:00
 * -s, --set=STRING
 *      set time described by STRING
 * -u, --utc, --universal
 *      print or set Coordinated Universal Time
 *
 * Busybox:
 * long options are not supported
 * -f is not supported
 * -I seems to roughly match --rfc-3339, but -I has _optional_ param
 *    (thus "-I seconds" doesn't work, only "-Iseconds"),
 *    and does not support -Ins
 * -D FMT is a bbox extension for _input_ conversion of -d DATE
 */

//usage:#define date_trivial_usage
//usage:       "[OPTIONS] [+FMT] [TIME]"
//usage:#define date_full_usage "\n\n"
//usage:       "Display time (using +FMT), or set time\n"
//usage:	IF_NOT_LONG_OPTS(
//usage:     "\n	[-s] TIME	Set time to TIME"
//usage:     "\n	-u		Work in UTC (don't convert to local time)"
//usage:     "\n	-R		Output RFC-2822 compliant date string"
//usage:	) IF_LONG_OPTS(
//usage:     "\n	[-s,--set] TIME	Set time to TIME"
//usage:     "\n	-u,--utc	Work in UTC (don't convert to local time)"
//usage:     "\n	-R,--rfc-2822	Output RFC-2822 compliant date string"
//usage:	)
//usage:	IF_FEATURE_DATE_ISOFMT(
//usage:     "\n	-I[SPEC]	Output ISO-8601 compliant date string"
//usage:     "\n			SPEC='date' (default) for date only,"
//usage:     "\n			'hours', 'minutes', or 'seconds' for date and"
//usage:     "\n			time to the indicated precision"
//usage:	)
//usage:	IF_NOT_LONG_OPTS(
//usage:     "\n	-r FILE		Display last modification time of FILE"
//usage:     "\n	-d TIME		Display TIME, not 'now'"
//usage:	) IF_LONG_OPTS(
//usage:     "\n	-r,--reference FILE	Display last modification time of FILE"
//usage:     "\n	-d,--date TIME	Display TIME, not 'now'"
//usage:	)
//usage:	IF_FEATURE_DATE_ISOFMT(
//usage:     "\n	-D FMT		Use FMT for -d TIME conversion"
//usage:	)
//usage:     "\n"
//usage:     "\nRecognized TIME formats:"
//usage:     "\n	hh:mm[:ss]"
//usage:     "\n	[YYYY.]MM.DD-hh:mm[:ss]"
//usage:     "\n	YYYY-MM-DD hh:mm[:ss]"
//usage:     "\n	[[[[[YY]YY]MM]DD]hh]mm[.ss]"
//usage:	IF_FEATURE_DATE_COMPAT(
//usage:     "\n	'date TIME' form accepts MMDDhhmm[[YY]YY][.ss] instead"
//usage:	)
//usage:
//usage:#define date_example_usage
//usage:       "$ date\n"
//usage:       "Wed Apr 12 18:52:41 MDT 2000\n"

#include "libbb.h"
#include "common_bufsiz.h"
#if ENABLE_FEATURE_DATE_NANO
# include <sys/syscall.h>
#endif

enum {
	OPT_RFC2822   = (1 << 0), /* R */
	OPT_SET       = (1 << 1), /* s */
	OPT_UTC       = (1 << 2), /* u */
	OPT_DATE      = (1 << 3), /* d */
	OPT_REFERENCE = (1 << 4), /* r */
	OPT_TIMESPEC  = (1 << 5) * ENABLE_FEATURE_DATE_ISOFMT, /* I */
	OPT_HINT      = (1 << 6) * ENABLE_FEATURE_DATE_ISOFMT, /* D */
};

#if ENABLE_LONG_OPTS
static const char date_longopts[] ALIGN1 =
		"rfc-822\0"   No_argument       "R"
		"rfc-2822\0"  No_argument       "R"
		"set\0"       Required_argument "s"
		"utc\0"       No_argument       "u"
	/*	"universal\0" No_argument       "u" */
		"date\0"      Required_argument "d"
		"reference\0" Required_argument "r"
		;
#endif

/* We are a NOEXEC applet.
 * Obstacles to NOFORK:
 * - we change env
 * - xasprintf result not freed
 * - after xasprintf we use other xfuncs
 */

static void maybe_set_utc(int opt)
{
	if (opt & OPT_UTC)
		putenv((char*)"TZ=UTC0");
}

int date_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int date_main(int argc UNUSED_PARAM, char **argv)
{
	struct timespec ts;
	struct tm tm_time;
	char buf_fmt_dt2str[64];
	unsigned opt;
	int ifmt = -1;
	char *date_str;
	char *fmt_dt2str;
	char *fmt_str2dt;
	char *filename;
	char *isofmt_arg = NULL;

	opt = getopt32long(argv, "^"
			"Rs:ud:r:"
			IF_FEATURE_DATE_ISOFMT("I::D:")
			"\0"
			"d--s:s--d"
			IF_FEATURE_DATE_ISOFMT(":R--I:I--R"),
			date_longopts,
			&date_str, &date_str, &filename
			IF_FEATURE_DATE_ISOFMT(, &isofmt_arg, &fmt_str2dt)
	);
	argv += optind;

	maybe_set_utc(opt);

	if (ENABLE_FEATURE_DATE_ISOFMT && (opt & OPT_TIMESPEC)) {
		ifmt = 0; /* default is date */
		if (isofmt_arg) {
			static const char isoformats[] ALIGN1 =
				"date\0""hours\0""minutes\0""seconds\0"; /* ns? */
			ifmt = index_in_substrings(isoformats, isofmt_arg);
			if (ifmt < 0)
				bb_show_usage();
		}
	}

	fmt_dt2str = NULL;
	if (argv[0] && argv[0][0] == '+') {
		fmt_dt2str = &argv[0][1]; /* skip over the '+' */
		argv++;
	}
	if (!(opt & (OPT_SET | OPT_DATE))) {
		opt |= OPT_SET;
		date_str = argv[0]; /* can be NULL */
		if (date_str) {
#if ENABLE_FEATURE_DATE_COMPAT
			int len = strspn(date_str, "0123456789");
			if (date_str[len] == '\0'
			 || (date_str[len] == '.'
			    && isdigit(date_str[len+1])
			    && isdigit(date_str[len+2])
			    && date_str[len+3] == '\0'
			    )
			) {
				/* Dreaded MMDDhhmm[[CC]YY][.ss] format!
				 * It does not match -d or -s format.
				 * Some users actually do use it.
				 */
				len -= 8;
				if (len < 0 || len > 4 || (len & 1))
					bb_error_msg_and_die(bb_msg_invalid_date, date_str);
				if (len != 0) { /* move YY or CCYY to front */
					char buf[4];
					memcpy(buf, date_str + 8, len);
					memmove(date_str + len, date_str, 8);
					memcpy(date_str, buf, len);
				}
			}
#endif
			argv++;
		}
	}
	if (*argv)
		bb_show_usage();

	/* Now we have parsed all the information except the date format
	 * which depends on whether the clock is being set or read */

	if (opt & OPT_REFERENCE) {
		struct stat statbuf;
		xstat(filename, &statbuf);
		ts.tv_sec = statbuf.st_mtime;
#if ENABLE_FEATURE_DATE_NANO
		ts.tv_nsec = statbuf.st_mtim.tv_nsec;
		/* Some toolchains use .st_mtimensec instead of st_mtim.tv_nsec.
		 * If you need #define _SVID_SOURCE 1 to enable st_mtim.tv_nsec,
		 * drop a mail to project mailing list please
		 */
#endif
	} else {
#if ENABLE_FEATURE_DATE_NANO
		/* libc has incredibly messy way of doing this,
		 * typically requiring -lrt. We just skip all this mess */
		syscall(__NR_clock_gettime, CLOCK_REALTIME, &ts);
#else
		time(&ts.tv_sec);
#endif
	}
	localtime_r(&ts.tv_sec, &tm_time);

	/* If date string is given, update tm_time, and maybe set date */
	if (date_str != NULL) {
		/* Zero out fields - take her back to midnight! */
		tm_time.tm_sec = 0;
		tm_time.tm_min = 0;
		tm_time.tm_hour = 0;

		/* Process any date input to UNIX time since 1 Jan 1970 */
		if (ENABLE_FEATURE_DATE_ISOFMT && (opt & OPT_HINT)) {
			if (strptime(date_str, fmt_str2dt, &tm_time) == NULL)
				bb_error_msg_and_die(bb_msg_invalid_date, date_str);
		} else {
			parse_datestr(date_str, &tm_time);
		}

		/* Correct any day of week and day of year etc. fields */
		/* Be sure to recheck dst (but not if date is time_t format) */
		if (date_str[0] != '@')
			tm_time.tm_isdst = -1;
		ts.tv_sec = validate_tm_time(date_str, &tm_time);

		/* if setting time, set it */
		if ((opt & OPT_SET) && stime(&ts.tv_sec) < 0) {
			bb_perror_msg("can't set date");
		}
	}

	/* Display output */

	/* Deal with format string */
	if (fmt_dt2str == NULL) {
		int i;
		fmt_dt2str = buf_fmt_dt2str;
		if (ENABLE_FEATURE_DATE_ISOFMT && ifmt >= 0) {
			/* -I[SPEC]: 0:date 1:hours 2:minutes 3:seconds */
			strcpy(fmt_dt2str, "%Y-%m-%dT%H:%M:%S");
			i = 8 + 3 * ifmt;
			if (ifmt != 0) {
				/* TODO: if (ifmt==4) i += sprintf(&fmt_dt2str[i], ",%09u", nanoseconds); */
 format_utc:
				fmt_dt2str[i++] = '%';
				fmt_dt2str[i++] = (opt & OPT_UTC) ? 'Z' : 'z';
			}
			fmt_dt2str[i] = '\0';
		} else if (opt & OPT_RFC2822) {
			/* -R. undo busybox.c setlocale */
			if (ENABLE_LOCALE_SUPPORT)
				setlocale(LC_TIME, "C");
			strcpy(fmt_dt2str, "%a, %d %b %Y %H:%M:%S ");
			i = sizeof("%a, %d %b %Y %H:%M:%S ")-1;
			goto format_utc;
		} else { /* default case */
			fmt_dt2str = (char*)"%a %b %e %H:%M:%S %Z %Y";
		}
	}
#if ENABLE_FEATURE_DATE_NANO
	else {
		/* User-specified fmt_dt2str */
		/* Search for and process "%N" */
		char *p = fmt_dt2str;
		while ((p = strchr(p, '%')) != NULL) {
			int n, m;
			unsigned pres, scale;

			p++;
			if (*p == '%') {
				p++;
				continue;
			}
			n = strspn(p, "0123456789");
			if (p[n] != 'N') {
				p += n;
				continue;
			}
			/* We have "%[nnn]N" */
			p[-1] = '\0';
			p[n] = '\0';
			scale = 1;
			pres = 9;
			if (n) {
				pres = xatoi_positive(p);
				if (pres == 0)
					pres = 9;
				m = 9 - pres;
				while (--m >= 0)
					scale *= 10;
			}

			m = p - fmt_dt2str;
			p += n + 1;
			fmt_dt2str = xasprintf("%s%0*u%s", fmt_dt2str, pres, (unsigned)ts.tv_nsec / scale, p);
			p = fmt_dt2str + m;
		}
	}
#endif

#define date_buf bb_common_bufsiz1
	setup_common_bufsiz();
	if (*fmt_dt2str == '\0') {
		/* With no format string, just print a blank line */
		date_buf[0] = '\0';
	} else {
		/* Handle special conversions */
		if (is_prefixed_with(fmt_dt2str, "%f")) {
			fmt_dt2str = (char*)"%Y.%m.%d-%H:%M:%S";
		}
		/* Generate output string */
		strftime(date_buf, COMMON_BUFSIZE, fmt_dt2str, &tm_time);
	}
	puts(date_buf);

	return EXIT_SUCCESS;
}
