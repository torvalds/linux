/*
 * extcon-axp288.c - X-Power AXP288 PMIC extcon cable detection driver
 *
 * Copyright (C) 2015 Intel Corporation
 * Author: Ramakrishna Pallala <ramakrishna.pallala@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/notifier.h>
#include <linux/extcon.h>
#include <linux/regmap.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/axp20x.h>

/* Power source status register */
#define PS_STAT_VBUS_TRIGGER		BIT(0)
#define PS_STAT_BAT_CHRG_DIR		BIT(2)
#define PS_STAT_VBUS_ABOVE_VHOLD	BIT(3)
#define PS_STAT_VBUS_VALID		BIT(4)
#define PS_STAT_VBUS_PRESENT		BIT(5)

/* BC module global register */
#define BC_GLOBAL_RUN			BIT(0)
#define BC_GLOBAL_DET_STAT		BIT(2)
#define BC_GLOBAL_DBP_TOUT		BIT(3)
#define BC_GLOBAL_VLGC_COM_SEL		BIT(4)
#define BC_GLOBAL_DCD_TOUT_MASK		(BIT(6)|BIT(5))
#define BC_GLOBAL_DCD_TOUT_300MS	0
#define BC_GLOBAL_DCD_TOUT_100MS	1
#define BC_GLOBAL_DCD_TOUT_500MS	2
#define BC_GLOBAL_DCD_TOUT_900MS	3
#define BC_GLOBAL_DCD_DET_SEL		BIT(7)

/* BC module vbus control and status register */
#define VBUS_CNTL_DPDM_PD_EN		BIT(4)
#define VBUS_CNTL_DPDM_FD_EN		BIT(5)
#define VBUS_CNTL_FIRST_PO_STAT		BIT(6)

/* BC USB status register */
#define USB_STAT_BUS_STAT_MASK		(BIT(3)|BIT(2)|BIT(1)|BIT(0))
#define USB_STAT_BUS_STAT_SHIFT		0
#define USB_STAT_BUS_STAT_ATHD		0
#define USB_STAT_BUS_STAT_CONN		1
#define USB_STAT_BUS_STAT_SUSP		2
#define USB_STAT_BUS_STAT_CONF		3
#define USB_STAT_USB_SS_MODE		BIT(4)
#define USB_STAT_DEAD_BAT_DET		BIT(6)
#define USB_STAT_DBP_UNCFG		BIT(7)

/* BC detect status register */
#define DET_STAT_MASK			(BIT(7)|BIT(6)|BIT(5))
#define DET_STAT_SHIFT			5
#define DET_STAT_SDP			1
#define DET_STAT_CDP			2
#define DET_STAT_DCP			3

/* IRQ enable-1 register */
#define PWRSRC_IRQ_CFG_MASK		(BIT(4)|BIT(3)|BIT(2))

/* IRQ enable-6 register */
#define BC12_IRQ_CFG_MASK		BIT(1)

enum axp288_extcon_reg {
	AXP288_PS_STAT_REG		= 0x00,
	AXP288_PS_BOOT_REASON_REG	= 0x02,
	AXP288_BC_GLOBAL_REG		= 0x2c,
	AXP288_BC_VBUS_CNTL_REG		= 0x2d,
	AXP288_BC_USB_STAT_REG		= 0x2e,
	AXP288_BC_DET_STAT_REG		= 0x2f,
	AXP288_PWRSRC_IRQ_CFG_REG	= 0x40,
	AXP288_BC12_IRQ_CFG_REG		= 0x45,
};

enum axp288_mux_select {
	EXTCON_GPIO_MUX_SEL_PMIC = 0,
	EXTCON_GPIO_MUX_SEL_SOC,
};

enum axp288_extcon_irq {
	VBUS_FALLING_IRQ = 0,
	VBUS_RISING_IRQ,
	MV_CHNG_IRQ,
	BC_USB_CHNG_IRQ,
	EXTCON_IRQ_END,
};

static const unsigned int axp288_extcon_cables[] = {
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_CDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_NONE,
};

struct axp288_extcon_info {
	struct device *dev;
	struct regmap *regmap;
	struct regmap_irq_chip_data *regmap_irqc;
	struct gpio_desc *gpio_mux_cntl;
	int irq[EXTCON_IRQ_END];
	struct extcon_dev *edev;
	struct notifier_block extcon_nb;
	unsigned int previous_cable;
};

/* Power up/down reason string array */
static char *axp288_pwr_up_down_info[] = {
	"Last wake caused by user pressing the power button",
	"Last wake caused by a charger insertion",
	"Last wake caused by a battery insertion",
	"Last wake caused by SOC initiated global reset",
	"Last wake caused by cold reset",
	"Last shutdown caused by PMIC UVLO threshold",
	"Last shutdown caused by SOC initiated cold off",
	"Last shutdown caused by user pressing the power button",
	NULL,
};

/*
 * Decode and log the given "reset source indicator" (rsi)
 * register and then clear it.
 */
static void axp288_extcon_log_rsi(struct axp288_extcon_info *info)
{
	char **rsi;
	unsigned int val, i, clear_mask = 0;
	int ret;

	ret = regmap_read(info->regmap, AXP288_PS_BOOT_REASON_REG, &val);
	for (i = 0, rsi = axp288_pwr_up_down_info; *rsi; rsi++, i++) {
		if (val & BIT(i)) {
			dev_dbg(info->dev, "%s\n", *rsi);
			clear_mask |= BIT(i);
		}
	}

	/* Clear the register value for next reboot (write 1 to clear bit) */
	regmap_write(info->regmap, AXP288_PS_BOOT_REASON_REG, clear_mask);
}

static int axp288_handle_chrg_det_event(struct axp288_extcon_info *info)
{
	int ret, stat, cfg, pwr_stat;
	u8 chrg_type;
	unsigned int cable = info->previous_cable;
	bool vbus_attach = false;

	ret = regmap_read(info->regmap, AXP288_PS_STAT_REG, &pwr_stat);
	if (ret < 0) {
		dev_err(info->dev, "failed to read vbus status\n");
		return ret;
	}

	vbus_attach = (pwr_stat & PS_STAT_VBUS_VALID);
	if (!vbus_attach)
		goto no_vbus;

	/* Check charger detection completion status */
	ret = regmap_read(info->regmap, AXP288_BC_GLOBAL_REG, &cfg);
	if (ret < 0)
		goto dev_det_ret;
	if (cfg & BC_GLOBAL_DET_STAT) {
		dev_dbg(info->dev, "can't complete the charger detection\n");
		goto dev_det_ret;
	}

	ret = regmap_read(info->regmap, AXP288_BC_DET_STAT_REG, &stat);
	if (ret < 0)
		goto dev_det_ret;

	chrg_type = (stat & DET_STAT_MASK) >> DET_STAT_SHIFT;

	switch (chrg_type) {
	case DET_STAT_SDP:
		dev_dbg(info->dev, "sdp cable is connected\n");
		cable = EXTCON_CHG_USB_SDP;
		break;
	case DET_STAT_CDP:
		dev_dbg(info->dev, "cdp cable is connected\n");
		cable = EXTCON_CHG_USB_CDP;
		break;
	case DET_STAT_DCP:
		dev_dbg(info->dev, "dcp cable is connected\n");
		cable = EXTCON_CHG_USB_DCP;
		break;
	default:
		dev_warn(info->dev,
			"disconnect or unknown or ID event\n");
	}

no_vbus:
	/*
	 * If VBUS is absent Connect D+/D- lines to PMIC for BC
	 * detection. Else connect them to SOC for USB communication.
	 */
	if (info->gpio_mux_cntl)
		gpiod_set_value(info->gpio_mux_cntl,
			vbus_attach ? EXTCON_GPIO_MUX_SEL_SOC
					: EXTCON_GPIO_MUX_SEL_PMIC);

	extcon_set_state_sync(info->edev, info->previous_cable, false);
	if (vbus_attach) {
		extcon_set_state_sync(info->edev, cable, vbus_attach);
		info->previous_cable = cable;
	}

	return 0;

dev_det_ret:
	if (ret < 0)
		dev_err(info->dev, "failed to detect BC Mod\n");

	return ret;
}

static irqreturn_t axp288_extcon_isr(int irq, void *data)
{
	struct axp288_extcon_info *info = data;
	int ret;

	ret = axp288_handle_chrg_det_event(info);
	if (ret < 0)
		dev_err(info->dev, "failed to handle the interrupt\n");

	return IRQ_HANDLED;
}

static void axp288_extcon_enable_irq(struct axp288_extcon_info *info)
{
	/* Unmask VBUS interrupt */
	regmap_write(info->regmap, AXP288_PWRSRC_IRQ_CFG_REG,
						PWRSRC_IRQ_CFG_MASK);
	regmap_update_bits(info->regmap, AXP288_BC_GLOBAL_REG,
						BC_GLOBAL_RUN, 0);
	/* Unmask the BC1.2 complete interrupts */
	regmap_write(info->regmap, AXP288_BC12_IRQ_CFG_REG, BC12_IRQ_CFG_MASK);
	/* Enable the charger detection logic */
	regmap_update_bits(info->regmap, AXP288_BC_GLOBAL_REG,
					BC_GLOBAL_RUN, BC_GLOBAL_RUN);
}

static int axp288_extcon_probe(struct platform_device *pdev)
{
	struct axp288_extcon_info *info;
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	struct axp288_extcon_pdata *pdata = pdev->dev.platform_data;
	int ret, i, pirq, gpio;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->regmap = axp20x->regmap;
	info->regmap_irqc = axp20x->regmap_irqc;
	info->previous_cable = EXTCON_NONE;
	if (pdata)
		info->gpio_mux_cntl = pdata->gpio_mux_cntl;

	platform_set_drvdata(pdev, info);

	axp288_extcon_log_rsi(info);

	/* Initialize extcon device */
	info->edev = devm_extcon_dev_allocate(&pdev->dev,
					      axp288_extcon_cables);
	if (IS_ERR(info->edev)) {
		dev_err(&pdev->dev, "failed to allocate memory for extcon\n");
		return PTR_ERR(info->edev);
	}

	/* Register extcon device */
	ret = devm_extcon_dev_register(&pdev->dev, info->edev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register extcon device\n");
		return ret;
	}

	/* Set up gpio control for USB Mux */
	if (info->gpio_mux_cntl) {
		gpio = desc_to_gpio(info->gpio_mux_cntl);
		ret = devm_gpio_request(&pdev->dev, gpio, "USB_MUX");
		if (ret < 0) {
			dev_err(&pdev->dev,
				"failed to request the gpio=%d\n", gpio);
			return ret;
		}
		gpiod_direction_output(info->gpio_mux_cntl,
						EXTCON_GPIO_MUX_SEL_PMIC);
	}

	for (i = 0; i < EXTCON_IRQ_END; i++) {
		pirq = platform_get_irq(pdev, i);
		info->irq[i] = regmap_irq_get_virq(info->regmap_irqc, pirq);
		if (info->irq[i] < 0) {
			dev_err(&pdev->dev,
				"failed to get virtual interrupt=%d\n", pirq);
			ret = info->irq[i];
			return ret;
		}

		ret = devm_request_threaded_irq(&pdev->dev, info->irq[i],
				NULL, axp288_extcon_isr,
				IRQF_ONESHOT | IRQF_NO_SUSPEND,
				pdev->name, info);
		if (ret) {
			dev_err(&pdev->dev, "failed to request interrupt=%d\n",
							info->irq[i]);
			return ret;
		}
	}

	/* Enable interrupts */
	axp288_extcon_enable_irq(info);

	return 0;
}

static struct platform_driver axp288_extcon_driver = {
	.probe = axp288_extcon_probe,
	.driver = {
		.name = "axp288_extcon",
	},
};
module_platform_driver(axp288_extcon_driver);

MODULE_AUTHOR("Ramakrishna Pallala <ramakrishna.pallala@intel.com>");
MODULE_DESCRIPTION("X-Powers AXP288 extcon driver");
MODULE_LICENSE("GPL v2");
