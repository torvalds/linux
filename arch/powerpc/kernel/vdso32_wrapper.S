/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/page.h>

	.section ".data..ro_after_init", "aw"

	.globl vdso32_start, vdso32_end
	.balign PAGE_SIZE
vdso32_start:
	.incbin "arch/powerpc/kernel/vdso/vdso32.so.dbg"
	.balign PAGE_SIZE
vdso32_end:

	.previous
