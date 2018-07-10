/* vi: set sw=4 ts=4: */
/*
 * cksum - calculate the CRC32 checksum of a file
 *
 * Copyright (C) 2006 by Rob Sullivan, with ideas from code by Walter Harms
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config CKSUM
//config:	bool "cksum (4.2 kb)"
//config:	default y
//config:	help
//config:	cksum is used to calculate the CRC32 checksum of a file.

//applet:IF_CKSUM(APPLET_NOEXEC(cksum, cksum, BB_DIR_USR_BIN, BB_SUID_DROP, cksum))
/* bb_common_bufsiz1 usage here is safe wrt NOEXEC: not expecting it to be zeroed. */

//kbuild:lib-$(CONFIG_CKSUM) += cksum.o

//usage:#define cksum_trivial_usage
//usage:       "FILE..."
//usage:#define cksum_full_usage "\n\n"
//usage:       "Calculate the CRC32 checksums of FILEs"

#include "libbb.h"
#include "common_bufsiz.h"

/* This is a NOEXEC applet. Be very careful! */

int cksum_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int cksum_main(int argc UNUSED_PARAM, char **argv)
{
	uint32_t *crc32_table = crc32_filltable(NULL, 1);
	int exit_code = EXIT_SUCCESS;

#if ENABLE_DESKTOP
	getopt32(argv, ""); /* coreutils 6.9 compat */
	argv += optind;
#else
	argv++;
#endif

	setup_common_bufsiz();
	do {
		uint32_t crc;
		off_t filesize;
		int fd = open_or_warn_stdin(*argv ? *argv : bb_msg_standard_input);

		if (fd < 0) {
			exit_code = EXIT_FAILURE;
			continue;
		}

		crc = 0;
		filesize = 0;
#define read_buf bb_common_bufsiz1
		for (;;) {
			uoff_t t;
			int bytes_read = safe_read(fd, read_buf, COMMON_BUFSIZE);
			if (bytes_read > 0) {
				filesize += bytes_read;
			} else {
				/* Checksum filesize bytes, LSB first, and exit */
				close(fd);
				fd = -1; /* break flag */
				t = filesize;
				bytes_read = 0;
				while (t != 0) {
					read_buf[bytes_read++] = (uint8_t)t;
					t >>= 8;
				}
			}
			crc = crc32_block_endian1(crc, read_buf, bytes_read, crc32_table);
			if (fd < 0)
				break;
		}

		crc = ~crc;
		printf((*argv ? "%u %"OFF_FMT"u %s\n" : "%u %"OFF_FMT"u\n"),
				(unsigned)crc, filesize, *argv);
	} while (*argv && *++argv);

	fflush_stdout_and_exit(exit_code);
}
