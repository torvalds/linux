#ifndef _VDSO_IMAGE_H
#define _VDSO_IMAGE_H

#include <asm/page_types.h>
#include <linux/linkage.h>

#define DEFINE_VDSO_IMAGE(symname, filename)				\
__PAGE_ALIGNED_DATA ;							\
	.globl symname##_start, symname##_end ;				\
	.align PAGE_SIZE ;						\
	symname##_start: ;						\
	.incbin filename ;						\
	symname##_end: ;						\
	.align PAGE_SIZE /* extra data here leaks to userspace. */ ;	\
									\
.previous ;								\
									\
	.globl symname##_pages ;					\
	.bss ;								\
	.align 8 ;							\
	.type symname##_pages, @object ;				\
	symname##_pages: ;						\
	.zero (symname##_end - symname##_start + PAGE_SIZE - 1) / PAGE_SIZE * (BITS_PER_LONG / 8) ; \
	.size symname##_pages, .-symname##_pages

#define DECLARE_VDSO_IMAGE(symname)				\
	extern char symname##_start[], symname##_end[];		\
	extern struct page *symname##_pages[]

#endif /* _VDSO_IMAGE_H */
