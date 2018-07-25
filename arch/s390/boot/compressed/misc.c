// SPDX-License-Identifier: GPL-2.0
/*
 * Definitions and wrapper functions for kernel decompressor
 *
 * Copyright IBM Corp. 2010
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/uaccess.h>
#include <asm/page.h>
#include <asm/sclp.h>
#include <asm/ipl.h>
#include <asm/sections.h>
#include "decompressor.h"

/*
 * gzip declarations
 */
#define STATIC static
#define STATIC_RW_DATA static __section(.data)

#undef memset
#undef memcpy
#undef memmove
#define memmove memmove
#define memzero(s, n) memset((s), 0, (n))

/* Symbols defined by linker scripts */
extern char _end[];
extern unsigned char _compressed_start[];
extern unsigned char _compressed_end[];

static void error(char *m);

#ifdef CONFIG_HAVE_KERNEL_BZIP2
#define HEAP_SIZE	0x400000
#else
#define HEAP_SIZE	0x10000
#endif

static unsigned long free_mem_ptr = (unsigned long) _end;
static unsigned long free_mem_end_ptr = (unsigned long) _end + HEAP_SIZE;

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

static int puts(const char *s)
{
	sclp_early_printk(s);
	return 0;
}

static void error(char *x)
{
	unsigned long long psw = 0x000a0000deadbeefULL;

	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted");

	asm volatile("lpsw %0" : : "Q" (psw));
}

void *decompress_kernel(void)
{
	void *output, *kernel_end;

	output = (void *) ALIGN((unsigned long) _end + HEAP_SIZE, PAGE_SIZE);
	kernel_end = output + vmlinux.image_size;

#ifdef CONFIG_BLK_DEV_INITRD
	/*
	 * Move the initrd right behind the end of the decompressed
	 * kernel image. This also prevents initrd corruption caused by
	 * bss clearing since kernel_end will always be located behind the
	 * current bss section..
	 */
	if (INITRD_START && INITRD_SIZE && kernel_end > (void *) INITRD_START) {
		memmove(kernel_end, (void *) INITRD_START, INITRD_SIZE);
		INITRD_START = (unsigned long) kernel_end;
	}
#endif

	__decompress(_compressed_start, _compressed_end - _compressed_start,
		     NULL, NULL, output, 0, NULL, error);
	return output;
}
