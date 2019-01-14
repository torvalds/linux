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
#ifdef CONFIG_LOONGSON3_ENHANCEMENT
	/* Enable STFill Buffer */
	mfc0	t0, CP0_CONFIG6
	or	t0, 0x100
	mtc0	t0, CP0_CONFIG6
#endif
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
#ifdef CONFIG_LOONGSON3_ENHANCEMENT
	/* Enable STFill Buffer */
	mfc0	t0, CP0_CONFIG6
	or	t0, 0x100
	mtc0	t0, CP0_CONFIG6
#endif
	_ehb
	.set	pop
#endif
	.endm

#endif /* __ASM_MACH_LOONGSON64_KERNEL_ENTRY_H */
