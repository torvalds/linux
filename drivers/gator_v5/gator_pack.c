/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

static void gator_buffer_write_packed_int(int cpu, int buftype, unsigned int x)
{
	uint32_t write = per_cpu(gator_buffer_write, cpu)[buftype];
	uint32_t mask = gator_buffer_mask[buftype];
	char *buffer = per_cpu(gator_buffer, cpu)[buftype];
	int write0 = (write + 0) & mask;
	int write1 = (write + 1) & mask;

	if ((x & 0xffffff80) == 0) {
		buffer[write0] = x & 0x7f;
		per_cpu(gator_buffer_write, cpu)[buftype] = write1;
	} else if ((x & 0xffffc000) == 0) {
		int write2 = (write + 2) & mask;
		buffer[write0] = x | 0x80;
		buffer[write1] = (x>>7) & 0x7f;
		per_cpu(gator_buffer_write, cpu)[buftype] = write2;
	} else if ((x & 0xffe00000) == 0) {
		int write2 = (write + 2) & mask;
		int write3 = (write + 3) & mask;
		buffer[write0] = x | 0x80;
		buffer[write1] = (x>>7) | 0x80;
		buffer[write2] = (x>>14) & 0x7f;
		per_cpu(gator_buffer_write, cpu)[buftype] = write3;
	} else if ((x & 0xf0000000) == 0) {
		int write2 = (write + 2) & mask;
		int write3 = (write + 3) & mask;
		int write4 = (write + 4) & mask;
		buffer[write0] = x | 0x80;
		buffer[write1] = (x>>7) | 0x80;
		buffer[write2] = (x>>14) | 0x80;
		buffer[write3] = (x>>21) & 0x7f;
		per_cpu(gator_buffer_write, cpu)[buftype] = write4;
	} else {
		int write2 = (write + 2) & mask;
		int write3 = (write + 3) & mask;
		int write4 = (write + 4) & mask;
		int write5 = (write + 5) & mask;
		buffer[write0] = x | 0x80;
		buffer[write1] = (x>>7) | 0x80;
		buffer[write2] = (x>>14) | 0x80;
		buffer[write3] = (x>>21) | 0x80;
		buffer[write4] = (x>>28) & 0x0f;
		per_cpu(gator_buffer_write, cpu)[buftype] = write5;
	}
}

static void gator_buffer_write_packed_int64(int cpu, int buftype, unsigned long long x)
{
	uint32_t write = per_cpu(gator_buffer_write, cpu)[buftype];
	uint32_t mask = gator_buffer_mask[buftype];
	char *buffer = per_cpu(gator_buffer, cpu)[buftype];
	int write0 = (write + 0) & mask;
	int write1 = (write + 1) & mask;

	if ((x & 0xffffffffffffff80LL) == 0) {
		buffer[write0] = x & 0x7f;
		per_cpu(gator_buffer_write, cpu)[buftype] = write1;
	} else if ((x & 0xffffffffffffc000LL) == 0) {
		int write2 = (write + 2) & mask;
		buffer[write0] = x | 0x80;
		buffer[write1] = (x>>7) & 0x7f;
		per_cpu(gator_buffer_write, cpu)[buftype] = write2;
	} else if ((x & 0xffffffffffe00000LL) == 0) {
		int write2 = (write + 2) & mask;
		int write3 = (write + 3) & mask;
		buffer[write0] = x | 0x80;
		buffer[write1] = (x>>7) | 0x80;
		buffer[write2] = (x>>14) & 0x7f;
		per_cpu(gator_buffer_write, cpu)[buftype] = write3;
	} else if ((x & 0xfffffffff0000000LL) == 0) {
		int write2 = (write + 2) & mask;
		int write3 = (write + 3) & mask;
		int write4 = (write + 4) & mask;
		buffer[write0] = x | 0x80;
		buffer[write1] = (x>>7) | 0x80;
		buffer[write2] = (x>>14) | 0x80;
		buffer[write3] = (x>>21) & 0x7f;
		per_cpu(gator_buffer_write, cpu)[buftype] = write4;
	} else if ((x & 0xfffffff800000000LL) == 0) {
		int write2 = (write + 2) & mask;
		int write3 = (write + 3) & mask;
		int write4 = (write + 4) & mask;
		int write5 = (write + 5) & mask;
		buffer[write0] = x | 0x80;
		buffer[write1] = (x>>7) | 0x80;
		buffer[write2] = (x>>14) | 0x80;
		buffer[write3] = (x>>21) | 0x80;
		buffer[write4] = (x>>28) & 0x7f;
		per_cpu(gator_buffer_write, cpu)[buftype] = write5;
	} else if ((x & 0xfffffc0000000000LL) == 0) {
		int write2 = (write + 2) & mask;
		int write3 = (write + 3) & mask;
		int write4 = (write + 4) & mask;
		int write5 = (write + 5) & mask;
		int write6 = (write + 6) & mask;
		buffer[write0] = x | 0x80;
		buffer[write1] = (x>>7) | 0x80;
		buffer[write2] = (x>>14) | 0x80;
		buffer[write3] = (x>>21) | 0x80;
		buffer[write4] = (x>>28) | 0x80;
		buffer[write5] = (x>>35) & 0x7f;
		per_cpu(gator_buffer_write, cpu)[buftype] = write6;
	} else if ((x & 0xfffe000000000000LL) == 0) {
		int write2 = (write + 2) & mask;
		int write3 = (write + 3) & mask;
		int write4 = (write + 4) & mask;
		int write5 = (write + 5) & mask;
		int write6 = (write + 6) & mask;
		int write7 = (write + 7) & mask;
		buffer[write0] = x | 0x80;
		buffer[write1] = (x>>7) | 0x80;
		buffer[write2] = (x>>14) | 0x80;
		buffer[write3] = (x>>21) | 0x80;
		buffer[write4] = (x>>28) | 0x80;
		buffer[write5] = (x>>35) | 0x80;
		buffer[write6] = (x>>42) & 0x7f;
		per_cpu(gator_buffer_write, cpu)[buftype] = write7;
	} else if ((x & 0xff00000000000000LL) == 0) {
		int write2 = (write + 2) & mask;
		int write3 = (write + 3) & mask;
		int write4 = (write + 4) & mask;
		int write5 = (write + 5) & mask;
		int write6 = (write + 6) & mask;
		int write7 = (write + 7) & mask;
		int write8 = (write + 8) & mask;
		buffer[write0] = x | 0x80;
		buffer[write1] = (x>>7) | 0x80;
		buffer[write2] = (x>>14) | 0x80;
		buffer[write3] = (x>>21) | 0x80;
		buffer[write4] = (x>>28) | 0x80;
		buffer[write5] = (x>>35) | 0x80;
		buffer[write6] = (x>>42) | 0x80;
		buffer[write7] = (x>>49) & 0x7f;
		per_cpu(gator_buffer_write, cpu)[buftype] = write8;
	} else {
		int write2 = (write + 2) & mask;
		int write3 = (write + 3) & mask;
		int write4 = (write + 4) & mask;
		int write5 = (write + 5) & mask;
		int write6 = (write + 6) & mask;
		int write7 = (write + 7) & mask;
		int write8 = (write + 8) & mask;
		int write9 = (write + 9) & mask;
		buffer[write0] = x | 0x80;
		buffer[write1] = (x>>7) | 0x80;
		buffer[write2] = (x>>14) | 0x80;
		buffer[write3] = (x>>21) | 0x80;
		buffer[write4] = (x>>28) | 0x80;
		buffer[write5] = (x>>35) | 0x80;
		buffer[write6] = (x>>42) | 0x80;
		buffer[write7] = (x>>49) | 0x80;
		buffer[write8] = (x>>56) & 0xff;
		per_cpu(gator_buffer_write, cpu)[buftype] = write9;
	}
}
