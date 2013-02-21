/*
 * Copyright (c) 2010-2012, NVIDIA Corporation. All rights reserved.
 * Copyright (c) 2011, Google, Inc.
 *
 * Author: Colin Cross <ccross@android.com>
 *         Gary King <gking@nvidia.com>
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
#include <asm/proc-fns.h>
#include <asm/cp15.h>

#include "sleep.h"
#include "flowctrl.h"

#if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_PM_SLEEP)
/*
 * tegra20_hotplug_shutdown(void)
 *
 * puts the current cpu in reset
 * should never return
 */
ENTRY(tegra20_hotplug_shutdown)
	/* Put this CPU down */
	cpu_id	r0
	bl	tegra20_cpu_shutdown
	mov	pc, lr			@ should never get here
ENDPROC(tegra20_hotplug_shutdown)

/*
 * tegra20_cpu_shutdown(int cpu)
 *
 * r0 is cpu to reset
 *
 * puts the specified CPU in wait-for-event mode on the flow controller
 * and puts the CPU in reset
 * can be called on the current cpu or another cpu
 * if called on the current cpu, does not return
 * MUST NOT BE CALLED FOR CPU 0.
 *
 * corrupts r0-r3, r12
 */
ENTRY(tegra20_cpu_shutdown)
	cmp	r0, #0
	moveq	pc, lr			@ must not be called for CPU 0
	mov32	r1, TEGRA_PMC_VIRT + PMC_SCRATCH41
	mov	r12, #CPU_RESETTABLE
	str	r12, [r1]

	cpu_to_halt_reg r1, r0
	ldr	r3, =TEGRA_FLOW_CTRL_VIRT
	mov	r2, #FLOW_CTRL_WAITEVENT | FLOW_CTRL_JTAG_RESUME
	str	r2, [r3, r1]		@ put flow controller in wait event mode
	ldr	r2, [r3, r1]
	isb
	dsb
	movw	r1, 0x1011
	mov	r1, r1, lsl r0
	ldr	r3, =TEGRA_CLK_RESET_VIRT
	str	r1, [r3, #0x340]	@ put slave CPU in reset
	isb
	dsb
	cpu_id	r3
	cmp	r3, r0
	beq	.
	mov	pc, lr
ENDPROC(tegra20_cpu_shutdown)
#endif

#ifdef CONFIG_PM_SLEEP
/*
 * tegra_pen_lock
 *
 * spinlock implementation with no atomic test-and-set and no coherence
 * using Peterson's algorithm on strongly-ordered registers
 * used to synchronize a cpu waking up from wfi with entering lp2 on idle
 *
 * The reference link of Peterson's algorithm:
 * http://en.wikipedia.org/wiki/Peterson's_algorithm
 *
 * SCRATCH37 = r1 = !turn (inverted from Peterson's algorithm)
 * on cpu 0:
 * r2 = flag[0] (in SCRATCH38)
 * r3 = flag[1] (in SCRATCH39)
 * on cpu1:
 * r2 = flag[1] (in SCRATCH39)
 * r3 = flag[0] (in SCRATCH38)
 *
 * must be called with MMU on
 * corrupts r0-r3, r12
 */
ENTRY(tegra_pen_lock)
	mov32	r3, TEGRA_PMC_VIRT
	cpu_id	r0
	add	r1, r3, #PMC_SCRATCH37
	cmp	r0, #0
	addeq	r2, r3, #PMC_SCRATCH38
	addeq	r3, r3, #PMC_SCRATCH39
	addne	r2, r3, #PMC_SCRATCH39
	addne	r3, r3, #PMC_SCRATCH38

	mov	r12, #1
	str	r12, [r2]		@ flag[cpu] = 1
	dsb
	str	r12, [r1]		@ !turn = cpu
1:	dsb
	ldr	r12, [r3]
	cmp	r12, #1			@ flag[!cpu] == 1?
	ldreq	r12, [r1]
	cmpeq	r12, r0			@ !turn == cpu?
	beq	1b			@ while !turn == cpu && flag[!cpu] == 1

	mov	pc, lr			@ locked
ENDPROC(tegra_pen_lock)

ENTRY(tegra_pen_unlock)
	dsb
	mov32	r3, TEGRA_PMC_VIRT
	cpu_id	r0
	cmp	r0, #0
	addeq	r2, r3, #PMC_SCRATCH38
	addne	r2, r3, #PMC_SCRATCH39
	mov	r12, #0
	str	r12, [r2]
	mov     pc, lr
ENDPROC(tegra_pen_unlock)

/*
 * tegra20_cpu_clear_resettable(void)
 *
 * Called to clear the "resettable soon" flag in PMC_SCRATCH41 when
 * it is expected that the secondary CPU will be idle soon.
 */
ENTRY(tegra20_cpu_clear_resettable)
	mov32	r1, TEGRA_PMC_VIRT + PMC_SCRATCH41
	mov	r12, #CPU_NOT_RESETTABLE
	str	r12, [r1]
	mov	pc, lr
ENDPROC(tegra20_cpu_clear_resettable)

/*
 * tegra20_cpu_set_resettable_soon(void)
 *
 * Called to set the "resettable soon" flag in PMC_SCRATCH41 when
 * it is expected that the secondary CPU will be idle soon.
 */
ENTRY(tegra20_cpu_set_resettable_soon)
	mov32	r1, TEGRA_PMC_VIRT + PMC_SCRATCH41
	mov	r12, #CPU_RESETTABLE_SOON
	str	r12, [r1]
	mov	pc, lr
ENDPROC(tegra20_cpu_set_resettable_soon)

/*
 * tegra20_cpu_is_resettable_soon(void)
 *
 * Returns true if the "resettable soon" flag in PMC_SCRATCH41 has been
 * set because it is expected that the secondary CPU will be idle soon.
 */
ENTRY(tegra20_cpu_is_resettable_soon)
	mov32	r1, TEGRA_PMC_VIRT + PMC_SCRATCH41
	ldr	r12, [r1]
	cmp	r12, #CPU_RESETTABLE_SOON
	moveq	r0, #1
	movne	r0, #0
	mov	pc, lr
ENDPROC(tegra20_cpu_is_resettable_soon)

/*
 * tegra20_sleep_cpu_secondary_finish(unsigned long v2p)
 *
 * Enters WFI on secondary CPU by exiting coherency.
 */
ENTRY(tegra20_sleep_cpu_secondary_finish)
	stmfd	sp!, {r4-r11, lr}

	mrc	p15, 0, r11, c1, c0, 1  @ save actlr before exiting coherency

	/* Flush and disable the L1 data cache */
	bl	tegra_disable_clean_inv_dcache

	mov32	r0, TEGRA_PMC_VIRT + PMC_SCRATCH41
	mov	r3, #CPU_RESETTABLE
	str	r3, [r0]

	bl	cpu_do_idle

	/*
	 * cpu may be reset while in wfi, which will return through
	 * tegra_resume to cpu_resume
	 * or interrupt may wake wfi, which will return here
	 * cpu state is unchanged - MMU is on, cache is on, coherency
	 * is off, and the data cache is off
	 *
	 * r11 contains the original actlr
	 */

	bl	tegra_pen_lock

	mov32	r3, TEGRA_PMC_VIRT
	add	r0, r3, #PMC_SCRATCH41
	mov	r3, #CPU_NOT_RESETTABLE
	str	r3, [r0]

	bl	tegra_pen_unlock

	/* Re-enable the data cache */
	mrc	p15, 0, r10, c1, c0, 0
	orr	r10, r10, #CR_C
	mcr	p15, 0, r10, c1, c0, 0
	isb

	mcr	p15, 0, r11, c1, c0, 1	@ reenable coherency

	/* Invalidate the TLBs & BTAC */
	mov	r1, #0
	mcr	p15, 0, r1, c8, c3, 0	@ invalidate shared TLBs
	mcr	p15, 0, r1, c7, c1, 6	@ invalidate shared BTAC
	dsb
	isb

	/* the cpu was running with coherency disabled,
	 * caches may be out of date */
	bl	v7_flush_kern_cache_louis

	ldmfd	sp!, {r4 - r11, pc}
ENDPROC(tegra20_sleep_cpu_secondary_finish)

/*
 * tegra20_tear_down_cpu
 *
 * Switches the CPU cluster to PLL-P and enters sleep.
 */
ENTRY(tegra20_tear_down_cpu)
	bl	tegra_switch_cpu_to_pllp
	b	tegra20_enter_sleep
ENDPROC(tegra20_tear_down_cpu)

/*
 * tegra20_enter_sleep
 *
 * uses flow controller to enter sleep state
 * executes from IRAM with SDRAM in selfrefresh when target state is LP0 or LP1
 * executes from SDRAM with target state is LP2
 */
tegra20_enter_sleep:
	mov32   r6, TEGRA_FLOW_CTRL_BASE

	mov     r0, #FLOW_CTRL_WAIT_FOR_INTERRUPT
	orr	r0, r0, #FLOW_CTRL_HALT_CPU_IRQ | FLOW_CTRL_HALT_CPU_FIQ
	cpu_id	r1
	cpu_to_halt_reg r1, r1
	str	r0, [r6, r1]
	dsb
	ldr	r0, [r6, r1] /* memory barrier */

halted:
	dsb
	wfe	/* CPU should be power gated here */
	isb
	b	halted

#endif
