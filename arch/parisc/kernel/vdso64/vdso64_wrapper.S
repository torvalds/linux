/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/page.h>

	__PAGE_ALIGNED_DATA

	.globl vdso64_start, vdso64_end
	.balign PAGE_SIZE
vdso64_start:
	.incbin "arch/parisc/kernel/vdso64/vdso64.so"
	.balign PAGE_SIZE
vdso64_end:

	.previous
