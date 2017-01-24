/*
 * TI da8xx master peripheral priority driver
 *
 * Copyright (C) 2016 BayLibre SAS
 *
 * Author:
 *   Bartosz Golaszewski <bgolaszewski@baylibre.com.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/regmap.h>

/*
 * REVISIT: Linux doesn't have a good framework for the kind of performance
 * knobs this driver controls. We can't use device tree properties as it deals
 * with hardware configuration rather than description. We also don't want to
 * commit to maintaining some random sysfs attributes.
 *
 * For now we just hardcode the register values for the boards that need
 * some changes (as is the case for the LCD controller on da850-lcdk - the
 * first board we support here). When linux gets an appropriate framework,
 * we'll easily convert the driver to it.
 */

#define DA8XX_MSTPRI0_OFFSET		0
#define DA8XX_MSTPRI1_OFFSET		4
#define DA8XX_MSTPRI2_OFFSET		8

enum {
	DA8XX_MSTPRI_ARM_I = 0,
	DA8XX_MSTPRI_ARM_D,
	DA8XX_MSTPRI_UPP,
	DA8XX_MSTPRI_SATA,
	DA8XX_MSTPRI_PRU0,
	DA8XX_MSTPRI_PRU1,
	DA8XX_MSTPRI_EDMA30TC0,
	DA8XX_MSTPRI_EDMA30TC1,
	DA8XX_MSTPRI_EDMA31TC0,
	DA8XX_MSTPRI_VPIF_DMA_0,
	DA8XX_MSTPRI_VPIF_DMA_1,
	DA8XX_MSTPRI_EMAC,
	DA8XX_MSTPRI_USB0CFG,
	DA8XX_MSTPRI_USB0CDMA,
	DA8XX_MSTPRI_UHPI,
	DA8XX_MSTPRI_USB1,
	DA8XX_MSTPRI_LCDC,
};

struct da8xx_mstpri_descr {
	int reg;
	int shift;
	int mask;
};

static const struct da8xx_mstpri_descr da8xx_mstpri_priority_list[] = {
	[DA8XX_MSTPRI_ARM_I] = {
		.reg = DA8XX_MSTPRI0_OFFSET,
		.shift = 0,
		.mask = 0x0000000f,
	},
	[DA8XX_MSTPRI_ARM_D] = {
		.reg = DA8XX_MSTPRI0_OFFSET,
		.shift = 4,
		.mask = 0x000000f0,
	},
	[DA8XX_MSTPRI_UPP] = {
		.reg = DA8XX_MSTPRI0_OFFSET,
		.shift = 16,
		.mask = 0x000f0000,
	},
	[DA8XX_MSTPRI_SATA] = {
		.reg = DA8XX_MSTPRI0_OFFSET,
		.shift = 20,
		.mask = 0x00f00000,
	},
	[DA8XX_MSTPRI_PRU0] = {
		.reg = DA8XX_MSTPRI1_OFFSET,
		.shift = 0,
		.mask = 0x0000000f,
	},
	[DA8XX_MSTPRI_PRU1] = {
		.reg = DA8XX_MSTPRI1_OFFSET,
		.shift = 4,
		.mask = 0x000000f0,
	},
	[DA8XX_MSTPRI_EDMA30TC0] = {
		.reg = DA8XX_MSTPRI1_OFFSET,
		.shift = 8,
		.mask = 0x00000f00,
	},
	[DA8XX_MSTPRI_EDMA30TC1] = {
		.reg = DA8XX_MSTPRI1_OFFSET,
		.shift = 12,
		.mask = 0x0000f000,
	},
	[DA8XX_MSTPRI_EDMA31TC0] = {
		.reg = DA8XX_MSTPRI1_OFFSET,
		.shift = 16,
		.mask = 0x000f0000,
	},
	[DA8XX_MSTPRI_VPIF_DMA_0] = {
		.reg = DA8XX_MSTPRI1_OFFSET,
		.shift = 24,
		.mask = 0x0f000000,
	},
	[DA8XX_MSTPRI_VPIF_DMA_1] = {
		.reg = DA8XX_MSTPRI1_OFFSET,
		.shift = 28,
		.mask = 0xf0000000,
	},
	[DA8XX_MSTPRI_EMAC] = {
		.reg = DA8XX_MSTPRI2_OFFSET,
		.shift = 0,
		.mask = 0x0000000f,
	},
	[DA8XX_MSTPRI_USB0CFG] = {
		.reg = DA8XX_MSTPRI2_OFFSET,
		.shift = 8,
		.mask = 0x00000f00,
	},
	[DA8XX_MSTPRI_USB0CDMA] = {
		.reg = DA8XX_MSTPRI2_OFFSET,
		.shift = 12,
		.mask = 0x0000f000,
	},
	[DA8XX_MSTPRI_UHPI] = {
		.reg = DA8XX_MSTPRI2_OFFSET,
		.shift = 20,
		.mask = 0x00f00000,
	},
	[DA8XX_MSTPRI_USB1] = {
		.reg = DA8XX_MSTPRI2_OFFSET,
		.shift = 24,
		.mask = 0x0f000000,
	},
	[DA8XX_MSTPRI_LCDC] = {
		.reg = DA8XX_MSTPRI2_OFFSET,
		.shift = 28,
		.mask = 0xf0000000,
	},
};

struct da8xx_mstpri_priority {
	int which;
	u32 val;
};

struct da8xx_mstpri_board_priorities {
	const char *board;
	const struct da8xx_mstpri_priority *priorities;
	size_t numprio;
};

/*
 * Default memory settings of da850 do not meet the throughput/latency
 * requirements of tilcdc. This results in the image displayed being
 * incorrect and the following warning being displayed by the LCDC
 * drm driver:
 *
 *   tilcdc da8xx_lcdc.0: tilcdc_crtc_irq(0x00000020): FIFO underfow
 */
static const struct da8xx_mstpri_priority da850_lcdk_priorities[] = {
	{
		.which = DA8XX_MSTPRI_LCDC,
		.val = 0,
	},
	{
		.which = DA8XX_MSTPRI_EDMA30TC1,
		.val = 0,
	},
	{
		.which = DA8XX_MSTPRI_EDMA30TC0,
		.val = 1,
	},
};

static const struct da8xx_mstpri_board_priorities da8xx_mstpri_board_confs[] = {
	{
		.board = "ti,da850-lcdk",
		.priorities = da850_lcdk_priorities,
		.numprio = ARRAY_SIZE(da850_lcdk_priorities),
	},
};

static const struct da8xx_mstpri_board_priorities *
da8xx_mstpri_get_board_prio(void)
{
	const struct da8xx_mstpri_board_priorities *board_prio;
	int i;

	for (i = 0; i < ARRAY_SIZE(da8xx_mstpri_board_confs); i++) {
		board_prio = &da8xx_mstpri_board_confs[i];

		if (of_machine_is_compatible(board_prio->board))
			return board_prio;
	}

	return NULL;
}

static int da8xx_mstpri_probe(struct platform_device *pdev)
{
	const struct da8xx_mstpri_board_priorities *prio_list;
	const struct da8xx_mstpri_descr *prio_descr;
	const struct da8xx_mstpri_priority *prio;
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem *mstpri;
	u32 reg;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mstpri = devm_ioremap_resource(dev, res);
	if (IS_ERR(mstpri)) {
		dev_err(dev, "unable to map MSTPRI registers\n");
		return PTR_ERR(mstpri);
	}

	prio_list = da8xx_mstpri_get_board_prio();
	if (!prio_list) {
		dev_err(dev, "no master priorities defined for this board\n");
		return -EINVAL;
	}

	for (i = 0; i < prio_list->numprio; i++) {
		prio = &prio_list->priorities[i];
		prio_descr = &da8xx_mstpri_priority_list[prio->which];

		if (prio_descr->reg + sizeof(u32) > resource_size(res)) {
			dev_warn(dev, "register offset out of range\n");
			continue;
		}

		reg = readl(mstpri + prio_descr->reg);
		reg &= ~prio_descr->mask;
		reg |= prio->val << prio_descr->shift;

		writel(reg, mstpri + prio_descr->reg);
	}

	return 0;
}

static const struct of_device_id da8xx_mstpri_of_match[] = {
	{ .compatible = "ti,da850-mstpri", },
	{ },
};

static struct platform_driver da8xx_mstpri_driver = {
	.probe = da8xx_mstpri_probe,
	.driver = {
		.name = "da8xx-mstpri",
		.of_match_table = da8xx_mstpri_of_match,
	},
};
module_platform_driver(da8xx_mstpri_driver);

MODULE_AUTHOR("Bartosz Golaszewski <bgolaszewski@baylibre.com>");
MODULE_DESCRIPTION("TI da8xx master peripheral priority driver");
MODULE_LICENSE("GPL v2");
