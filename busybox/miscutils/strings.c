/* vi: set sw=4 ts=4: */
/*
 * strings implementation for busybox
 *
 * Copyright 2003 Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config STRINGS
//config:	bool "strings (4.3 kb)"
//config:	default y
//config:	help
//config:	strings prints the printable character sequences for each file
//config:	specified.

//applet:IF_STRINGS(APPLET(strings, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_STRINGS) += strings.o

//usage:#define strings_trivial_usage
//usage:       "[-fo] [-t o/d/x] [-n LEN] [FILE]..."
//usage:#define strings_full_usage "\n\n"
//usage:       "Display printable strings in a binary file\n"
//We usually don't bother user with "nop" options. They work, but are not shown:
////usage:     "\n	-a		Scan whole file (default)"
//unimplemented alternative is -d: Only strings from initialized, loaded data sections
//usage:     "\n	-f		Precede strings with filenames"
//usage:     "\n	-o		Precede strings with octal offsets"
//usage:     "\n	-t o/d/x	Precede strings with offsets in base 8/10/16"
//usage:     "\n	-n LEN		At least LEN characters form a string (default 4)"

#include "libbb.h"

#define WHOLE_FILE    1
#define PRINT_NAME    2
#define PRINT_OFFSET  4
#define SIZE          8
#define PRINT_RADIX  16

int strings_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int strings_main(int argc UNUSED_PARAM, char **argv)
{
	int n, c, status = EXIT_SUCCESS;
	unsigned count;
	off_t offset;
	FILE *file;
	char *string;
	const char *fmt = "%s: ";
	const char *n_arg = "4";
	/* default for -o */
	const char *radix = "o";
	char *radix_fmt;

	getopt32(argv, "afon:t:", &n_arg, &radix);
	/* -a is our default behaviour */
	/*argc -= optind;*/
	argv += optind;

	n = xatou_range(n_arg, 1, INT_MAX);
	string = xzalloc(n + 1);
	n--;

	if ((radix[0] != 'd' && radix[0] != 'o' && radix[0] != 'x') || radix[1] != 0)
		bb_show_usage();

	radix_fmt = xasprintf("%%7"OFF_FMT"%s ", radix);

	if (!*argv) {
		fmt = "{%s}: ";
		*--argv = (char *)bb_msg_standard_input;
	}

	do {
		file = fopen_or_warn_stdin(*argv);
		if (!file) {
			status = EXIT_FAILURE;
			continue;
		}
		offset = 0;
		count = 0;
		do {
			c = fgetc(file);
			if (isprint_asciionly(c) || c == '\t') {
				if (count > n) {
					bb_putchar(c);
				} else {
					string[count] = c;
					if (count == n) {
						if (option_mask32 & PRINT_NAME) {
							printf(fmt, *argv);
						}
						if (option_mask32 & (PRINT_OFFSET | PRINT_RADIX)) {
							printf(radix_fmt, offset - n);
						}
						fputs(string, stdout);
					}
					count++;
				}
			} else {
				if (count > n) {
					bb_putchar('\n');
				}
				count = 0;
			}
			offset++;
		} while (c != EOF);
		fclose_if_not_stdin(file);
	} while (*++argv);

	if (ENABLE_FEATURE_CLEAN_UP) {
		free(string);
		free(radix_fmt);
	}

	fflush_stdout_and_exit(status);
}
