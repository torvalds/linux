#ifndef _ASM_POWERPC_SECTIONS_H
#define _ASM_POWERPC_SECTIONS_H
#ifdef __KERNEL__

#include <asm-generic/sections.h>

#ifdef __powerpc64__

extern char _end[];

static inline int in_kernel_text(unsigned long addr)
{
	if (addr >= (unsigned long)_stext && addr < (unsigned long)__init_end)
		return 1;

	return 0;
}

#undef dereference_function_descriptor
void *dereference_function_descriptor(void *);

#endif

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_SECTIONS_H */
