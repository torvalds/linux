/*
 * Low-level Power Management code.
 *
 * Copyright (C) 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/arch/pm.h>

	.section .bss, "wa", @nobits
	.global	disable_idle_sleep
	.type	disable_idle_sleep, @object
disable_idle_sleep:
	.int	4
	.size	disable_idle_sleep, . - disable_idle_sleep

	/* Keep this close to the irq handlers */
	.section .irq.text, "ax", @progbits

	/*
	 * void cpu_enter_idle(void)
	 *
	 * Put the CPU into "idle" mode, in which it will consume
	 * significantly less power.
	 *
	 * If an interrupt comes along in the window between
	 * unmask_interrupts and the sleep instruction below, the
	 * interrupt code will adjust the return address so that we
	 * never execute the sleep instruction. This is required
	 * because the AP7000 doesn't unmask interrupts when entering
	 * sleep modes; later CPUs may not need this workaround.
	 */
	.global	cpu_enter_idle
	.type	cpu_enter_idle, @function
cpu_enter_idle:
	mask_interrupts
	get_thread_info r8
	ld.w	r9, r8[TI_flags]
	bld	r9, TIF_NEED_RESCHED
	brcs	.Lret_from_sleep
	sbr	r9, TIF_CPU_GOING_TO_SLEEP
	st.w	r8[TI_flags], r9
	unmask_interrupts
	sleep	CPU_SLEEP_IDLE
	.size	cpu_idle_sleep, . - cpu_idle_sleep

	/*
	 * Common return path for PM functions that don't run from
	 * SRAM.
	 */
	.global cpu_idle_skip_sleep
	.type	cpu_idle_skip_sleep, @function
cpu_idle_skip_sleep:
	mask_interrupts
	ld.w	r9, r8[TI_flags]
	cbr	r9, TIF_CPU_GOING_TO_SLEEP
	st.w	r8[TI_flags], r9
.Lret_from_sleep:
	unmask_interrupts
	retal	r12
	.size	cpu_idle_skip_sleep, . - cpu_idle_skip_sleep
