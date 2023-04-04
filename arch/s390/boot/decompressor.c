// SPDX-License-Identifier: GPL-2.0
/*
 * Definitions and wrapper functions for kernel decompressor
 *
 * Copyright IBM Corp. 2010
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/page.h>
#include "decompressor.h"
#include "boot.h"

/*
 * gzip declarations
 */
#define STATIC static

#undef memset
#undef memcpy
#undef memmove
#define memmove memmove
#define memzero(s, n) memset((s), 0, (n))

#ifdef CONFIG_KERNEL_BZIP2
#define BOOT_HEAP_SIZE	0x400000
#elif CONFIG_KERNEL_ZSTD
#define BOOT_HEAP_SIZE	0x30000
#else
#define BOOT_HEAP_SIZE	0x10000
#endif

static unsigned long free_mem_ptr = (unsigned long) _end;
static unsigned long free_mem_end_ptr = (unsigned long) _end + BOOT_HEAP_SIZE;

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

#define decompress_offset ALIGN((unsigned long)_end + BOOT_HEAP_SIZE, PAGE_SIZE)

unsigned long mem_safe_offset(void)
{
	/*
	 * due to 4MB HEAD_SIZE for bzip2
	 * 'decompress_offset + vmlinux.image_size' could be larger than
	 * kernel at final position + its .bss, so take the larger of two
	 */
	return max(decompress_offset + vmlinux.image_size,
		   vmlinux.default_lma + vmlinux.image_size + vmlinux.bss_size);
}

void *decompress_kernel(void)
{
	void *output = (void *)decompress_offset;

	__decompress(_compressed_start, _compressed_end - _compressed_start,
		     NULL, NULL, output, vmlinux.image_size, NULL, error);
	return output;
}
