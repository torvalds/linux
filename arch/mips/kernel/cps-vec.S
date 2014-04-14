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
#include <asm/mipsregs.h>

#define GCR_CL_COHERENCE_OFS 0x2008

.section .text.cps-vec
.balign 0x1000
.set noreorder

LEAF(mips_cps_core_entry)
	/*
	 * These first 8 bytes will be patched by cps_smp_setup to load the
	 * base address of the CM GCRs into register v1.
	 */
	.quad	0

	/* Check whether we're here due to an NMI */
	mfc0	k0, CP0_STATUS
	and	k0, k0, ST0_NMI
	beqz	k0, not_nmi
	 nop

	/* This is an NMI */
	la	k0, nmi_handler
	jr	k0
	 nop

not_nmi:
	/* Setup Cause */
	li	t0, CAUSEF_IV
	mtc0	t0, CP0_CAUSE

	/* Setup Status */
	li	t0, ST0_CU1 | ST0_CU0
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
	addi	t1, t1, 1
	sllv	t1, t3, t1
1:	/* At this point t1 == I-cache sets per way */
	_EXT	t2, v0, MIPS_CONF1_IA_SHF, MIPS_CONF1_IA_SZ
	addi	t2, t2, 1
	mul	t1, t1, t0
	mul	t1, t1, t2

	li	a0, KSEG0
	add	a1, a0, t1
1:	cache	Index_Store_Tag_I, 0(a0)
	add	a0, a0, t0
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
	addi	t1, t1, 1
	sllv	t1, t3, t1
1:	/* At this point t1 == D-cache sets per way */
	_EXT	t2, v0, MIPS_CONF1_DA_SHF, MIPS_CONF1_DA_SZ
	addi	t2, t2, 1
	mul	t1, t1, t0
	mul	t1, t1, t2

	li	a0, KSEG0
	addu	a1, a0, t1
	subu	a1, a1, t0
1:	cache	Index_Store_Tag_D, 0(a0)
	bne	a0, a1, 1b
	 add	a0, a0, t0
dcache_done:

	/* Set Kseg0 cacheable, coherent, write-back, write-allocate */
	mfc0	t0, CP0_CONFIG
	ori	t0, 0x7
	xori	t0, 0x2
	mtc0	t0, CP0_CONFIG
	ehb

	/* Enter the coherent domain */
	li	t0, 0xff
	sw	t0, GCR_CL_COHERENCE_OFS(v1)
	ehb

	/* Jump to kseg0 */
	la	t0, 1f
	jr	t0
	 nop

1:	/* We're up, cached & coherent */

	/*
	 * TODO: We should check the VPE number we intended to boot here, and
	 *       if non-zero we should start that VPE and stop this one. For
	 *       the moment this doesn't matter since CPUs are brought up
	 *       sequentially and in order, but once hotplug is implemented
	 *       this will need revisiting.
	 */

	/* Off we go! */
	la	t0, mips_cps_bootcfg
	lw	t1, BOOTCFG_PC(t0)
	lw	gp, BOOTCFG_GP(t0)
	lw	sp, BOOTCFG_SP(t0)
	jr	t1
	 nop
	END(mips_cps_core_entry)

.org 0x200
LEAF(excep_tlbfill)
	b	.
	 nop
	END(excep_tlbfill)

.org 0x280
LEAF(excep_xtlbfill)
	b	.
	 nop
	END(excep_xtlbfill)

.org 0x300
LEAF(excep_cache)
	b	.
	 nop
	END(excep_cache)

.org 0x380
LEAF(excep_genex)
	b	.
	 nop
	END(excep_genex)

.org 0x400
LEAF(excep_intex)
	b	.
	 nop
	END(excep_intex)

.org 0x480
LEAF(excep_ejtag)
	la	k0, ejtag_debug_handler
	jr	k0
	 nop
	END(excep_ejtag)
