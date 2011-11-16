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

#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/xlp-hal/sys.h>
#include <asm/netlogic/xlp-hal/cpucontrol.h>

#define	CP0_EBASE	$15
#define SYS_CPU_COHERENT_BASE(node)	CKSEG1ADDR(XLP_DEFAULT_IO_BASE) + \
			XLP_IO_SYS_OFFSET(node) + XLP_IO_PCI_HDRSZ + \
			SYS_CPU_NONCOHERENT_MODE * 4

.macro __config_lsu
	li      t0, LSU_DEFEATURE
	mfcr    t1, t0

	lui     t2, 0x4080  /* Enable Unaligned Access, L2HPE */
	or      t1, t1, t2
	li	t2, ~0xe    /* S1RCM */
	and	t1, t1, t2
	mtcr    t1, t0

	li      t0, SCHED_DEFEATURE
	lui     t1, 0x0100  /* Experimental: Disable BRU accepting ALU ops */
	mtcr    t1, t0
.endm

	.set	noreorder
	.set	arch=xlr	/* for mfcr/mtcr, XLR is sufficient */

	__CPUINIT
EXPORT(nlm_reset_entry)
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
	and     t1, t1, t0
	sw      t1, 0(t2)

	/* read back to ensure complete */
	lw      t1, 0(t2)
	sync

	/* Configure LSU on Non-0 Cores. */
	__config_lsu

/*
 * Wake up sibling threads from the initial thread in
 * a core.
 */
EXPORT(nlm_boot_siblings)
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

#if 1
	/* A0 errata - Write MMU_SETUP after changing thread mode register. */
	andi	v1, v0, 0x3		/* v1 <- thread id */
	bnez	v1, 2f
	nop

        li	t0, MMU_SETUP
        li	t1, 0
        mtcr	t1, t0
	ehb
#endif

2:	beqz	v0, 3f
	nop

	/* setup status reg */
	mfc0	t1, CP0_STATUS
	li	t0, ST0_BEV
	or	t1, t0
	xor	t1, t0
#ifdef CONFIG_64BIT
	ori	t1, ST0_KX
#endif
	mtc0	t1, CP0_STATUS

	/* SETUP TLBs for a mapped kernel here */
	PTR_LA	t0, prom_pre_boot_secondary_cpus
	jalr	t0
	nop

	/*
	 * For the boot CPU, we have to restore registers and
	 * return
	 */
3:	dmfc0	t0, $4, 2       /* restore SP from UserLocal */
	li	t1, 0xfadebeef
	dmtc0	t1, $4, 2       /* restore SP from UserLocal */
	PTR_SUBU sp, t0, PT_SIZE
	RESTORE_ALL
	jr   ra
	nop
EXPORT(nlm_reset_entry_end)

EXPORT(nlm_boot_core0_siblings)	/* "Master" (n0c0t0) cpu starts from here */
	__config_lsu
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
	__FINIT

	__CPUINIT
NESTED(prom_pre_boot_secondary_cpus, 16, sp)
	.set	mips64
	mfc0	a0, CP0_EBASE, 1	/* read ebase */
	andi	a0, 0x3ff		/* a0 has the processor_id() */
	sll	t0, a0, 2		/* offset in cpu array */

	PTR_LA	t1, nlm_cpu_ready	/* mark CPU ready */
	PTR_ADDU t1, t0
	li	t2, 1
	sw	t2, 0(t1)

	PTR_LA	t1, nlm_cpu_unblock
	PTR_ADDU t1, t0
1:	lw	t2, 0(t1)		/* wait till unblocked */
	bnez	t2, 2f
	nop
	nop
	nop
	nop
	nop
	nop
	j	1b
	nop

2:	PTR_LA	t1, nlm_next_sp
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
END(prom_pre_boot_secondary_cpus)
	__FINIT
