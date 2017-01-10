/*
 * Rockchip rk3288 resume code
 *
 * This code is intended to be linked into the embedded resume binary
 * for the rk3288 SoC
 *
 * Copyright (c) 2014 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>

#include "rk3288_resume.h"
#include "rk3288_resume_embedded.h"

#define INIT_CPSR	(PSR_I_BIT | PSR_F_BIT | SVC_MODE)

static __noreturn void rk3288_resume(void);

/* Parameters of early board initialization in SPL */
struct rk3288_resume_params rk3288_resume_params
		__attribute__((section(".resume_params"))) = {
	.resume_loc	= rk3288_resume,
};

/**
 * rk3288_resume_c - Main C entry point for rk3288 at resume time
 *
 * This function is called by rk3288_resume() and does the brunt of
 * any resume processing.  After it's done it will call the kernel's
 * cpu_resume() function (which it finds through its params structure).
 *
 * At the time this function is called:
 * - We know we're on CPU0.
 * - Interrupts are disabled.
 * - We've got a stack.
 * - The cache is turned off, so all addresses are physical.
 * - SDRAM hasn't been restored yet (if it was off).
 *
 * WARNING: This code, the stack and the params structure are all sitting in
 * PMU SRAM.  If you try to write to that memory using an 8-bit access (or even
 * 16-bit) you'll get an imprecise data abort and it will be very hard to debug.
 * Keep everything in here as 32-bit wide and aligned.  YOU'VE BEEN WARNED.
 */
static void __noreturn rk3288_resume_c(void)
{
#ifdef CONFIG_ARM_ERRATA_818325
	u32 val = 0;

	asm("mrc p15, 0, %0, c15, c0, 1" : "=r" (val));
	val |= BIT(12);
	asm("mcr p15, 0, %0, c15, c0, 1" : : "r" (val));
#endif

	if (rk3288_resume_params.l2ctlr_f)
		asm("mcr p15, 1, %0, c9, c0, 2" : :
			"r" (rk3288_resume_params.l2ctlr));

	if (rk3288_resume_params.ddr_resume_f)
		rk3288_ddr_resume_early(&rk3288_resume_params.ddr_save_data);

	rk3288_resume_params.cpu_resume();
}

/**
 * rk3288_resume - First entry point for rk3288 at resume time
 *
 * A pointer to this function is stored in rk3288_resume_params.  The
 * kernel uses the pointer in that structure to find this function and
 * to put its (physical) address in a location that it will get jumped
 * to at resume time.
 *
 * There is no stack at the time this function is called, so this
 * function is in charge of setting it up.  We get to a function with
 * a normal stack pointer ASAP.
 */
static void __naked __noreturn rk3288_resume(void)
{
	/* Make sure we're on CPU0, no IRQs and get a stack setup */
	asm volatile (
			"msr	cpsr_cxf, %0\n"

			/* Only cpu0 continues to run, the others halt here */
			"mrc	p15, 0, r1, c0, c0, 5\n"
			"and	r1, r1, #0xf\n"
			"cmp	r1, #0\n"
			"beq	cpu0run\n"
		"secondary_loop:\n"
			"wfe\n"
			"b	secondary_loop\n"

		"cpu0run:\n"
			"mov	sp, %1\n"
		:
		: "i" (INIT_CPSR), "r" (&__stack_start)
		: "cc", "r1", "sp");

	/* Now get into a normal function that can use a stack */
	rk3288_resume_c();
}
