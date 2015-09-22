/*
 * Copyright 2003-2013 Broadcom Corporation.
 * All Rights Reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/cpu.h>
#include <asm/cacheops.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/asmmacro.h>
#include <asm/addrspace.h>

#include <asm/netlogic/common.h>

#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/xlp-hal/sys.h>
#include <asm/netlogic/xlp-hal/cpucontrol.h>

#define CP0_EBASE	$15
#define SYS_CPU_COHERENT_BASE	CKSEG1ADDR(XLP_DEFAULT_IO_BASE) + \
			XLP_IO_SYS_OFFSET(0) + XLP_IO_PCI_HDRSZ + \
			SYS_CPU_NONCOHERENT_MODE * 4

/* Enable XLP features and workarounds in the LSU */
.macro xlp_config_lsu
	li	t0, LSU_DEFEATURE
	mfcr	t1, t0

	lui	t2, 0x4080	/* Enable Unaligned Access, L2HPE */
	or	t1, t1, t2
	mtcr	t1, t0

	li	t0, ICU_DEFEATURE
	mfcr	t1, t0
	ori	t1, 0x1000	/* Enable Icache partitioning */
	mtcr	t1, t0

	li	t0, SCHED_DEFEATURE
	lui	t1, 0x0100	/* Disable BRU accepting ALU ops */
	mtcr	t1, t0
.endm

/*
 * Allow access to physical mem >64G by enabling ELPA in PAGEGRAIN
 * register. This is needed before going to C code since the SP can
 * in this region. Called from all HW threads.
 */
.macro xlp_early_mmu_init
	mfc0	t0, CP0_PAGEMASK, 1
	li	t1, (1 << 29)		/* ELPA bit */
	or	t0, t1
	mtc0	t0, CP0_PAGEMASK, 1
.endm

/*
 * L1D cache has to be flushed before enabling threads in XLP.
 * On XLP8xx/XLP3xx, we do a low level flush using processor control
 * registers. On XLPII CPUs, usual cache instructions work.
 */
.macro	xlp_flush_l1_dcache
	mfc0	t0, CP0_EBASE, 0
	andi	t0, t0, PRID_IMP_MASK
	slt	t1, t0, 0x1200
	beqz	t1, 15f
	nop

	/* XLP8xx low level cache flush */
	li	t0, LSU_DEBUG_DATA0
	li	t1, LSU_DEBUG_ADDR
	li	t2, 0		/* index */
	li	t3, 0x1000	/* loop count */
11:
	sll	v0, t2, 5
	mtcr	zero, t0
	ori	v1, v0, 0x3	/* way0 | write_enable | write_active */
	mtcr	v1, t1
12:
	mfcr	v1, t1
	andi	v1, 0x1		/* wait for write_active == 0 */
	bnez	v1, 12b
	nop
	mtcr	zero, t0
	ori	v1, v0, 0x7	/* way1 | write_enable | write_active */
	mtcr	v1, t1
13:
	mfcr	v1, t1
	andi	v1, 0x1		/* wait for write_active == 0 */
	bnez	v1, 13b
	nop
	addi	t2, 1
	bne	t3, t2, 11b
	nop
	b	17f
	nop

	/* XLPII CPUs, Invalidate all 64k of L1 D-cache */
15:
	li	t0, 0x80000000
	li	t1, 0x80010000
16:	cache	Index_Writeback_Inv_D, 0(t0)
	addiu	t0, t0, 32
	bne	t0, t1, 16b
	nop
17:
.endm

/*
 * nlm_reset_entry will be copied to the reset entry point for
 * XLR and XLP. The XLP cores start here when they are woken up. This
 * is also the NMI entry point.
 *
 * We use scratch reg 6/7 to save k0/k1 and check for NMI first.
 *
 * The data corresponding to reset/NMI is stored at RESET_DATA_PHYS
 * location, this will have the thread mask (used when core is woken up)
 * and the current NMI handler in case we reached here for an NMI.
 *
 * When a core or thread is newly woken up, it marks itself ready and
 * loops in a 'wait'. When the CPU really needs waking up, we send an NMI
 * IPI to it, with the NMI handler set to prom_boot_secondary_cpus
 */
	.set	noreorder
	.set	noat
	.set	arch=xlr	/* for mfcr/mtcr, XLR is sufficient */

FEXPORT(nlm_reset_entry)
	dmtc0	k0, $22, 6
	dmtc0	k1, $22, 7
	mfc0	k0, CP0_STATUS
	li	k1, 0x80000
	and	k1, k0, k1
	beqz	k1, 1f		/* go to real reset entry */
	nop
	li	k1, CKSEG1ADDR(RESET_DATA_PHYS) /* NMI */
	ld	k0, BOOT_NMI_HANDLER(k1)
	jr	k0
	nop

1:	/* Entry point on core wakeup */
	mfc0	t0, CP0_EBASE, 0	/* processor ID */
	andi	t0, PRID_IMP_MASK
	li	t1, 0x1500		/* XLP 9xx */
	beq	t0, t1, 2f		/* does not need to set coherent */
	nop

	li	t1, 0x1300		/* XLP 5xx */
	beq	t0, t1, 2f		/* does not need to set coherent */
	nop

	/* set bit in SYS coherent register for the core */
	mfc0	t0, CP0_EBASE, 1
	mfc0	t1, CP0_EBASE, 1
	srl	t1, 5
	andi	t1, 0x3			/* t1 <- node */
	li	t2, 0x40000
	mul	t3, t2, t1		/* t3 = node * 0x40000 */
	srl	t0, t0, 2
	and	t0, t0, 0x7		/* t0 <- core */
	li	t1, 0x1
	sll	t0, t1, t0
	nor	t0, t0, zero		/* t0 <- ~(1 << core) */
	li	t2, SYS_CPU_COHERENT_BASE
	add	t2, t2, t3		/* t2 <- SYS offset for node */
	lw	t1, 0(t2)
	and	t1, t1, t0
	sw	t1, 0(t2)

	/* read back to ensure complete */
	lw	t1, 0(t2)
	sync

2:
	/* Configure LSU on Non-0 Cores. */
	xlp_config_lsu
	/* FALL THROUGH */

/*
 * Wake up sibling threads from the initial thread in a core.
 */
EXPORT(nlm_boot_siblings)
	/* core L1D flush before enable threads */
	xlp_flush_l1_dcache
	/* save ra and sp, will be used later (only for boot cpu) */
	dmtc0	ra, $22, 6
	dmtc0	sp, $22, 7
	/* Enable hw threads by writing to MAP_THREADMODE of the core */
	li	t0, CKSEG1ADDR(RESET_DATA_PHYS)
	lw	t1, BOOT_THREAD_MODE(t0)	/* t1 <- thread mode */
	li	t0, ((CPU_BLOCKID_MAP << 8) | MAP_THREADMODE)
	mfcr	t2, t0
	or	t2, t2, t1
	mtcr	t2, t0

	/*
	 * The new hardware thread starts at the next instruction
	 * For all the cases other than core 0 thread 0, we will
	 * jump to the secondary wait function.

	 * NOTE: All GPR contents are lost after the mtcr above!
	 */
	mfc0	v0, CP0_EBASE, 1
	andi	v0, 0x3ff		/* v0 <- node/core */

	/*
	 * Errata: to avoid potential live lock, setup IFU_BRUB_RESERVE
	 * when running 4 threads per core
	 */
	andi	v1, v0, 0x3             /* v1 <- thread id */
	bnez	v1, 2f
	nop

	/* thread 0 of each core. */
	li	t0, CKSEG1ADDR(RESET_DATA_PHYS)
	lw	t1, BOOT_THREAD_MODE(t0)        /* t1 <- thread mode */
	subu	t1, 0x3				/* 4-thread per core mode? */
	bnez	t1, 2f
	nop

	li	t0, IFU_BRUB_RESERVE
	li	t1, 0x55
	mtcr	t1, t0
	_ehb
2:
	beqz	v0, 4f		/* boot cpu (cpuid == 0)? */
	nop

	/* setup status reg */
	move	t1, zero
#ifdef CONFIG_64BIT
	ori	t1, ST0_KX
#endif
	mtc0	t1, CP0_STATUS

	xlp_early_mmu_init

	/* mark CPU ready */
	li	t3, CKSEG1ADDR(RESET_DATA_PHYS)
	ADDIU	t1, t3, BOOT_CPU_READY
	sll	v1, v0, 2
	PTR_ADDU t1, v1
	li	t2, 1
	sw	t2, 0(t1)
	/* Wait until NMI hits */
3:	wait
	b	3b
	nop

	/*
	 * For the boot CPU, we have to restore ra and sp and return, rest
	 * of the registers will be restored by the caller
	 */
4:
	dmfc0	ra, $22, 6
	dmfc0	sp, $22, 7
	jr	ra
	nop
EXPORT(nlm_reset_entry_end)

LEAF(nlm_init_boot_cpu)
#ifdef CONFIG_CPU_XLP
	xlp_config_lsu
	xlp_early_mmu_init
#endif
	jr	ra
	nop
END(nlm_init_boot_cpu)
