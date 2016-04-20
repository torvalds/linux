/*
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/busfreq-imx.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <asm/cpuidle.h>
#include <asm/fncpy.h>
#include <asm/proc-fns.h>
#include <asm/suspend.h>

#include "common.h"
#include "cpuidle.h"
#include "hardware.h"

#define MAX_MMDC_IO_NUM		14

#define PMU_LOW_PWR_CTRL	0x270
#define XTALOSC24M_OSC_CONFIG0	0x2a0
#define XTALOSC24M_OSC_CONFIG1	0x2b0
#define XTALOSC24M_OSC_CONFIG2	0x2c0
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

extern unsigned long iram_tlb_phys_addr;
static void __iomem *wfi_iram_base;

#ifdef CONFIG_CPU_FREQ
static void __iomem *wfi_iram_base_phys;
extern unsigned long mx6ul_lpm_wfi_start asm("mx6ul_lpm_wfi_start");
extern unsigned long mx6ul_lpm_wfi_end asm("mx6ul_lpm_wfi_end");
extern unsigned long mx6ull_lpm_wfi_start asm("mx6ull_lpm_wfi_start");
extern unsigned long mx6ull_lpm_wfi_end asm("mx6ull_lpm_wfi_end");
#endif

struct imx6_pm_base {
	phys_addr_t pbase;
	void __iomem *vbase;
};

struct imx6_cpuidle_pm_info {
	phys_addr_t pbase; /* The physical address of pm_info. */
	phys_addr_t resume_addr; /* The physical resume address for asm code */
	u32 pm_info_size; /* Size of pm_info. */
	u32 ttbr;
	struct imx6_pm_base mmdc_base;
	struct imx6_pm_base iomuxc_base;
	struct imx6_pm_base ccm_base;
	struct imx6_pm_base gpc_base;
	struct imx6_pm_base anatop_base;
	struct imx6_pm_base src_base;
	u32 mmdc_io_num; /* Number of MMDC IOs which need saved/restored. */
	u32 mmdc_io_val[MAX_MMDC_IO_NUM][2]; /* To save offset and value */
} __aligned(8);

static const u32 imx6ul_mmdc_io_offset[] __initconst = {
	0x244, 0x248, 0x24c, 0x250, /* DQM0, DQM1, RAS, CAS */
	0x27c, 0x498, 0x4a4, 0x490, /* SDCLK0, GPR_B0DS-B1DS, GPR_ADDS */
	0x280, 0x284, 0x260, 0x264, /* SDQS0~1, SODT0, SODT1 */
	0x494, 0x4b0,	            /* MODE_CTL, MODE, */
};

static void (*imx6ul_wfi_in_iram_fn)(void __iomem *iram_vbase);

static int imx6ul_idle_finish(unsigned long val)
{
	imx6ul_wfi_in_iram_fn(wfi_iram_base);

	return 0;
}

static int imx6ul_enter_wait(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv, int index)
{
	int mode = get_bus_freq_mode();

	imx6q_set_lpm(WAIT_UNCLOCKED);
	if ((index == 1) || ((mode != BUS_FREQ_LOW) && index == 2)) {
		cpu_do_idle();
	} else {
		/*
		 * i.MX6UL TO1.0 ARM power up uses IPG/2048 as clock source,
		 * from TO1.1, PGC_CPU_PUPSCR bit [5] is re-defined to switch
		 * clock to IPG/32, enable this bit to speed up the ARM power
		 * up process in low power idle case.
		 */
		if (cpu_is_imx6ul() && imx_get_soc_revision() >
			IMX_CHIP_REVISION_1_0)
			imx_gpc_switch_pupscr_clk(true);
		/* Need to notify there is a cpu pm operation. */
		cpu_pm_enter();
		cpu_cluster_pm_enter();

		cpu_suspend(0, imx6ul_idle_finish);

		cpu_cluster_pm_exit();
		cpu_pm_exit();
		imx6_enable_rbc(false);

		if (cpu_is_imx6ul() && imx_get_soc_revision() >
			IMX_CHIP_REVISION_1_0)
			imx_gpc_switch_pupscr_clk(false);
	}

	imx6q_set_lpm(WAIT_CLOCKED);

	return index;
}

static struct cpuidle_driver imx6ul_cpuidle_driver_v2 = {
	.name = "imx6ul_cpuidle",
	.owner = THIS_MODULE,
	.states = {
		/* WFI */
		ARM_CPUIDLE_WFI_STATE,
		/* WAIT */
		{
			.exit_latency = 50,
			.target_residency = 75,
			.enter = imx6ul_enter_wait,
			.name = "WAIT",
			.desc = "Clock off",
		},
		/* LOW POWER IDLE */
		{
			/*
			 * RBC 130us + ARM gating 43us + RBC clear 65us
			 * + PLL2 relock 450us and some margin, here set
			 * it to 700us.
			 */
			.exit_latency = 700,
			.target_residency = 1000,
			.enter = imx6ul_enter_wait,
			.name = "LOW-POWER-IDLE",
			.desc = "ARM power off",
		}
	},
	.state_count = 3,
	.safe_state_index = 0,
};

static struct cpuidle_driver imx6ul_cpuidle_driver = {
	.name = "imx6ul_cpuidle",
	.owner = THIS_MODULE,
	.states = {
		/* WFI */
		ARM_CPUIDLE_WFI_STATE,
		/* WAIT */
		{
			.exit_latency = 50,
			.target_residency = 75,
			.enter = imx6ul_enter_wait,
			.name = "WAIT",
			.desc = "Clock off",
		},
		/* LOW POWER IDLE */
		{
			/*
			 * RBC 130us + ARM gating 1370us + RBC clear 65us
			 * + PLL2 relock 450us and some margin, here set
			 * it to 2100us.
			 */
			.exit_latency = 2100,
			.target_residency = 2500,
			.enter = imx6ul_enter_wait,
			.name = "LOW-POWER-IDLE",
			.desc = "ARM power off",
		}
	},
	.state_count = 3,
	.safe_state_index = 0,
};

int __init imx6ul_cpuidle_init(void)
{
	void __iomem *anatop_base = (void __iomem *)IMX_IO_P2V(MX6Q_ANATOP_BASE_ADDR);
	u32 val;
#ifdef CONFIG_CPU_FREQ
	struct imx6_cpuidle_pm_info *cpuidle_pm_info;
	int i;
	const u32 *mmdc_offset_array;
	u32 wfi_code_size;

	wfi_iram_base_phys = (void *)(iram_tlb_phys_addr + MX6_CPUIDLE_IRAM_ADDR_OFFSET);

	/* Make sure wfi_iram_base is 8 byte aligned. */
	if ((uintptr_t)(wfi_iram_base_phys) & (FNCPY_ALIGN - 1))
		wfi_iram_base_phys += FNCPY_ALIGN - ((uintptr_t)wfi_iram_base_phys % (FNCPY_ALIGN));

	wfi_iram_base = (void *)IMX_IO_P2V((unsigned long) wfi_iram_base_phys);

	cpuidle_pm_info = wfi_iram_base;
	cpuidle_pm_info->pbase = (phys_addr_t) wfi_iram_base_phys;
	cpuidle_pm_info->pm_info_size = sizeof(*cpuidle_pm_info);
	cpuidle_pm_info->resume_addr = virt_to_phys(v7_cpu_resume);
	cpuidle_pm_info->mmdc_io_num = ARRAY_SIZE(imx6ul_mmdc_io_offset);
	mmdc_offset_array = imx6ul_mmdc_io_offset;

	cpuidle_pm_info->mmdc_base.pbase = MX6Q_MMDC_P0_BASE_ADDR;
	cpuidle_pm_info->mmdc_base.vbase = (void __iomem *)IMX_IO_P2V(MX6Q_MMDC_P0_BASE_ADDR);

	cpuidle_pm_info->ccm_base.pbase = MX6Q_CCM_BASE_ADDR;
	cpuidle_pm_info->ccm_base.vbase = (void __iomem *)IMX_IO_P2V(MX6Q_CCM_BASE_ADDR);

	cpuidle_pm_info->anatop_base.pbase = MX6Q_ANATOP_BASE_ADDR;
	cpuidle_pm_info->anatop_base.vbase = (void __iomem *)IMX_IO_P2V(MX6Q_ANATOP_BASE_ADDR);

	cpuidle_pm_info->gpc_base.pbase = MX6Q_GPC_BASE_ADDR;
	cpuidle_pm_info->gpc_base.vbase = (void __iomem *)IMX_IO_P2V(MX6Q_GPC_BASE_ADDR);

	cpuidle_pm_info->iomuxc_base.pbase = MX6Q_IOMUXC_BASE_ADDR;
	cpuidle_pm_info->iomuxc_base.vbase = (void __iomem *)IMX_IO_P2V(MX6Q_IOMUXC_BASE_ADDR);

	cpuidle_pm_info->src_base.pbase = MX6Q_SRC_BASE_ADDR;
	cpuidle_pm_info->src_base.vbase = (void __iomem *)IMX_IO_P2V(MX6Q_SRC_BASE_ADDR);

	/* Only save mmdc io offset, settings will be saved in asm code */
	for (i = 0; i < cpuidle_pm_info->mmdc_io_num; i++)
		cpuidle_pm_info->mmdc_io_val[i][0] = mmdc_offset_array[i];

	/* calculate the wfi code size */
	if (cpu_is_imx6ul()) {
		wfi_code_size = (&mx6ul_lpm_wfi_end -&mx6ul_lpm_wfi_start) *4;

		imx6ul_wfi_in_iram_fn = (void *)fncpy(wfi_iram_base + sizeof(*cpuidle_pm_info),
			&imx6ul_low_power_idle, wfi_code_size);
	} else {
		wfi_code_size = (&mx6ull_lpm_wfi_end -&mx6ull_lpm_wfi_start) *4;

		imx6ul_wfi_in_iram_fn = (void *)fncpy(wfi_iram_base + sizeof(*cpuidle_pm_info),
			&imx6ull_low_power_idle, wfi_code_size);
	}
#endif

	imx6q_set_int_mem_clk_lpm(true);

	/*
	 * enable RC-OSC here, as it needs at least 4ms for RC-OSC to
	 * be stable, low power idle flow can NOT endure this big
	 * latency, so we make RC-OSC self-tuning enabled here.
	 */
	val = readl_relaxed(anatop_base + PMU_LOW_PWR_CTRL);
	val |= 0x1;
	writel_relaxed(val, anatop_base + PMU_LOW_PWR_CTRL);
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
	/* wait 4ms according to hardware design */
	msleep(4);
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
	writel_relaxed(val, anatop_base  + XTALOSC24M_OSC_CONFIG0);
	/* set the count_1m_trg = 0x2d7 */
	val = readl_relaxed(anatop_base  + XTALOSC24M_OSC_CONFIG2);
	val &= ~(XTALOSC24M_OSC_CONFIG2_COUNT_1M_TRG_MASK <<
		XTALOSC24M_OSC_CONFIG2_COUNT_1M_TRG_SHIFT);
	val |= 0x2d7 << XTALOSC24M_OSC_CONFIG2_COUNT_1M_TRG_SHIFT;
	writel_relaxed(val, anatop_base  + XTALOSC24M_OSC_CONFIG2);
	/*
	 * hardware design require to write XTALOSC24M_OSC_CONFIG0 or
	 * XTALOSC24M_OSC_CONFIG1 to
	 * make XTALOSC24M_OSC_CONFIG2 write work
	 */
	val = readl_relaxed(anatop_base  + XTALOSC24M_OSC_CONFIG1);

	/* ARM power up time is reduced since TO1.1 */
	if (imx_get_soc_revision() > IMX_CHIP_REVISION_1_0)
		return cpuidle_register(&imx6ul_cpuidle_driver_v2, NULL);
	else
		return cpuidle_register(&imx6ul_cpuidle_driver, NULL);
}
