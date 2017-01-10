/*
 * Samsung SoC USB 1.1/2.0 PHY driver - Exynos 4210 support
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Kamil Debski <k.debski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include "phy-samsung-usb2.h"

/* Exynos USB PHY registers */

/* PHY power control */
#define EXYNOS_4210_UPHYPWR			0x0

#define EXYNOS_4210_UPHYPWR_PHY0_SUSPEND	BIT(0)
#define EXYNOS_4210_UPHYPWR_PHY0_PWR		BIT(3)
#define EXYNOS_4210_UPHYPWR_PHY0_OTG_PWR	BIT(4)
#define EXYNOS_4210_UPHYPWR_PHY0_SLEEP		BIT(5)
#define EXYNOS_4210_UPHYPWR_PHY0	( \
	EXYNOS_4210_UPHYPWR_PHY0_SUSPEND | \
	EXYNOS_4210_UPHYPWR_PHY0_PWR | \
	EXYNOS_4210_UPHYPWR_PHY0_OTG_PWR | \
	EXYNOS_4210_UPHYPWR_PHY0_SLEEP)

#define EXYNOS_4210_UPHYPWR_PHY1_SUSPEND	BIT(6)
#define EXYNOS_4210_UPHYPWR_PHY1_PWR		BIT(7)
#define EXYNOS_4210_UPHYPWR_PHY1_SLEEP		BIT(8)
#define EXYNOS_4210_UPHYPWR_PHY1 ( \
	EXYNOS_4210_UPHYPWR_PHY1_SUSPEND | \
	EXYNOS_4210_UPHYPWR_PHY1_PWR | \
	EXYNOS_4210_UPHYPWR_PHY1_SLEEP)

#define EXYNOS_4210_UPHYPWR_HSIC0_SUSPEND	BIT(9)
#define EXYNOS_4210_UPHYPWR_HSIC0_SLEEP		BIT(10)
#define EXYNOS_4210_UPHYPWR_HSIC0 ( \
	EXYNOS_4210_UPHYPWR_HSIC0_SUSPEND | \
	EXYNOS_4210_UPHYPWR_HSIC0_SLEEP)

#define EXYNOS_4210_UPHYPWR_HSIC1_SUSPEND	BIT(11)
#define EXYNOS_4210_UPHYPWR_HSIC1_SLEEP		BIT(12)
#define EXYNOS_4210_UPHYPWR_HSIC1 ( \
	EXYNOS_4210_UPHYPWR_HSIC1_SUSPEND | \
	EXYNOS_4210_UPHYPWR_HSIC1_SLEEP)

/* PHY clock control */
#define EXYNOS_4210_UPHYCLK			0x4

#define EXYNOS_4210_UPHYCLK_PHYFSEL_MASK	(0x3 << 0)
#define EXYNOS_4210_UPHYCLK_PHYFSEL_OFFSET	0
#define EXYNOS_4210_UPHYCLK_PHYFSEL_48MHZ	(0x0 << 0)
#define EXYNOS_4210_UPHYCLK_PHYFSEL_24MHZ	(0x3 << 0)
#define EXYNOS_4210_UPHYCLK_PHYFSEL_12MHZ	(0x2 << 0)

#define EXYNOS_4210_UPHYCLK_PHY0_ID_PULLUP	BIT(2)
#define EXYNOS_4210_UPHYCLK_PHY0_COMMON_ON	BIT(4)
#define EXYNOS_4210_UPHYCLK_PHY1_COMMON_ON	BIT(7)

/* PHY reset control */
#define EXYNOS_4210_UPHYRST			0x8

#define EXYNOS_4210_URSTCON_PHY0		BIT(0)
#define EXYNOS_4210_URSTCON_OTG_HLINK		BIT(1)
#define EXYNOS_4210_URSTCON_OTG_PHYLINK		BIT(2)
#define EXYNOS_4210_URSTCON_PHY1_ALL		BIT(3)
#define EXYNOS_4210_URSTCON_PHY1_P0		BIT(4)
#define EXYNOS_4210_URSTCON_PHY1_P1P2		BIT(5)
#define EXYNOS_4210_URSTCON_HOST_LINK_ALL	BIT(6)
#define EXYNOS_4210_URSTCON_HOST_LINK_P0	BIT(7)
#define EXYNOS_4210_URSTCON_HOST_LINK_P1	BIT(8)
#define EXYNOS_4210_URSTCON_HOST_LINK_P2	BIT(9)

/* Isolation, configured in the power management unit */
#define EXYNOS_4210_USB_ISOL_DEVICE_OFFSET	0x704
#define EXYNOS_4210_USB_ISOL_DEVICE		BIT(0)
#define EXYNOS_4210_USB_ISOL_HOST_OFFSET	0x708
#define EXYNOS_4210_USB_ISOL_HOST		BIT(0)

/* USBYPHY1 Floating prevention */
#define EXYNOS_4210_UPHY1CON			0x34
#define EXYNOS_4210_UPHY1CON_FLOAT_PREVENTION	0x1

/* Mode switching SUB Device <-> Host */
#define EXYNOS_4210_MODE_SWITCH_OFFSET		0x21c
#define EXYNOS_4210_MODE_SWITCH_MASK		1
#define EXYNOS_4210_MODE_SWITCH_DEVICE		0
#define EXYNOS_4210_MODE_SWITCH_HOST		1

enum exynos4210_phy_id {
	EXYNOS4210_DEVICE,
	EXYNOS4210_HOST,
	EXYNOS4210_HSIC0,
	EXYNOS4210_HSIC1,
	EXYNOS4210_NUM_PHYS,
};

/*
 * exynos4210_rate_to_clk() converts the supplied clock rate to the value that
 * can be written to the phy register.
 */
static int exynos4210_rate_to_clk(unsigned long rate, u32 *reg)
{
	switch (rate) {
	case 12 * MHZ:
		*reg = EXYNOS_4210_UPHYCLK_PHYFSEL_12MHZ;
		break;
	case 24 * MHZ:
		*reg = EXYNOS_4210_UPHYCLK_PHYFSEL_24MHZ;
		break;
	case 48 * MHZ:
		*reg = EXYNOS_4210_UPHYCLK_PHYFSEL_48MHZ;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void exynos4210_isol(struct samsung_usb2_phy_instance *inst, bool on)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	u32 offset;
	u32 mask;

	switch (inst->cfg->id) {
	case EXYNOS4210_DEVICE:
		offset = EXYNOS_4210_USB_ISOL_DEVICE_OFFSET;
		mask = EXYNOS_4210_USB_ISOL_DEVICE;
		break;
	case EXYNOS4210_HOST:
		offset = EXYNOS_4210_USB_ISOL_HOST_OFFSET;
		mask = EXYNOS_4210_USB_ISOL_HOST;
		break;
	default:
		return;
	}

	regmap_update_bits(drv->reg_pmu, offset, mask, on ? 0 : mask);
}

static void exynos4210_phy_pwr(struct samsung_usb2_phy_instance *inst, bool on)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	u32 rstbits = 0;
	u32 phypwr = 0;
	u32 rst;
	u32 pwr;
	u32 clk;

	switch (inst->cfg->id) {
	case EXYNOS4210_DEVICE:
		phypwr =	EXYNOS_4210_UPHYPWR_PHY0;
		rstbits =	EXYNOS_4210_URSTCON_PHY0;
		break;
	case EXYNOS4210_HOST:
		phypwr =	EXYNOS_4210_UPHYPWR_PHY1;
		rstbits =	EXYNOS_4210_URSTCON_PHY1_ALL |
				EXYNOS_4210_URSTCON_PHY1_P0 |
				EXYNOS_4210_URSTCON_PHY1_P1P2 |
				EXYNOS_4210_URSTCON_HOST_LINK_ALL |
				EXYNOS_4210_URSTCON_HOST_LINK_P0;
		writel(on, drv->reg_phy + EXYNOS_4210_UPHY1CON);
		break;
	case EXYNOS4210_HSIC0:
		phypwr =	EXYNOS_4210_UPHYPWR_HSIC0;
		rstbits =	EXYNOS_4210_URSTCON_PHY1_P1P2 |
				EXYNOS_4210_URSTCON_HOST_LINK_P1;
		break;
	case EXYNOS4210_HSIC1:
		phypwr =	EXYNOS_4210_UPHYPWR_HSIC1;
		rstbits =	EXYNOS_4210_URSTCON_PHY1_P1P2 |
				EXYNOS_4210_URSTCON_HOST_LINK_P2;
		break;
	}

	if (on) {
		clk = readl(drv->reg_phy + EXYNOS_4210_UPHYCLK);
		clk &= ~EXYNOS_4210_UPHYCLK_PHYFSEL_MASK;
		clk |= drv->ref_reg_val << EXYNOS_4210_UPHYCLK_PHYFSEL_OFFSET;
		writel(clk, drv->reg_phy + EXYNOS_4210_UPHYCLK);

		pwr = readl(drv->reg_phy + EXYNOS_4210_UPHYPWR);
		pwr &= ~phypwr;
		writel(pwr, drv->reg_phy + EXYNOS_4210_UPHYPWR);

		rst = readl(drv->reg_phy + EXYNOS_4210_UPHYRST);
		rst |= rstbits;
		writel(rst, drv->reg_phy + EXYNOS_4210_UPHYRST);
		udelay(10);
		rst &= ~rstbits;
		writel(rst, drv->reg_phy + EXYNOS_4210_UPHYRST);
		/* The following delay is necessary for the reset sequence to be
		 * completed */
		udelay(80);
	} else {
		pwr = readl(drv->reg_phy + EXYNOS_4210_UPHYPWR);
		pwr |= phypwr;
		writel(pwr, drv->reg_phy + EXYNOS_4210_UPHYPWR);
	}
}

static int exynos4210_power_on(struct samsung_usb2_phy_instance *inst)
{
	/* Order of initialisation is important - first power then isolation */
	exynos4210_phy_pwr(inst, 1);
	exynos4210_isol(inst, 0);

	return 0;
}

static int exynos4210_power_off(struct samsung_usb2_phy_instance *inst)
{
	exynos4210_isol(inst, 1);
	exynos4210_phy_pwr(inst, 0);

	return 0;
}


static const struct samsung_usb2_common_phy exynos4210_phys[] = {
	{
		.label		= "device",
		.id		= EXYNOS4210_DEVICE,
		.power_on	= exynos4210_power_on,
		.power_off	= exynos4210_power_off,
	},
	{
		.label		= "host",
		.id		= EXYNOS4210_HOST,
		.power_on	= exynos4210_power_on,
		.power_off	= exynos4210_power_off,
	},
	{
		.label		= "hsic0",
		.id		= EXYNOS4210_HSIC0,
		.power_on	= exynos4210_power_on,
		.power_off	= exynos4210_power_off,
	},
	{
		.label		= "hsic1",
		.id		= EXYNOS4210_HSIC1,
		.power_on	= exynos4210_power_on,
		.power_off	= exynos4210_power_off,
	},
};

const struct samsung_usb2_phy_config exynos4210_usb2_phy_config = {
	.has_mode_switch	= 0,
	.num_phys		= EXYNOS4210_NUM_PHYS,
	.phys			= exynos4210_phys,
	.rate_to_clk		= exynos4210_rate_to_clk,
};
