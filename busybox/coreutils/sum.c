/* vi: set sw=4 ts=4: */
/*
 * sum -- checksum and count the blocks in a file
 *     Like BSD sum or SysV sum -r, except like SysV sum if -s option is given.
 *
 * Copyright (C) 86, 89, 91, 1995-2002, 2004 Free Software Foundation, Inc.
 * Copyright (C) 2005 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2005 by Mike Frysinger <vapier@gentoo.org>
 *
 * Written by Kayvan Aghaiepour and David MacKenzie
 * Taken from coreutils and turned into a busybox applet by Mike Frysinger
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config SUM
//config:	bool "sum (4.3 kb)"
//config:	default y
//config:	help
//config:	checksum and count the blocks in a file

//applet:IF_SUM(APPLET(sum, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_SUM) += sum.o

//usage:#define sum_trivial_usage
//usage:       "[-rs] [FILE]..."
//usage:#define sum_full_usage "\n\n"
//usage:       "Checksum and count the blocks in a file\n"
//usage:     "\n	-r	Use BSD sum algorithm (1K blocks)"
//usage:     "\n	-s	Use System V sum algorithm (512byte blocks)"

#include "libbb.h"
#include "common_bufsiz.h"

enum { SUM_BSD, PRINT_NAME, SUM_SYSV };

/* BSD: calculate and print the rotated checksum and the size in 1K blocks
   The checksum varies depending on sizeof (int). */
/* SYSV: calculate and print the checksum and the size in 512-byte blocks */
/* Return 1 if successful.  */
static unsigned sum_file(const char *file, unsigned type)
{
	unsigned long long total_bytes = 0;
	int fd, r;
	/* The sum of all the input bytes, modulo (UINT_MAX + 1).  */
	unsigned s = 0;

#define buf bb_common_bufsiz1
	setup_common_bufsiz();

	fd = open_or_warn_stdin(file);
	if (fd == -1)
		return 0;

	while (1) {
		size_t bytes_read = safe_read(fd, buf, COMMON_BUFSIZE);

		if ((ssize_t)bytes_read <= 0) {
			r = (fd && close(fd) != 0);
			if (!bytes_read && !r)
				/* no error */
				break;
			bb_simple_perror_msg(file);
			return 0;
		}

		total_bytes += bytes_read;
		if (type >= SUM_SYSV) {
			do s += buf[--bytes_read]; while (bytes_read);
		} else {
			r = 0;
			do {
				s = (s >> 1) + ((s & 1) << 15);
				s += buf[r++];
				s &= 0xffff; /* Keep it within bounds. */
			} while (--bytes_read);
		}
	}

	if (type < PRINT_NAME)
		file = "";
	if (type >= SUM_SYSV) {
		r = (s & 0xffff) + ((s & 0xffffffff) >> 16);
		s = (r & 0xffff) + (r >> 16);
		printf("%u %llu %s\n", s, (total_bytes + 511) / 512, file);
	} else
		printf("%05u %5llu %s\n", s, (total_bytes + 1023) / 1024, file);
	return 1;
#undef buf
}

int sum_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int sum_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned n;
	unsigned type = SUM_BSD;

	n = getopt32(argv, "sr");
	argv += optind;
	if (n & 1) type = SUM_SYSV;
	/* give the bsd priority over sysv func */
	if (n & 2) type = SUM_BSD;

	if (!argv[0]) {
		/* Do not print the name */
		n = sum_file("-", type);
	} else {
		/* Need to print the name if either
		 * - more than one file given
		 * - doing sysv */
		type += (argv[1] || type == SUM_SYSV);
		n = 1;
		do {
			n &= sum_file(*argv, type);
		} while (*++argv);
	}
	return !n;
}
