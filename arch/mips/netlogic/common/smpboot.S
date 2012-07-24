/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
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
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/init.h>

#include <asm/asm.h>
#include <asm/asm-offsets.h>
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

#define	CP0_EBASE	$15
#define SYS_CPU_COHERENT_BASE(node)	CKSEG1ADDR(XLP_DEFAULT_IO_BASE) + \
			XLP_IO_SYS_OFFSET(node) + XLP_IO_PCI_HDRSZ + \
			SYS_CPU_NONCOHERENT_MODE * 4

#define	XLP_AX_WORKAROUND	/* enable Ax silicon workarounds */

/* Enable XLP features and workarounds in the LSU */
.macro xlp_config_lsu
	li	t0, LSU_DEFEATURE
	mfcr	t1, t0

	lui	t2, 0x4080	/* Enable Unaligned Access, L2HPE */
	or	t1, t1, t2
#ifdef XLP_AX_WORKAROUND
	li	t2, ~0xe	/* S1RCM */
	and	t1, t1, t2
#endif
	mtcr    t1, t0

#ifdef XLP_AX_WORKAROUND
	li	t0, SCHED_DEFEATURE
	lui	t1, 0x0100	/* Disable BRU accepting ALU ops */
	mtcr	t1, t0
#endif
.endm

/*
 * This is the code that will be copied to the reset entry point for
 * XLR and XLP. The XLP cores start here when they are woken up. This
 * is also the NMI entry point.
 */
.macro	xlp_flush_l1_dcache
	li	t0, LSU_DEBUG_DATA0
	li	t1, LSU_DEBUG_ADDR
	li	t2, 0		/* index */
	li 	t3, 0x1000	/* loop count */
1:
	sll	v0, t2, 5
	mtcr	zero, t0
	ori	v1, v0, 0x3	/* way0 | write_enable | write_active */
	mtcr	v1, t1
2:
	mfcr	v1, t1
	andi	v1, 0x1		/* wait for write_active == 0 */
	bnez	v1, 2b
	nop
	mtcr	zero, t0
	ori	v1, v0, 0x7	/* way1 | write_enable | write_active */
	mtcr	v1, t1
3:
	mfcr	v1, t1
	andi	v1, 0x1		/* wait for write_active == 0 */
	bnez	v1, 3b
	nop
	addi	t2, 1
	bne	t3, t2, 1b
	nop
.endm

/*
 * The cores can come start when they are woken up. This is also the NMI
 * entry, so check that first.
 *
 * The data corresponding to reset/NMI is stored at RESET_DATA_PHYS
 * location, this will have the thread mask (used when core is woken up)
 * and the current NMI handler in case we reached here for an NMI.
 *
 * When a core or thread is newly woken up, it loops in a 'wait'. When
 * the CPU really needs waking up, we send an NMI to it, with the NMI
 * handler set to prom_boot_secondary_cpus
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
	li	k1, CKSEG1ADDR(RESET_DATA_PHYS)	/* NMI */
	ld	k0, BOOT_NMI_HANDLER(k1)
	jr	k0
	nop

1:	/* Entry point on core wakeup */
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
	li	t2, SYS_CPU_COHERENT_BASE(0)
	add	t2, t2, t3		/* t2 <- SYS offset for node */
	lw	t1, 0(t2)
	and	t1, t1, t0
	sw	t1, 0(t2)

	/* read back to ensure complete */
	lw	t1, 0(t2)
	sync

	/* Configure LSU on Non-0 Cores. */
	xlp_config_lsu
	/* FALL THROUGH */

/*
 * Wake up sibling threads from the initial thread in
 * a core.
 */
EXPORT(nlm_boot_siblings)
	/* core L1D flush before enable threads */
	xlp_flush_l1_dcache
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
	*/
	mfc0	v0, CP0_EBASE, 1
	andi	v0, 0x7f		/* v0 <- node/core */

	/* Init MMU in the first thread after changing THREAD_MODE
	 * register (Ax Errata?)
	 */
	andi	v1, v0, 0x3		/* v1 <- thread id */
	bnez	v1, 2f
	nop

        li	t0, MMU_SETUP
        li	t1, 0
        mtcr	t1, t0
	_ehb

2:	beqz	v0, 4f		/* boot cpu (cpuid == 0)? */
	nop

	/* setup status reg */
	move	t1, zero
#ifdef CONFIG_64BIT
	ori	t1, ST0_KX
#endif
	mtc0	t1, CP0_STATUS
	/* mark CPU ready */
	PTR_LA	t1, nlm_cpu_ready
	sll	v1, v0, 2
	PTR_ADDU t1, v1
	li	t2, 1
	sw	t2, 0(t1)
	/* Wait until NMI hits */
3:	wait
	j	3b
	nop

	/*
	 * For the boot CPU, we have to restore registers and
	 * return
	 */
4:	dmfc0	t0, $4, 2	/* restore SP from UserLocal */
	li	t1, 0xfadebeef
	dmtc0	t1, $4, 2	/* restore SP from UserLocal */
	PTR_SUBU sp, t0, PT_SIZE
	RESTORE_ALL
	jr   ra
	nop
EXPORT(nlm_reset_entry_end)

FEXPORT(xlp_boot_core0_siblings)	/* "Master" cpu starts from here */
	xlp_config_lsu
	dmtc0   sp, $4, 2		/* SP saved in UserLocal */
	SAVE_ALL
	sync
	/* find the location to which nlm_boot_siblings was relocated */
	li	t0, CKSEG1ADDR(RESET_VEC_PHYS)
	dla	t1, nlm_reset_entry
	dla	t2, nlm_boot_siblings
	dsubu	t2, t1
	daddu	t2, t0
	/* call it */
	jr	t2
	nop
	/* not reached */

	__CPUINIT
NESTED(nlm_boot_secondary_cpus, 16, sp)
	/* Initialize CP0 Status */
	move	t1, zero
#ifdef CONFIG_64BIT
	ori	t1, ST0_KX
#endif
	mtc0	t1, CP0_STATUS
	PTR_LA	t1, nlm_next_sp
	PTR_L	sp, 0(t1)
	PTR_LA	t1, nlm_next_gp
	PTR_L	gp, 0(t1)

	/* a0 has the processor id */
	PTR_LA	t0, nlm_early_init_secondary
	jalr	t0
	nop

	PTR_LA	t0, smp_bootstrap
	jr	t0
	nop
END(nlm_boot_secondary_cpus)
	__FINIT

/*
 * In case of RMIboot bootloader which is used on XLR boards, the CPUs
 * be already woken up and waiting in bootloader code.
 * This will get them out of the bootloader code and into linux. Needed
 *  because the bootloader area will be taken and initialized by linux.
 */
	__CPUINIT
NESTED(nlm_rmiboot_preboot, 16, sp)
	mfc0	t0, $15, 1	/* read ebase */
	andi	t0, 0x1f	/* t0 has the processor_id() */
	andi	t2, t0, 0x3	/* thread num */
	sll	t0, 2		/* offset in cpu array */

	PTR_LA	t1, nlm_cpu_ready /* mark CPU ready */
	PTR_ADDU t1, t0
	li	t3, 1
	sw	t3, 0(t1)

	bnez	t2, 1f		/* skip thread programming */
	nop			/* for thread id != 0 */

	/*
	 * XLR MMU setup only for first thread in core
	 */
	li	t0, 0x400
	mfcr	t1, t0
	li	t2, 6 		/* XLR thread mode mask */
	nor	t3, t2, zero
	and	t2, t1, t2	/* t2 - current thread mode */
	li	v0, CKSEG1ADDR(RESET_DATA_PHYS)
	lw	v1, BOOT_THREAD_MODE(v0) /* v1 - new thread mode */
	sll	v1, 1
	beq	v1, t2, 1f 	/* same as request value */
	nop			/* nothing to do */

	and	t2, t1, t3	/* mask out old thread mode */
	or	t1, t2, v1	/* put in new value */
	mtcr	t1, t0		/* update core control */

1:	wait
	j	1b
	nop
END(nlm_rmiboot_preboot)
	__FINIT
