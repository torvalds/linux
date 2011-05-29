/*
 * altera-comp.c
 *
 * altera FPGA driver
 *
 * Copyright (C) Altera Corporation 1998-2001
 * Copyright (C) 2010 NetUP Inc.
 * Copyright (C) 2010 Igor M. Liplianin <liplianin@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include "altera-exprt.h"

#define	SHORT_BITS		16
#define	CHAR_BITS		8
#define	DATA_BLOB_LENGTH	3
#define	MATCH_DATA_LENGTH	8192
#define ALTERA_REQUEST_SIZE	1024
#define ALTERA_BUFFER_SIZE	(MATCH_DATA_LENGTH + ALTERA_REQUEST_SIZE)

static u32 altera_bits_req(u32 n)
{
	u32 result = SHORT_BITS;

	if (n == 0)
		result = 1;
	else {
		/* Look for the highest non-zero bit position */
		while ((n & (1 << (SHORT_BITS - 1))) == 0) {
			n <<= 1;
			--result;
		}
	}

	return result;
}

static u32 altera_read_packed(u8 *buffer, u32 bits, u32 *bits_avail,
							u32 *in_index)
{
	u32 result = 0;
	u32 shift = 0;
	u32 databyte = 0;

	while (bits > 0) {
		databyte = buffer[*in_index];
		result |= (((databyte >> (CHAR_BITS - *bits_avail))
			& (0xff >> (CHAR_BITS - *bits_avail))) << shift);

		if (bits <= *bits_avail) {
			result &= (0xffff >> (SHORT_BITS - (bits + shift)));
			*bits_avail -= bits;
			bits = 0;
		} else {
			++(*in_index);
			shift += *bits_avail;
			bits -= *bits_avail;
			*bits_avail = CHAR_BITS;
		}
	}

	return result;
}

u32 altera_shrink(u8 *in, u32 in_length, u8 *out, u32 out_length, s32 version)
{
	u32 i, j, data_length = 0L;
	u32 offset, length;
	u32 match_data_length = MATCH_DATA_LENGTH;
	u32 bits_avail = CHAR_BITS;
	u32 in_index = 0L;

	if (version > 0)
		--match_data_length;

	for (i = 0; i < out_length; ++i)
		out[i] = 0;

	/* Read number of bytes in data. */
	for (i = 0; i < sizeof(in_length); ++i) {
		data_length = data_length | (
			altera_read_packed(in,
					CHAR_BITS,
					&bits_avail,
					&in_index) << (i * CHAR_BITS));
	}

	if (data_length > out_length) {
		data_length = 0L;
		return data_length;
	}

	i = 0;
	while (i < data_length) {
		/* A 0 bit indicates literal data. */
		if (altera_read_packed(in, 1, &bits_avail,
						&in_index) == 0) {
			for (j = 0; j < DATA_BLOB_LENGTH; ++j) {
				if (i < data_length) {
					out[i] = (u8)altera_read_packed(in,
							CHAR_BITS,
							&bits_avail,
							&in_index);
					i++;
				}
			}
		} else {
			/* A 1 bit indicates offset/length to follow. */
			offset = altera_read_packed(in, altera_bits_req((s16)
					(i > match_data_length ?
						match_data_length : i)),
					&bits_avail,
					&in_index);
			length = altera_read_packed(in, CHAR_BITS,
					&bits_avail,
					&in_index);
			for (j = 0; j < length; ++j) {
				if (i < data_length) {
					out[i] = out[i - offset];
					i++;
				}
			}
		}
	}

	return data_length;
}
