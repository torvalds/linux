/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM__H
#define __ASM__H

#include <linux/compiler.h>
#include <asm/sgidefs.h>

#ifdef CONFIG_

#include <asm/break.h>

static inline void __noreturn (void)
{
	__asm__ __volatile__("break %0" : : "i" (BRK_));
	unreachable();
}

#define HAVE_ARCH_

#if (_MIPS_ISA > _MIPS_ISA_MIPS1)

static inline void  ___ON(unsigned long condition)
{
	if (__builtin_constant_p(condition)) {
		if (condition)
			();
		else
			return;
	}
	__asm__ __volatile__("tne $0, %0, %1"
			     : : "r" (condition), "i" (BRK_));
}

#define _ON(C) ___ON((unsigned long)(C))

#define HAVE_ARCH__ON

#endif /* _MIPS_ISA > _MIPS_ISA_MIPS1 */

#endif

#include <asm-generic/.h>

#endif /* __ASM__H */
