// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP1 Dual-Mode Timers - platform device registration
 *
 * Contains first level initialization routines which internally
 * generates timer device information and registers with linux
 * device model. It also has a low level function to change the timer
 * input clock source.
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - https://www.ti.com/
 * Tarun Kanti DebBarma <tarun.kanti@ti.com>
 * Thara Gopinath <thara@ti.com>
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/platform_data/dmtimer-omap.h>
#include <linux/soc/ti/omap1-io.h>

#include <clocksource/timer-ti-dm.h>

#include "soc.h"

#define OMAP1610_GPTIMER1_BASE		0xfffb1400
#define OMAP1610_GPTIMER2_BASE		0xfffb1c00
#define OMAP1610_GPTIMER3_BASE		0xfffb2400
#define OMAP1610_GPTIMER4_BASE		0xfffb2c00
#define OMAP1610_GPTIMER5_BASE		0xfffb3400
#define OMAP1610_GPTIMER6_BASE		0xfffb3c00
#define OMAP1610_GPTIMER7_BASE		0xfffb7400
#define OMAP1610_GPTIMER8_BASE		0xfffbd400

#define OMAP1_DM_TIMER_COUNT		8

static int omap1_dm_timer_set_src(struct platform_device *pdev,
				int source)
{
	int n = (pdev->id - 1) << 1;
	u32 l;

	l = omap_readl(MOD_CONF_CTRL_1) & ~(0x03 << n);
	l |= source << n;
	omap_writel(l, MOD_CONF_CTRL_1);

	return 0;
}

static int __init omap1_dm_timer_init(void)
{
	int i;
	int ret;
	struct dmtimer_platform_data *pdata;
	struct platform_device *pdev;

	if (!cpu_is_omap16xx())
		return 0;

	for (i = 1; i <= OMAP1_DM_TIMER_COUNT; i++) {
		struct resource res[2];
		u32 base, irq;

		switch (i) {
		case 1:
			base = OMAP1610_GPTIMER1_BASE;
			irq = INT_1610_GPTIMER1;
			break;
		case 2:
			base = OMAP1610_GPTIMER2_BASE;
			irq = INT_1610_GPTIMER2;
			break;
		case 3:
			base = OMAP1610_GPTIMER3_BASE;
			irq = INT_1610_GPTIMER3;
			break;
		case 4:
			base = OMAP1610_GPTIMER4_BASE;
			irq = INT_1610_GPTIMER4;
			break;
		case 5:
			base = OMAP1610_GPTIMER5_BASE;
			irq = INT_1610_GPTIMER5;
			break;
		case 6:
			base = OMAP1610_GPTIMER6_BASE;
			irq = INT_1610_GPTIMER6;
			break;
		case 7:
			base = OMAP1610_GPTIMER7_BASE;
			irq = INT_1610_GPTIMER7;
			break;
		case 8:
			base = OMAP1610_GPTIMER8_BASE;
			irq = INT_1610_GPTIMER8;
			break;
		default:
			/*
			 * not supposed to reach here.
			 * this is to remove warning.
			 */
			return -EINVAL;
		}

		pdev = platform_device_alloc("omap_timer", i);
		if (!pdev) {
			pr_err("%s: Failed to device alloc for dmtimer%d\n",
				__func__, i);
			return -ENOMEM;
		}

		memset(res, 0, 2 * sizeof(struct resource));
		res[0].start = base;
		res[0].end = base + 0x46;
		res[0].flags = IORESOURCE_MEM;
		res[1].start = irq;
		res[1].end = irq;
		res[1].flags = IORESOURCE_IRQ;
		ret = platform_device_add_resources(pdev, res,
				ARRAY_SIZE(res));
		if (ret) {
			dev_err(&pdev->dev, "%s: Failed to add resources.\n",
				__func__);
			goto err_free_pdev;
		}

		pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			ret = -ENOMEM;
			goto err_free_pdata;
		}

		pdata->set_timer_src = omap1_dm_timer_set_src;
		pdata->timer_capability = OMAP_TIMER_ALWON |
				OMAP_TIMER_NEEDS_RESET | OMAP_TIMER_HAS_DSP_IRQ;

		ret = platform_device_add_data(pdev, pdata, sizeof(*pdata));
		if (ret) {
			dev_err(&pdev->dev, "%s: Failed to add platform data.\n",
				__func__);
			goto err_free_pdata;
		}

		ret = platform_device_add(pdev);
		if (ret) {
			dev_err(&pdev->dev, "%s: Failed to add platform device.\n",
				__func__);
			goto err_free_pdata;
		}

		dev_dbg(&pdev->dev, " Registered.\n");
	}

	return 0;

err_free_pdata:
	kfree(pdata);

err_free_pdev:
	platform_device_put(pdev);

	return ret;
}
arch_initcall(omap1_dm_timer_init);
