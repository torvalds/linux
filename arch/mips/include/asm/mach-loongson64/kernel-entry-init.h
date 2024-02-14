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
	.set	push
	.set	mips64
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
	slti	t0, t0, (PRID_IMP_LOONGSON_64C | PRID_REV_LOONGSON3A_R2_0)
	bnez	t0, 2f
	nop
1:
	mfc0	t0, CP0_CONFIG6
	or	t0, 0x100
	mtc0	t0, CP0_CONFIG6
2:
	_ehb
	.set	pop
	.endm

/*
 * Do SMP slave processor setup.
 */
	.macro	smp_slave_setup
	.set	push
	.set	mips64
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
	slti	t0, t0, (PRID_IMP_LOONGSON_64C | PRID_REV_LOONGSON3A_R2_0)
	bnez	t0, 2f
	nop
1:
	mfc0	t0, CP0_CONFIG6
	or	t0, 0x100
	mtc0	t0, CP0_CONFIG6
2:
	_ehb
	.set	pop
	.endm

#define USE_KEXEC_SMP_WAIT_FINAL
	.macro  kexec_smp_wait_final
	/* s0:prid s1:initfn */
	/* a0:base t1:cpuid t2:node t9:count */
	mfc0		t1, CP0_EBASE
	andi		t1, MIPS_EBASE_CPUNUM
	dins		a0, t1, 8, 2       /* insert core id*/
	dext		t2, t1, 2, 2
	dins		a0, t2, 44, 2      /* insert node id */
	mfc0		s0, CP0_PRID
	andi		s0, s0, (PRID_IMP_MASK | PRID_REV_MASK)
	beq		s0, (PRID_IMP_LOONGSON_64C | PRID_REV_LOONGSON3B_R1), 1f
	beq		s0, (PRID_IMP_LOONGSON_64C | PRID_REV_LOONGSON3B_R2), 1f
	b		2f                 /* Loongson-3A1000/3A2000/3A3000/3A4000 */
1:	dins		a0, t2, 14, 2      /* Loongson-3B1000/3B1500 need bit 15~14 */
2:	li		t9, 0x100          /* wait for init loop */
3:	addiu		t9, -1             /* limit mailbox access */
	bnez		t9, 3b
	lw		s1, 0x20(a0)       /* check PC as an indicator */
	beqz		s1, 2b
	ld		s1, 0x20(a0)       /* get PC via mailbox reg0 */
	ld		sp, 0x28(a0)       /* get SP via mailbox reg1 */
	ld		gp, 0x30(a0)       /* get GP via mailbox reg2 */
	ld		a1, 0x38(a0)
	jr		s1                 /* jump to initial PC */
	.endm

#endif /* __ASM_MACH_LOONGSON64_KERNEL_ENTRY_H */
