#include <linux/linkage.h>
#include <asm/page.h>

	__PAGE_ALIGNED_DATA

	.globl vdso64_start, vdso64_end
	.balign PAGE_SIZE
vdso64_start:
	.incbin "arch/powerpc/kernel/vdso64/vdso64.so.dbg"
	.balign PAGE_SIZE
vdso64_end:

	.previous
