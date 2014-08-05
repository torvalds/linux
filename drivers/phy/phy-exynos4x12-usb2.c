/*
 * Samsung SoC USB 1.1/2.0 PHY driver - Exynos 4x12 support
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
#define EXYNOS_4x12_UPHYPWR			0x0

#define EXYNOS_4x12_UPHYPWR_PHY0_SUSPEND	BIT(0)
#define EXYNOS_4x12_UPHYPWR_PHY0_PWR		BIT(3)
#define EXYNOS_4x12_UPHYPWR_PHY0_OTG_PWR	BIT(4)
#define EXYNOS_4x12_UPHYPWR_PHY0_SLEEP		BIT(5)
#define EXYNOS_4x12_UPHYPWR_PHY0 ( \
	EXYNOS_4x12_UPHYPWR_PHY0_SUSPEND | \
	EXYNOS_4x12_UPHYPWR_PHY0_PWR | \
	EXYNOS_4x12_UPHYPWR_PHY0_OTG_PWR | \
	EXYNOS_4x12_UPHYPWR_PHY0_SLEEP)

#define EXYNOS_4x12_UPHYPWR_PHY1_SUSPEND	BIT(6)
#define EXYNOS_4x12_UPHYPWR_PHY1_PWR		BIT(7)
#define EXYNOS_4x12_UPHYPWR_PHY1_SLEEP		BIT(8)
#define EXYNOS_4x12_UPHYPWR_PHY1 ( \
	EXYNOS_4x12_UPHYPWR_PHY1_SUSPEND | \
	EXYNOS_4x12_UPHYPWR_PHY1_PWR | \
	EXYNOS_4x12_UPHYPWR_PHY1_SLEEP)

#define EXYNOS_4x12_UPHYPWR_HSIC0_SUSPEND	BIT(9)
#define EXYNOS_4x12_UPHYPWR_HSIC0_PWR		BIT(10)
#define EXYNOS_4x12_UPHYPWR_HSIC0_SLEEP		BIT(11)
#define EXYNOS_4x12_UPHYPWR_HSIC0 ( \
	EXYNOS_4x12_UPHYPWR_HSIC0_SUSPEND | \
	EXYNOS_4x12_UPHYPWR_HSIC0_PWR | \
	EXYNOS_4x12_UPHYPWR_HSIC0_SLEEP)

#define EXYNOS_4x12_UPHYPWR_HSIC1_SUSPEND	BIT(12)
#define EXYNOS_4x12_UPHYPWR_HSIC1_PWR		BIT(13)
#define EXYNOS_4x12_UPHYPWR_HSIC1_SLEEP		BIT(14)
#define EXYNOS_4x12_UPHYPWR_HSIC1 ( \
	EXYNOS_4x12_UPHYPWR_HSIC1_SUSPEND | \
	EXYNOS_4x12_UPHYPWR_HSIC1_PWR | \
	EXYNOS_4x12_UPHYPWR_HSIC1_SLEEP)

/* PHY clock control */
#define EXYNOS_4x12_UPHYCLK			0x4

#define EXYNOS_4x12_UPHYCLK_PHYFSEL_MASK	(0x7 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_OFFSET	0
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_9MHZ6	(0x0 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_10MHZ	(0x1 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_12MHZ	(0x2 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_19MHZ2	(0x3 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_20MHZ	(0x4 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_24MHZ	(0x5 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_50MHZ	(0x7 << 0)

#define EXYNOS_3250_UPHYCLK_REFCLKSEL		(0x2 << 8)

#define EXYNOS_4x12_UPHYCLK_PHY0_ID_PULLUP	BIT(3)
#define EXYNOS_4x12_UPHYCLK_PHY0_COMMON_ON	BIT(4)
#define EXYNOS_4x12_UPHYCLK_PHY1_COMMON_ON	BIT(7)

#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_MASK	(0x7f << 10)
#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_OFFSET  10
#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_12MHZ	(0x24 << 10)
#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_15MHZ	(0x1c << 10)
#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_16MHZ	(0x1a << 10)
#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_19MHZ2	(0x15 << 10)
#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_20MHZ	(0x14 << 10)

/* PHY reset control */
#define EXYNOS_4x12_UPHYRST			0x8

#define EXYNOS_4x12_URSTCON_PHY0		BIT(0)
#define EXYNOS_4x12_URSTCON_OTG_HLINK		BIT(1)
#define EXYNOS_4x12_URSTCON_OTG_PHYLINK		BIT(2)
#define EXYNOS_4x12_URSTCON_HOST_PHY		BIT(3)
/* The following bit defines are presented in the
 * order taken from the Exynos4412 reference manual.
 *
 * During experiments with the hardware and debugging
 * it was determined that the hardware behaves contrary
 * to the manual.
 *
 * The following bit values were chaned accordingly to the
 * results of real hardware experiments.
 */
#define EXYNOS_4x12_URSTCON_PHY1		BIT(4)
#define EXYNOS_4x12_URSTCON_HSIC0		BIT(6)
#define EXYNOS_4x12_URSTCON_HSIC1		BIT(5)
#define EXYNOS_4x12_URSTCON_HOST_LINK_ALL	BIT(7)
#define EXYNOS_4x12_URSTCON_HOST_LINK_P0	BIT(10)
#define EXYNOS_4x12_URSTCON_HOST_LINK_P1	BIT(9)
#define EXYNOS_4x12_URSTCON_HOST_LINK_P2	BIT(8)

/* Isolation, configured in the power management unit */
#define EXYNOS_4x12_USB_ISOL_OFFSET		0x704
#define EXYNOS_4x12_USB_ISOL_OTG		BIT(0)
#define EXYNOS_4x12_USB_ISOL_HSIC0_OFFSET	0x708
#define EXYNOS_4x12_USB_ISOL_HSIC0		BIT(0)
#define EXYNOS_4x12_USB_ISOL_HSIC1_OFFSET	0x70c
#define EXYNOS_4x12_USB_ISOL_HSIC1		BIT(0)

/* Mode switching SUB Device <-> Host */
#define EXYNOS_4x12_MODE_SWITCH_OFFSET		0x21c
#define EXYNOS_4x12_MODE_SWITCH_MASK		1
#define EXYNOS_4x12_MODE_SWITCH_DEVICE		0
#define EXYNOS_4x12_MODE_SWITCH_HOST		1

enum exynos4x12_phy_id {
	EXYNOS4x12_DEVICE,
	EXYNOS4x12_HOST,
	EXYNOS4x12_HSIC0,
	EXYNOS4x12_HSIC1,
	EXYNOS4x12_NUM_PHYS,
};

/*
 * exynos4x12_rate_to_clk() converts the supplied clock rate to the value that
 * can be written to the phy register.
 */
static int exynos4x12_rate_to_clk(unsigned long rate, u32 *reg)
{
	/* EXYNOS_4x12_UPHYCLK_PHYFSEL_MASK */

	switch (rate) {
	case 9600 * KHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_9MHZ6;
		break;
	case 10 * MHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_10MHZ;
		break;
	case 12 * MHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_12MHZ;
		break;
	case 19200 * KHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_19MHZ2;
		break;
	case 20 * MHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_20MHZ;
		break;
	case 24 * MHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_24MHZ;
		break;
	case 50 * MHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_50MHZ;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void exynos4x12_isol(struct samsung_usb2_phy_instance *inst, bool on)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	u32 offset;
	u32 mask;

	switch (inst->cfg->id) {
	case EXYNOS4x12_DEVICE:
	case EXYNOS4x12_HOST:
		offset = EXYNOS_4x12_USB_ISOL_OFFSET;
		mask = EXYNOS_4x12_USB_ISOL_OTG;
		break;
	case EXYNOS4x12_HSIC0:
		offset = EXYNOS_4x12_USB_ISOL_HSIC0_OFFSET;
		mask = EXYNOS_4x12_USB_ISOL_HSIC0;
		break;
	case EXYNOS4x12_HSIC1:
		offset = EXYNOS_4x12_USB_ISOL_HSIC1_OFFSET;
		mask = EXYNOS_4x12_USB_ISOL_HSIC1;
		break;
	default:
		return;
	};

	regmap_update_bits(drv->reg_pmu, offset, mask, on ? 0 : mask);
}

static void exynos4x12_setup_clk(struct samsung_usb2_phy_instance *inst)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	u32 clk;

	clk = readl(drv->reg_phy + EXYNOS_4x12_UPHYCLK);
	clk &= ~EXYNOS_4x12_UPHYCLK_PHYFSEL_MASK;

	if (drv->cfg->has_refclk_sel)
		clk = EXYNOS_3250_UPHYCLK_REFCLKSEL;

	clk |= drv->ref_reg_val << EXYNOS_4x12_UPHYCLK_PHYFSEL_OFFSET;
	clk |= EXYNOS_4x12_UPHYCLK_PHY1_COMMON_ON;
	writel(clk, drv->reg_phy + EXYNOS_4x12_UPHYCLK);
}

static void exynos4x12_phy_pwr(struct samsung_usb2_phy_instance *inst, bool on)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	u32 rstbits = 0;
	u32 phypwr = 0;
	u32 rst;
	u32 pwr;

	switch (inst->cfg->id) {
	case EXYNOS4x12_DEVICE:
		phypwr =	EXYNOS_4x12_UPHYPWR_PHY0;
		rstbits =	EXYNOS_4x12_URSTCON_PHY0;
		break;
	case EXYNOS4x12_HOST:
		phypwr =	EXYNOS_4x12_UPHYPWR_PHY1;
		rstbits =	EXYNOS_4x12_URSTCON_HOST_PHY |
				EXYNOS_4x12_URSTCON_PHY1 |
				EXYNOS_4x12_URSTCON_HOST_LINK_P0;
		break;
	case EXYNOS4x12_HSIC0:
		phypwr =	EXYNOS_4x12_UPHYPWR_HSIC0;
		rstbits =	EXYNOS_4x12_URSTCON_HSIC0 |
				EXYNOS_4x12_URSTCON_HOST_LINK_P1;
		break;
	case EXYNOS4x12_HSIC1:
		phypwr =	EXYNOS_4x12_UPHYPWR_HSIC1;
		rstbits =	EXYNOS_4x12_URSTCON_HSIC1 |
				EXYNOS_4x12_URSTCON_HOST_LINK_P1;
		break;
	};

	if (on) {
		pwr = readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR);
		pwr &= ~phypwr;
		writel(pwr, drv->reg_phy + EXYNOS_4x12_UPHYPWR);

		rst = readl(drv->reg_phy + EXYNOS_4x12_UPHYRST);
		rst |= rstbits;
		writel(rst, drv->reg_phy + EXYNOS_4x12_UPHYRST);
		udelay(10);
		rst &= ~rstbits;
		writel(rst, drv->reg_phy + EXYNOS_4x12_UPHYRST);
		/* The following delay is necessary for the reset sequence to be
		 * completed */
		udelay(80);
	} else {
		pwr = readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR);
		pwr |= phypwr;
		writel(pwr, drv->reg_phy + EXYNOS_4x12_UPHYPWR);
	}
}

static void exynos4x12_power_on_int(struct samsung_usb2_phy_instance *inst)
{
	if (inst->int_cnt++ > 0)
		return;

	exynos4x12_setup_clk(inst);
	exynos4x12_isol(inst, 0);
	exynos4x12_phy_pwr(inst, 1);
}

static int exynos4x12_power_on(struct samsung_usb2_phy_instance *inst)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;

	if (inst->ext_cnt++ > 0)
		return 0;

	if (inst->cfg->id == EXYNOS4x12_HOST) {
		regmap_update_bits(drv->reg_sys, EXYNOS_4x12_MODE_SWITCH_OFFSET,
						EXYNOS_4x12_MODE_SWITCH_MASK,
						EXYNOS_4x12_MODE_SWITCH_HOST);
		exynos4x12_power_on_int(&drv->instances[EXYNOS4x12_DEVICE]);
	}

	if (inst->cfg->id == EXYNOS4x12_DEVICE && drv->cfg->has_mode_switch)
		regmap_update_bits(drv->reg_sys, EXYNOS_4x12_MODE_SWITCH_OFFSET,
						EXYNOS_4x12_MODE_SWITCH_MASK,
						EXYNOS_4x12_MODE_SWITCH_DEVICE);

	if (inst->cfg->id == EXYNOS4x12_HSIC0 ||
		inst->cfg->id == EXYNOS4x12_HSIC1) {
		exynos4x12_power_on_int(&drv->instances[EXYNOS4x12_DEVICE]);
		exynos4x12_power_on_int(&drv->instances[EXYNOS4x12_HOST]);
	}

	exynos4x12_power_on_int(inst);

	return 0;
}

static void exynos4x12_power_off_int(struct samsung_usb2_phy_instance *inst)
{
	if (inst->int_cnt-- > 1)
		return;

	exynos4x12_isol(inst, 1);
	exynos4x12_phy_pwr(inst, 0);
}

static int exynos4x12_power_off(struct samsung_usb2_phy_instance *inst)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;

	if (inst->ext_cnt-- > 1)
		return 0;

	if (inst->cfg->id == EXYNOS4x12_DEVICE && drv->cfg->has_mode_switch)
		regmap_update_bits(drv->reg_sys, EXYNOS_4x12_MODE_SWITCH_OFFSET,
						EXYNOS_4x12_MODE_SWITCH_MASK,
						EXYNOS_4x12_MODE_SWITCH_HOST);

	if (inst->cfg->id == EXYNOS4x12_HOST)
		exynos4x12_power_off_int(&drv->instances[EXYNOS4x12_DEVICE]);

	if (inst->cfg->id == EXYNOS4x12_HSIC0 ||
		inst->cfg->id == EXYNOS4x12_HSIC1) {
		exynos4x12_power_off_int(&drv->instances[EXYNOS4x12_DEVICE]);
		exynos4x12_power_off_int(&drv->instances[EXYNOS4x12_HOST]);
	}

	exynos4x12_power_off_int(inst);

	return 0;
}


static const struct samsung_usb2_common_phy exynos4x12_phys[] = {
	{
		.label		= "device",
		.id		= EXYNOS4x12_DEVICE,
		.power_on	= exynos4x12_power_on,
		.power_off	= exynos4x12_power_off,
	},
	{
		.label		= "host",
		.id		= EXYNOS4x12_HOST,
		.power_on	= exynos4x12_power_on,
		.power_off	= exynos4x12_power_off,
	},
	{
		.label		= "hsic0",
		.id		= EXYNOS4x12_HSIC0,
		.power_on	= exynos4x12_power_on,
		.power_off	= exynos4x12_power_off,
	},
	{
		.label		= "hsic1",
		.id		= EXYNOS4x12_HSIC1,
		.power_on	= exynos4x12_power_on,
		.power_off	= exynos4x12_power_off,
	},
	{},
};

const struct samsung_usb2_phy_config exynos3250_usb2_phy_config = {
	.has_refclk_sel		= 1,
	.num_phys		= 1,
	.phys			= exynos4x12_phys,
	.rate_to_clk		= exynos4x12_rate_to_clk,
};

const struct samsung_usb2_phy_config exynos4x12_usb2_phy_config = {
	.has_mode_switch	= 1,
	.num_phys		= EXYNOS4x12_NUM_PHYS,
	.phys			= exynos4x12_phys,
	.rate_to_clk		= exynos4x12_rate_to_clk,
};
