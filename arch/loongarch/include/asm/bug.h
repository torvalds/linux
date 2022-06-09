/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_BUG_H
#define __ASM_BUG_H

#include <linux/compiler.h>

#ifdef CONFIG_BUG

#include <asm/break.h>

static inline void __noreturn BUG(void)
{
	__asm__ __volatile__("break %0" : : "i" (BRK_BUG));
	unreachable();
}

#define HAVE_ARCH_BUG

#endif

#include <asm-generic/bug.h>

#endif /* __ASM_BUG_H */
