/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2010 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

uoff_t FAST_FUNC get_volume_size_in_bytes(int fd,
		const char *override,
		unsigned override_units,
		int extend)
{
	uoff_t result;

	if (override) {
		result = XATOOFF(override);
		if (result >= (uoff_t)(MAXINT(off_t)) / override_units)
			bb_error_msg_and_die("image size is too big");
		result *= override_units;
		/* seek past end fails on block devices but works on files */
		if (lseek(fd, result - 1, SEEK_SET) != (off_t)-1) {
			if (extend)
				xwrite(fd, "", 1); /* file grows if needed */
		}
		//else {
		//	bb_error_msg("warning, block device is smaller");
		//}
	} else {
		/* more portable than BLKGETSIZE[64] */
		result = xlseek(fd, 0, SEEK_END);
	}

	xlseek(fd, 0, SEEK_SET);

	/* Prevent things like this:
	 * $ dd if=/dev/zero of=foo count=1 bs=1024
	 * $ mkswap foo
	 * Setting up swapspace version 1, size = 18446744073709548544 bytes
	 *
	 * Picked 16k arbitrarily: */
	if (result < 16*1024)
		bb_error_msg_and_die("image is too small");

	return result;
}
