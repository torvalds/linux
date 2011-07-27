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

#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>


/* Don't jump to linux function from Bootloader stack. Change it
 * here. Kernel might allocate bootloader memory before all the CPUs are
 * brought up (eg: Inode cache region) and we better don't overwrite this
 * memory
 */
NESTED(prom_pre_boot_secondary_cpus, 16, sp)
	.set	mips64
	mfc0	t0, $15, 1	# read ebase
	andi	t0, 0x1f	# t0 has the processor_id()
	sll	t0, 2		# offset in cpu array

	PTR_LA	t1, nlm_cpu_ready # mark CPU ready
	PTR_ADDU t1, t0
	li	t2, 1
	sw	t2, 0(t1)

	PTR_LA	t1, nlm_cpu_unblock
	PTR_ADDU t1, t0
1:	lw	t2, 0(t1)	# wait till unblocked
	beqz	t2, 1b
	nop

	PTR_LA	t1, nlm_next_sp
	PTR_L	sp, 0(t1)
	PTR_LA	t1, nlm_next_gp
	PTR_L	gp, 0(t1)

	PTR_LA	t0, nlm_early_init_secondary
	jalr	t0
	nop

	PTR_LA	t0, smp_bootstrap
	jr	t0
	nop
END(prom_pre_boot_secondary_cpus)

NESTED(nlm_boot_smp_nmi, 0, sp)
	.set push
	.set noat
	.set mips64
	.set noreorder

	/* Clear the  NMI and BEV bits */
	MFC0	k0, CP0_STATUS
	li 	k1, 0xffb7ffff
	and	k0, k0, k1
	MTC0	k0, CP0_STATUS

	PTR_LA  k1, secondary_entry_point
	PTR_L	k0, 0(k1)
	jr	k0
	nop
	.set pop
END(nlm_boot_smp_nmi)
