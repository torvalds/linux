/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/busfreq-imx.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/delay.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <asm/cp15.h>
#include <asm/cpuidle.h>
#include <asm/fncpy.h>
#include <asm/mach/map.h>
#include <asm/proc-fns.h>
#include <asm/suspend.h>
#include <asm/tlb.h>

#include "common.h"
#include "cpuidle.h"
#include "hardware.h"

#define XTALOSC24M_OSC_CONFIG0	0x10
#define XTALOSC24M_OSC_CONFIG1	0x20
#define XTALOSC24M_OSC_CONFIG2	0x30
#define XTALOSC24M_OSC_CONFIG0_RC_OSC_PROG_CUR_SHIFT	24
#define XTALOSC24M_OSC_CONFIG0_HYST_MINUS_MASK		0xf
#define XTALOSC24M_OSC_CONFIG0_HYST_MINUS_SHIFT		16
#define XTALOSC24M_OSC_CONFIG0_HYST_PLUS_MASK		0xf
#define XTALOSC24M_OSC_CONFIG0_HYST_PLUS_SHIFT		12
#define XTALOSC24M_OSC_CONFIG0_RC_OSC_PROG_SHIFT	4
#define XTALOSC24M_OSC_CONFIG0_ENABLE_SHIFT		1
#define XTALOSC24M_OSC_CONFIG0_START_SHIFT		0
#define XTALOSC24M_OSC_CONFIG1_COUNT_RC_CUR_SHIFT	20
#define XTALOSC24M_OSC_CONFIG1_COUNT_RC_TRG_SHIFT	0
#define XTALOSC24M_OSC_CONFIG2_COUNT_1M_TRG_MASK	0xfff
#define XTALOSC24M_OSC_CONFIG2_COUNT_1M_TRG_SHIFT	0

#define XTALOSC_CTRL_24M				0x0
#define XTALOSC_CTRL_24M_RC_OSC_EN_SHIFT		13
#define REG_SET						0x4

static void __iomem *wfi_iram_base;
static void __iomem *wfi_iram_base_phys;
extern unsigned long iram_tlb_phys_addr;

struct imx7_pm_base {
	phys_addr_t pbase;
	void __iomem *vbase;
};

struct imx7_cpuidle_pm_info {
	phys_addr_t vbase; /* The virtual address of pm_info. */
	phys_addr_t pbase; /* The physical address of pm_info. */
	phys_addr_t resume_addr; /* The physical resume address for asm code */
	u32 pm_info_size;
	int last_cpu;
	u32 ttbr;
	u32 cpu1_wfi;
	u32 lpi_enter;
	u32 val;
	u32 flag0;
	u32 flag1;
	struct imx7_pm_base ddrc_base;
	struct imx7_pm_base ccm_base;
	struct imx7_pm_base anatop_base;
	struct imx7_pm_base src_base;
	struct imx7_pm_base iomuxc_gpr_base;
} __aligned(8);

static atomic_t master_lpi = ATOMIC_INIT(0);
static atomic_t master_wait = ATOMIC_INIT(0);

static void (*imx7d_wfi_in_iram_fn)(void __iomem *iram_vbase);
static struct imx7_cpuidle_pm_info *cpuidle_pm_info;

static int imx7d_idle_finish(unsigned long val)
{
	imx7d_wfi_in_iram_fn(wfi_iram_base);
	return 0;
}

static int imx7d_enter_low_power_idle(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv, int index)
{
	int mode = get_bus_freq_mode();

	if ((index == 1) || ((mode != BUS_FREQ_LOW) && index == 2)) {
		if (atomic_inc_return(&master_wait) == num_online_cpus())
			imx_gpcv2_set_lpm_mode(WAIT_UNCLOCKED);

		cpu_do_idle();

		atomic_dec(&master_wait);
		imx_gpcv2_set_lpm_mode(WAIT_CLOCKED);
	} else {
		imx_gpcv2_set_lpm_mode(WAIT_UNCLOCKED);
		cpu_pm_enter();

		if (atomic_inc_return(&master_lpi) < num_online_cpus()) {
			imx_set_cpu_jump(dev->cpu, ca7_cpu_resume);
			/* initialize the last cpu id to invalid here */
			cpuidle_pm_info->last_cpu = -1;
			cpu_suspend(0, imx7d_idle_finish);
		} else {
			imx_gpcv2_set_cpu_power_gate_in_idle(true);
			cpu_cluster_pm_enter();

			cpuidle_pm_info->last_cpu = dev->cpu;
			cpu_suspend(0, imx7d_idle_finish);

			cpu_cluster_pm_exit();
			imx_gpcv2_set_cpu_power_gate_in_idle(false);
		}
		atomic_dec(&master_lpi);

		cpu_pm_exit();
		imx_gpcv2_set_lpm_mode(WAIT_CLOCKED);
	}

	return index;
}

static struct cpuidle_driver imx7d_cpuidle_driver = {
	.name = "imx7d_cpuidle",
	.owner = THIS_MODULE,
	.states = {
		/* WFI */
		ARM_CPUIDLE_WFI_STATE,
		/* WAIT MODE */
		{
			.exit_latency = 50,
			.target_residency = 75,
			.flags = CPUIDLE_FLAG_TIMER_STOP,
			.enter = imx7d_enter_low_power_idle,
			.name = "WAIT",
			.desc = "Clock off",
		},
		/* LOW POWER IDLE */
		{
			.exit_latency = 300,
			.target_residency = 500,
			.flags = CPUIDLE_FLAG_TIMER_STOP,
			.enter = imx7d_enter_low_power_idle,
			.name = "LOW-POWER-IDLE",
			.desc = "ARM power off",
		},
	},
	.state_count = 3,
	.safe_state_index = 0,
};

int imx7d_enable_rcosc(void)
{
	void __iomem *anatop_base =
		(void __iomem *)IMX_IO_P2V(MX7D_ANATOP_BASE_ADDR);
	u32 val;

	imx_gpcv2_set_lpm_mode(WAIT_CLOCKED);
	/* set RC-OSC freq and turn it on */
	writel_relaxed(0x1 << XTALOSC_CTRL_24M_RC_OSC_EN_SHIFT,
		anatop_base + XTALOSC_CTRL_24M + REG_SET);
	/*
	 * config RC-OSC freq
	 * tune_enable = 1;tune_start = 1;hyst_plus = 0;hyst_minus = 0;
	 * osc_prog = 0xa7;
	 */
	writel_relaxed(
		0x4 << XTALOSC24M_OSC_CONFIG0_RC_OSC_PROG_CUR_SHIFT |
		0xa7 << XTALOSC24M_OSC_CONFIG0_RC_OSC_PROG_SHIFT |
		0x1 << XTALOSC24M_OSC_CONFIG0_ENABLE_SHIFT |
		0x1 << XTALOSC24M_OSC_CONFIG0_START_SHIFT,
		anatop_base + XTALOSC24M_OSC_CONFIG0);
	/* set count_trg = 0x2dc */
	writel_relaxed(
		0x40 << XTALOSC24M_OSC_CONFIG1_COUNT_RC_CUR_SHIFT |
		0x2dc << XTALOSC24M_OSC_CONFIG1_COUNT_RC_TRG_SHIFT,
		anatop_base + XTALOSC24M_OSC_CONFIG1);
	/* wait at least 4ms according to hardware design */
	mdelay(6);
	/*
	 * now add some hysteresis, hyst_plus=3, hyst_minus=3
	 * (the minimum hysteresis that looks good is 2)
	 */
	val = readl_relaxed(anatop_base + XTALOSC24M_OSC_CONFIG0);
	val &= ~((XTALOSC24M_OSC_CONFIG0_HYST_MINUS_MASK <<
		XTALOSC24M_OSC_CONFIG0_HYST_MINUS_SHIFT) |
		(XTALOSC24M_OSC_CONFIG0_HYST_PLUS_MASK <<
		XTALOSC24M_OSC_CONFIG0_HYST_PLUS_SHIFT));
	val |= (0x3 << XTALOSC24M_OSC_CONFIG0_HYST_MINUS_SHIFT) |
		(0x3 << XTALOSC24M_OSC_CONFIG0_HYST_PLUS_SHIFT);
	writel_relaxed(val, anatop_base + XTALOSC24M_OSC_CONFIG0);
	/* set the count_1m_trg = 0x2d7 */
	val = readl_relaxed(anatop_base + XTALOSC24M_OSC_CONFIG2);
	val &= ~(XTALOSC24M_OSC_CONFIG2_COUNT_1M_TRG_MASK <<
		XTALOSC24M_OSC_CONFIG2_COUNT_1M_TRG_SHIFT);
	val |= 0x2d7 << XTALOSC24M_OSC_CONFIG2_COUNT_1M_TRG_SHIFT;
	writel_relaxed(val, anatop_base + XTALOSC24M_OSC_CONFIG2);
	/*
	 * hardware design require to write XTALOSC24M_OSC_CONFIG0 or
	 * XTALOSC24M_OSC_CONFIG1 to
	 * make XTALOSC24M_OSC_CONFIG2 write work
	 */
	val = readl_relaxed(anatop_base + XTALOSC24M_OSC_CONFIG1);
	writel_relaxed(val, anatop_base + XTALOSC24M_OSC_CONFIG1);

	return 0;
}

int __init imx7d_cpuidle_init(void)
{
	wfi_iram_base_phys = (void *)(iram_tlb_phys_addr +
		MX7_CPUIDLE_OCRAM_ADDR_OFFSET);

	/* Make sure wfi_iram_base is 8 byte aligned. */
	if ((uintptr_t)(wfi_iram_base_phys) & (FNCPY_ALIGN - 1))
		wfi_iram_base_phys += FNCPY_ALIGN -
		((uintptr_t)wfi_iram_base_phys % (FNCPY_ALIGN));

	wfi_iram_base = (void *)IMX_IO_P2V((unsigned long) wfi_iram_base_phys);

	cpuidle_pm_info = wfi_iram_base;
	cpuidle_pm_info->vbase = (phys_addr_t) wfi_iram_base;
	cpuidle_pm_info->pbase = (phys_addr_t) wfi_iram_base_phys;
	cpuidle_pm_info->pm_info_size = sizeof(*cpuidle_pm_info);
	cpuidle_pm_info->resume_addr = virt_to_phys(ca7_cpu_resume);
	cpuidle_pm_info->cpu1_wfi = 0;
	cpuidle_pm_info->lpi_enter = 0;
	/* initialize the last cpu id to invalid here */
	cpuidle_pm_info->last_cpu = -1;

	cpuidle_pm_info->ddrc_base.pbase = MX7D_DDRC_BASE_ADDR;
	cpuidle_pm_info->ddrc_base.vbase =
		(void __iomem *)IMX_IO_P2V(MX7D_DDRC_BASE_ADDR);

	cpuidle_pm_info->ccm_base.pbase = MX7D_CCM_BASE_ADDR;
	cpuidle_pm_info->ccm_base.vbase =
		(void __iomem *)IMX_IO_P2V(MX7D_CCM_BASE_ADDR);

	cpuidle_pm_info->anatop_base.pbase = MX7D_ANATOP_BASE_ADDR;
	cpuidle_pm_info->anatop_base.vbase =
		(void __iomem *)IMX_IO_P2V(MX7D_ANATOP_BASE_ADDR);

	cpuidle_pm_info->src_base.pbase = MX7D_SRC_BASE_ADDR;
	cpuidle_pm_info->src_base.vbase =
		(void __iomem *)IMX_IO_P2V(MX7D_SRC_BASE_ADDR);

	cpuidle_pm_info->iomuxc_gpr_base.pbase = MX7D_IOMUXC_GPR_BASE_ADDR;
	cpuidle_pm_info->iomuxc_gpr_base.vbase =
		(void __iomem *)IMX_IO_P2V(MX7D_IOMUXC_GPR_BASE_ADDR);

	imx7d_enable_rcosc();

	/* code size should include cpuidle_pm_info size */
	imx7d_wfi_in_iram_fn = (void *)fncpy(wfi_iram_base +
		sizeof(*cpuidle_pm_info),
		&imx7d_low_power_idle,
		MX7_CPUIDLE_OCRAM_SIZE - sizeof(*cpuidle_pm_info));

	return cpuidle_register(&imx7d_cpuidle_driver, NULL);
}
