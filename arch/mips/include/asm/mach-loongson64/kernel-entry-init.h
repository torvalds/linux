/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 Embedded Alley Solutions, Inc
 * Copyright (C) 2005 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2009 Jiajie Chen (chenjiajie@cse.buaa.edu.cn)
 * Copyright (C) 2012 Huacai Chen (chenhc@lemote.com)
 */
#ifndef __ASM_MACH_LOONGSON64_KERNEL_ENTRY_H
#define __ASM_MACH_LOONGSON64_KERNEL_ENTRY_H

#include <asm/cpu.h>

/*
 * Override macros used in arch/mips/kernel/head.S.
 */
	.macro	kernel_entry_setup
#ifdef CONFIG_CPU_LOONGSON3
	.set	push
	.set	mips64
	/* Set LPA on LOONGSON3 config3 */
	mfc0	t0, CP0_CONFIG3
	or	t0, (0x1 << 7)
	mtc0	t0, CP0_CONFIG3
	/* Set ELPA on LOONGSON3 pagegrain */
	mfc0	t0, CP0_PAGEGRAIN
	or	t0, (0x1 << 29)
	mtc0	t0, CP0_PAGEGRAIN
	/* Enable STFill Buffer */
	mfc0	t0, CP0_PRID
	/* Loongson-3A R4+ */
	andi	t1, t0, PRID_IMP_MASK
	li	t2, PRID_IMP_LOONGSON_64G
	beq     t1, t2, 1f
	nop
	/* Loongson-3A R2/R3 */
	andi	t0, (PRID_IMP_MASK | PRID_REV_MASK)
	slti	t0, (PRID_IMP_LOONGSON_64C | PRID_REV_LOONGSON3A_R2_0)
	bnez	t0, 2f
	nop
1:
	mfc0	t0, CP0_CONFIG6
	or	t0, 0x100
	mtc0	t0, CP0_CONFIG6
2:
	_ehb
	.set	pop
#endif
	.endm

/*
 * Do SMP slave processor setup.
 */
	.macro	smp_slave_setup
#ifdef CONFIG_CPU_LOONGSON3
	.set	push
	.set	mips64
	/* Set LPA on LOONGSON3 config3 */
	mfc0	t0, CP0_CONFIG3
	or	t0, (0x1 << 7)
	mtc0	t0, CP0_CONFIG3
	/* Set ELPA on LOONGSON3 pagegrain */
	mfc0	t0, CP0_PAGEGRAIN
	or	t0, (0x1 << 29)
	mtc0	t0, CP0_PAGEGRAIN
	/* Enable STFill Buffer */
	mfc0	t0, CP0_PRID
	/* Loongson-3A R4+ */
	andi	t1, t0, PRID_IMP_MASK
	li	t2, PRID_IMP_LOONGSON_64G
	beq     t1, t2, 1f
	nop
	/* Loongson-3A R2/R3 */
	andi	t0, (PRID_IMP_MASK | PRID_REV_MASK)
	slti	t0, (PRID_IMP_LOONGSON_64C | PRID_REV_LOONGSON3A_R2_0)
	bnez	t0, 2f
	nop
1:
	mfc0	t0, CP0_CONFIG6
	or	t0, 0x100
	mtc0	t0, CP0_CONFIG6
2:
	_ehb
	.set	pop
#endif
	.endm

#endif /* __ASM_MACH_LOONGSON64_KERNEL_ENTRY_H */
