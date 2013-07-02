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
#include <linux/init.h>

#include <asm/cache.h>
#include <asm/asm-offsets.h>
#include <asm/hardware/cache-l2x0.h>

#include "flowctrl.h"
#include "iomap.h"
#include "reset.h"
#include "sleep.h"

#define APB_MISC_GP_HIDREV	0x804
#define PMC_SCRATCH41	0x140

#define RESET_DATA(x)	((TEGRA_RESET_##x)*4)

#ifdef CONFIG_PM_SLEEP
/*
 *	tegra_resume
 *
 *	  CPU boot vector when restarting the a CPU following
 *	  an LP2 transition. Also branched to by LP0 and LP1 resume after
 *	  re-enabling sdram.
 */
ENTRY(tegra_resume)
	bl	v7_invalidate_l1

	cpu_id	r0
	cmp	r0, #0				@ CPU0?
 THUMB(	it	ne )
	bne	cpu_resume			@ no

#ifdef CONFIG_ARCH_TEGRA_3x_SOC
	/* Are we on Tegra20? */
	mov32	r6, TEGRA_APB_MISC_BASE
	ldr	r0, [r6, #APB_MISC_GP_HIDREV]
	and	r0, r0, #0xff00
	cmp	r0, #(0x20 << 8)
	beq	1f				@ Yes
	/* Clear the flow controller flags for this CPU. */
	mov32	r2, TEGRA_FLOW_CTRL_BASE + FLOW_CTRL_CPU0_CSR	@ CPU0 CSR
	ldr	r1, [r2]
	/* Clear event & intr flag */
	orr	r1, r1, \
		#FLOW_CTRL_CSR_INTR_FLAG | FLOW_CTRL_CSR_EVENT_FLAG
	movw	r0, #0x0FFD	@ enable, cluster_switch, immed, & bitmaps
	bic	r1, r1, r0
	str	r1, [r2]
1:
#endif

#ifdef CONFIG_HAVE_ARM_SCU
	/* enable SCU */
	mov32	r0, TEGRA_ARM_PERIF_BASE
	ldr	r1, [r0]
	orr	r1, r1, #1
	str	r1, [r0]
#endif

	/* L2 cache resume & re-enable */
	l2_cache_resume r0, r1, r2, l2x0_saved_regs_addr

	b	cpu_resume
ENDPROC(tegra_resume)
#endif

#ifdef CONFIG_CACHE_L2X0
	.globl	l2x0_saved_regs_addr
l2x0_saved_regs_addr:
	.long	0
#endif

	.align L1_CACHE_SHIFT
ENTRY(__tegra_cpu_reset_handler_start)

/*
 * __tegra_cpu_reset_handler:
 *
 * Common handler for all CPU reset events.
 *
 * Register usage within the reset handler:
 *
 *      Others: scratch
 *      R6  = SoC ID << 8
 *      R7  = CPU present (to the OS) mask
 *      R8  = CPU in LP1 state mask
 *      R9  = CPU in LP2 state mask
 *      R10 = CPU number
 *      R11 = CPU mask
 *      R12 = pointer to reset handler data
 *
 * NOTE: This code is copied to IRAM. All code and data accesses
 *       must be position-independent.
 */

	.align L1_CACHE_SHIFT
ENTRY(__tegra_cpu_reset_handler)

	cpsid	aif, 0x13			@ SVC mode, interrupts disabled

	mov32	r6, TEGRA_APB_MISC_BASE
	ldr	r6, [r6, #APB_MISC_GP_HIDREV]
	and	r6, r6, #0xff00
#ifdef CONFIG_ARCH_TEGRA_2x_SOC
t20_check:
	cmp	r6, #(0x20 << 8)
	bne	after_t20_check
t20_errata:
	# Tegra20 is a Cortex-A9 r1p1
	mrc	p15, 0, r0, c1, c0, 0   @ read system control register
	orr	r0, r0, #1 << 14        @ erratum 716044
	mcr	p15, 0, r0, c1, c0, 0   @ write system control register
	mrc	p15, 0, r0, c15, c0, 1  @ read diagnostic register
	orr	r0, r0, #1 << 4         @ erratum 742230
	orr	r0, r0, #1 << 11        @ erratum 751472
	mcr	p15, 0, r0, c15, c0, 1  @ write diagnostic register
	b	after_errata
after_t20_check:
#endif
#ifdef CONFIG_ARCH_TEGRA_3x_SOC
t30_check:
	cmp	r6, #(0x30 << 8)
	bne	after_t30_check
t30_errata:
	# Tegra30 is a Cortex-A9 r2p9
	mrc	p15, 0, r0, c15, c0, 1  @ read diagnostic register
	orr	r0, r0, #1 << 6         @ erratum 743622
	orr	r0, r0, #1 << 11        @ erratum 751472
	mcr	p15, 0, r0, c15, c0, 1  @ write diagnostic register
	b	after_errata
after_t30_check:
#endif
after_errata:
	mrc	p15, 0, r10, c0, c0, 5		@ MPIDR
	and	r10, r10, #0x3			@ R10 = CPU number
	mov	r11, #1
	mov	r11, r11, lsl r10  		@ R11 = CPU mask
	adr	r12, __tegra_cpu_reset_handler_data

#ifdef CONFIG_SMP
	/* Does the OS know about this CPU? */
	ldr	r7, [r12, #RESET_DATA(MASK_PRESENT)]
	tst	r7, r11 			@ if !present
	bleq	__die				@ CPU not present (to OS)
#endif

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
	/* Are we on Tegra20? */
	cmp	r6, #(0x20 << 8)
	bne	1f
	/* If not CPU0, don't let CPU0 reset CPU1 now that CPU1 is coming up. */
	mov32	r5, TEGRA_PMC_BASE
	mov	r0, #0
	cmp	r10, #0
	strne	r0, [r5, #PMC_SCRATCH41]
1:
#endif

	/* Waking up from LP2? */
	ldr	r9, [r12, #RESET_DATA(MASK_LP2)]
	tst	r9, r11				@ if in_lp2
	beq	__is_not_lp2
	ldr	lr, [r12, #RESET_DATA(STARTUP_LP2)]
	cmp	lr, #0
	bleq	__die				@ no LP2 startup handler
	bx	lr

__is_not_lp2:

#ifdef CONFIG_SMP
	/*
	 * Can only be secondary boot (initial or hotplug) but CPU 0
	 * cannot be here.
	 */
	cmp	r10, #0
	bleq	__die				@ CPU0 cannot be here
	ldr	lr, [r12, #RESET_DATA(STARTUP_SECONDARY)]
	cmp	lr, #0
	bleq	__die				@ no secondary startup handler
	bx	lr
#endif

/*
 * We don't know why the CPU reset. Just kill it.
 * The LR register will contain the address we died at + 4.
 */

__die:
	sub	lr, lr, #4
	mov32	r7, TEGRA_PMC_BASE
	str	lr, [r7, #PMC_SCRATCH41]

	mov32	r7, TEGRA_CLK_RESET_BASE

	/* Are we on Tegra20? */
	mov32	r6, TEGRA_APB_MISC_BASE
	ldr	r0, [r6, #APB_MISC_GP_HIDREV]
	and	r0, r0, #0xff00
	cmp	r0, #(0x20 << 8)
	bne	1f

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
	mov32	r0, 0x1111
	mov	r1, r0, lsl r10
	str	r1, [r7, #0x340]		@ CLK_RST_CPU_CMPLX_SET
#endif
1:
#ifdef CONFIG_ARCH_TEGRA_3x_SOC
	mov32	r6, TEGRA_FLOW_CTRL_BASE

	cmp	r10, #0
	moveq	r1, #FLOW_CTRL_HALT_CPU0_EVENTS
	moveq	r2, #FLOW_CTRL_CPU0_CSR
	movne	r1, r10, lsl #3
	addne	r2, r1, #(FLOW_CTRL_CPU1_CSR-8)
	addne	r1, r1, #(FLOW_CTRL_HALT_CPU1_EVENTS-8)

	/* Clear CPU "event" and "interrupt" flags and power gate
	   it when halting but not before it is in the "WFI" state. */
	ldr	r0, [r6, +r2]
	orr	r0, r0, #FLOW_CTRL_CSR_INTR_FLAG | FLOW_CTRL_CSR_EVENT_FLAG
	orr	r0, r0, #FLOW_CTRL_CSR_ENABLE
	str	r0, [r6, +r2]

	/* Unconditionally halt this CPU */
	mov	r0, #FLOW_CTRL_WAITEVENT
	str	r0, [r6, +r1]
	ldr	r0, [r6, +r1]			@ memory barrier

	dsb
	isb
	wfi					@ CPU should be power gated here

	/* If the CPU didn't power gate above just kill it's clock. */

	mov	r0, r11, lsl #8
	str	r0, [r7, #348]			@ CLK_CPU_CMPLX_SET
#endif

	/* If the CPU still isn't dead, just spin here. */
	b	.
ENDPROC(__tegra_cpu_reset_handler)

	.align L1_CACHE_SHIFT
	.type	__tegra_cpu_reset_handler_data, %object
	.globl	__tegra_cpu_reset_handler_data
__tegra_cpu_reset_handler_data:
	.rept	TEGRA_RESET_DATA_SIZE
	.long	0
	.endr
	.align L1_CACHE_SHIFT

ENTRY(__tegra_cpu_reset_handler_end)
