/* vi: set sw=4 ts=4: */
/*
 * CRC32 table fill function
 * Copyright (C) 2006 by Rob Sullivan <cogito.ergo.cogito@gmail.com>
 * (I can't really claim much credit however, as the algorithm is
 * very well-known)
 *
 * The following function creates a CRC32 table depending on whether
 * a big-endian (0x04c11db7) or little-endian (0xedb88320) CRC32 is
 * required. Admittedly, there are other CRC32 polynomials floating
 * around, but Busybox doesn't use them.
 *
 * endian = 1: big-endian
 * endian = 0: little-endian
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

uint32_t *global_crc32_table;

uint32_t* FAST_FUNC crc32_filltable(uint32_t *crc_table, int endian)
{
	uint32_t polynomial = endian ? 0x04c11db7 : 0xedb88320;
	uint32_t c;
	unsigned i, j;

	if (!crc_table)
		crc_table = xmalloc(256 * sizeof(uint32_t));

	for (i = 0; i < 256; i++) {
		c = endian ? (i << 24) : i;
		for (j = 8; j; j--) {
			if (endian)
				c = (c&0x80000000) ? ((c << 1) ^ polynomial) : (c << 1);
			else
				c = (c&1) ? ((c >> 1) ^ polynomial) : (c >> 1);
		}
		*crc_table++ = c;
	}

	return crc_table - 256;
}
/* Common uses: */
uint32_t* FAST_FUNC crc32_new_table_le(void)
{
	return crc32_filltable(NULL, 0);
}
uint32_t* FAST_FUNC global_crc32_new_table_le(void)
{
	global_crc32_table = crc32_new_table_le();
	return global_crc32_table;
}

uint32_t FAST_FUNC crc32_block_endian1(uint32_t val, const void *buf, unsigned len, uint32_t *crc_table)
{
	const void *end = (uint8_t*)buf + len;

	while (buf != end) {
		val = (val << 8) ^ crc_table[(val >> 24) ^ *(uint8_t*)buf];
		buf = (uint8_t*)buf + 1;
	}
	return val;
}

uint32_t FAST_FUNC crc32_block_endian0(uint32_t val, const void *buf, unsigned len, uint32_t *crc_table)
{
	const void *end = (uint8_t*)buf + len;

	while (buf != end) {
		val = crc_table[(uint8_t)val ^ *(uint8_t*)buf] ^ (val >> 8);
		buf = (uint8_t*)buf + 1;
	}
	return val;
}
