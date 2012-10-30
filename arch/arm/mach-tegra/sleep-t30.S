/*
 * Copyright (c) 2012, NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/linkage.h>

#include <asm/assembler.h>

#include <mach/iomap.h>

#include "sleep.h"
#include "flowctrl.h"

#define TEGRA30_POWER_HOTPLUG_SHUTDOWN	(1 << 27) /* Hotplug shutdown */

#if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_PM_SLEEP)
/*
 * tegra30_hotplug_shutdown(void)
 *
 * Powergates the current CPU.
 * Should never return.
 */
ENTRY(tegra30_hotplug_shutdown)
	/* Turn off SMP coherency */
	exit_smp r4, r5

	/* Powergate this CPU */
	mov	r0, #TEGRA30_POWER_HOTPLUG_SHUTDOWN
	bl	tegra30_cpu_shutdown
	mov	pc, lr			@ should never get here
ENDPROC(tegra30_hotplug_shutdown)

/*
 * tegra30_cpu_shutdown(unsigned long flags)
 *
 * Puts the current CPU in wait-for-event mode on the flow controller
 * and powergates it -- flags (in R0) indicate the request type.
 * Must never be called for CPU 0.
 *
 * corrupts r0-r4, r12
 */
ENTRY(tegra30_cpu_shutdown)
	cpu_id	r3
	cmp	r3, #0
	moveq	pc, lr		@ Must never be called for CPU 0

	ldr	r12, =TEGRA_FLOW_CTRL_VIRT
	cpu_to_csr_reg r1, r3
	add	r1, r1, r12	@ virtual CSR address for this CPU
	cpu_to_halt_reg r2, r3
	add	r2, r2, r12	@ virtual HALT_EVENTS address for this CPU

	/*
	 * Clear this CPU's "event" and "interrupt" flags and power gate
	 * it when halting but not before it is in the "WFE" state.
	 */
	movw	r12, \
		FLOW_CTRL_CSR_INTR_FLAG | FLOW_CTRL_CSR_EVENT_FLAG | \
		FLOW_CTRL_CSR_ENABLE
	mov	r4, #(1 << 4)
	orr	r12, r12, r4, lsl r3
	str	r12, [r1]

	/* Halt this CPU. */
	mov	r3, #0x400
delay_1:
	subs	r3, r3, #1			@ delay as a part of wfe war.
	bge	delay_1;
	cpsid	a				@ disable imprecise aborts.
	ldr	r3, [r1]			@ read CSR
	str	r3, [r1]			@ clear CSR
	tst	r0, #TEGRA30_POWER_HOTPLUG_SHUTDOWN
	movne	r3, #FLOW_CTRL_WAITEVENT		@ For hotplug
	str	r3, [r2]
	ldr	r0, [r2]
	b	wfe_war

__cpu_reset_again:
	dsb
	.align 5
	wfe					@ CPU should be power gated here
wfe_war:
	b	__cpu_reset_again

	/*
	 * 38 nop's, which fills reset of wfe cache line and
	 * 4 more cachelines with nop
	 */
	.rept 38
	nop
	.endr
	b	.				@ should never get here

ENDPROC(tegra30_cpu_shutdown)
#endif
