#ifndef _ASM_X86_VDSO_H
#define _ASM_X86_VDSO_H

#include <asm/page_types.h>
#include <linux/linkage.h>

#ifdef __ASSEMBLER__

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

#else

#define DECLARE_VDSO_IMAGE(symname)				\
	extern char symname##_start[], symname##_end[];		\
	extern struct page *symname##_pages[]

#if defined CONFIG_X86_32 || defined CONFIG_COMPAT

#include <asm/vdso32.h>

DECLARE_VDSO_IMAGE(vdso32_int80);
#ifdef CONFIG_COMPAT
DECLARE_VDSO_IMAGE(vdso32_syscall);
#endif
DECLARE_VDSO_IMAGE(vdso32_sysenter);

/*
 * Given a pointer to the vDSO image, find the pointer to VDSO32_name
 * as that symbol is defined in the vDSO sources or linker script.
 */
#define VDSO32_SYMBOL(base, name)					\
({									\
	extern const char VDSO32_##name[];				\
	(void __user *)(VDSO32_##name + (unsigned long)(base));		\
})
#endif

/*
 * These symbols are defined with the addresses in the vsyscall page.
 * See vsyscall-sigreturn.S.
 */
extern void __user __kernel_sigreturn;
extern void __user __kernel_rt_sigreturn;

void __init patch_vdso32(void *vdso, size_t len);

#endif /* __ASSEMBLER__ */

#endif /* _ASM_X86_VDSO_H */
