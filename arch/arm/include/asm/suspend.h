#ifndef __ASM_ARM_SUSPEND_H
#define __ASM_ARM_SUSPEND_H

#include <asm/pie.h>

extern void cpu_resume(void);
extern int cpu_suspend(unsigned long, int (*)(unsigned long));

/**
 * ARM_PIE_RESUME - generate a PIE trampoline for resume
 * @proc:	SoC, should match argument used with PIE_OVERLAY_SECTION()
 * @func:	C or asm function to call at resume
 * @stack:	stack to use before calling func
 */
#define ARM_PIE_RESUME(proc, func, stack)				\
static void __naked __noreturn __pie(proc) proc##_resume_trampoline2(void) \
{									\
	__asm__ __volatile__(						\
	"	mov	sp, %0\n"					\
	: : "r"((stack)) : "sp");					\
									\
	func();								\
}									\
									\
void __naked __noreturn __pie(proc) proc##_resume_trampoline(void)	\
{									\
	pie_relocate_from_pie();					\
	proc##_resume_trampoline2();					\
}									\
EXPORT_PIE_SYMBOL(proc##_resume_trampoline)

#endif
