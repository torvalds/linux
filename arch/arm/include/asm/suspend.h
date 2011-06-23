#ifndef __ASM_ARM_SUSPEND_H
#define __ASM_ARM_SUSPEND_H

#include <asm/memory.h>
#include <asm/tlbflush.h>

extern void cpu_resume(void);

/*
 * Hide the first two arguments to __cpu_suspend - these are an implementation
 * detail which platform code shouldn't have to know about.
 */
static inline void cpu_suspend(unsigned long arg, void (*fn)(unsigned long))
{
	extern void __cpu_suspend(int, long, unsigned long,
				  void (*)(unsigned long));
	__cpu_suspend(0, PHYS_OFFSET - PAGE_OFFSET, arg, fn);
	flush_tlb_all();
}

#endif
