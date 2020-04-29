/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2015-2017 Netronome Systems, Inc. */

#ifndef NFP_CRC32_H
#define NFP_CRC32_H

#include <linux/kernel.h>
#include <linux/crc32.h>

/**
 * crc32_posix_end() - Finalize POSIX CRC32 working state
 * @crc:	Current CRC32 working state
 * @total_len:	Total length of data that was CRC32'd
 *
 * Return: Final POSIX CRC32 value
 */
static inline u32 crc32_posix_end(u32 crc, size_t total_len)
{
	/* Extend with the length of the string. */
	while (total_len != 0) {
		u8 c = total_len & 0xff;

		crc = crc32_be(crc, &c, 1);
		total_len >>= 8;
	}

	return ~crc;
}

static inline u32 crc32_posix(const void *buff, size_t len)
{
	return crc32_posix_end(crc32_be(0, buff, len), len);
}

#endif /* NFP_CRC32_H */
