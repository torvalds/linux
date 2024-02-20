// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP.
 */

#include <linux/device.h>
#include <linux/of.h>
#include <linux/slab.h>

#include "dcss-dev.h"

#define DCSS_BLKCTL_RESET_CTRL		0x00
#define   B_CLK_RESETN			BIT(0)
#define   APB_CLK_RESETN		BIT(1)
#define   P_CLK_RESETN			BIT(2)
#define   RTR_CLK_RESETN		BIT(4)
#define DCSS_BLKCTL_CONTROL0		0x10
#define   HDMI_MIPI_CLK_SEL		BIT(0)
#define   DISPMIX_REFCLK_SEL_POS	4
#define   DISPMIX_REFCLK_SEL_MASK	GENMASK(5, 4)
#define   DISPMIX_PIXCLK_SEL		BIT(8)
#define   HDMI_SRC_SECURE_EN		BIT(16)

struct dcss_blkctl {
	struct dcss_dev *dcss;
	void __iomem *base_reg;
};

void dcss_blkctl_cfg(struct dcss_blkctl *blkctl)
{
	if (blkctl->dcss->hdmi_output)
		dcss_writel(0, blkctl->base_reg + DCSS_BLKCTL_CONTROL0);
	else
		dcss_writel(DISPMIX_PIXCLK_SEL,
			    blkctl->base_reg + DCSS_BLKCTL_CONTROL0);

	dcss_set(B_CLK_RESETN | APB_CLK_RESETN | P_CLK_RESETN | RTR_CLK_RESETN,
		 blkctl->base_reg + DCSS_BLKCTL_RESET_CTRL);
}

int dcss_blkctl_init(struct dcss_dev *dcss, unsigned long blkctl_base)
{
	struct dcss_blkctl *blkctl;

	blkctl = devm_kzalloc(dcss->dev, sizeof(*blkctl), GFP_KERNEL);
	if (!blkctl)
		return -ENOMEM;

	blkctl->base_reg = devm_ioremap(dcss->dev, blkctl_base, SZ_4K);
	if (!blkctl->base_reg) {
		dev_err(dcss->dev, "unable to remap BLK CTRL base\n");
		return -ENOMEM;
	}

	dcss->blkctl = blkctl;
	blkctl->dcss = dcss;

	dcss_blkctl_cfg(blkctl);

	return 0;
}
