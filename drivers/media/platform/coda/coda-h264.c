/*
 * Coda multi-standard codec IP - H.264 helper functions
 *
 * Copyright (C) 2012 Vista Silicon S.L.
 *    Javier Martin, <javier.martin@vista-silicon.com>
 *    Xavier Duret
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <coda.h>

static const u8 coda_filler_nal[14] = { 0x00, 0x00, 0x00, 0x01, 0x0c, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80 };
static const u8 coda_filler_size[8] = { 0, 7, 14, 13, 12, 11, 10, 9 };

int coda_h264_padding(int size, char *p)
{
	int nal_size;
	int diff;

	diff = size - (size & ~0x7);
	if (diff == 0)
		return 0;

	nal_size = coda_filler_size[diff];
	memcpy(p, coda_filler_nal, nal_size);

	/* Add rbsp stop bit and trailing at the end */
	*(p + nal_size - 1) = 0x80;

	return nal_size;
}
