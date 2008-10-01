#ifndef _ASM_POWERPC_SECTIONS_H
#define _ASM_POWERPC_SECTIONS_H
#ifdef __KERNEL__

#include <linux/elf.h>
#include <linux/uaccess.h>
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
static inline void *dereference_function_descriptor(void *ptr)
{
	struct ppc64_opd_entry *desc = ptr;
	void *p;

	if (!probe_kernel_address(&desc->funcaddr, p))
		ptr = p;
	return ptr;
}

#endif

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_SECTIONS_H */
