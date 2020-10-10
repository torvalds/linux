/*
 * Samsung SoC USB 1.1/2.0 PHY driver - S5PV210 support
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Authors: Kamil Debski <k.debski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/phy/phy.h>
#include "phy-samsung-usb2.h"

/* Exynos USB PHY registers */

/* PHY power control */
#define S5PV210_UPHYPWR			0x0

#define S5PV210_UPHYPWR_PHY0_SUSPEND	BIT(0)
#define S5PV210_UPHYPWR_PHY0_PWR	BIT(3)
#define S5PV210_UPHYPWR_PHY0_OTG_PWR	BIT(4)
#define S5PV210_UPHYPWR_PHY0	( \
	S5PV210_UPHYPWR_PHY0_SUSPEND | \
	S5PV210_UPHYPWR_PHY0_PWR | \
	S5PV210_UPHYPWR_PHY0_OTG_PWR)

#define S5PV210_UPHYPWR_PHY1_SUSPEND	BIT(6)
#define S5PV210_UPHYPWR_PHY1_PWR	BIT(7)
#define S5PV210_UPHYPWR_PHY1 ( \
	S5PV210_UPHYPWR_PHY1_SUSPEND | \
	S5PV210_UPHYPWR_PHY1_PWR)

/* PHY clock control */
#define S5PV210_UPHYCLK			0x4

#define S5PV210_UPHYCLK_PHYFSEL_MASK	(0x3 << 0)
#define S5PV210_UPHYCLK_PHYFSEL_48MHZ	(0x0 << 0)
#define S5PV210_UPHYCLK_PHYFSEL_24MHZ	(0x3 << 0)
#define S5PV210_UPHYCLK_PHYFSEL_12MHZ	(0x2 << 0)

#define S5PV210_UPHYCLK_PHY0_ID_PULLUP	BIT(2)
#define S5PV210_UPHYCLK_PHY0_COMMON_ON	BIT(4)
#define S5PV210_UPHYCLK_PHY1_COMMON_ON	BIT(7)

/* PHY reset control */
#define S5PV210_UPHYRST			0x8

#define S5PV210_URSTCON_PHY0		BIT(0)
#define S5PV210_URSTCON_OTG_HLINK	BIT(1)
#define S5PV210_URSTCON_OTG_PHYLINK	BIT(2)
#define S5PV210_URSTCON_PHY1_ALL	BIT(3)
#define S5PV210_URSTCON_HOST_LINK_ALL	BIT(4)

/* Isolation, configured in the power management unit */
#define S5PV210_USB_ISOL_OFFSET		0x680c
#define S5PV210_USB_ISOL_DEVICE		BIT(0)
#define S5PV210_USB_ISOL_HOST		BIT(1)


enum s5pv210_phy_id {
	S5PV210_DEVICE,
	S5PV210_HOST,
	S5PV210_NUM_PHYS,
};

/*
 * s5pv210_rate_to_clk() converts the supplied clock rate to the value that
 * can be written to the phy register.
 */
static int s5pv210_rate_to_clk(unsigned long rate, u32 *reg)
{
	switch (rate) {
	case 12 * MHZ:
		*reg = S5PV210_UPHYCLK_PHYFSEL_12MHZ;
		break;
	case 24 * MHZ:
		*reg = S5PV210_UPHYCLK_PHYFSEL_24MHZ;
		break;
	case 48 * MHZ:
		*reg = S5PV210_UPHYCLK_PHYFSEL_48MHZ;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void s5pv210_isol(struct samsung_usb2_phy_instance *inst, bool on)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	u32 mask;

	switch (inst->cfg->id) {
	case S5PV210_DEVICE:
		mask = S5PV210_USB_ISOL_DEVICE;
		break;
	case S5PV210_HOST:
		mask = S5PV210_USB_ISOL_HOST;
		break;
	default:
		return;
	}

	regmap_update_bits(drv->reg_pmu, S5PV210_USB_ISOL_OFFSET,
							mask, on ? 0 : mask);
}

static void s5pv210_phy_pwr(struct samsung_usb2_phy_instance *inst, bool on)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	u32 rstbits = 0;
	u32 phypwr = 0;
	u32 rst;
	u32 pwr;

	switch (inst->cfg->id) {
	case S5PV210_DEVICE:
		phypwr =	S5PV210_UPHYPWR_PHY0;
		rstbits =	S5PV210_URSTCON_PHY0;
		break;
	case S5PV210_HOST:
		phypwr =	S5PV210_UPHYPWR_PHY1;
		rstbits =	S5PV210_URSTCON_PHY1_ALL |
				S5PV210_URSTCON_HOST_LINK_ALL;
		break;
	}

	if (on) {
		writel(drv->ref_reg_val, drv->reg_phy + S5PV210_UPHYCLK);

		pwr = readl(drv->reg_phy + S5PV210_UPHYPWR);
		pwr &= ~phypwr;
		writel(pwr, drv->reg_phy + S5PV210_UPHYPWR);

		rst = readl(drv->reg_phy + S5PV210_UPHYRST);
		rst |= rstbits;
		writel(rst, drv->reg_phy + S5PV210_UPHYRST);
		udelay(10);
		rst &= ~rstbits;
		writel(rst, drv->reg_phy + S5PV210_UPHYRST);
		/* The following delay is necessary for the reset sequence to be
		 * completed
		 */
		udelay(80);
	} else {
		pwr = readl(drv->reg_phy + S5PV210_UPHYPWR);
		pwr |= phypwr;
		writel(pwr, drv->reg_phy + S5PV210_UPHYPWR);
	}
}

static int s5pv210_power_on(struct samsung_usb2_phy_instance *inst)
{
	s5pv210_isol(inst, 0);
	s5pv210_phy_pwr(inst, 1);

	return 0;
}

static int s5pv210_power_off(struct samsung_usb2_phy_instance *inst)
{
	s5pv210_phy_pwr(inst, 0);
	s5pv210_isol(inst, 1);

	return 0;
}

static const struct samsung_usb2_common_phy s5pv210_phys[S5PV210_NUM_PHYS] = {
	[S5PV210_DEVICE] = {
		.label		= "device",
		.id		= S5PV210_DEVICE,
		.power_on	= s5pv210_power_on,
		.power_off	= s5pv210_power_off,
	},
	[S5PV210_HOST] = {
		.label		= "host",
		.id		= S5PV210_HOST,
		.power_on	= s5pv210_power_on,
		.power_off	= s5pv210_power_off,
	},
};

const struct samsung_usb2_phy_config s5pv210_usb2_phy_config = {
	.num_phys	= ARRAY_SIZE(s5pv210_phys),
	.phys		= s5pv210_phys,
	.rate_to_clk	= s5pv210_rate_to_clk,
};
