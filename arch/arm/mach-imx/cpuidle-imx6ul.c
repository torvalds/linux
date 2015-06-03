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

extern unsigned long iram_tlb_phys_addr;
static void __iomem *wfi_iram_base;

#ifdef CONFIG_CPU_FREQ
static void __iomem *wfi_iram_base_phys;
extern unsigned long mx6ul_lpm_wfi_start asm("mx6ul_lpm_wfi_start");
extern unsigned long mx6ul_lpm_wfi_end asm("mx6ul_lpm_wfi_end");
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
		/* Need to notify there is a cpu pm operation. */
		cpu_pm_enter();
		cpu_cluster_pm_enter();

		cpu_suspend(0, imx6ul_idle_finish);

		cpu_cluster_pm_exit();
		cpu_pm_exit();
		imx6_enable_rbc(false);
	}

	imx6q_set_lpm(WAIT_CLOCKED);

	return index;
}

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
			 * RBC 31us + ARM gating 93us + RBC clear 65us
			 * + PLL2 relock 450us and some margin, here set
			 * it to 650us.
			 */
			.exit_latency = 650,
			.target_residency = 1000,
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
	wfi_code_size = (&mx6ul_lpm_wfi_end -&mx6ul_lpm_wfi_start) *4;

	imx6ul_wfi_in_iram_fn = (void *)fncpy(wfi_iram_base + sizeof(*cpuidle_pm_info),
		&imx6ul_low_power_idle, wfi_code_size);
#endif
	return cpuidle_register(&imx6ul_cpuidle_driver, NULL);
}
