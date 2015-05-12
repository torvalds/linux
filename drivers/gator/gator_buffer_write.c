/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

static void gator_buffer_write_packed_int(int cpu, int buftype, int x)
{
	uint32_t write = per_cpu(gator_buffer_write, cpu)[buftype];
	uint32_t mask = gator_buffer_mask[buftype];
	char *buffer = per_cpu(gator_buffer, cpu)[buftype];
	int packedBytes = 0;
	int more = true;

	while (more) {
		/* low order 7 bits of x */
		char b = x & 0x7f;

		x >>= 7;

		if ((x == 0 && (b & 0x40) == 0) || (x == -1 && (b & 0x40) != 0))
			more = false;
		else
			b |= 0x80;

		buffer[(write + packedBytes) & mask] = b;
		packedBytes++;
	}

	per_cpu(gator_buffer_write, cpu)[buftype] = (write + packedBytes) & mask;
}

static void gator_buffer_write_packed_int64(int cpu, int buftype, long long x)
{
	uint32_t write = per_cpu(gator_buffer_write, cpu)[buftype];
	uint32_t mask = gator_buffer_mask[buftype];
	char *buffer = per_cpu(gator_buffer, cpu)[buftype];
	int packedBytes = 0;
	int more = true;

	while (more) {
		/* low order 7 bits of x */
		char b = x & 0x7f;

		x >>= 7;

		if ((x == 0 && (b & 0x40) == 0) || (x == -1 && (b & 0x40) != 0))
			more = false;
		else
			b |= 0x80;

		buffer[(write + packedBytes) & mask] = b;
		packedBytes++;
	}

	per_cpu(gator_buffer_write, cpu)[buftype] = (write + packedBytes) & mask;
}

static void gator_buffer_write_bytes(int cpu, int buftype, const char *x, int len)
{
	int i;
	u32 write = per_cpu(gator_buffer_write, cpu)[buftype];
	u32 mask = gator_buffer_mask[buftype];
	char *buffer = per_cpu(gator_buffer, cpu)[buftype];

	for (i = 0; i < len; i++) {
		buffer[write] = x[i];
		write = (write + 1) & mask;
	}

	per_cpu(gator_buffer_write, cpu)[buftype] = write;
}

static void gator_buffer_write_string(int cpu, int buftype, const char *x)
{
	int len = strlen(x);

	gator_buffer_write_packed_int(cpu, buftype, len);
	gator_buffer_write_bytes(cpu, buftype, x, len);
}
