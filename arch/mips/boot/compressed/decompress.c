/*
 * Misc. bootloader code for many machines.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: Matt Porter <mporter@mvista.com> Derived from
 * arch/ppc/boot/prep/misc.c
 *
 * Copyright (C) 2009 Lemote, Inc. & Institute of Computing Technology
 * Author: Wu Zhangjin <wuzj@lemote.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <linux/kernel.h>

#include <asm/addrspace.h>

/* These two variables specify the free mem region
 * that can be used for temporary malloc area
 */
unsigned long free_mem_ptr;
unsigned long free_mem_end_ptr;
char *zimage_start;

/* The linker tells us where the image is. */
extern unsigned char __image_begin, __image_end;

/* debug interfaces  */
extern void puts(const char *s);
extern void puthex(unsigned long long val);

void error(char *x)
{
	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted");

	while (1)
		;	/* Halt */
}

/* activate the code for pre-boot environment */
#define STATIC static

#ifdef CONFIG_KERNEL_GZIP
void *memcpy(void *dest, const void *src, size_t n)
{
	int i;
	const char *s = src;
	char *d = dest;

	for (i = 0; i < n; i++)
		d[i] = s[i];
	return dest;
}
#include "../../../../lib/decompress_inflate.c"
#endif

#ifdef CONFIG_KERNEL_BZIP2
void *memset(void *s, int c, size_t n)
{
	int i;
	char *ss = s;

	for (i = 0; i < n; i++)
		ss[i] = c;
	return s;
}
#include "../../../../lib/decompress_bunzip2.c"
#endif

#ifdef CONFIG_KERNEL_LZMA
#include "../../../../lib/decompress_unlzma.c"
#endif

#ifdef CONFIG_KERNEL_LZO
#include "../../../../lib/decompress_unlzo.c"
#endif

void decompress_kernel(unsigned long boot_heap_start)
{
	int zimage_size;

	/*
	 * We link ourself to an arbitrary low address.  When we run, we
	 * relocate outself to that address.  __image_beign points to
	 * the part of the image where the zImage is. -- Tom
	 */
	zimage_start = (char *)(unsigned long)(&__image_begin);
	zimage_size = (unsigned long)(&__image_end) -
	    (unsigned long)(&__image_begin);

	/*
	 * The zImage and initrd will be between start and _end, so they've
	 * already been moved once.  We're good to go now. -- Tom
	 */
	puts("zimage at:     ");
	puthex((unsigned long)zimage_start);
	puts(" ");
	puthex((unsigned long)(zimage_size + zimage_start));
	puts("\n");

	/* this area are prepared for mallocing when decompressing */
	free_mem_ptr = boot_heap_start;
	free_mem_end_ptr = boot_heap_start + BOOT_HEAP_SIZE;

	/* Display standard Linux/MIPS boot prompt for kernel args */
	puts("Uncompressing Linux at load address ");
	puthex(VMLINUX_LOAD_ADDRESS_ULL);
	puts("\n");
	/* Decompress the kernel with according algorithm */
	decompress(zimage_start, zimage_size, 0, 0,
		   (void *)VMLINUX_LOAD_ADDRESS_ULL, 0, error);
	/* FIXME: is there a need to flush cache here? */
	puts("Now, booting the kernel...\n");
}
