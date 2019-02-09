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
#include "sizes.h"

/*
 * gzip declarations
 */
#define STATIC static

#undef memset
#undef memcpy
#undef memmove
#define memmove memmove
#define memzero(s, n) memset((s), 0, (n))

/* Symbols defined by linker scripts */
extern char input_data[];
extern int input_len;
extern char _end[];
extern char _bss[], _ebss[];

static void error(char *m);

static unsigned long free_mem_ptr;
static unsigned long free_mem_end_ptr;

#ifdef CONFIG_HAVE_KERNEL_BZIP2
#define HEAP_SIZE	0x400000
#else
#define HEAP_SIZE	0x10000
#endif

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

unsigned long decompress_kernel(void)
{
	void *output, *kernel_end;

	output = (void *) ALIGN((unsigned long) _end + HEAP_SIZE, PAGE_SIZE);
	kernel_end = output + SZ__bss_start;

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

	/*
	 * Clear bss section. free_mem_ptr and free_mem_end_ptr need to be
	 * initialized afterwards since they reside in bss.
	 */
	memset(_bss, 0, _ebss - _bss);
	free_mem_ptr = (unsigned long) _end;
	free_mem_end_ptr = free_mem_ptr + HEAP_SIZE;

	__decompress(input_data, input_len, NULL, NULL, output, 0, NULL, error);
	return (unsigned long) output;
}

