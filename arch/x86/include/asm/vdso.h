#ifndef _ASM_X86_VDSO_H
#define _ASM_X86_VDSO_H

#if defined CONFIG_X86_32 || defined CONFIG_COMPAT

#include <asm/vdso32.h>

extern const char VDSO32_PRELINK[];

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

#endif /* _ASM_X86_VDSO_H */
