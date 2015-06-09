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

#include <soc/tegra/fuse.h>

#include <asm/asm-offsets.h>
#include <asm/assembler.h>
#include <asm/cache.h>

#include "flowctrl.h"
#include "irammap.h"
#include "sleep.h"

#define EMC_CFG				0xc
#define EMC_ADR_CFG			0x10
#define EMC_TIMING_CONTROL		0x28
#define EMC_REFRESH			0x70
#define EMC_NOP				0xdc
#define EMC_SELF_REF			0xe0
#define EMC_MRW				0xe8
#define EMC_FBIO_CFG5			0x104
#define EMC_AUTO_CAL_CONFIG		0x2a4
#define EMC_AUTO_CAL_INTERVAL		0x2a8
#define EMC_AUTO_CAL_STATUS		0x2ac
#define EMC_REQ_CTRL			0x2b0
#define EMC_CFG_DIG_DLL			0x2bc
#define EMC_EMC_STATUS			0x2b4
#define EMC_ZCAL_INTERVAL		0x2e0
#define EMC_ZQ_CAL			0x2ec
#define EMC_XM2VTTGENPADCTRL		0x310
#define EMC_XM2VTTGENPADCTRL2		0x314

#define PMC_CTRL			0x0
#define PMC_CTRL_SIDE_EFFECT_LP0 (1 << 14) /* enter LP0 when CPU pwr gated */

#define PMC_PLLP_WB0_OVERRIDE		0xf8
#define PMC_IO_DPD_REQ			0x1b8
#define PMC_IO_DPD_STATUS		0x1bc

#define CLK_RESET_CCLK_BURST		0x20
#define CLK_RESET_CCLK_DIVIDER		0x24
#define CLK_RESET_SCLK_BURST		0x28
#define CLK_RESET_SCLK_DIVIDER		0x2c

#define CLK_RESET_PLLC_BASE		0x80
#define CLK_RESET_PLLC_MISC		0x8c
#define CLK_RESET_PLLM_BASE		0x90
#define CLK_RESET_PLLM_MISC		0x9c
#define CLK_RESET_PLLP_BASE		0xa0
#define CLK_RESET_PLLP_MISC		0xac
#define CLK_RESET_PLLA_BASE		0xb0
#define CLK_RESET_PLLA_MISC		0xbc
#define CLK_RESET_PLLX_BASE		0xe0
#define CLK_RESET_PLLX_MISC		0xe4
#define CLK_RESET_PLLX_MISC3		0x518
#define CLK_RESET_PLLX_MISC3_IDDQ	3
#define CLK_RESET_PLLM_MISC_IDDQ	5
#define CLK_RESET_PLLC_MISC_IDDQ	26

#define CLK_RESET_CLK_SOURCE_MSELECT	0x3b4

#define MSELECT_CLKM			(0x3 << 30)

#define LOCK_DELAY 50 /* safety delay after lock is detected */

#define TEGRA30_POWER_HOTPLUG_SHUTDOWN	(1 << 27) /* Hotplug shutdown */

.macro emc_device_mask, rd, base
	ldr	\rd, [\base, #EMC_ADR_CFG]
	tst	\rd, #0x1
	moveq	\rd, #(0x1 << 8)		@ just 1 device
	movne	\rd, #(0x3 << 8)		@ 2 devices
.endm

.macro emc_timing_update, rd, base
	mov	\rd, #1
	str	\rd, [\base, #EMC_TIMING_CONTROL]
1001:
	ldr	\rd, [\base, #EMC_EMC_STATUS]
	tst	\rd, #(0x1<<23)	@ wait EMC_STATUS_TIMING_UPDATE_STALLED is clear
	bne	1001b
.endm

.macro pll_enable, rd, r_car_base, pll_base, pll_misc
	ldr	\rd, [\r_car_base, #\pll_base]
	tst	\rd, #(1 << 30)
	orreq	\rd, \rd, #(1 << 30)
	streq	\rd, [\r_car_base, #\pll_base]
	/* Enable lock detector */
	.if	\pll_misc
	ldr	\rd, [\r_car_base, #\pll_misc]
	bic	\rd, \rd, #(1 << 18)
	str	\rd, [\r_car_base, #\pll_misc]
	ldr	\rd, [\r_car_base, #\pll_misc]
	ldr	\rd, [\r_car_base, #\pll_misc]
	orr	\rd, \rd, #(1 << 18)
	str	\rd, [\r_car_base, #\pll_misc]
	.endif
.endm

.macro pll_locked, rd, r_car_base, pll_base
1:
	ldr	\rd, [\r_car_base, #\pll_base]
	tst	\rd, #(1 << 27)
	beq	1b
.endm

.macro pll_iddq_exit, rd, car, iddq, iddq_bit
	ldr	\rd, [\car, #\iddq]
	bic	\rd, \rd, #(1<<\iddq_bit)
	str	\rd, [\car, #\iddq]
.endm

.macro pll_iddq_entry, rd, car, iddq, iddq_bit
	ldr	\rd, [\car, #\iddq]
	orr	\rd, \rd, #(1<<\iddq_bit)
	str	\rd, [\car, #\iddq]
.endm

#if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_PM_SLEEP)
/*
 * tegra30_hotplug_shutdown(void)
 *
 * Powergates the current CPU.
 * Should never return.
 */
ENTRY(tegra30_hotplug_shutdown)
	/* Powergate this CPU */
	mov	r0, #TEGRA30_POWER_HOTPLUG_SHUTDOWN
	bl	tegra30_cpu_shutdown
	ret	lr			@ should never get here
ENDPROC(tegra30_hotplug_shutdown)

/*
 * tegra30_cpu_shutdown(unsigned long flags)
 *
 * Puts the current CPU in wait-for-event mode on the flow controller
 * and powergates it -- flags (in R0) indicate the request type.
 *
 * r10 = SoC ID
 * corrupts r0-r4, r10-r12
 */
ENTRY(tegra30_cpu_shutdown)
	cpu_id	r3
	tegra_get_soc_id TEGRA_APB_MISC_VIRT, r10
	cmp	r10, #TEGRA30
	bne	_no_cpu0_chk	@ It's not Tegra30

	cmp	r3, #0
	reteq	lr		@ Must never be called for CPU 0
_no_cpu0_chk:

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
	cmp	r10, #TEGRA30
	moveq	r4, #(1 << 4)			@ wfe bitmap
	movne	r4, #(1 << 8)			@ wfi bitmap
 ARM(	orr	r12, r12, r4, lsl r3	)
 THUMB(	lsl	r4, r4, r3		)
 THUMB(	orr	r12, r12, r4		)
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
	beq	flow_ctrl_setting_for_lp2

	/* flow controller set up for hotplug */
	mov	r3, #FLOW_CTRL_WAITEVENT		@ For hotplug
	b	flow_ctrl_done
flow_ctrl_setting_for_lp2:
	/* flow controller set up for LP2 */
	cmp	r10, #TEGRA30
	moveq   r3, #FLOW_CTRL_WAIT_FOR_INTERRUPT	@ For LP2
	movne	r3, #FLOW_CTRL_WAITEVENT
	orrne	r3, r3, #FLOW_CTRL_HALT_GIC_IRQ
	orrne	r3, r3, #FLOW_CTRL_HALT_GIC_FIQ
flow_ctrl_done:
	cmp	r10, #TEGRA30
	str	r3, [r2]
	ldr	r0, [r2]
	b	wfe_war

__cpu_reset_again:
	dsb
	.align 5
	wfeeq					@ CPU should be power gated here
	wfine
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

#ifdef CONFIG_PM_SLEEP
/*
 * tegra30_sleep_core_finish(unsigned long v2p)
 *
 * Enters suspend in LP0 or LP1 by turning off the MMU and jumping to
 * tegra30_tear_down_core in IRAM
 */
ENTRY(tegra30_sleep_core_finish)
	/* Flush, disable the L1 data cache and exit SMP */
	bl	tegra_disable_clean_inv_dcache

	/*
	 * Preload all the address literals that are needed for the
	 * CPU power-gating process, to avoid loading from SDRAM which
	 * are not supported once SDRAM is put into self-refresh.
	 * LP0 / LP1 use physical address, since the MMU needs to be
	 * disabled before putting SDRAM into self-refresh to avoid
	 * memory access due to page table walks.
	 */
	mov32	r4, TEGRA_PMC_BASE
	mov32	r5, TEGRA_CLK_RESET_BASE
	mov32	r6, TEGRA_FLOW_CTRL_BASE
	mov32	r7, TEGRA_TMRUS_BASE

	mov32	r3, tegra_shut_off_mmu
	add	r3, r3, r0

	mov32	r0, tegra30_tear_down_core
	mov32	r1, tegra30_iram_start
	sub	r0, r0, r1
	mov32	r1, TEGRA_IRAM_LPx_RESUME_AREA
	add	r0, r0, r1

	ret	r3
ENDPROC(tegra30_sleep_core_finish)

/*
 * tegra30_sleep_cpu_secondary_finish(unsigned long v2p)
 *
 * Enters LP2 on secondary CPU by exiting coherency and powergating the CPU.
 */
ENTRY(tegra30_sleep_cpu_secondary_finish)
	mov	r7, lr

	/* Flush and disable the L1 data cache */
	mov 	r0, #TEGRA_FLUSH_CACHE_LOUIS
	bl	tegra_disable_clean_inv_dcache

	/* Powergate this CPU. */
	mov	r0, #0                          @ power mode flags (!hotplug)
	bl	tegra30_cpu_shutdown
	mov	r0, #1                          @ never return here
	ret	r7
ENDPROC(tegra30_sleep_cpu_secondary_finish)

/*
 * tegra30_tear_down_cpu
 *
 * Switches the CPU to enter sleep.
 */
ENTRY(tegra30_tear_down_cpu)
	mov32	r6, TEGRA_FLOW_CTRL_BASE

	b	tegra30_enter_sleep
ENDPROC(tegra30_tear_down_cpu)

/* START OF ROUTINES COPIED TO IRAM */
	.align L1_CACHE_SHIFT
	.globl tegra30_iram_start
tegra30_iram_start:

/*
 * tegra30_lp1_reset
 *
 * reset vector for LP1 restore; copied into IRAM during suspend.
 * Brings the system back up to a safe staring point (SDRAM out of
 * self-refresh, PLLC, PLLM and PLLP reenabled, CPU running on PLLX,
 * system clock running on the same PLL that it suspended at), and
 * jumps to tegra_resume to restore virtual addressing.
 * The physical address of tegra_resume expected to be stored in
 * PMC_SCRATCH41.
 *
 * NOTE: THIS *MUST* BE RELOCATED TO TEGRA_IRAM_LPx_RESUME_AREA.
 */
ENTRY(tegra30_lp1_reset)
	/*
	 * The CPU and system bus are running at 32KHz and executing from
	 * IRAM when this code is executed; immediately switch to CLKM and
	 * enable PLLP, PLLM, PLLC, PLLA and PLLX.
	 */
	mov32	r0, TEGRA_CLK_RESET_BASE

	mov	r1, #(1 << 28)
	str	r1, [r0, #CLK_RESET_SCLK_BURST]
	str	r1, [r0, #CLK_RESET_CCLK_BURST]
	mov	r1, #0
	str	r1, [r0, #CLK_RESET_CCLK_DIVIDER]
	str	r1, [r0, #CLK_RESET_SCLK_DIVIDER]

	tegra_get_soc_id TEGRA_APB_MISC_BASE, r10
	cmp	r10, #TEGRA30
	beq	_no_pll_iddq_exit

	pll_iddq_exit r1, r0, CLK_RESET_PLLM_MISC, CLK_RESET_PLLM_MISC_IDDQ
	pll_iddq_exit r1, r0, CLK_RESET_PLLC_MISC, CLK_RESET_PLLC_MISC_IDDQ
	pll_iddq_exit r1, r0, CLK_RESET_PLLX_MISC3, CLK_RESET_PLLX_MISC3_IDDQ

	mov32	r7, TEGRA_TMRUS_BASE
	ldr	r1, [r7]
	add	r1, r1, #2
	wait_until r1, r7, r3

	/* enable PLLM via PMC */
	mov32	r2, TEGRA_PMC_BASE
	ldr	r1, [r2, #PMC_PLLP_WB0_OVERRIDE]
	orr	r1, r1, #(1 << 12)
	str	r1, [r2, #PMC_PLLP_WB0_OVERRIDE]

	pll_enable r1, r0, CLK_RESET_PLLM_BASE, 0
	pll_enable r1, r0, CLK_RESET_PLLC_BASE, 0
	pll_enable r1, r0, CLK_RESET_PLLX_BASE, 0

	b	_pll_m_c_x_done

_no_pll_iddq_exit:
	/* enable PLLM via PMC */
	mov32	r2, TEGRA_PMC_BASE
	ldr	r1, [r2, #PMC_PLLP_WB0_OVERRIDE]
	orr	r1, r1, #(1 << 12)
	str	r1, [r2, #PMC_PLLP_WB0_OVERRIDE]

	pll_enable r1, r0, CLK_RESET_PLLM_BASE, CLK_RESET_PLLM_MISC
	pll_enable r1, r0, CLK_RESET_PLLC_BASE, CLK_RESET_PLLC_MISC
	pll_enable r1, r0, CLK_RESET_PLLX_BASE, CLK_RESET_PLLX_MISC

_pll_m_c_x_done:
	pll_enable r1, r0, CLK_RESET_PLLP_BASE, CLK_RESET_PLLP_MISC
	pll_enable r1, r0, CLK_RESET_PLLA_BASE, CLK_RESET_PLLA_MISC

	pll_locked r1, r0, CLK_RESET_PLLM_BASE
	pll_locked r1, r0, CLK_RESET_PLLP_BASE
	pll_locked r1, r0, CLK_RESET_PLLA_BASE
	pll_locked r1, r0, CLK_RESET_PLLC_BASE
	pll_locked r1, r0, CLK_RESET_PLLX_BASE

	mov32	r7, TEGRA_TMRUS_BASE
	ldr	r1, [r7]
	add	r1, r1, #LOCK_DELAY
	wait_until r1, r7, r3

	adr	r5, tegra_sdram_pad_save

	ldr	r4, [r5, #0x18]		@ restore CLK_SOURCE_MSELECT
	str	r4, [r0, #CLK_RESET_CLK_SOURCE_MSELECT]

	ldr	r4, [r5, #0x1C]		@ restore SCLK_BURST
	str	r4, [r0, #CLK_RESET_SCLK_BURST]

	cmp	r10, #TEGRA30
	movweq	r4, #:lower16:((1 << 28) | (0x8))	@ burst policy is PLLX
	movteq	r4, #:upper16:((1 << 28) | (0x8))
	movwne	r4, #:lower16:((1 << 28) | (0xe))
	movtne	r4, #:upper16:((1 << 28) | (0xe))
	str	r4, [r0, #CLK_RESET_CCLK_BURST]

	/* Restore pad power state to normal */
	ldr	r1, [r5, #0x14]		@ PMC_IO_DPD_STATUS
	mvn	r1, r1
	bic	r1, r1, #(1 << 31)
	orr	r1, r1, #(1 << 30)
	str	r1, [r2, #PMC_IO_DPD_REQ]	@ DPD_OFF

	cmp	r10, #TEGRA30
	movweq	r0, #:lower16:TEGRA_EMC_BASE	@ r0 reserved for emc base
	movteq	r0, #:upper16:TEGRA_EMC_BASE
	cmp	r10, #TEGRA114
	movweq	r0, #:lower16:TEGRA_EMC0_BASE
	movteq	r0, #:upper16:TEGRA_EMC0_BASE
	cmp	r10, #TEGRA124
	movweq	r0, #:lower16:TEGRA124_EMC_BASE
	movteq	r0, #:upper16:TEGRA124_EMC_BASE

exit_self_refresh:
	ldr	r1, [r5, #0xC]		@ restore EMC_XM2VTTGENPADCTRL
	str	r1, [r0, #EMC_XM2VTTGENPADCTRL]
	ldr	r1, [r5, #0x10]		@ restore EMC_XM2VTTGENPADCTRL2
	str	r1, [r0, #EMC_XM2VTTGENPADCTRL2]
	ldr	r1, [r5, #0x8]		@ restore EMC_AUTO_CAL_INTERVAL
	str	r1, [r0, #EMC_AUTO_CAL_INTERVAL]

	/* Relock DLL */
	ldr	r1, [r0, #EMC_CFG_DIG_DLL]
	orr	r1, r1, #(1 << 30)	@ set DLL_RESET
	str	r1, [r0, #EMC_CFG_DIG_DLL]

	emc_timing_update r1, r0

	cmp	r10, #TEGRA114
	movweq	r1, #:lower16:TEGRA_EMC1_BASE
	movteq	r1, #:upper16:TEGRA_EMC1_BASE
	cmpeq	r0, r1

	ldr	r1, [r0, #EMC_AUTO_CAL_CONFIG]
	orr	r1, r1, #(1 << 31)	@ set AUTO_CAL_ACTIVE
	orreq	r1, r1, #(1 << 27)	@ set slave mode for channel 1
	str	r1, [r0, #EMC_AUTO_CAL_CONFIG]

emc_wait_auto_cal_onetime:
	ldr	r1, [r0, #EMC_AUTO_CAL_STATUS]
	tst	r1, #(1 << 31)		@ wait until AUTO_CAL_ACTIVE is cleared
	bne	emc_wait_auto_cal_onetime

	ldr	r1, [r0, #EMC_CFG]
	bic	r1, r1, #(1 << 31)	@ disable DRAM_CLK_STOP_PD
	str	r1, [r0, #EMC_CFG]

	mov	r1, #0
	str	r1, [r0, #EMC_SELF_REF]	@ take DRAM out of self refresh
	mov	r1, #1
	cmp	r10, #TEGRA30
	streq	r1, [r0, #EMC_NOP]
	streq	r1, [r0, #EMC_NOP]
	streq	r1, [r0, #EMC_REFRESH]

	emc_device_mask r1, r0

exit_selfrefresh_loop:
	ldr	r2, [r0, #EMC_EMC_STATUS]
	ands	r2, r2, r1
	bne	exit_selfrefresh_loop

	lsr	r1, r1, #8		@ devSel, bit0:dev0, bit1:dev1

	mov32	r7, TEGRA_TMRUS_BASE
	ldr	r2, [r0, #EMC_FBIO_CFG5]

	and	r2, r2,	#3		@ check DRAM_TYPE
	cmp	r2, #2
	beq	emc_lpddr2

	/* Issue a ZQ_CAL for dev0 - DDR3 */
	mov32	r2, 0x80000011		@ DEV_SELECTION=2, LENGTH=LONG, CMD=1
	str	r2, [r0, #EMC_ZQ_CAL]
	ldr	r2, [r7]
	add	r2, r2, #10
	wait_until r2, r7, r3

	tst	r1, #2
	beq	zcal_done

	/* Issue a ZQ_CAL for dev1 - DDR3 */
	mov32	r2, 0x40000011		@ DEV_SELECTION=1, LENGTH=LONG, CMD=1
	str	r2, [r0, #EMC_ZQ_CAL]
	ldr	r2, [r7]
	add	r2, r2, #10
	wait_until r2, r7, r3
	b	zcal_done

emc_lpddr2:
	/* Issue a ZQ_CAL for dev0 - LPDDR2 */
	mov32	r2, 0x800A00AB		@ DEV_SELECTION=2, MA=10, OP=0xAB
	str	r2, [r0, #EMC_MRW]
	ldr	r2, [r7]
	add	r2, r2, #1
	wait_until r2, r7, r3

	tst	r1, #2
	beq	zcal_done

	/* Issue a ZQ_CAL for dev0 - LPDDR2 */
	mov32	r2, 0x400A00AB		@ DEV_SELECTION=1, MA=10, OP=0xAB
	str	r2, [r0, #EMC_MRW]
	ldr	r2, [r7]
	add	r2, r2, #1
	wait_until r2, r7, r3

zcal_done:
	mov	r1, #0			@ unstall all transactions
	str	r1, [r0, #EMC_REQ_CTRL]
	ldr	r1, [r5, #0x4]		@ restore EMC_ZCAL_INTERVAL
	str	r1, [r0, #EMC_ZCAL_INTERVAL]
	ldr	r1, [r5, #0x0]		@ restore EMC_CFG
	str	r1, [r0, #EMC_CFG]

	/* Tegra114 had dual EMC channel, now config the other one */
	cmp	r10, #TEGRA114
	bne	__no_dual_emc_chanl
	mov32	r1, TEGRA_EMC1_BASE
	cmp	r0, r1
	movne	r0, r1
	addne	r5, r5, #0x20
	bne	exit_self_refresh
__no_dual_emc_chanl:

	mov32	r0, TEGRA_PMC_BASE
	ldr	r0, [r0, #PMC_SCRATCH41]
	ret	r0			@ jump to tegra_resume
ENDPROC(tegra30_lp1_reset)

	.align	L1_CACHE_SHIFT
tegra30_sdram_pad_address:
	.word	TEGRA_EMC_BASE + EMC_CFG				@0x0
	.word	TEGRA_EMC_BASE + EMC_ZCAL_INTERVAL			@0x4
	.word	TEGRA_EMC_BASE + EMC_AUTO_CAL_INTERVAL			@0x8
	.word	TEGRA_EMC_BASE + EMC_XM2VTTGENPADCTRL			@0xc
	.word	TEGRA_EMC_BASE + EMC_XM2VTTGENPADCTRL2			@0x10
	.word	TEGRA_PMC_BASE + PMC_IO_DPD_STATUS			@0x14
	.word	TEGRA_CLK_RESET_BASE + CLK_RESET_CLK_SOURCE_MSELECT	@0x18
	.word	TEGRA_CLK_RESET_BASE + CLK_RESET_SCLK_BURST		@0x1c
tegra30_sdram_pad_address_end:

tegra114_sdram_pad_address:
	.word	TEGRA_EMC0_BASE + EMC_CFG				@0x0
	.word	TEGRA_EMC0_BASE + EMC_ZCAL_INTERVAL			@0x4
	.word	TEGRA_EMC0_BASE + EMC_AUTO_CAL_INTERVAL			@0x8
	.word	TEGRA_EMC0_BASE + EMC_XM2VTTGENPADCTRL			@0xc
	.word	TEGRA_EMC0_BASE + EMC_XM2VTTGENPADCTRL2			@0x10
	.word	TEGRA_PMC_BASE + PMC_IO_DPD_STATUS			@0x14
	.word	TEGRA_CLK_RESET_BASE + CLK_RESET_CLK_SOURCE_MSELECT	@0x18
	.word	TEGRA_CLK_RESET_BASE + CLK_RESET_SCLK_BURST		@0x1c
	.word	TEGRA_EMC1_BASE + EMC_CFG				@0x20
	.word	TEGRA_EMC1_BASE + EMC_ZCAL_INTERVAL			@0x24
	.word	TEGRA_EMC1_BASE + EMC_AUTO_CAL_INTERVAL			@0x28
	.word	TEGRA_EMC1_BASE + EMC_XM2VTTGENPADCTRL			@0x2c
	.word	TEGRA_EMC1_BASE + EMC_XM2VTTGENPADCTRL2			@0x30
tegra114_sdram_pad_adress_end:

tegra124_sdram_pad_address:
	.word	TEGRA124_EMC_BASE + EMC_CFG				@0x0
	.word	TEGRA124_EMC_BASE + EMC_ZCAL_INTERVAL			@0x4
	.word	TEGRA124_EMC_BASE + EMC_AUTO_CAL_INTERVAL		@0x8
	.word	TEGRA124_EMC_BASE + EMC_XM2VTTGENPADCTRL		@0xc
	.word	TEGRA124_EMC_BASE + EMC_XM2VTTGENPADCTRL2		@0x10
	.word	TEGRA_PMC_BASE + PMC_IO_DPD_STATUS			@0x14
	.word	TEGRA_CLK_RESET_BASE + CLK_RESET_CLK_SOURCE_MSELECT	@0x18
	.word	TEGRA_CLK_RESET_BASE + CLK_RESET_SCLK_BURST		@0x1c
tegra124_sdram_pad_address_end:

tegra30_sdram_pad_size:
	.word	tegra30_sdram_pad_address_end - tegra30_sdram_pad_address

tegra114_sdram_pad_size:
	.word	tegra114_sdram_pad_adress_end - tegra114_sdram_pad_address

	.type	tegra_sdram_pad_save, %object
tegra_sdram_pad_save:
	.rept (tegra114_sdram_pad_adress_end - tegra114_sdram_pad_address) / 4
	.long	0
	.endr

/*
 * tegra30_tear_down_core
 *
 * copied into and executed from IRAM
 * puts memory in self-refresh for LP0 and LP1
 */
tegra30_tear_down_core:
	bl	tegra30_sdram_self_refresh
	bl	tegra30_switch_cpu_to_clk32k
	b	tegra30_enter_sleep

/*
 * tegra30_switch_cpu_to_clk32k
 *
 * In LP0 and LP1 all PLLs will be turned off. Switching the CPU and System CLK
 * to the 32KHz clock.
 * r4 = TEGRA_PMC_BASE
 * r5 = TEGRA_CLK_RESET_BASE
 * r6 = TEGRA_FLOW_CTRL_BASE
 * r7 = TEGRA_TMRUS_BASE
 * r10= SoC ID
 */
tegra30_switch_cpu_to_clk32k:
	/*
	 * start by jumping to CLKM to safely disable PLLs, then jump to
	 * CLKS.
	 */
	mov	r0, #(1 << 28)
	str	r0, [r5, #CLK_RESET_SCLK_BURST]
	/* 2uS delay delay between changing SCLK and CCLK */
	ldr	r1, [r7]
	add	r1, r1, #2
	wait_until r1, r7, r9
	str	r0, [r5, #CLK_RESET_CCLK_BURST]
	mov	r0, #0
	str	r0, [r5, #CLK_RESET_CCLK_DIVIDER]
	str	r0, [r5, #CLK_RESET_SCLK_DIVIDER]

	/* switch the clock source of mselect to be CLK_M */
	ldr	r0, [r5, #CLK_RESET_CLK_SOURCE_MSELECT]
	orr	r0, r0, #MSELECT_CLKM
	str	r0, [r5, #CLK_RESET_CLK_SOURCE_MSELECT]

	/* 2uS delay delay between changing SCLK and disabling PLLs */
	ldr	r1, [r7]
	add	r1, r1, #2
	wait_until r1, r7, r9

	/* disable PLLM via PMC in LP1 */
	ldr	r0, [r4, #PMC_PLLP_WB0_OVERRIDE]
	bic	r0, r0, #(1 << 12)
	str	r0, [r4, #PMC_PLLP_WB0_OVERRIDE]

	/* disable PLLP, PLLA, PLLC and PLLX */
	ldr	r0, [r5, #CLK_RESET_PLLP_BASE]
	bic	r0, r0, #(1 << 30)
	str	r0, [r5, #CLK_RESET_PLLP_BASE]
	ldr	r0, [r5, #CLK_RESET_PLLA_BASE]
	bic	r0, r0, #(1 << 30)
	str	r0, [r5, #CLK_RESET_PLLA_BASE]
	ldr	r0, [r5, #CLK_RESET_PLLC_BASE]
	bic	r0, r0, #(1 << 30)
	str	r0, [r5, #CLK_RESET_PLLC_BASE]
	ldr	r0, [r5, #CLK_RESET_PLLX_BASE]
	bic	r0, r0, #(1 << 30)
	str	r0, [r5, #CLK_RESET_PLLX_BASE]

	cmp	r10, #TEGRA30
	beq	_no_pll_in_iddq
	pll_iddq_entry r1, r5, CLK_RESET_PLLX_MISC3, CLK_RESET_PLLX_MISC3_IDDQ
_no_pll_in_iddq:

	/* switch to CLKS */
	mov	r0, #0	/* brust policy = 32KHz */
	str	r0, [r5, #CLK_RESET_SCLK_BURST]

	ret	lr

/*
 * tegra30_enter_sleep
 *
 * uses flow controller to enter sleep state
 * executes from IRAM with SDRAM in selfrefresh when target state is LP0 or LP1
 * executes from SDRAM with target state is LP2
 * r6 = TEGRA_FLOW_CTRL_BASE
 */
tegra30_enter_sleep:
	cpu_id	r1

	cpu_to_csr_reg	r2, r1
	ldr	r0, [r6, r2]
	orr	r0, r0, #FLOW_CTRL_CSR_INTR_FLAG | FLOW_CTRL_CSR_EVENT_FLAG
	orr	r0, r0, #FLOW_CTRL_CSR_ENABLE
	str	r0, [r6, r2]

	tegra_get_soc_id TEGRA_APB_MISC_BASE, r10
	cmp	r10, #TEGRA30
	mov	r0, #FLOW_CTRL_WAIT_FOR_INTERRUPT
	orreq	r0, r0, #FLOW_CTRL_HALT_CPU_IRQ | FLOW_CTRL_HALT_CPU_FIQ
	orrne   r0, r0, #FLOW_CTRL_HALT_LIC_IRQ | FLOW_CTRL_HALT_LIC_FIQ

	cpu_to_halt_reg r2, r1
	str	r0, [r6, r2]
	dsb
	ldr	r0, [r6, r2] /* memory barrier */

halted:
	isb
	dsb
	wfi	/* CPU should be power gated here */

	/* !!!FIXME!!! Implement halt failure handler */
	b	halted

/*
 * tegra30_sdram_self_refresh
 *
 * called with MMU off and caches disabled
 * must be executed from IRAM
 * r4 = TEGRA_PMC_BASE
 * r5 = TEGRA_CLK_RESET_BASE
 * r6 = TEGRA_FLOW_CTRL_BASE
 * r7 = TEGRA_TMRUS_BASE
 * r10= SoC ID
 */
tegra30_sdram_self_refresh:

	adr	r8, tegra_sdram_pad_save
	tegra_get_soc_id TEGRA_APB_MISC_BASE, r10
	cmp	r10, #TEGRA30
	adreq	r2, tegra30_sdram_pad_address
	ldreq	r3, tegra30_sdram_pad_size
	cmp	r10, #TEGRA114
	adreq	r2, tegra114_sdram_pad_address
	ldreq	r3, tegra114_sdram_pad_size
	cmp	r10, #TEGRA124
	adreq	r2, tegra124_sdram_pad_address
	ldreq	r3, tegra30_sdram_pad_size

	mov	r9, #0

padsave:
	ldr	r0, [r2, r9]		@ r0 is the addr in the pad_address

	ldr	r1, [r0]
	str	r1, [r8, r9]		@ save the content of the addr

	add	r9, r9, #4
	cmp	r3, r9
	bne	padsave
padsave_done:

	dsb

	cmp	r10, #TEGRA30
	ldreq	r0, =TEGRA_EMC_BASE	@ r0 reserved for emc base addr
	cmp	r10, #TEGRA114
	ldreq	r0, =TEGRA_EMC0_BASE
	cmp	r10, #TEGRA124
	ldreq	r0, =TEGRA124_EMC_BASE

enter_self_refresh:
	cmp	r10, #TEGRA30
	mov	r1, #0
	str	r1, [r0, #EMC_ZCAL_INTERVAL]
	str	r1, [r0, #EMC_AUTO_CAL_INTERVAL]
	ldr	r1, [r0, #EMC_CFG]
	bic	r1, r1, #(1 << 28)
	bicne	r1, r1, #(1 << 29)
	str	r1, [r0, #EMC_CFG]	@ disable DYN_SELF_REF

	emc_timing_update r1, r0

	ldr	r1, [r7]
	add	r1, r1, #5
	wait_until r1, r7, r2

emc_wait_auto_cal:
	ldr	r1, [r0, #EMC_AUTO_CAL_STATUS]
	tst	r1, #(1 << 31)		@ wait until AUTO_CAL_ACTIVE is cleared
	bne	emc_wait_auto_cal

	mov	r1, #3
	str	r1, [r0, #EMC_REQ_CTRL]	@ stall incoming DRAM requests

emcidle:
	ldr	r1, [r0, #EMC_EMC_STATUS]
	tst	r1, #4
	beq	emcidle

	mov	r1, #1
	str	r1, [r0, #EMC_SELF_REF]

	emc_device_mask r1, r0

emcself:
	ldr	r2, [r0, #EMC_EMC_STATUS]
	and	r2, r2, r1
	cmp	r2, r1
	bne	emcself			@ loop until DDR in self-refresh

	/* Put VTTGEN in the lowest power mode */
	ldr	r1, [r0, #EMC_XM2VTTGENPADCTRL]
	mov32	r2, 0xF8F8FFFF	@ clear XM2VTTGEN_DRVUP and XM2VTTGEN_DRVDN
	and	r1, r1, r2
	str	r1, [r0, #EMC_XM2VTTGENPADCTRL]
	ldr	r1, [r0, #EMC_XM2VTTGENPADCTRL2]
	cmp	r10, #TEGRA30
	orreq	r1, r1, #7		@ set E_NO_VTTGEN
	orrne	r1, r1, #0x3f
	str	r1, [r0, #EMC_XM2VTTGENPADCTRL2]

	emc_timing_update r1, r0

	/* Tegra114 had dual EMC channel, now config the other one */
	cmp	r10, #TEGRA114
	bne	no_dual_emc_chanl
	mov32	r1, TEGRA_EMC1_BASE
	cmp	r0, r1
	movne	r0, r1
	bne	enter_self_refresh
no_dual_emc_chanl:

	ldr	r1, [r4, #PMC_CTRL]
	tst	r1, #PMC_CTRL_SIDE_EFFECT_LP0
	bne	pmc_io_dpd_skip
	/*
	 * Put DDR_DATA, DISC_ADDR_CMD, DDR_ADDR_CMD, POP_ADDR_CMD, POP_CLK
	 * and COMP in the lowest power mode when LP1.
	 */
	mov32	r1, 0x8EC00000
	str	r1, [r4, #PMC_IO_DPD_REQ]
pmc_io_dpd_skip:

	dsb

	ret	lr

	.ltorg
/* dummy symbol for end of IRAM */
	.align L1_CACHE_SHIFT
	.global tegra30_iram_end
tegra30_iram_end:
	b	.
#endif
