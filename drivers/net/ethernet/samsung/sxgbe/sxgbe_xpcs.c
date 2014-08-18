/* 10G controller driver for Samsung SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Siva Reddy Kallam <siva.kallam@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include "sxgbe_common.h"
#include "sxgbe_xpcs.h"

static int sxgbe_xpcs_read(struct net_device *ndev, unsigned int reg)
{
	u32 value;
	struct sxgbe_priv_data *priv = netdev_priv(ndev);

	value = readl(priv->ioaddr + XPCS_OFFSET + reg);

	return value;
}

static int sxgbe_xpcs_write(struct net_device *ndev, int reg, int data)
{
	struct sxgbe_priv_data *priv = netdev_priv(ndev);

	writel(data, priv->ioaddr + XPCS_OFFSET + reg);

	return 0;
}

int sxgbe_xpcs_init(struct net_device *ndev)
{
	u32 value;

	value = sxgbe_xpcs_read(ndev, SR_PCS_MMD_CONTROL1);
	/* 10G XAUI mode */
	sxgbe_xpcs_write(ndev, SR_PCS_CONTROL2, XPCS_TYPE_SEL_X);
	sxgbe_xpcs_write(ndev, VR_PCS_MMD_XAUI_MODE_CONTROL, XPCS_XAUI_MODE);
	sxgbe_xpcs_write(ndev, VR_PCS_MMD_XAUI_MODE_CONTROL, value | BIT(13));
	sxgbe_xpcs_write(ndev, SR_PCS_MMD_CONTROL1, value | BIT(11));

	do {
		value = sxgbe_xpcs_read(ndev, VR_PCS_MMD_DIGITAL_STATUS);
	} while ((value & XPCS_QSEQ_STATE_MPLLOFF) == XPCS_QSEQ_STATE_STABLE);

	value = sxgbe_xpcs_read(ndev, SR_PCS_MMD_CONTROL1);
	sxgbe_xpcs_write(ndev, SR_PCS_MMD_CONTROL1, value & ~BIT(11));

	do {
		value = sxgbe_xpcs_read(ndev, VR_PCS_MMD_DIGITAL_STATUS);
	} while ((value & XPCS_QSEQ_STATE_MPLLOFF) != XPCS_QSEQ_STATE_STABLE);

	return 0;
}

int sxgbe_xpcs_init_1G(struct net_device *ndev)
{
	int value;

	/* 10GBASE-X PCS (1G) mode */
	sxgbe_xpcs_write(ndev, SR_PCS_CONTROL2, XPCS_TYPE_SEL_X);
	sxgbe_xpcs_write(ndev, VR_PCS_MMD_XAUI_MODE_CONTROL, XPCS_XAUI_MODE);
	value = sxgbe_xpcs_read(ndev, SR_PCS_MMD_CONTROL1);
	sxgbe_xpcs_write(ndev, SR_PCS_MMD_CONTROL1, value & ~BIT(13));

	value = sxgbe_xpcs_read(ndev, SR_MII_MMD_CONTROL);
	sxgbe_xpcs_write(ndev, SR_MII_MMD_CONTROL, value | BIT(6));
	sxgbe_xpcs_write(ndev, SR_MII_MMD_CONTROL, value & ~BIT(13));
	value = sxgbe_xpcs_read(ndev, SR_PCS_MMD_CONTROL1);
	sxgbe_xpcs_write(ndev, SR_PCS_MMD_CONTROL1, value | BIT(11));

	do {
		value = sxgbe_xpcs_read(ndev, VR_PCS_MMD_DIGITAL_STATUS);
	} while ((value & XPCS_QSEQ_STATE_MPLLOFF) != XPCS_QSEQ_STATE_STABLE);

	value = sxgbe_xpcs_read(ndev, SR_PCS_MMD_CONTROL1);
	sxgbe_xpcs_write(ndev, SR_PCS_MMD_CONTROL1, value & ~BIT(11));

	/* Auto Negotiation cluase 37 enable */
	value = sxgbe_xpcs_read(ndev, SR_MII_MMD_CONTROL);
	sxgbe_xpcs_write(ndev, SR_MII_MMD_CONTROL, value | BIT(12));

	return 0;
}
