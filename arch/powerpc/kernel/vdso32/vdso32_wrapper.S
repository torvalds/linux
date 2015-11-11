#include <linux/linkage.h>
#include <asm/page.h>

	__PAGE_ALIGNED_DATA

	.globl vdso32_start, vdso32_end
	.balign PAGE_SIZE
vdso32_start:
	.incbin "arch/powerpc/kernel/vdso32/vdso32.so.dbg"
	.balign PAGE_SIZE
vdso32_end:

	.previous
