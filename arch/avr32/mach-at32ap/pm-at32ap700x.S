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
#include <mach/pm.h>

#include "pm.h"
#include "sdramc.h"

/* Same as 0xfff00000 but fits in a 21 bit signed immediate */
#define PM_BASE	-0x100000

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

#ifdef CONFIG_PM
	.section .init.text, "ax", @progbits

	.global	pm_exception
	.type	pm_exception, @function
pm_exception:
	/*
	 * Exceptions are masked when we switch to this handler, so
	 * we'll only get "unrecoverable" exceptions (offset 0.)
	 */
	sub	r12, pc, . - .Lpanic_msg
	lddpc	pc, .Lpanic_addr

	.align	2
.Lpanic_addr:
	.long	panic
.Lpanic_msg:
	.asciz	"Unrecoverable exception during suspend\n"
	.size	pm_exception, . - pm_exception

	.global	pm_irq0
	.type	pm_irq0, @function
pm_irq0:
	/* Disable interrupts and return after the sleep instruction */
	mfsr	r9, SYSREG_RSR_INT0
	mtsr	SYSREG_RAR_INT0, r8
	sbr	r9, SYSREG_GM_OFFSET
	mtsr	SYSREG_RSR_INT0, r9
	rete

	/*
	 * void cpu_enter_standby(unsigned long sdramc_base)
	 *
	 * Enter PM_SUSPEND_STANDBY mode. At this point, all drivers
	 * are suspended and interrupts are disabled. Interrupts
	 * marked as 'wakeup' event sources may still come along and
	 * get us out of here.
	 *
	 * The SDRAM will be put into self-refresh mode (which does
	 * not require a clock from the CPU), and the CPU will be put
	 * into "frozen" mode (HSB bus stopped). The SDRAM controller
	 * will automatically bring the SDRAM into normal mode on the
	 * first access, and the power manager will automatically
	 * start the HSB and CPU clocks upon a wakeup event.
	 *
	 * This code uses the same "skip sleep" technique as above.
	 * It is very important that we jump directly to
	 * cpu_after_sleep after the sleep instruction since that's
	 * where we'll end up if the interrupt handler decides that we
	 * need to skip the sleep instruction.
	 */
	.global	pm_standby
	.type	pm_standby, @function
pm_standby:
	/*
	 * interrupts are already masked at this point, and EVBA
	 * points to pm_exception above.
	 */
	ld.w	r10, r12[SDRAMC_LPR]
	sub	r8, pc, . - 1f		/* return address for irq handler */
	mov	r11, SDRAMC_LPR_LPCB_SELF_RFR
	bfins	r10, r11, 0, 2		/* LPCB <- self Refresh */
	sync	0			/* flush write buffer */
	st.w	r12[SDRAMC_LPR], r11	/* put SDRAM in self-refresh mode */
	ld.w	r11, r12[SDRAMC_LPR]
	unmask_interrupts
	sleep	CPU_SLEEP_FROZEN
1:	mask_interrupts
	retal	r12
	.size	pm_standby, . - pm_standby

	.global	pm_suspend_to_ram
	.type	pm_suspend_to_ram, @function
pm_suspend_to_ram:
	/*
	 * interrupts are already masked at this point, and EVBA
	 * points to pm_exception above.
	 */
	mov	r11, 0
	cache	r11[2], 8		/* clean all dcache lines */
	sync	0			/* flush write buffer */
	ld.w	r10, r12[SDRAMC_LPR]
	sub	r8, pc, . - 1f		/* return address for irq handler */
	mov	r11, SDRAMC_LPR_LPCB_SELF_RFR
	bfins	r10, r11, 0, 2		/* LPCB <- self refresh */
	st.w	r12[SDRAMC_LPR], r10	/* put SDRAM in self-refresh mode */
	ld.w	r11, r12[SDRAMC_LPR]

	unmask_interrupts
	sleep	CPU_SLEEP_STOP
1:	mask_interrupts

	retal	r12
	.size	pm_suspend_to_ram, . - pm_suspend_to_ram

	.global	pm_sram_end
	.type	pm_sram_end, @function
pm_sram_end:
	.size	pm_sram_end, 0

#endif /* CONFIG_PM */
