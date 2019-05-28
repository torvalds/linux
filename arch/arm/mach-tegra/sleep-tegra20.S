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

#include <soc/tegra/flowctrl.h>

#include <asm/assembler.h>
#include <asm/proc-fns.h>
#include <asm/cp15.h>
#include <asm/cache.h>

#include "irammap.h"
#include "reset.h"
#include "sleep.h"

#define EMC_CFG				0xc
#define EMC_ADR_CFG			0x10
#define EMC_NOP				0xdc
#define EMC_SELF_REF			0xe0
#define EMC_REQ_CTRL			0x2b0
#define EMC_EMC_STATUS			0x2b4

#define CLK_RESET_CCLK_BURST		0x20
#define CLK_RESET_CCLK_DIVIDER		0x24
#define CLK_RESET_SCLK_BURST		0x28
#define CLK_RESET_SCLK_DIVIDER		0x2c
#define CLK_RESET_PLLC_BASE		0x80
#define CLK_RESET_PLLM_BASE		0x90
#define CLK_RESET_PLLP_BASE		0xa0

#define APB_MISC_XM2CFGCPADCTRL		0x8c8
#define APB_MISC_XM2CFGDPADCTRL		0x8cc
#define APB_MISC_XM2CLKCFGPADCTRL	0x8d0
#define APB_MISC_XM2COMPPADCTRL		0x8d4
#define APB_MISC_XM2VTTGENPADCTRL	0x8d8
#define APB_MISC_XM2CFGCPADCTRL2	0x8e4
#define APB_MISC_XM2CFGDPADCTRL2	0x8e8

#define __tegra20_cpu1_resettable_status_offset \
	(__tegra_cpu_reset_handler_data_offset + RESET_DATA(RESETTABLE_STATUS))

.macro pll_enable, rd, r_car_base, pll_base
	ldr	\rd, [\r_car_base, #\pll_base]
	tst	\rd, #(1 << 30)
	orreq	\rd, \rd, #(1 << 30)
	streq	\rd, [\r_car_base, #\pll_base]
.endm

.macro emc_device_mask, rd, base
	ldr	\rd, [\base, #EMC_ADR_CFG]
	tst	\rd, #(0x3 << 24)
	moveq	\rd, #(0x1 << 8)		@ just 1 device
	movne	\rd, #(0x3 << 8)		@ 2 devices
.endm

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
	ret	lr			@ should never get here
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
	reteq	lr			@ must not be called for CPU 0
	mov32	r1, TEGRA_IRAM_RESET_BASE_VIRT
	ldr	r2, =__tegra20_cpu1_resettable_status_offset
	mov	r12, #CPU_RESETTABLE
	strb	r12, [r1, r2]

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
	ret	lr
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

	ret	lr			@ locked
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
	ret     lr
ENDPROC(tegra_pen_unlock)

/*
 * tegra20_cpu_clear_resettable(void)
 *
 * Called to clear the "resettable soon" flag in IRAM variable when
 * it is expected that the secondary CPU will be idle soon.
 */
ENTRY(tegra20_cpu_clear_resettable)
	mov32	r1, TEGRA_IRAM_RESET_BASE_VIRT
	ldr	r2, =__tegra20_cpu1_resettable_status_offset
	mov	r12, #CPU_NOT_RESETTABLE
	strb	r12, [r1, r2]
	ret	lr
ENDPROC(tegra20_cpu_clear_resettable)

/*
 * tegra20_cpu_set_resettable_soon(void)
 *
 * Called to set the "resettable soon" flag in IRAM variable when
 * it is expected that the secondary CPU will be idle soon.
 */
ENTRY(tegra20_cpu_set_resettable_soon)
	mov32	r1, TEGRA_IRAM_RESET_BASE_VIRT
	ldr	r2, =__tegra20_cpu1_resettable_status_offset
	mov	r12, #CPU_RESETTABLE_SOON
	strb	r12, [r1, r2]
	ret	lr
ENDPROC(tegra20_cpu_set_resettable_soon)

/*
 * tegra20_cpu_is_resettable_soon(void)
 *
 * Returns true if the "resettable soon" flag in IRAM variable has been
 * set because it is expected that the secondary CPU will be idle soon.
 */
ENTRY(tegra20_cpu_is_resettable_soon)
	mov32	r1, TEGRA_IRAM_RESET_BASE_VIRT
	ldr	r2, =__tegra20_cpu1_resettable_status_offset
	ldrb	r12, [r1, r2]
	cmp	r12, #CPU_RESETTABLE_SOON
	moveq	r0, #1
	movne	r0, #0
	ret	lr
ENDPROC(tegra20_cpu_is_resettable_soon)

/*
 * tegra20_sleep_core_finish(unsigned long v2p)
 *
 * Enters suspend in LP0 or LP1 by turning off the mmu and jumping to
 * tegra20_tear_down_core in IRAM
 */
ENTRY(tegra20_sleep_core_finish)
	mov     r4, r0
	/* Flush, disable the L1 data cache and exit SMP */
	mov     r0, #TEGRA_FLUSH_CACHE_ALL
	bl	tegra_disable_clean_inv_dcache
	mov     r0, r4

	mov32	r3, tegra_shut_off_mmu
	add	r3, r3, r0

	mov32	r0, tegra20_tear_down_core
	mov32	r1, tegra20_iram_start
	sub	r0, r0, r1
	mov32	r1, TEGRA_IRAM_LPx_RESUME_AREA
	add	r0, r0, r1

	ret	r3
ENDPROC(tegra20_sleep_core_finish)

/*
 * tegra20_sleep_cpu_secondary_finish(unsigned long v2p)
 *
 * Enters WFI on secondary CPU by exiting coherency.
 */
ENTRY(tegra20_sleep_cpu_secondary_finish)
	stmfd	sp!, {r4-r11, lr}

	mrc	p15, 0, r11, c1, c0, 1  @ save actlr before exiting coherency

	/* Flush and disable the L1 data cache */
	mov	r0, #TEGRA_FLUSH_CACHE_LOUIS
	bl	tegra_disable_clean_inv_dcache

	mov32	r0, TEGRA_IRAM_RESET_BASE_VIRT
	ldr	r4, =__tegra20_cpu1_resettable_status_offset
	mov	r3, #CPU_RESETTABLE
	strb	r3, [r0, r4]

	bl	tegra_cpu_do_idle

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

	mov32	r0, TEGRA_IRAM_RESET_BASE_VIRT
	ldr	r4, =__tegra20_cpu1_resettable_status_offset
	mov	r3, #CPU_NOT_RESETTABLE
	strb	r3, [r0, r4]

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

/* START OF ROUTINES COPIED TO IRAM */
	.align L1_CACHE_SHIFT
	.globl tegra20_iram_start
tegra20_iram_start:

/*
 * tegra20_lp1_reset
 *
 * reset vector for LP1 restore; copied into IRAM during suspend.
 * Brings the system back up to a safe staring point (SDRAM out of
 * self-refresh, PLLC, PLLM and PLLP reenabled, CPU running on PLLP,
 * system clock running on the same PLL that it suspended at), and
 * jumps to tegra_resume to restore virtual addressing and PLLX.
 * The physical address of tegra_resume expected to be stored in
 * PMC_SCRATCH41.
 *
 * NOTE: THIS *MUST* BE RELOCATED TO TEGRA_IRAM_LPx_RESUME_AREA.
 */
ENTRY(tegra20_lp1_reset)
	/*
	 * The CPU and system bus are running at 32KHz and executing from
	 * IRAM when this code is executed; immediately switch to CLKM and
	 * enable PLLM, PLLP, PLLC.
	 */
	mov32	r0, TEGRA_CLK_RESET_BASE

	mov	r1, #(1 << 28)
	str	r1, [r0, #CLK_RESET_SCLK_BURST]
	str	r1, [r0, #CLK_RESET_CCLK_BURST]
	mov	r1, #0
	str	r1, [r0, #CLK_RESET_CCLK_DIVIDER]
	str	r1, [r0, #CLK_RESET_SCLK_DIVIDER]

	pll_enable r1, r0, CLK_RESET_PLLM_BASE
	pll_enable r1, r0, CLK_RESET_PLLP_BASE
	pll_enable r1, r0, CLK_RESET_PLLC_BASE

	adr	r2, tegra20_sdram_pad_address
	adr	r4, tegra20_sdram_pad_save
	mov	r5, #0

	ldr	r6, tegra20_sdram_pad_size
padload:
	ldr	r7, [r2, r5]		@ r7 is the addr in the pad_address

	ldr	r1, [r4, r5]
	str	r1, [r7]		@ restore the value in pad_save

	add	r5, r5, #4
	cmp	r6, r5
	bne	padload

padload_done:
	/* 255uS delay for PLL stabilization */
	mov32	r7, TEGRA_TMRUS_BASE
	ldr	r1, [r7]
	add	r1, r1, #0xff
	wait_until r1, r7, r9

	adr	r4, tegra20_sclk_save
	ldr	r4, [r4]
	str	r4, [r0, #CLK_RESET_SCLK_BURST]
	mov32	r4, ((1 << 28) | (4))	@ burst policy is PLLP
	str	r4, [r0, #CLK_RESET_CCLK_BURST]

	mov32	r0, TEGRA_EMC_BASE
	ldr	r1, [r0, #EMC_CFG]
	bic	r1, r1, #(1 << 31)	@ disable DRAM_CLK_STOP
	str	r1, [r0, #EMC_CFG]

	mov	r1, #0
	str	r1, [r0, #EMC_SELF_REF]	@ take DRAM out of self refresh
	mov	r1, #1
	str	r1, [r0, #EMC_NOP]
	str	r1, [r0, #EMC_NOP]

	emc_device_mask r1, r0

exit_selfrefresh_loop:
	ldr	r2, [r0, #EMC_EMC_STATUS]
	ands	r2, r2, r1
	bne	exit_selfrefresh_loop

	mov	r1, #0			@ unstall all transactions
	str	r1, [r0, #EMC_REQ_CTRL]

	mov32	r0, TEGRA_PMC_BASE
	ldr	r0, [r0, #PMC_SCRATCH41]
	ret	r0			@ jump to tegra_resume
ENDPROC(tegra20_lp1_reset)

/*
 * tegra20_tear_down_core
 *
 * copied into and executed from IRAM
 * puts memory in self-refresh for LP0 and LP1
 */
tegra20_tear_down_core:
	bl	tegra20_sdram_self_refresh
	bl	tegra20_switch_cpu_to_clk32k
	b	tegra20_enter_sleep

/*
 * tegra20_switch_cpu_to_clk32k
 *
 * In LP0 and LP1 all PLLs will be turned off. Switch the CPU and system clock
 * to the 32KHz clock.
 */
tegra20_switch_cpu_to_clk32k:
	/*
	 * start by switching to CLKM to safely disable PLLs, then switch to
	 * CLKS.
	 */
	mov	r0, #(1 << 28)
	str	r0, [r5, #CLK_RESET_SCLK_BURST]
	str	r0, [r5, #CLK_RESET_CCLK_BURST]
	mov	r0, #0
	str	r0, [r5, #CLK_RESET_CCLK_DIVIDER]
	str	r0, [r5, #CLK_RESET_SCLK_DIVIDER]

	/* 2uS delay delay between changing SCLK and disabling PLLs */
	mov32	r7, TEGRA_TMRUS_BASE
	ldr	r1, [r7]
	add	r1, r1, #2
	wait_until r1, r7, r9

	/* disable PLLM, PLLP and PLLC */
	ldr	r0, [r5, #CLK_RESET_PLLM_BASE]
	bic	r0, r0, #(1 << 30)
	str	r0, [r5, #CLK_RESET_PLLM_BASE]
	ldr	r0, [r5, #CLK_RESET_PLLP_BASE]
	bic	r0, r0, #(1 << 30)
	str	r0, [r5, #CLK_RESET_PLLP_BASE]
	ldr	r0, [r5, #CLK_RESET_PLLC_BASE]
	bic	r0, r0, #(1 << 30)
	str	r0, [r5, #CLK_RESET_PLLC_BASE]

	/* switch to CLKS */
	mov	r0, #0	/* brust policy = 32KHz */
	str	r0, [r5, #CLK_RESET_SCLK_BURST]

	ret	lr

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

/*
 * tegra20_sdram_self_refresh
 *
 * called with MMU off and caches disabled
 * puts sdram in self refresh
 * must be executed from IRAM
 */
tegra20_sdram_self_refresh:
	mov32	r1, TEGRA_EMC_BASE	@ r1 reserved for emc base addr

	mov	r2, #3
	str	r2, [r1, #EMC_REQ_CTRL]	@ stall incoming DRAM requests

emcidle:
	ldr	r2, [r1, #EMC_EMC_STATUS]
	tst	r2, #4
	beq	emcidle

	mov	r2, #1
	str	r2, [r1, #EMC_SELF_REF]

	emc_device_mask r2, r1

emcself:
	ldr	r3, [r1, #EMC_EMC_STATUS]
	and	r3, r3, r2
	cmp	r3, r2
	bne	emcself			@ loop until DDR in self-refresh

	adr	r2, tegra20_sdram_pad_address
	adr	r3, tegra20_sdram_pad_safe
	adr	r4, tegra20_sdram_pad_save
	mov	r5, #0

	ldr	r6, tegra20_sdram_pad_size
padsave:
	ldr	r0, [r2, r5]		@ r0 is the addr in the pad_address

	ldr	r1, [r0]
	str	r1, [r4, r5]		@ save the content of the addr

	ldr	r1, [r3, r5]
	str	r1, [r0]		@ set the save val to the addr

	add	r5, r5, #4
	cmp	r6, r5
	bne	padsave
padsave_done:

	mov32	r5, TEGRA_CLK_RESET_BASE
	ldr	r0, [r5, #CLK_RESET_SCLK_BURST]
	adr	r2, tegra20_sclk_save
	str	r0, [r2]
	dsb
	ret	lr

tegra20_sdram_pad_address:
	.word	TEGRA_APB_MISC_BASE + APB_MISC_XM2CFGCPADCTRL
	.word	TEGRA_APB_MISC_BASE + APB_MISC_XM2CFGDPADCTRL
	.word	TEGRA_APB_MISC_BASE + APB_MISC_XM2CLKCFGPADCTRL
	.word	TEGRA_APB_MISC_BASE + APB_MISC_XM2COMPPADCTRL
	.word	TEGRA_APB_MISC_BASE + APB_MISC_XM2VTTGENPADCTRL
	.word	TEGRA_APB_MISC_BASE + APB_MISC_XM2CFGCPADCTRL2
	.word	TEGRA_APB_MISC_BASE + APB_MISC_XM2CFGDPADCTRL2

tegra20_sdram_pad_size:
	.word	tegra20_sdram_pad_size - tegra20_sdram_pad_address

tegra20_sdram_pad_safe:
	.word	0x8
	.word	0x8
	.word	0x0
	.word	0x8
	.word	0x5500
	.word	0x08080040
	.word	0x0

tegra20_sclk_save:
	.word	0x0

tegra20_sdram_pad_save:
	.rept (tegra20_sdram_pad_size - tegra20_sdram_pad_address) / 4
	.long	0
	.endr

	.ltorg
/* dummy symbol for end of IRAM */
	.align L1_CACHE_SHIFT
	.globl tegra20_iram_end
tegra20_iram_end:
	b	.
#endif
