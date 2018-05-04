/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 Chen-Yu Tsai
 * Copyright (c) 2018 Bootlin
 *
 * Chen-Yu Tsai <wens@csie.org>
 * Mylène Josserand <mylene.josserand@bootlin.com>
 *
 * SMP support for sunxi based systems with Cortex A7/A15
 *
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/cputype.h>

ENTRY(sunxi_mc_smp_cluster_cache_enable)
	.arch	armv7-a
	/*
	 * Enable cluster-level coherency, in preparation for turning on the MMU.
	 *
	 * Also enable regional clock gating and L2 data latency settings for
	 * Cortex-A15. These settings are from the vendor kernel.
	 */
	mrc	p15, 0, r1, c0, c0, 0
	movw	r2, #(ARM_CPU_PART_MASK & 0xffff)
	movt	r2, #(ARM_CPU_PART_MASK >> 16)
	and	r1, r1, r2
	movw	r2, #(ARM_CPU_PART_CORTEX_A15 & 0xffff)
	movt	r2, #(ARM_CPU_PART_CORTEX_A15 >> 16)
	cmp	r1, r2
	bne	not_a15

	/* The following is Cortex-A15 specific */

	/* ACTLR2: Enable CPU regional clock gates */
	mrc p15, 1, r1, c15, c0, 4
	orr r1, r1, #(0x1 << 31)
	mcr p15, 1, r1, c15, c0, 4

	/* L2ACTLR */
	mrc p15, 1, r1, c15, c0, 0
	/* Enable L2, GIC, and Timer regional clock gates */
	orr r1, r1, #(0x1 << 26)
	/* Disable clean/evict from being pushed to external */
	orr r1, r1, #(0x1<<3)
	mcr p15, 1, r1, c15, c0, 0

	/* L2CTRL: L2 data RAM latency */
	mrc p15, 1, r1, c9, c0, 2
	bic r1, r1, #(0x7 << 0)
	orr r1, r1, #(0x3 << 0)
	mcr p15, 1, r1, c9, c0, 2

	/* End of Cortex-A15 specific setup */
	not_a15:

	/* Get value of sunxi_mc_smp_first_comer */
	adr	r1, first
	ldr	r0, [r1]
	ldr	r0, [r1, r0]

	/* Skip cci_enable_port_for_self if not first comer */
	cmp	r0, #0
	bxeq	lr
	b	cci_enable_port_for_self

	.align 2
	first: .word sunxi_mc_smp_first_comer - .
ENDPROC(sunxi_mc_smp_cluster_cache_enable)

ENTRY(sunxi_mc_smp_secondary_startup)
	bl	sunxi_mc_smp_cluster_cache_enable
	b	secondary_startup
ENDPROC(sunxi_mc_smp_secondary_startup)

ENTRY(sunxi_mc_smp_resume)
	bl	sunxi_mc_smp_cluster_cache_enable
	b	cpu_resume
ENDPROC(sunxi_mc_smp_resume)
