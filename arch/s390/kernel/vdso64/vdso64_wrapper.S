#include <linux/init.h>
#include <asm/page.h>

	.section ".data.page_aligned"

	.globl vdso64_start, vdso64_end
	.balign PAGE_SIZE
vdso64_start:
	.incbin "arch/s390/kernel/vdso64/vdso64.so"
	.balign PAGE_SIZE
vdso64_end:

	.previous
