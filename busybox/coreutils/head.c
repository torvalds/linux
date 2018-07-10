/* vi: set sw=4 ts=4: */
/*
 * head implementation for busybox
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config HEAD
//config:	bool "head (3.7 kb)"
//config:	default y
//config:	help
//config:	head is used to print the first specified number of lines
//config:	from files.
//config:
//config:config FEATURE_FANCY_HEAD
//config:	bool "Enable -c, -q, and -v"
//config:	default y
//config:	depends on HEAD

//applet:IF_HEAD(APPLET_NOEXEC(head, head, BB_DIR_USR_BIN, BB_SUID_DROP, head))

//kbuild:lib-$(CONFIG_HEAD) += head.o

/* BB_AUDIT SUSv3 compliant */
/* BB_AUDIT GNU compatible -c, -q, and -v options in 'fancy' configuration. */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/head.html */

//usage:#define head_trivial_usage
//usage:       "[OPTIONS] [FILE]..."
//usage:#define head_full_usage "\n\n"
//usage:       "Print first 10 lines of each FILE (or stdin) to stdout.\n"
//usage:       "With more than one FILE, precede each with a filename header.\n"
//usage:     "\n	-n N[kbm]	Print first N lines"
//usage:	IF_FEATURE_FANCY_HEAD(
//usage:     "\n	-n -N[kbm]	Print all except N last lines"
//usage:     "\n	-c [-]N[kbm]	Print first N bytes"
//usage:     "\n	-q		Never print headers"
//usage:     "\n	-v		Always print headers"
//usage:	)
//usage:     "\n"
//usage:     "\nN may be suffixed by k (x1024), b (x512), or m (x1024^2)."
//usage:
//usage:#define head_example_usage
//usage:       "$ head -n 2 /etc/passwd\n"
//usage:       "root:x:0:0:root:/root:/bin/bash\n"
//usage:       "daemon:x:1:1:daemon:/usr/sbin:/bin/sh\n"

#include "libbb.h"

/* This is a NOEXEC applet. Be very careful! */

#if !ENABLE_FEATURE_FANCY_HEAD
# define print_first_N(fp,count,bytes) print_first_N(fp,count)
#endif
static void
print_first_N(FILE *fp, unsigned long count, bool count_bytes)
{
#if !ENABLE_FEATURE_FANCY_HEAD
	const int count_bytes = 0;
#endif
	while (count) {
		int c = getc(fp);
		if (c == EOF)
			break;
		if (count_bytes || (c == '\n'))
			--count;
		putchar(c);
	}
}

#if ENABLE_FEATURE_FANCY_HEAD
static void
print_except_N_last_bytes(FILE *fp, unsigned count)
{
	unsigned char *circle = xmalloc(++count);
	unsigned head = 0;
	for(;;) {
		int c;
		c = getc(fp);
		if (c == EOF)
			goto ret;
		circle[head++] = c;
		if (head == count)
			break;
	}
	for (;;) {
		int c;
		if (head == count)
			head = 0;
		putchar(circle[head]);
		c = getc(fp);
		if (c == EOF)
			goto ret;
		circle[head] = c;
		head++;
	}
 ret:
	free(circle);
}

static void
print_except_N_last_lines(FILE *fp, unsigned count)
{
	char **circle = xzalloc((++count) * sizeof(circle[0]));
	unsigned head = 0;
	for(;;) {
		char *c;
		c = xmalloc_fgets(fp);
		if (!c)
			goto ret;
		circle[head++] = c;
		if (head == count)
			break;
	}
	for (;;) {
		char *c;
		if (head == count)
			head = 0;
		fputs(circle[head], stdout);
		c = xmalloc_fgets(fp);
		if (!c)
			goto ret;
		free(circle[head]);
		circle[head++] = c;
	}
 ret:
	head = 0;
	for(;;) {
		free(circle[head++]);
		if (head == count)
			break;
	}
	free(circle);
}
#else
/* Must never be called */
void print_except_N_last_bytes(FILE *fp, unsigned count);
void print_except_N_last_lines(FILE *fp, unsigned count);
#endif

#if !ENABLE_FEATURE_FANCY_HEAD
# define eat_num(negative_N,p) eat_num(p)
#endif
static unsigned long
eat_num(bool *negative_N, const char *p)
{
#if ENABLE_FEATURE_FANCY_HEAD
	if (*p == '-') {
		*negative_N = 1;
		p++;
	}
#endif
	return xatoul_sfx(p, bkm_suffixes);
}

static const char head_opts[] ALIGN1 =
	"n:"
#if ENABLE_FEATURE_FANCY_HEAD
	"c:qv"
#endif
	;

#define header_fmt_str "\n==> %s <==\n"

int head_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int head_main(int argc, char **argv)
{
	unsigned long count = 10;
#if ENABLE_FEATURE_FANCY_HEAD
	int header_threshhold = 1;
	bool count_bytes = 0;
	bool negative_N = 0;
#else
# define header_threshhold 1
# define count_bytes       0
# define negative_N        0
#endif
	FILE *fp;
	const char *fmt;
	char *p;
	int opt;
	int retval = EXIT_SUCCESS;

#if ENABLE_INCLUDE_SUSv2 || ENABLE_FEATURE_FANCY_HEAD
	/* Allow legacy syntax of an initial numeric option without -n. */
	if (argv[1] && argv[1][0] == '-'
	 && isdigit(argv[1][1])
	) {
		--argc;
		++argv;
		p = argv[0] + 1;
		goto GET_COUNT;
	}
#endif

	/* No size benefit in converting this to getopt32 */
	while ((opt = getopt(argc, argv, head_opts)) > 0) {
		switch (opt) {
#if ENABLE_FEATURE_FANCY_HEAD
		case 'q':
			header_threshhold = INT_MAX;
			break;
		case 'v':
			header_threshhold = -1;
			break;
		case 'c':
			count_bytes = 1;
			/* fall through */
#endif
		case 'n':
			p = optarg;
#if ENABLE_INCLUDE_SUSv2 || ENABLE_FEATURE_FANCY_HEAD
 GET_COUNT:
#endif
			count = eat_num(&negative_N, p);
			break;
		default:
			bb_show_usage();
		}
	}

	argc -= optind;
	argv += optind;
	if (!*argv)
		*--argv = (char*)"-";

	fmt = header_fmt_str + 1;
	if (argc <= header_threshhold) {
#if ENABLE_FEATURE_FANCY_HEAD
		header_threshhold = 0;
#else
		fmt += 11; /* "" */
#endif
	}
	if (negative_N) {
		if (count >= INT_MAX / sizeof(char*))
			bb_error_msg("count is too big: %lu", count);
	}

	do {
		fp = fopen_or_warn_stdin(*argv);
		if (fp) {
			if (fp == stdin) {
				*argv = (char *) bb_msg_standard_input;
			}
			if (header_threshhold) {
				printf(fmt, *argv);
			}
			if (negative_N) {
				if (count_bytes) {
					print_except_N_last_bytes(fp, count);
				} else {
					print_except_N_last_lines(fp, count);
				}
			} else {
				print_first_N(fp, count, count_bytes);
			}
			die_if_ferror_stdout();
			if (fclose_if_not_stdin(fp)) {
				bb_simple_perror_msg(*argv);
				retval = EXIT_FAILURE;
			}
		} else {
			retval = EXIT_FAILURE;
		}
		fmt = header_fmt_str;
	} while (*++argv);

	fflush_stdout_and_exit(retval);
}
