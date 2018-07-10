/*
 * Mini blkdiscard implementation for busybox
 *
 * Copyright (C) 2015 by Ari Sundholm <ari@tuxera.com> and Tuxera Inc.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config BLKDISCARD
//config:	bool "blkdiscard (5.3 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	blkdiscard discards sectors on a given device.

//applet:IF_BLKDISCARD(APPLET_NOEXEC(blkdiscard, blkdiscard, BB_DIR_USR_BIN, BB_SUID_DROP, blkdiscard))

//kbuild:lib-$(CONFIG_BLKDISCARD) += blkdiscard.o

//usage:#define blkdiscard_trivial_usage
//usage:       "[-o OFS] [-l LEN] [-s] DEVICE"
//usage:#define blkdiscard_full_usage "\n\n"
//usage:	"Discard sectors on DEVICE\n"
//usage:	"\n	-o OFS	Byte offset into device"
//usage:	"\n	-l LEN	Number of bytes to discard"
//usage:	"\n	-s	Perform a secure discard"
//usage:
//usage:#define blkdiscard_example_usage
//usage:	"$ blkdiscard -o 0 -l 1G /dev/sdb"

#include "libbb.h"
#include <linux/fs.h>

#ifndef BLKDISCARD
#define BLKDISCARD 0x1277
#endif
#ifndef BLKSECDISCARD
#define BLKSECDISCARD 0x127d
#endif

int blkdiscard_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int blkdiscard_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opts;
	const char *offset_str = "0";
	const char *length_str;
	uint64_t offset; /* Leaving these two variables out does not  */
	uint64_t length; /* shrink code size and hampers readability. */
	uint64_t range[2];
	int fd;

	enum {
		OPT_OFFSET = (1 << 0),
		OPT_LENGTH = (1 << 1),
		OPT_SECURE = (1 << 2),
	};

	opts = getopt32(argv, "^" "o:l:s" "\0" "=1", &offset_str, &length_str);
	argv += optind;

	fd = xopen(argv[0], O_RDWR|O_EXCL);
//Why bother, BLK[SEC]DISCARD will fail on non-blockdevs anyway?
//	xfstat(fd, &st);
//	if (!S_ISBLK(st.st_mode))
//		bb_error_msg_and_die("%s: not a block device", argv[0]);

	offset = xatoull_sfx(offset_str, kMG_suffixes);

	if (opts & OPT_LENGTH)
		length = xatoull_sfx(length_str, kMG_suffixes);
	else {
		xioctl(fd, BLKGETSIZE64, &length);
		length -= offset;
	}

	range[0] = offset;
	range[1] = length;
	ioctl_or_perror_and_die(fd,
			(opts & OPT_SECURE) ? BLKSECDISCARD : BLKDISCARD,
			&range,
			"%s: %s failed",
			argv[0],
			(opts & OPT_SECURE) ? "BLKSECDISCARD" : "BLKDISCARD"
	);

	if (ENABLE_FEATURE_CLEAN_UP)
		close(fd);

	return EXIT_SUCCESS;
}
