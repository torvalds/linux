/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Tony Xie <tony.xie@rock-chips.com>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/memory.h>

.data
/*
 * this code will be copied from
 * ddr to sram for system resumeing.
 * so it is ".data section".
 */
	.align	2

ENTRY(rockchip_slp_cpu_resume)
	setmode	PSR_I_BIT | PSR_F_BIT | SVC_MODE, r1  @ set svc, irqs off
	mrc	p15, 0, r1, c0, c0, 5
	and	r1, r1, #0xf
	cmp	r1, #0
	/* olny cpu0 can continue to run, the others is halt here */
	beq	cpu0run
secondary_loop:
	wfe
	b	secondary_loop
cpu0run:
	ldr	r3, rkpm_bootdata_l2ctlr_f
	cmp	r3, #0
	beq	sp_set
	ldr	r3, rkpm_bootdata_l2ctlr
	mcr	p15, 1, r3, c9, c0, 2
sp_set:
	ldr	sp, rkpm_bootdata_cpusp
	ldr	r1, rkpm_bootdata_cpu_code
	bx	r1
ENDPROC(rockchip_slp_cpu_resume)

/* Parameters filled in by the kernel */

/* Flag for whether to restore L2CTLR on resume */
	.global rkpm_bootdata_l2ctlr_f
rkpm_bootdata_l2ctlr_f:
	.long 0

/* Saved L2CTLR to restore on resume */
	.global rkpm_bootdata_l2ctlr
rkpm_bootdata_l2ctlr:
	.long 0

/* CPU resume SP addr */
	.globl rkpm_bootdata_cpusp
rkpm_bootdata_cpusp:
	.long 0

/* CPU resume function (physical address) */
	.globl rkpm_bootdata_cpu_code
rkpm_bootdata_cpu_code:
	.long 0

ENTRY(rk3288_bootram_sz)
        .word   . - rockchip_slp_cpu_resume
