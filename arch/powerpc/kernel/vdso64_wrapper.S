/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/page.h>

	.section ".data..ro_after_init", "aw"

	.globl vdso64_start, vdso64_end
	.balign PAGE_SIZE
vdso64_start:
	.incbin "arch/powerpc/kernel/vdso/vdso64.so.dbg"
	.balign PAGE_SIZE
vdso64_end:

	.previous
