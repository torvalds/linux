/*
 * Copyright (C) 2014-2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/busfreq-imx.h>
#include <linux/cpuidle.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <asm/cpuidle.h>
#include <asm/fncpy.h>
#include <asm/proc-fns.h>

#include "common.h"
#include "cpuidle.h"
#include "hardware.h"

#define MAX_MMDC_IO_NUM		19

static void __iomem *wfi_iram_base;
extern unsigned long iram_tlb_base_addr;

#ifdef CONFIG_CPU_FREQ
extern unsigned long mx6sl_lpm_wfi_start asm("mx6sl_lpm_wfi_start");
extern unsigned long mx6sl_lpm_wfi_end asm("mx6sl_lpm_wfi_end");
#endif

struct imx6_cpuidle_pm_info {
	u32 pm_info_size; /* Size of pm_info */
	u32 ttbr;
	void __iomem *mmdc_base;
	void __iomem *iomuxc_base;
	void __iomem *ccm_base;
	void __iomem *l2_base;
	void __iomem *anatop_base;
	u32 mmdc_io_num; /*Number of MMDC IOs which need saved/restored. */
	u32 mmdc_io_val[MAX_MMDC_IO_NUM][2]; /* To save offset and value */
} __aligned(8);

static const u32 imx6sl_mmdc_io_offset[] __initconst = {
	0x30c, 0x310, 0x314, 0x318, /* DQM0 ~ DQM3 */
	0x5c4, 0x5cc, 0x5d4, 0x5d8, /* GPR_B0DS ~ GPR_B3DS */
	0x300, 0x31c, 0x338, 0x5ac, /*CAS, RAS, SDCLK_0, GPR_ADDS */
	0x33c, 0x340, 0x5b0, 0x5c0, /*SODT0, SODT1, ,MODE_CTL, MODE */
	0x330, 0x334, 0x320,	    /*SDCKE0, SDCK1, RESET */
};

static struct regulator *vbus_ldo;
static struct regulator_dev *ldo2p5_dummy_regulator_rdev;
static struct regulator_init_data ldo2p5_dummy_initdata = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};
static int ldo2p5_dummy_enable;

static void (*imx6sl_wfi_in_iram_fn)(void __iomem *iram_vbase,
		int audio_mode, bool vbus_ldo);

static int imx6sl_enter_wait(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv, int index)
{
	int mode = get_bus_freq_mode();

	imx6q_set_lpm(WAIT_UNCLOCKED);
	if ((mode == BUS_FREQ_AUDIO) || (mode == BUS_FREQ_ULTRA_LOW)) {
		imx6sl_wfi_in_iram_fn(wfi_iram_base, (mode == BUS_FREQ_AUDIO) ? 1 : 0 ,
			ldo2p5_dummy_enable);
	} else {
	/*
	 * Software workaround for ERR005311, see function
	 * description for details.
	 */
	imx6sl_set_wait_clk(true);
	cpu_do_idle();
	imx6sl_set_wait_clk(false);
	}
	imx6q_set_lpm(WAIT_CLOCKED);

	return index;
}

static struct cpuidle_driver imx6sl_cpuidle_driver = {
	.name = "imx6sl_cpuidle",
	.owner = THIS_MODULE,
	.states = {
		/* WFI */
		ARM_CPUIDLE_WFI_STATE,
		/* WAIT */
		{
			.exit_latency = 50,
			.target_residency = 75,
			.flags = CPUIDLE_FLAG_TIMER_STOP,
			.enter = imx6sl_enter_wait,
			.name = "WAIT",
			.desc = "Clock off",
		},
	},
	.state_count = 2,
	.safe_state_index = 0,
};

int __init imx6sl_cpuidle_init(void)
{

#ifdef CONFIG_CPU_FREQ
	struct imx6_cpuidle_pm_info *pm_info;
	int i;
	const u32 *mmdc_offset_array;
	u32 wfi_code_size;

	vbus_ldo = regulator_get(NULL, "ldo2p5-dummy");
	if (IS_ERR(vbus_ldo))
		vbus_ldo = NULL;

	wfi_iram_base = (void *)(iram_tlb_base_addr + MX6_CPUIDLE_IRAM_ADDR_OFFSET);

	/* Make sure wif_iram_base is 8 byte aligned. */
	if ((uintptr_t)(wfi_iram_base) & (FNCPY_ALIGN - 1))
		wfi_iram_base += FNCPY_ALIGN - ((uintptr_t)wfi_iram_base % (FNCPY_ALIGN));

	pm_info = wfi_iram_base;
	pm_info->pm_info_size = sizeof(*pm_info);
	pm_info->mmdc_io_num = ARRAY_SIZE(imx6sl_mmdc_io_offset);
	mmdc_offset_array = imx6sl_mmdc_io_offset;
	pm_info->mmdc_base = (void __iomem *)IMX_IO_P2V(MX6Q_MMDC_P0_BASE_ADDR);
	pm_info->ccm_base = (void __iomem *)IMX_IO_P2V(MX6Q_CCM_BASE_ADDR);
	pm_info->anatop_base = (void __iomem *)IMX_IO_P2V(MX6Q_ANATOP_BASE_ADDR);
	pm_info->iomuxc_base = (void __iomem *)IMX_IO_P2V(MX6Q_IOMUXC_BASE_ADDR);
	pm_info->l2_base = (void __iomem *)IMX_IO_P2V(MX6Q_L2_BASE_ADDR);

	/* Only save mmdc io offset, settings will be saved in asm code */
	for (i = 0; i < pm_info->mmdc_io_num; i++)
		pm_info->mmdc_io_val[i][0] = mmdc_offset_array[i];

	/* calculate the wfi code size */
	wfi_code_size = (&mx6sl_lpm_wfi_end -&mx6sl_lpm_wfi_start) *4;

	imx6sl_wfi_in_iram_fn = (void *)fncpy(wfi_iram_base + sizeof(*pm_info),
		&imx6sl_low_power_idle, wfi_code_size);
#endif

	return cpuidle_register(&imx6sl_cpuidle_driver, NULL);
}

static int imx_ldo2p5_dummy_enable(struct regulator_dev *rdev)
{
	ldo2p5_dummy_enable = 1;
	return 0;
}

static int imx_ldo2p5_dummy_disable(struct regulator_dev *rdev)
{
	ldo2p5_dummy_enable = 0;
	return 0;
}

static int imx_ldo2p5_dummy_is_enable(struct regulator_dev *rdev)
{
	return ldo2p5_dummy_enable;
}

static struct regulator_ops ldo2p5_dummy_ops = {
	.enable = imx_ldo2p5_dummy_enable,
	.disable = imx_ldo2p5_dummy_disable,
	.is_enabled = imx_ldo2p5_dummy_is_enable,
};

static struct regulator_desc ldo2p5_dummy_desc = {
	.name = "ldo2p5-dummy",
	.id = -1,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &ldo2p5_dummy_ops,
};

static int ldo2p5_dummy_probe(struct platform_device *pdev)
{
	struct regulator_config config = { };
	int ret;

	config.dev = &pdev->dev;
	config.init_data = &ldo2p5_dummy_initdata;
	config.of_node = pdev->dev.of_node;

	ldo2p5_dummy_regulator_rdev = regulator_register(&ldo2p5_dummy_desc, &config);
	if (IS_ERR(ldo2p5_dummy_regulator_rdev)) {
		ret = PTR_ERR(ldo2p5_dummy_regulator_rdev);
		dev_err(&pdev->dev, "Failed to register dummy ldo2p5 regulator: %d\n", ret);
		return ret;
	}
	return 0;
}

static const struct of_device_id imx_ldo2p5_dummy_ids[] = {
	{ .compatible = "fsl,imx6-dummy-ldo2p5"},
	};
MODULE_DEVICE_TABLE(ofm, imx_ldo2p5_dummy_ids);

static struct platform_driver ldo2p5_dummy_driver = {
	.probe = ldo2p5_dummy_probe,
	.driver = {
		.name = "ldo2p5-dummy",
		.owner = THIS_MODULE,
		.of_match_table = imx_ldo2p5_dummy_ids,
	},
};

module_platform_driver(ldo2p5_dummy_driver);
