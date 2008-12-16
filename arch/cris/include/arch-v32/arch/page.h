#ifndef _ASM_CRIS_ARCH_PAGE_H
#define _ASM_CRIS_ARCH_PAGE_H


#ifdef __KERNEL__

#define PAGE_OFFSET KSEG_C	/* kseg_c is mapped to physical ram. */

/*
 * Macros to convert between physical and virtual addresses. By stripping a
 * selected bit it's possible to convert between KSEG_x and 0x40000000 where the
 * DRAM really resides. DRAM is virtually at 0xc.
 */
#ifndef CONFIG_ETRAX_VCS_SIM
#define __pa(x) ((unsigned long)(x) & 0x7fffffff)
#define __va(x) ((void *)((unsigned long)(x) | 0x80000000))
#else
#define __pa(x) ((unsigned long)(x) & 0x3fffffff)
#define __va(x) ((void *)((unsigned long)(x) | 0xc0000000))
#endif

#define VM_STACK_DEFAULT_FLAGS	(VM_READ | VM_WRITE | \
				 VM_MAYREAD | VM_MAYWRITE)

#endif /* __KERNEL__ */

#endif /* _ASM_CRIS_ARCH_PAGE_H */
