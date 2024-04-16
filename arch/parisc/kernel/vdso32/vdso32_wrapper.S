/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/page.h>

	__PAGE_ALIGNED_DATA

	.globl vdso32_start, vdso32_end
	.balign PAGE_SIZE
vdso32_start:
	.incbin "arch/parisc/kernel/vdso32/vdso32.so"
	.balign PAGE_SIZE
vdso32_end:

	.previous
