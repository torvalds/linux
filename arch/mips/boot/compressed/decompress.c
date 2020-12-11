// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Matt Porter <mporter@mvista.com>
 *
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Wu Zhangjin <wuzhangjin@gmail.com>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/libfdt.h>

#include <asm/addrspace.h>

/*
 * These two variables specify the free mem region
 * that can be used for temporary malloc area
 */
unsigned long free_mem_ptr;
unsigned long free_mem_end_ptr;

/* The linker tells us where the image is. */
extern unsigned char __image_begin, __image_end;

/* debug interfaces  */
#ifdef CONFIG_DEBUG_ZBOOT
extern void puts(const char *s);
extern void puthex(unsigned long long val);
#else
#define puts(s) do {} while (0)
#define puthex(val) do {} while (0)
#endif

extern char __appended_dtb[];

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
#include "../../../../lib/decompress_inflate.c"
#endif

#ifdef CONFIG_KERNEL_BZIP2
#include "../../../../lib/decompress_bunzip2.c"
#endif

#ifdef CONFIG_KERNEL_LZ4
#include "../../../../lib/decompress_unlz4.c"
#endif

#ifdef CONFIG_KERNEL_LZMA
#include "../../../../lib/decompress_unlzma.c"
#endif

#ifdef CONFIG_KERNEL_LZO
#include "../../../../lib/decompress_unlzo.c"
#endif

#ifdef CONFIG_KERNEL_XZ
#include "../../../../lib/decompress_unxz.c"
#endif

#ifdef CONFIG_KERNEL_ZSTD
#include "../../../../lib/decompress_unzstd.c"
#endif

const unsigned long __stack_chk_guard = 0x000a0dff;

void __stack_chk_fail(void)
{
	error("stack-protector: Kernel stack is corrupted\n");
}

void decompress_kernel(unsigned long boot_heap_start)
{
	unsigned long zimage_start, zimage_size;

	zimage_start = (unsigned long)(&__image_begin);
	zimage_size = (unsigned long)(&__image_end) -
	    (unsigned long)(&__image_begin);

	puts("zimage at:     ");
	puthex(zimage_start);
	puts(" ");
	puthex(zimage_size + zimage_start);
	puts("\n");

	/* This area are prepared for mallocing when decompressing */
	free_mem_ptr = boot_heap_start;
	free_mem_end_ptr = boot_heap_start + BOOT_HEAP_SIZE;

	/* Display standard Linux/MIPS boot prompt */
	puts("Uncompressing Linux at load address ");
	puthex(VMLINUX_LOAD_ADDRESS_ULL);
	puts("\n");

	/* Decompress the kernel with according algorithm */
	__decompress((char *)zimage_start, zimage_size, 0, 0,
		   (void *)VMLINUX_LOAD_ADDRESS_ULL, 0, 0, error);

	if (IS_ENABLED(CONFIG_MIPS_RAW_APPENDED_DTB) &&
	    fdt_magic((void *)&__appended_dtb) == FDT_MAGIC) {
		unsigned int image_size, dtb_size;

		dtb_size = fdt_totalsize((void *)&__appended_dtb);

		/* last four bytes is always image size in little endian */
		image_size = le32_to_cpup((void *)&__image_end - 4);

		/* copy dtb to where the booted kernel will expect it */
		memcpy((void *)VMLINUX_LOAD_ADDRESS_ULL + image_size,
		       __appended_dtb, dtb_size);
	}

	/* FIXME: should we flush cache here? */
	puts("Now, booting the kernel...\n");
}
