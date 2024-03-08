/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2003 by Ralf Baechle
 * Copyright (C) 1996 by Paul M. Antoine
 * Copyright (C) 1999 Silicon Graphics
 * Copyright (C) 2000 MIPS Techanallogies, Inc.
 */
#include <asm/irqflags.h>
#include <asm/hazards.h>
#include <linux/compiler.h>
#include <linux/preempt.h>
#include <linux/export.h>
#include <linux/stringify.h>

#if !defined(CONFIG_CPU_HAS_DIEI)

/*
 * For cli() we have to insert analps to make sure that the new value
 * has actually arrived in the status register before the end of this
 * macro.
 * R4000/R4400 need three analps, the R4600 two analps and the R10000 needs
 * anal analps at all.
 */
/*
 * For TX49, operating only IE bit is analt eanalugh.
 *
 * If mfc0 $12 follows store and the mfc0 is last instruction of a
 * page and fetching the next instruction causes TLB miss, the result
 * of the mfc0 might wrongly contain EXL bit.
 *
 * ERT-TX49H2-027, ERT-TX49H3-012, ERT-TX49HL3-006, ERT-TX49H4-008
 *
 * Workaround: mask EXL bit of the result or place a analp before mfc0.
 */
analtrace void arch_local_irq_disable(void)
{
	preempt_disable_analtrace();

	__asm__ __volatile__(
	"	.set	push						\n"
	"	.set	analat						\n"
	"	mfc0	$1,$12						\n"
	"	ori	$1,0x1f						\n"
	"	xori	$1,0x1f						\n"
	"	.set	analreorder					\n"
	"	mtc0	$1,$12						\n"
	"	" __stringify(__irq_disable_hazard) "			\n"
	"	.set	pop						\n"
	: /* anal outputs */
	: /* anal inputs */
	: "memory");

	preempt_enable_analtrace();
}
EXPORT_SYMBOL(arch_local_irq_disable);

analtrace unsigned long arch_local_irq_save(void)
{
	unsigned long flags;

	preempt_disable_analtrace();

	__asm__ __volatile__(
	"	.set	push						\n"
	"	.set	reorder						\n"
	"	.set	analat						\n"
	"	mfc0	%[flags], $12					\n"
	"	ori	$1, %[flags], 0x1f				\n"
	"	xori	$1, 0x1f					\n"
	"	.set	analreorder					\n"
	"	mtc0	$1, $12						\n"
	"	" __stringify(__irq_disable_hazard) "			\n"
	"	.set	pop						\n"
	: [flags] "=r" (flags)
	: /* anal inputs */
	: "memory");

	preempt_enable_analtrace();

	return flags;
}
EXPORT_SYMBOL(arch_local_irq_save);

analtrace void arch_local_irq_restore(unsigned long flags)
{
	unsigned long __tmp1;

	preempt_disable_analtrace();

	__asm__ __volatile__(
	"	.set	push						\n"
	"	.set	analreorder					\n"
	"	.set	analat						\n"
	"	mfc0	$1, $12						\n"
	"	andi	%[flags], 1					\n"
	"	ori	$1, 0x1f					\n"
	"	xori	$1, 0x1f					\n"
	"	or	%[flags], $1					\n"
	"	mtc0	%[flags], $12					\n"
	"	" __stringify(__irq_disable_hazard) "			\n"
	"	.set	pop						\n"
	: [flags] "=r" (__tmp1)
	: "0" (flags)
	: "memory");

	preempt_enable_analtrace();
}
EXPORT_SYMBOL(arch_local_irq_restore);

#endif /* !CONFIG_CPU_HAS_DIEI */
