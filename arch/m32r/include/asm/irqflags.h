/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001  Hiroyuki Kondo, Hirokazu Takata, and Hitoshi Yamamoto
 * Copyright (C) 2004, 2006  Hirokazu Takata <takata at linux-m32r.org>
 */

#ifndef _ASM_M32R_IRQFLAGS_H
#define _ASM_M32R_IRQFLAGS_H

#include <linux/types.h>

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;
	asm volatile("mvfc %0,psw" : "=r"(flags));
	return flags;
}

static inline void arch_local_irq_disable(void)
{
#if !defined(CONFIG_CHIP_M32102) && !defined(CONFIG_CHIP_M32104)
	asm volatile (
		"clrpsw #0x40 -> nop"
		: : : "memory");
#else
	unsigned long tmpreg0, tmpreg1;
	asm volatile (
		"ld24	%0, #0	; Use 32-bit insn.			\n\t"
		"mvfc	%1, psw	; No interrupt can be accepted here.	\n\t"
		"mvtc	%0, psw						\n\t"
		"and3	%0, %1, #0xffbf					\n\t"
		"mvtc	%0, psw						\n\t"
		: "=&r" (tmpreg0), "=&r" (tmpreg1)
		:
		: "cbit", "memory");
#endif
}

static inline void arch_local_irq_enable(void)
{
#if !defined(CONFIG_CHIP_M32102) && !defined(CONFIG_CHIP_M32104)
	asm volatile (
		"setpsw #0x40 -> nop"
		: : : "memory");
#else
	unsigned long tmpreg;
	asm volatile (
		"mvfc	%0, psw;		\n\t"
		"or3	%0, %0, #0x0040;	\n\t"
		"mvtc	%0, psw;		\n\t"
		: "=&r" (tmpreg)
		:
		: "cbit", "memory");
#endif
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;

#if !(defined(CONFIG_CHIP_M32102) || defined(CONFIG_CHIP_M32104))
	asm volatile (
		"mvfc	%0, psw;	\n\t"
		"clrpsw	#0x40 -> nop;	\n\t"
		: "=r" (flags)
		:
		: "memory");
#else
	unsigned long tmpreg;
	asm volatile (
		"ld24	%1, #0		\n\t"
		"mvfc	%0, psw		\n\t"
		"mvtc	%1, psw		\n\t"
		"and3	%1, %0, #0xffbf	\n\t"
		"mvtc	%1, psw		\n\t"
		: "=r" (flags), "=&r" (tmpreg)
		:
		: "cbit", "memory");
#endif
	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	asm volatile("mvtc %0,psw"
		     :
		     : "r" (flags)
		     : "cbit", "memory");
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & 0x40);
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#endif /* _ASM_M32R_IRQFLAGS_H */
