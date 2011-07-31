/* ----------------------------------------------------------------------- *
 *
 *  Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version
 *  2 as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 *
 *  H. Peter Anvin <hpa@linux.intel.com>
 *
 * ----------------------------------------------------------------------- */

/*
 * Compute the desired load offset from a compressed program; outputs
 * a small assembly wrapper with the appropriate symbols defined.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static uint32_t getle32(const void *p)
{
	const uint8_t *cp = p;

	return (uint32_t)cp[0] + ((uint32_t)cp[1] << 8) +
		((uint32_t)cp[2] << 16) + ((uint32_t)cp[3] << 24);
}

int main(int argc, char *argv[])
{
	uint32_t olen;
	long ilen;
	unsigned long offs;
	FILE *f;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s compressed_file\n", argv[0]);
		return 1;
	}

	/* Get the information for the compressed kernel image first */

	f = fopen(argv[1], "r");
	if (!f) {
		perror(argv[1]);
		return 1;
	}


	if (fseek(f, -4L, SEEK_END)) {
		perror(argv[1]);
	}
	fread(&olen, sizeof olen, 1, f);
	ilen = ftell(f);
	olen = getle32(&olen);
	fclose(f);

	/*
	 * Now we have the input (compressed) and output (uncompressed)
	 * sizes, compute the necessary decompression offset...
	 */

	offs = (olen > ilen) ? olen - ilen : 0;
	offs += olen >> 12;	/* Add 8 bytes for each 32K block */
	offs += 32*1024 + 18;	/* Add 32K + 18 bytes slack */
	offs = (offs+4095) & ~4095; /* Round to a 4K boundary */

	printf(".section \".rodata..compressed\",\"a\",@progbits\n");
	printf(".globl z_input_len\n");
	printf("z_input_len = %lu\n", ilen);
	printf(".globl z_output_len\n");
	printf("z_output_len = %lu\n", (unsigned long)olen);
	printf(".globl z_extract_offset\n");
	printf("z_extract_offset = 0x%lx\n", offs);
	/* z_extract_offset_negative allows simplification of head_32.S */
	printf(".globl z_extract_offset_negative\n");
	printf("z_extract_offset_negative = -0x%lx\n", offs);

	printf(".globl input_data, input_data_end\n");
	printf("input_data:\n");
	printf(".incbin \"%s\"\n", argv[1]);
	printf("input_data_end:\n");

	return 0;
}
