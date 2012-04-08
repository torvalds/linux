#include <asm/page_types.h>
#include <linux/linkage.h>
#include <linux/init.h>

__PAGE_ALIGNED_DATA

	.globl vdsox32_start, vdsox32_end
	.align PAGE_SIZE
vdsox32_start:
	.incbin "arch/x86/vdso/vdsox32.so"
vdsox32_end:
	.align PAGE_SIZE /* extra data here leaks to userspace. */

.previous

	.globl vdsox32_pages
	.bss
	.align 8
	.type vdsox32_pages, @object
vdsox32_pages:
	.zero (vdsox32_end - vdsox32_start + PAGE_SIZE - 1) / PAGE_SIZE * 8
	.size vdsox32_pages, .-vdsox32_pages
