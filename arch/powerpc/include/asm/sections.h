/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_SECTIONS_H
#define _ASM_POWERPC_SECTIONS_H
#ifdef __KERNEL__

#include <linux/elf.h>
#include <linux/uaccess.h>

#ifdef CONFIG_HAVE_FUNCTION_DESCRIPTORS
typedef struct func_desc func_desc_t;
#endif

#include <asm-generic/sections.h>

extern char __head_end[];

#ifdef __powerpc64__

extern char __start_interrupts[];
extern char __end_interrupts[];

extern char __prom_init_toc_start[];
extern char __prom_init_toc_end[];

#ifdef CONFIG_PPC_POWERNV
extern char start_real_trampolines[];
extern char end_real_trampolines[];
extern char start_virt_trampolines[];
extern char end_virt_trampolines[];
#endif

/*
 * This assumes the kernel is never compiled -mcmodel=small or
 * the total .toc is always less than 64k.
 */
static inline unsigned long kernel_toc_addr(void)
{
	unsigned long toc_ptr;

	asm volatile("mr %0, 2" : "=r" (toc_ptr));
	return toc_ptr;
}

static inline int overlaps_interrupt_vector_text(unsigned long start,
							unsigned long end)
{
	unsigned long real_start, real_end;
	real_start = __start_interrupts - _stext;
	real_end = __end_interrupts - _stext;

	return start < (unsigned long)__va(real_end) &&
		(unsigned long)__va(real_start) < end;
}

static inline int overlaps_kernel_text(unsigned long start, unsigned long end)
{
	return start < (unsigned long)__init_end &&
		(unsigned long)_stext < end;
}

#endif

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_SECTIONS_H */
