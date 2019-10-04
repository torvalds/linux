// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

/* Copyright (c) 2011-2015 Stephan Brumme. All rights reserved.
 * Slicing-by-16 contributed by Bulat Ziganshin
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the author be held liable for any damages arising from the
 * of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software.
 * 2. If you use this software in a product, an acknowledgment in the product
 *    documentation would be appreciated but is not required.
 * 3. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 *
 * Taken from http://create.stephan-brumme.com/crc32/ and adapted.
 */

#include "dr_types.h"

#define DR_STE_CRC_POLY 0xEDB88320L

static u32 dr_ste_crc_tab32[8][256];

static void dr_crc32_calc_lookup_entry(u32 (*tbl)[256], u8 i, u8 j)
{
	tbl[i][j] = (tbl[i - 1][j] >> 8) ^ tbl[0][tbl[i - 1][j] & 0xff];
}

void mlx5dr_crc32_init_table(void)
{
	u32 crc, i, j;

	for (i = 0; i < 256; i++) {
		crc = i;
		for (j = 0; j < 8; j++) {
			if (crc & 0x00000001L)
				crc = (crc >> 1) ^ DR_STE_CRC_POLY;
			else
				crc = crc >> 1;
		}
		dr_ste_crc_tab32[0][i] = crc;
	}

	/* Init CRC lookup tables according to crc_slice_8 algorithm */
	for (i = 0; i < 256; i++) {
		dr_crc32_calc_lookup_entry(dr_ste_crc_tab32, 1, i);
		dr_crc32_calc_lookup_entry(dr_ste_crc_tab32, 2, i);
		dr_crc32_calc_lookup_entry(dr_ste_crc_tab32, 3, i);
		dr_crc32_calc_lookup_entry(dr_ste_crc_tab32, 4, i);
		dr_crc32_calc_lookup_entry(dr_ste_crc_tab32, 5, i);
		dr_crc32_calc_lookup_entry(dr_ste_crc_tab32, 6, i);
		dr_crc32_calc_lookup_entry(dr_ste_crc_tab32, 7, i);
	}
}

/* Compute CRC32 (Slicing-by-8 algorithm) */
u32 mlx5dr_crc32_slice8_calc(const void *input_data, size_t length)
{
	const u32 *curr = (const u32 *)input_data;
	const u8 *curr_char;
	u32 crc = 0, one, two;

	if (!input_data)
		return 0;

	/* Process eight bytes at once (Slicing-by-8) */
	while (length >= 8) {
		one = *curr++ ^ crc;
		two = *curr++;

		crc = dr_ste_crc_tab32[0][(two >> 24) & 0xff]
			^ dr_ste_crc_tab32[1][(two >> 16) & 0xff]
			^ dr_ste_crc_tab32[2][(two >> 8) & 0xff]
			^ dr_ste_crc_tab32[3][two & 0xff]
			^ dr_ste_crc_tab32[4][(one >> 24) & 0xff]
			^ dr_ste_crc_tab32[5][(one >> 16) & 0xff]
			^ dr_ste_crc_tab32[6][(one >> 8) & 0xff]
			^ dr_ste_crc_tab32[7][one & 0xff];

		length -= 8;
	}

	curr_char = (const u8 *)curr;
	/* Remaining 1 to 7 bytes (standard algorithm) */
	while (length-- != 0)
		crc = (crc >> 8) ^ dr_ste_crc_tab32[0][(crc & 0xff)
			^ *curr_char++];

	return ((crc >> 24) & 0xff) | ((crc << 8) & 0xff0000) |
		((crc >> 8) & 0xff00) | ((crc << 24) & 0xff000000);
}
