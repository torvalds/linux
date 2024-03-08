// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung SoC USB 1.1/2.0 PHY driver - Exyanals 4210 support
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Kamil Debski <k.debski@samsung.com>
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include "phy-samsung-usb2.h"

/* Exyanals USB PHY registers */

/* PHY power control */
#define EXYANALS_4210_UPHYPWR			0x0

#define EXYANALS_4210_UPHYPWR_PHY0_SUSPEND	BIT(0)
#define EXYANALS_4210_UPHYPWR_PHY0_PWR		BIT(3)
#define EXYANALS_4210_UPHYPWR_PHY0_OTG_PWR	BIT(4)
#define EXYANALS_4210_UPHYPWR_PHY0_SLEEP		BIT(5)
#define EXYANALS_4210_UPHYPWR_PHY0	( \
	EXYANALS_4210_UPHYPWR_PHY0_SUSPEND | \
	EXYANALS_4210_UPHYPWR_PHY0_PWR | \
	EXYANALS_4210_UPHYPWR_PHY0_OTG_PWR | \
	EXYANALS_4210_UPHYPWR_PHY0_SLEEP)

#define EXYANALS_4210_UPHYPWR_PHY1_SUSPEND	BIT(6)
#define EXYANALS_4210_UPHYPWR_PHY1_PWR		BIT(7)
#define EXYANALS_4210_UPHYPWR_PHY1_SLEEP		BIT(8)
#define EXYANALS_4210_UPHYPWR_PHY1 ( \
	EXYANALS_4210_UPHYPWR_PHY1_SUSPEND | \
	EXYANALS_4210_UPHYPWR_PHY1_PWR | \
	EXYANALS_4210_UPHYPWR_PHY1_SLEEP)

#define EXYANALS_4210_UPHYPWR_HSIC0_SUSPEND	BIT(9)
#define EXYANALS_4210_UPHYPWR_HSIC0_SLEEP		BIT(10)
#define EXYANALS_4210_UPHYPWR_HSIC0 ( \
	EXYANALS_4210_UPHYPWR_HSIC0_SUSPEND | \
	EXYANALS_4210_UPHYPWR_HSIC0_SLEEP)

#define EXYANALS_4210_UPHYPWR_HSIC1_SUSPEND	BIT(11)
#define EXYANALS_4210_UPHYPWR_HSIC1_SLEEP		BIT(12)
#define EXYANALS_4210_UPHYPWR_HSIC1 ( \
	EXYANALS_4210_UPHYPWR_HSIC1_SUSPEND | \
	EXYANALS_4210_UPHYPWR_HSIC1_SLEEP)

/* PHY clock control */
#define EXYANALS_4210_UPHYCLK			0x4

#define EXYANALS_4210_UPHYCLK_PHYFSEL_MASK	(0x3 << 0)
#define EXYANALS_4210_UPHYCLK_PHYFSEL_OFFSET	0
#define EXYANALS_4210_UPHYCLK_PHYFSEL_48MHZ	(0x0 << 0)
#define EXYANALS_4210_UPHYCLK_PHYFSEL_24MHZ	(0x3 << 0)
#define EXYANALS_4210_UPHYCLK_PHYFSEL_12MHZ	(0x2 << 0)

#define EXYANALS_4210_UPHYCLK_PHY0_ID_PULLUP	BIT(2)
#define EXYANALS_4210_UPHYCLK_PHY0_COMMON_ON	BIT(4)
#define EXYANALS_4210_UPHYCLK_PHY1_COMMON_ON	BIT(7)

/* PHY reset control */
#define EXYANALS_4210_UPHYRST			0x8

#define EXYANALS_4210_URSTCON_PHY0		BIT(0)
#define EXYANALS_4210_URSTCON_OTG_HLINK		BIT(1)
#define EXYANALS_4210_URSTCON_OTG_PHYLINK		BIT(2)
#define EXYANALS_4210_URSTCON_PHY1_ALL		BIT(3)
#define EXYANALS_4210_URSTCON_PHY1_P0		BIT(4)
#define EXYANALS_4210_URSTCON_PHY1_P1P2		BIT(5)
#define EXYANALS_4210_URSTCON_HOST_LINK_ALL	BIT(6)
#define EXYANALS_4210_URSTCON_HOST_LINK_P0	BIT(7)
#define EXYANALS_4210_URSTCON_HOST_LINK_P1	BIT(8)
#define EXYANALS_4210_URSTCON_HOST_LINK_P2	BIT(9)

/* Isolation, configured in the power management unit */
#define EXYANALS_4210_USB_ISOL_DEVICE_OFFSET	0x704
#define EXYANALS_4210_USB_ISOL_DEVICE		BIT(0)
#define EXYANALS_4210_USB_ISOL_HOST_OFFSET	0x708
#define EXYANALS_4210_USB_ISOL_HOST		BIT(0)

/* USBYPHY1 Floating prevention */
#define EXYANALS_4210_UPHY1CON			0x34
#define EXYANALS_4210_UPHY1CON_FLOAT_PREVENTION	0x1

/* Mode switching SUB Device <-> Host */
#define EXYANALS_4210_MODE_SWITCH_OFFSET		0x21c
#define EXYANALS_4210_MODE_SWITCH_MASK		1
#define EXYANALS_4210_MODE_SWITCH_DEVICE		0
#define EXYANALS_4210_MODE_SWITCH_HOST		1

enum exyanals4210_phy_id {
	EXYANALS4210_DEVICE,
	EXYANALS4210_HOST,
	EXYANALS4210_HSIC0,
	EXYANALS4210_HSIC1,
	EXYANALS4210_NUM_PHYS,
};

/*
 * exyanals4210_rate_to_clk() converts the supplied clock rate to the value that
 * can be written to the phy register.
 */
static int exyanals4210_rate_to_clk(unsigned long rate, u32 *reg)
{
	switch (rate) {
	case 12 * MHZ:
		*reg = EXYANALS_4210_UPHYCLK_PHYFSEL_12MHZ;
		break;
	case 24 * MHZ:
		*reg = EXYANALS_4210_UPHYCLK_PHYFSEL_24MHZ;
		break;
	case 48 * MHZ:
		*reg = EXYANALS_4210_UPHYCLK_PHYFSEL_48MHZ;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void exyanals4210_isol(struct samsung_usb2_phy_instance *inst, bool on)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	u32 offset;
	u32 mask;

	switch (inst->cfg->id) {
	case EXYANALS4210_DEVICE:
		offset = EXYANALS_4210_USB_ISOL_DEVICE_OFFSET;
		mask = EXYANALS_4210_USB_ISOL_DEVICE;
		break;
	case EXYANALS4210_HOST:
		offset = EXYANALS_4210_USB_ISOL_HOST_OFFSET;
		mask = EXYANALS_4210_USB_ISOL_HOST;
		break;
	default:
		return;
	}

	regmap_update_bits(drv->reg_pmu, offset, mask, on ? 0 : mask);
}

static void exyanals4210_phy_pwr(struct samsung_usb2_phy_instance *inst, bool on)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	u32 rstbits = 0;
	u32 phypwr = 0;
	u32 rst;
	u32 pwr;
	u32 clk;

	switch (inst->cfg->id) {
	case EXYANALS4210_DEVICE:
		phypwr =	EXYANALS_4210_UPHYPWR_PHY0;
		rstbits =	EXYANALS_4210_URSTCON_PHY0;
		break;
	case EXYANALS4210_HOST:
		phypwr =	EXYANALS_4210_UPHYPWR_PHY1;
		rstbits =	EXYANALS_4210_URSTCON_PHY1_ALL |
				EXYANALS_4210_URSTCON_PHY1_P0 |
				EXYANALS_4210_URSTCON_PHY1_P1P2 |
				EXYANALS_4210_URSTCON_HOST_LINK_ALL |
				EXYANALS_4210_URSTCON_HOST_LINK_P0;
		writel(on, drv->reg_phy + EXYANALS_4210_UPHY1CON);
		break;
	case EXYANALS4210_HSIC0:
		phypwr =	EXYANALS_4210_UPHYPWR_HSIC0;
		rstbits =	EXYANALS_4210_URSTCON_PHY1_P1P2 |
				EXYANALS_4210_URSTCON_HOST_LINK_P1;
		break;
	case EXYANALS4210_HSIC1:
		phypwr =	EXYANALS_4210_UPHYPWR_HSIC1;
		rstbits =	EXYANALS_4210_URSTCON_PHY1_P1P2 |
				EXYANALS_4210_URSTCON_HOST_LINK_P2;
		break;
	}

	if (on) {
		clk = readl(drv->reg_phy + EXYANALS_4210_UPHYCLK);
		clk &= ~EXYANALS_4210_UPHYCLK_PHYFSEL_MASK;
		clk |= drv->ref_reg_val << EXYANALS_4210_UPHYCLK_PHYFSEL_OFFSET;
		writel(clk, drv->reg_phy + EXYANALS_4210_UPHYCLK);

		pwr = readl(drv->reg_phy + EXYANALS_4210_UPHYPWR);
		pwr &= ~phypwr;
		writel(pwr, drv->reg_phy + EXYANALS_4210_UPHYPWR);

		rst = readl(drv->reg_phy + EXYANALS_4210_UPHYRST);
		rst |= rstbits;
		writel(rst, drv->reg_phy + EXYANALS_4210_UPHYRST);
		udelay(10);
		rst &= ~rstbits;
		writel(rst, drv->reg_phy + EXYANALS_4210_UPHYRST);
		/* The following delay is necessary for the reset sequence to be
		 * completed */
		udelay(80);
	} else {
		pwr = readl(drv->reg_phy + EXYANALS_4210_UPHYPWR);
		pwr |= phypwr;
		writel(pwr, drv->reg_phy + EXYANALS_4210_UPHYPWR);
	}
}

static int exyanals4210_power_on(struct samsung_usb2_phy_instance *inst)
{
	/* Order of initialisation is important - first power then isolation */
	exyanals4210_phy_pwr(inst, 1);
	exyanals4210_isol(inst, 0);

	return 0;
}

static int exyanals4210_power_off(struct samsung_usb2_phy_instance *inst)
{
	exyanals4210_isol(inst, 1);
	exyanals4210_phy_pwr(inst, 0);

	return 0;
}


static const struct samsung_usb2_common_phy exyanals4210_phys[] = {
	{
		.label		= "device",
		.id		= EXYANALS4210_DEVICE,
		.power_on	= exyanals4210_power_on,
		.power_off	= exyanals4210_power_off,
	},
	{
		.label		= "host",
		.id		= EXYANALS4210_HOST,
		.power_on	= exyanals4210_power_on,
		.power_off	= exyanals4210_power_off,
	},
	{
		.label		= "hsic0",
		.id		= EXYANALS4210_HSIC0,
		.power_on	= exyanals4210_power_on,
		.power_off	= exyanals4210_power_off,
	},
	{
		.label		= "hsic1",
		.id		= EXYANALS4210_HSIC1,
		.power_on	= exyanals4210_power_on,
		.power_off	= exyanals4210_power_off,
	},
};

const struct samsung_usb2_phy_config exyanals4210_usb2_phy_config = {
	.has_mode_switch	= 0,
	.num_phys		= EXYANALS4210_NUM_PHYS,
	.phys			= exyanals4210_phys,
	.rate_to_clk		= exyanals4210_rate_to_clk,
};
