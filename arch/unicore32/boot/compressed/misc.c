/*
 * linux/arch/unicore32/boot/compressed/misc.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/unaligned.h>
#include <mach/uncompress.h>

/*
 * gzip delarations
 */
unsigned char *output_data;
unsigned long output_ptr;

unsigned int free_mem_ptr;
unsigned int free_mem_end_ptr;

#define STATIC static
#define STATIC_RW_DATA	/* non-static please */

/*
 * arch-dependent implementations
 */
#ifndef ARCH_HAVE_DECOMP_ERROR
#define arch_decomp_error(x)
#endif

#ifndef ARCH_HAVE_DECOMP_SETUP
#define arch_decomp_setup()
#endif

#ifndef ARCH_HAVE_DECOMP_PUTS
#define arch_decomp_puts(p)
#endif

void *memcpy(void *dest, const void *src, size_t n)
{
	int i = 0;
	unsigned char *d = (unsigned char *)dest, *s = (unsigned char *)src;

	for (i = n >> 3; i > 0; i--) {
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
	}

	if (n & 1 << 2) {
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
	}

	if (n & 1 << 1) {
		*d++ = *s++;
		*d++ = *s++;
	}

	if (n & 1)
		*d++ = *s++;

	return dest;
}

void error(char *x)
{
	arch_decomp_puts("\n\n");
	arch_decomp_puts(x);
	arch_decomp_puts("\n\n -- System halted");

	arch_decomp_error(x);

	for (;;)
		; /* Halt */
}

/* Heap size should be adjusted for different decompress method */
#ifdef CONFIG_KERNEL_GZIP
#include "../../../../lib/decompress_inflate.c"
#endif

#ifdef CONFIG_KERNEL_BZIP2
#include "../../../../lib/decompress_bunzip2.c"
#endif

#ifdef CONFIG_KERNEL_LZO
#include "../../../../lib/decompress_unlzo.c"
#endif

#ifdef CONFIG_KERNEL_LZMA
#include "../../../../lib/decompress_unlzma.c"
#endif

unsigned long decompress_kernel(unsigned long output_start,
		unsigned long free_mem_ptr_p,
		unsigned long free_mem_ptr_end_p)
{
	unsigned char *tmp;

	output_data		= (unsigned char *)output_start;
	free_mem_ptr		= free_mem_ptr_p;
	free_mem_end_ptr	= free_mem_ptr_end_p;

	arch_decomp_setup();

	tmp = (unsigned char *) (((unsigned long)input_data_end) - 4);
	output_ptr = get_unaligned_le32(tmp);

	arch_decomp_puts("Uncompressing Linux...");
	decompress(input_data, input_data_end - input_data, NULL, NULL,
			output_data, NULL, error);
	arch_decomp_puts(" done, booting the kernel.\n");
	return output_ptr;
}
