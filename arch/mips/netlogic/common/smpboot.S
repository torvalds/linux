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

#define CP0_EBASE	$15

	.set	noreorder
	.set	noat
	.set	arch=xlr		/* for mfcr/mtcr, XLR is sufficient */

FEXPORT(xlp_boot_core0_siblings)	/* "Master" cpu starts from here */
	dmtc0	sp, $4, 2		/* SP saved in UserLocal */
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
	mfc0	a0, CP0_EBASE, 1
	andi	a0, 0x3ff		/* a0 <- node/core */
	PTR_LA	t0, nlm_early_init_secondary
	jalr	t0
	nop

	PTR_LA	t0, smp_bootstrap
	jr	t0
	nop
END(nlm_boot_secondary_cpus)

/*
 * In case of RMIboot bootloader which is used on XLR boards, the CPUs
 * be already woken up and waiting in bootloader code.
 * This will get them out of the bootloader code and into linux. Needed
 *  because the bootloader area will be taken and initialized by linux.
 */
NESTED(nlm_rmiboot_preboot, 16, sp)
	mfc0	t0, $15, 1	/* read ebase */
	andi	t0, 0x1f	/* t0 has the processor_id() */
	andi	t2, t0, 0x3	/* thread num */
	sll	t0, 2		/* offset in cpu array */

	li	t3, CKSEG1ADDR(RESET_DATA_PHYS)
	ADDIU	t1, t3, BOOT_CPU_READY
	ADDU	t1, t0
	li	t3, 1
	sw	t3, 0(t1)

	bnez	t2, 1f		/* skip thread programming */
	nop			/* for thread id != 0 */

	/*
	 * XLR MMU setup only for first thread in core
	 */
	li	t0, 0x400
	mfcr	t1, t0
	li	t2, 6		/* XLR thread mode mask */
	nor	t3, t2, zero
	and	t2, t1, t2	/* t2 - current thread mode */
	li	v0, CKSEG1ADDR(RESET_DATA_PHYS)
	lw	v1, BOOT_THREAD_MODE(v0) /* v1 - new thread mode */
	sll	v1, 1
	beq	v1, t2, 1f	/* same as request value */
	nop			/* nothing to do */

	and	t2, t1, t3	/* mask out old thread mode */
	or	t1, t2, v1	/* put in new value */
	mtcr	t1, t0		/* update core control */

1:	wait
	b	1b
	nop
END(nlm_rmiboot_preboot)
