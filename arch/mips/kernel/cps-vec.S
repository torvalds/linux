/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <asm/addrspace.h>
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/asmmacro.h>
#include <asm/cacheops.h>
#include <asm/eva.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/pm.h>

#define GCR_CL_COHERENCE_OFS	0x2008
#define GCR_CL_ID_OFS		0x2028

.extern mips_cm_base

.set noreorder

#ifdef CONFIG_64BIT
# define STATUS_BITDEPS		ST0_KX
#else
# define STATUS_BITDEPS		0
#endif

#ifdef CONFIG_MIPS_CPS_NS16550

#define DUMP_EXCEP(name)		\
	PTR_LA	a0, 8f;			\
	jal	mips_cps_bev_dump;	\
	 nop;				\
	TEXT(name)

#else /* !CONFIG_MIPS_CPS_NS16550 */

#define DUMP_EXCEP(name)

#endif /* !CONFIG_MIPS_CPS_NS16550 */

	/*
	 * Set dest to non-zero if the core supports the MT ASE, else zero. If
	 * MT is not supported then branch to nomt.
	 */
	.macro	has_mt	dest, nomt
	mfc0	\dest, CP0_CONFIG, 1
	bgez	\dest, \nomt
	 mfc0	\dest, CP0_CONFIG, 2
	bgez	\dest, \nomt
	 mfc0	\dest, CP0_CONFIG, 3
	andi	\dest, \dest, MIPS_CONF3_MT
	beqz	\dest, \nomt
	 nop
	.endm

.section .text.cps-vec
.balign 0x1000

LEAF(mips_cps_core_entry)
	/*
	 * These first 4 bytes will be patched by cps_smp_setup to load the
	 * CCA to use into register s0.
	 */
	.word	0

	/* Check whether we're here due to an NMI */
	mfc0	k0, CP0_STATUS
	and	k0, k0, ST0_NMI
	beqz	k0, not_nmi
	 nop

	/* This is an NMI */
	PTR_LA	k0, nmi_handler
	jr	k0
	 nop

not_nmi:
	/* Setup Cause */
	li	t0, CAUSEF_IV
	mtc0	t0, CP0_CAUSE

	/* Setup Status */
	li	t0, ST0_CU1 | ST0_CU0 | ST0_BEV | STATUS_BITDEPS
	mtc0	t0, CP0_STATUS

	/*
	 * Clear the bits used to index the caches. Note that the architecture
	 * dictates that writing to any of TagLo or TagHi selects 0 or 2 should
	 * be valid for all MIPS32 CPUs, even those for which said writes are
	 * unnecessary.
	 */
	mtc0	zero, CP0_TAGLO, 0
	mtc0	zero, CP0_TAGHI, 0
	mtc0	zero, CP0_TAGLO, 2
	mtc0	zero, CP0_TAGHI, 2
	ehb

	/* Primary cache configuration is indicated by Config1 */
	mfc0	v0, CP0_CONFIG, 1

	/* Detect I-cache line size */
	_EXT	t0, v0, MIPS_CONF1_IL_SHF, MIPS_CONF1_IL_SZ
	beqz	t0, icache_done
	 li	t1, 2
	sllv	t0, t1, t0

	/* Detect I-cache size */
	_EXT	t1, v0, MIPS_CONF1_IS_SHF, MIPS_CONF1_IS_SZ
	xori	t2, t1, 0x7
	beqz	t2, 1f
	 li	t3, 32
	addiu	t1, t1, 1
	sllv	t1, t3, t1
1:	/* At this point t1 == I-cache sets per way */
	_EXT	t2, v0, MIPS_CONF1_IA_SHF, MIPS_CONF1_IA_SZ
	addiu	t2, t2, 1
	mul	t1, t1, t0
	mul	t1, t1, t2

	li	a0, CKSEG0
	PTR_ADD	a1, a0, t1
1:	cache	Index_Store_Tag_I, 0(a0)
	PTR_ADD	a0, a0, t0
	bne	a0, a1, 1b
	 nop
icache_done:

	/* Detect D-cache line size */
	_EXT	t0, v0, MIPS_CONF1_DL_SHF, MIPS_CONF1_DL_SZ
	beqz	t0, dcache_done
	 li	t1, 2
	sllv	t0, t1, t0

	/* Detect D-cache size */
	_EXT	t1, v0, MIPS_CONF1_DS_SHF, MIPS_CONF1_DS_SZ
	xori	t2, t1, 0x7
	beqz	t2, 1f
	 li	t3, 32
	addiu	t1, t1, 1
	sllv	t1, t3, t1
1:	/* At this point t1 == D-cache sets per way */
	_EXT	t2, v0, MIPS_CONF1_DA_SHF, MIPS_CONF1_DA_SZ
	addiu	t2, t2, 1
	mul	t1, t1, t0
	mul	t1, t1, t2

	li	a0, CKSEG0
	PTR_ADDU a1, a0, t1
	PTR_SUBU a1, a1, t0
1:	cache	Index_Store_Tag_D, 0(a0)
	bne	a0, a1, 1b
	 PTR_ADD a0, a0, t0
dcache_done:

	/* Set Kseg0 CCA to that in s0 */
	mfc0	t0, CP0_CONFIG
	ori	t0, 0x7
	xori	t0, 0x7
	or	t0, t0, s0
	mtc0	t0, CP0_CONFIG
	ehb

	/* Calculate an uncached address for the CM GCRs */
	MFC0	v1, CP0_CMGCRBASE
	PTR_SLL	v1, v1, 4
	PTR_LI	t0, UNCAC_BASE
	PTR_ADDU v1, v1, t0

	/* Enter the coherent domain */
	li	t0, 0xff
	sw	t0, GCR_CL_COHERENCE_OFS(v1)
	ehb

	/* Jump to kseg0 */
	PTR_LA	t0, 1f
	jr	t0
	 nop

	/*
	 * We're up, cached & coherent. Perform any further required core-level
	 * initialisation.
	 */
1:	jal	mips_cps_core_init
	 nop

	/* Do any EVA initialization if necessary */
	eva_init

	/*
	 * Boot any other VPEs within this core that should be online, and
	 * deactivate this VPE if it should be offline.
	 */
	jal	mips_cps_boot_vpes
	 nop

	/* Off we go! */
	PTR_L	t1, VPEBOOTCFG_PC(v0)
	PTR_L	gp, VPEBOOTCFG_GP(v0)
	PTR_L	sp, VPEBOOTCFG_SP(v0)
	jr	t1
	 nop
	END(mips_cps_core_entry)

.org 0x200
LEAF(excep_tlbfill)
	DUMP_EXCEP("TLB Fill")
	b	.
	 nop
	END(excep_tlbfill)

.org 0x280
LEAF(excep_xtlbfill)
	DUMP_EXCEP("XTLB Fill")
	b	.
	 nop
	END(excep_xtlbfill)

.org 0x300
LEAF(excep_cache)
	DUMP_EXCEP("Cache")
	b	.
	 nop
	END(excep_cache)

.org 0x380
LEAF(excep_genex)
	DUMP_EXCEP("General")
	b	.
	 nop
	END(excep_genex)

.org 0x400
LEAF(excep_intex)
	DUMP_EXCEP("Interrupt")
	b	.
	 nop
	END(excep_intex)

.org 0x480
LEAF(excep_ejtag)
	DUMP_EXCEP("EJTAG")
	PTR_LA	k0, ejtag_debug_handler
	jr	k0
	 nop
	END(excep_ejtag)

LEAF(mips_cps_core_init)
#ifdef CONFIG_MIPS_MT_SMP
	/* Check that the core implements the MT ASE */
	has_mt	t0, 3f

	.set	push
	.set	mt

	/* Only allow 1 TC per VPE to execute... */
	dmt

	/* ...and for the moment only 1 VPE */
	dvpe
	PTR_LA	t1, 1f
	jr.hb	t1
	 nop

	/* Enter VPE configuration state */
1:	mfc0	t0, CP0_MVPCONTROL
	ori	t0, t0, MVPCONTROL_VPC
	mtc0	t0, CP0_MVPCONTROL

	/* Retrieve the number of VPEs within the core */
	mfc0	t0, CP0_MVPCONF0
	srl	t0, t0, MVPCONF0_PVPE_SHIFT
	andi	t0, t0, (MVPCONF0_PVPE >> MVPCONF0_PVPE_SHIFT)
	addiu	ta3, t0, 1

	/* If there's only 1, we're done */
	beqz	t0, 2f
	 nop

	/* Loop through each VPE within this core */
	li	ta1, 1

1:	/* Operate on the appropriate TC */
	mtc0	ta1, CP0_VPECONTROL
	ehb

	/* Bind TC to VPE (1:1 TC:VPE mapping) */
	mttc0	ta1, CP0_TCBIND

	/* Set exclusive TC, non-active, master */
	li	t0, VPECONF0_MVP
	sll	t1, ta1, VPECONF0_XTC_SHIFT
	or	t0, t0, t1
	mttc0	t0, CP0_VPECONF0

	/* Set TC non-active, non-allocatable */
	mttc0	zero, CP0_TCSTATUS

	/* Set TC halted */
	li	t0, TCHALT_H
	mttc0	t0, CP0_TCHALT

	/* Next VPE */
	addiu	ta1, ta1, 1
	slt	t0, ta1, ta3
	bnez	t0, 1b
	 nop

	/* Leave VPE configuration state */
2:	mfc0	t0, CP0_MVPCONTROL
	xori	t0, t0, MVPCONTROL_VPC
	mtc0	t0, CP0_MVPCONTROL

3:	.set	pop
#endif
	jr	ra
	 nop
	END(mips_cps_core_init)

LEAF(mips_cps_boot_vpes)
	/* Retrieve CM base address */
	PTR_LA	t0, mips_cm_base
	PTR_L	t0, 0(t0)

	/* Calculate a pointer to this cores struct core_boot_config */
	lw	t0, GCR_CL_ID_OFS(t0)
	li	t1, COREBOOTCFG_SIZE
	mul	t0, t0, t1
	PTR_LA	t1, mips_cps_core_bootcfg
	PTR_L	t1, 0(t1)
	PTR_ADDU t0, t0, t1

	/* Calculate this VPEs ID. If the core doesn't support MT use 0 */
	li	t9, 0
#ifdef CONFIG_MIPS_MT_SMP
	has_mt	ta2, 1f

	/* Find the number of VPEs present in the core */
	mfc0	t1, CP0_MVPCONF0
	srl	t1, t1, MVPCONF0_PVPE_SHIFT
	andi	t1, t1, MVPCONF0_PVPE >> MVPCONF0_PVPE_SHIFT
	addiu	t1, t1, 1

	/* Calculate a mask for the VPE ID from EBase.CPUNum */
	clz	t1, t1
	li	t2, 31
	subu	t1, t2, t1
	li	t2, 1
	sll	t1, t2, t1
	addiu	t1, t1, -1

	/* Retrieve the VPE ID from EBase.CPUNum */
	mfc0	t9, $15, 1
	and	t9, t9, t1
#endif

1:	/* Calculate a pointer to this VPEs struct vpe_boot_config */
	li	t1, VPEBOOTCFG_SIZE
	mul	v0, t9, t1
	PTR_L	ta3, COREBOOTCFG_VPECONFIG(t0)
	PTR_ADDU v0, v0, ta3

#ifdef CONFIG_MIPS_MT_SMP

	/* If the core doesn't support MT then return */
	bnez	ta2, 1f
	 nop
	jr	ra
	 nop

	.set	push
	.set	mt

1:	/* Enter VPE configuration state */
	dvpe
	PTR_LA	t1, 1f
	jr.hb	t1
	 nop
1:	mfc0	t1, CP0_MVPCONTROL
	ori	t1, t1, MVPCONTROL_VPC
	mtc0	t1, CP0_MVPCONTROL
	ehb

	/* Loop through each VPE */
	PTR_L	ta2, COREBOOTCFG_VPEMASK(t0)
	move	t8, ta2
	li	ta1, 0

	/* Check whether the VPE should be running. If not, skip it */
1:	andi	t0, ta2, 1
	beqz	t0, 2f
	 nop

	/* Operate on the appropriate TC */
	mfc0	t0, CP0_VPECONTROL
	ori	t0, t0, VPECONTROL_TARGTC
	xori	t0, t0, VPECONTROL_TARGTC
	or	t0, t0, ta1
	mtc0	t0, CP0_VPECONTROL
	ehb

	/* Skip the VPE if its TC is not halted */
	mftc0	t0, CP0_TCHALT
	beqz	t0, 2f
	 nop

	/* Calculate a pointer to the VPEs struct vpe_boot_config */
	li	t0, VPEBOOTCFG_SIZE
	mul	t0, t0, ta1
	addu	t0, t0, ta3

	/* Set the TC restart PC */
	lw	t1, VPEBOOTCFG_PC(t0)
	mttc0	t1, CP0_TCRESTART

	/* Set the TC stack pointer */
	lw	t1, VPEBOOTCFG_SP(t0)
	mttgpr	t1, sp

	/* Set the TC global pointer */
	lw	t1, VPEBOOTCFG_GP(t0)
	mttgpr	t1, gp

	/* Copy config from this VPE */
	mfc0	t0, CP0_CONFIG
	mttc0	t0, CP0_CONFIG

	/* Ensure no software interrupts are pending */
	mttc0	zero, CP0_CAUSE
	mttc0	zero, CP0_STATUS

	/* Set TC active, not interrupt exempt */
	mftc0	t0, CP0_TCSTATUS
	li	t1, ~TCSTATUS_IXMT
	and	t0, t0, t1
	ori	t0, t0, TCSTATUS_A
	mttc0	t0, CP0_TCSTATUS

	/* Clear the TC halt bit */
	mttc0	zero, CP0_TCHALT

	/* Set VPE active */
	mftc0	t0, CP0_VPECONF0
	ori	t0, t0, VPECONF0_VPA
	mttc0	t0, CP0_VPECONF0

	/* Next VPE */
2:	srl	ta2, ta2, 1
	addiu	ta1, ta1, 1
	bnez	ta2, 1b
	 nop

	/* Leave VPE configuration state */
	mfc0	t1, CP0_MVPCONTROL
	xori	t1, t1, MVPCONTROL_VPC
	mtc0	t1, CP0_MVPCONTROL
	ehb
	evpe

	/* Check whether this VPE is meant to be running */
	li	t0, 1
	sll	t0, t0, t9
	and	t0, t0, t8
	bnez	t0, 2f
	 nop

	/* This VPE should be offline, halt the TC */
	li	t0, TCHALT_H
	mtc0	t0, CP0_TCHALT
	PTR_LA	t0, 1f
1:	jr.hb	t0
	 nop

2:	.set	pop

#endif /* CONFIG_MIPS_MT_SMP */

	/* Return */
	jr	ra
	 nop
	END(mips_cps_boot_vpes)

#if defined(CONFIG_MIPS_CPS_PM) && defined(CONFIG_CPU_PM)

	/* Calculate a pointer to this CPUs struct mips_static_suspend_state */
	.macro	psstate	dest
	.set	push
	.set	noat
	lw	$1, TI_CPU(gp)
	sll	$1, $1, LONGLOG
	PTR_LA	\dest, __per_cpu_offset
	addu	$1, $1, \dest
	lw	$1, 0($1)
	PTR_LA	\dest, cps_cpu_state
	addu	\dest, \dest, $1
	.set	pop
	.endm

LEAF(mips_cps_pm_save)
	/* Save CPU state */
	SUSPEND_SAVE_REGS
	psstate	t1
	SUSPEND_SAVE_STATIC
	jr	v0
	 nop
	END(mips_cps_pm_save)

LEAF(mips_cps_pm_restore)
	/* Restore CPU state */
	psstate	t1
	RESUME_RESTORE_STATIC
	RESUME_RESTORE_REGS_RETURN
	END(mips_cps_pm_restore)

#endif /* CONFIG_MIPS_CPS_PM && CONFIG_CPU_PM */
