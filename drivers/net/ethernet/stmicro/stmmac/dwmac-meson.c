/*
 * Amlogic Meson DWMAC glue layer
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/stmmac.h>

#define ETHMAC_SPEED_100	BIT(1)

struct meson_dwmac {
	struct device	*dev;
	void __iomem	*reg;
};

static void meson6_dwmac_fix_mac_speed(void *priv, unsigned int speed)
{
	struct meson_dwmac *dwmac = priv;
	unsigned int val;

	val = readl(dwmac->reg);

	switch (speed) {
	case SPEED_10:
		val &= ~ETHMAC_SPEED_100;
		break;
	case SPEED_100:
		val |= ETHMAC_SPEED_100;
		break;
	}

	writel(val, dwmac->reg);
}

static void *meson6_dwmac_setup(struct platform_device *pdev)
{
	struct meson_dwmac *dwmac;
	struct resource *res;

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return ERR_PTR(-ENOMEM);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	dwmac->reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dwmac->reg))
		return dwmac->reg;

	return dwmac;
}

const struct stmmac_of_data meson6_dwmac_data = {
	.setup		= meson6_dwmac_setup,
	.fix_mac_speed	= meson6_dwmac_fix_mac_speed,
};
