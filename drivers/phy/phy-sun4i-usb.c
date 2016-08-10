/*
 * Allwinner sun4i USB phy driver
 *
 * Copyright (C) 2014-2015 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on code from
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * Modelled after: Samsung S5P/EXYNOS SoC series MIPI CSIS/DSIM DPHY driver
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/extcon.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-sun4i-usb.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/workqueue.h>

#define REG_ISCR			0x00
#define REG_PHYCTL_A10			0x04
#define REG_PHYBIST			0x08
#define REG_PHYTUNE			0x0c
#define REG_PHYCTL_A33			0x10
#define REG_PHY_UNK_H3			0x20

#define REG_PMU_UNK_H3			0x10

#define PHYCTL_DATA			BIT(7)

#define SUNXI_AHB_ICHR8_EN		BIT(10)
#define SUNXI_AHB_INCR4_BURST_EN	BIT(9)
#define SUNXI_AHB_INCRX_ALIGN_EN	BIT(8)
#define SUNXI_ULPI_BYPASS_EN		BIT(0)

/* ISCR, Interface Status and Control bits */
#define ISCR_ID_PULLUP_EN		(1 << 17)
#define ISCR_DPDM_PULLUP_EN	(1 << 16)
/* sunxi has the phy id/vbus pins not connected, so we use the force bits */
#define ISCR_FORCE_ID_MASK	(3 << 14)
#define ISCR_FORCE_ID_LOW		(2 << 14)
#define ISCR_FORCE_ID_HIGH	(3 << 14)
#define ISCR_FORCE_VBUS_MASK	(3 << 12)
#define ISCR_FORCE_VBUS_LOW	(2 << 12)
#define ISCR_FORCE_VBUS_HIGH	(3 << 12)

/* Common Control Bits for Both PHYs */
#define PHY_PLL_BW			0x03
#define PHY_RES45_CAL_EN		0x0c

/* Private Control Bits for Each PHY */
#define PHY_TX_AMPLITUDE_TUNE		0x20
#define PHY_TX_SLEWRATE_TUNE		0x22
#define PHY_VBUSVALID_TH_SEL		0x25
#define PHY_PULLUP_RES_SEL		0x27
#define PHY_OTG_FUNC_EN			0x28
#define PHY_VBUS_DET_EN			0x29
#define PHY_DISCON_TH_SEL		0x2a
#define PHY_SQUELCH_DETECT		0x3c

#define MAX_PHYS			4

/*
 * Note do not raise the debounce time, we must report Vusb high within 100ms
 * otherwise we get Vbus errors
 */
#define DEBOUNCE_TIME			msecs_to_jiffies(50)
#define POLL_TIME			msecs_to_jiffies(250)

enum sun4i_usb_phy_type {
	sun4i_a10_phy,
	sun6i_a31_phy,
	sun8i_a33_phy,
	sun8i_h3_phy,
};

struct sun4i_usb_phy_cfg {
	int num_phys;
	enum sun4i_usb_phy_type type;
	u32 disc_thresh;
	u8 phyctl_offset;
	bool dedicated_clocks;
};

struct sun4i_usb_phy_data {
	void __iomem *base;
	const struct sun4i_usb_phy_cfg *cfg;
	struct mutex mutex;
	struct sun4i_usb_phy {
		struct phy *phy;
		void __iomem *pmu;
		struct regulator *vbus;
		struct reset_control *reset;
		struct clk *clk;
		bool regulator_on;
		int index;
	} phys[MAX_PHYS];
	/* phy0 / otg related variables */
	struct extcon_dev *extcon;
	bool phy0_init;
	struct gpio_desc *id_det_gpio;
	struct gpio_desc *vbus_det_gpio;
	struct power_supply *vbus_power_supply;
	struct notifier_block vbus_power_nb;
	bool vbus_power_nb_registered;
	int id_det_irq;
	int vbus_det_irq;
	int id_det;
	int vbus_det;
	struct delayed_work detect;
};

#define to_sun4i_usb_phy_data(phy) \
	container_of((phy), struct sun4i_usb_phy_data, phys[(phy)->index])

static void sun4i_usb_phy0_update_iscr(struct phy *_phy, u32 clr, u32 set)
{
	struct sun4i_usb_phy *phy = phy_get_drvdata(_phy);
	struct sun4i_usb_phy_data *data = to_sun4i_usb_phy_data(phy);
	u32 iscr;

	iscr = readl(data->base + REG_ISCR);
	iscr &= ~clr;
	iscr |= set;
	writel(iscr, data->base + REG_ISCR);
}

static void sun4i_usb_phy0_set_id_detect(struct phy *phy, u32 val)
{
	if (val)
		val = ISCR_FORCE_ID_HIGH;
	else
		val = ISCR_FORCE_ID_LOW;

	sun4i_usb_phy0_update_iscr(phy, ISCR_FORCE_ID_MASK, val);
}

static void sun4i_usb_phy0_set_vbus_detect(struct phy *phy, u32 val)
{
	if (val)
		val = ISCR_FORCE_VBUS_HIGH;
	else
		val = ISCR_FORCE_VBUS_LOW;

	sun4i_usb_phy0_update_iscr(phy, ISCR_FORCE_VBUS_MASK, val);
}

static void sun4i_usb_phy_write(struct sun4i_usb_phy *phy, u32 addr, u32 data,
				int len)
{
	struct sun4i_usb_phy_data *phy_data = to_sun4i_usb_phy_data(phy);
	u32 temp, usbc_bit = BIT(phy->index * 2);
	void __iomem *phyctl = phy_data->base + phy_data->cfg->phyctl_offset;
	int i;

	mutex_lock(&phy_data->mutex);

	if (phy_data->cfg->type == sun8i_a33_phy) {
		/* A33 needs us to set phyctl to 0 explicitly */
		writel(0, phyctl);
	}

	for (i = 0; i < len; i++) {
		temp = readl(phyctl);

		/* clear the address portion */
		temp &= ~(0xff << 8);

		/* set the address */
		temp |= ((addr + i) << 8);
		writel(temp, phyctl);

		/* set the data bit and clear usbc bit*/
		temp = readb(phyctl);
		if (data & 0x1)
			temp |= PHYCTL_DATA;
		else
			temp &= ~PHYCTL_DATA;
		temp &= ~usbc_bit;
		writeb(temp, phyctl);

		/* pulse usbc_bit */
		temp = readb(phyctl);
		temp |= usbc_bit;
		writeb(temp, phyctl);

		temp = readb(phyctl);
		temp &= ~usbc_bit;
		writeb(temp, phyctl);

		data >>= 1;
	}
	mutex_unlock(&phy_data->mutex);
}

static void sun4i_usb_phy_passby(struct sun4i_usb_phy *phy, int enable)
{
	u32 bits, reg_value;

	if (!phy->pmu)
		return;

	bits = SUNXI_AHB_ICHR8_EN | SUNXI_AHB_INCR4_BURST_EN |
		SUNXI_AHB_INCRX_ALIGN_EN | SUNXI_ULPI_BYPASS_EN;

	reg_value = readl(phy->pmu);

	if (enable)
		reg_value |= bits;
	else
		reg_value &= ~bits;

	writel(reg_value, phy->pmu);
}

static int sun4i_usb_phy_init(struct phy *_phy)
{
	struct sun4i_usb_phy *phy = phy_get_drvdata(_phy);
	struct sun4i_usb_phy_data *data = to_sun4i_usb_phy_data(phy);
	int ret;
	u32 val;

	ret = clk_prepare_enable(phy->clk);
	if (ret)
		return ret;

	ret = reset_control_deassert(phy->reset);
	if (ret) {
		clk_disable_unprepare(phy->clk);
		return ret;
	}

	if (data->cfg->type == sun8i_h3_phy) {
		if (phy->index == 0) {
			val = readl(data->base + REG_PHY_UNK_H3);
			writel(val & ~1, data->base + REG_PHY_UNK_H3);
		}

		val = readl(phy->pmu + REG_PMU_UNK_H3);
		writel(val & ~2, phy->pmu + REG_PMU_UNK_H3);
	} else {
		/* Enable USB 45 Ohm resistor calibration */
		if (phy->index == 0)
			sun4i_usb_phy_write(phy, PHY_RES45_CAL_EN, 0x01, 1);

		/* Adjust PHY's magnitude and rate */
		sun4i_usb_phy_write(phy, PHY_TX_AMPLITUDE_TUNE, 0x14, 5);

		/* Disconnect threshold adjustment */
		sun4i_usb_phy_write(phy, PHY_DISCON_TH_SEL,
				    data->cfg->disc_thresh, 2);
	}

	sun4i_usb_phy_passby(phy, 1);

	if (phy->index == 0) {
		data->phy0_init = true;

		/* Enable pull-ups */
		sun4i_usb_phy0_update_iscr(_phy, 0, ISCR_DPDM_PULLUP_EN);
		sun4i_usb_phy0_update_iscr(_phy, 0, ISCR_ID_PULLUP_EN);

		if (data->id_det_gpio) {
			/* OTG mode, force ISCR and cable state updates */
			data->id_det = -1;
			data->vbus_det = -1;
			queue_delayed_work(system_wq, &data->detect, 0);
		} else {
			/* Host only mode */
			sun4i_usb_phy0_set_id_detect(_phy, 0);
			sun4i_usb_phy0_set_vbus_detect(_phy, 1);
		}
	}

	return 0;
}

static int sun4i_usb_phy_exit(struct phy *_phy)
{
	struct sun4i_usb_phy *phy = phy_get_drvdata(_phy);
	struct sun4i_usb_phy_data *data = to_sun4i_usb_phy_data(phy);

	if (phy->index == 0) {
		/* Disable pull-ups */
		sun4i_usb_phy0_update_iscr(_phy, ISCR_DPDM_PULLUP_EN, 0);
		sun4i_usb_phy0_update_iscr(_phy, ISCR_ID_PULLUP_EN, 0);
		data->phy0_init = false;
	}

	sun4i_usb_phy_passby(phy, 0);
	reset_control_assert(phy->reset);
	clk_disable_unprepare(phy->clk);

	return 0;
}

static int sun4i_usb_phy0_get_vbus_det(struct sun4i_usb_phy_data *data)
{
	if (data->vbus_det_gpio)
		return gpiod_get_value_cansleep(data->vbus_det_gpio);

	if (data->vbus_power_supply) {
		union power_supply_propval val;
		int r;

		r = power_supply_get_property(data->vbus_power_supply,
					      POWER_SUPPLY_PROP_PRESENT, &val);
		if (r == 0)
			return val.intval;
	}

	/* Fallback: report vbus as high */
	return 1;
}

static bool sun4i_usb_phy0_have_vbus_det(struct sun4i_usb_phy_data *data)
{
	return data->vbus_det_gpio || data->vbus_power_supply;
}

static bool sun4i_usb_phy0_poll(struct sun4i_usb_phy_data *data)
{
	if ((data->id_det_gpio && data->id_det_irq <= 0) ||
	    (data->vbus_det_gpio && data->vbus_det_irq <= 0))
		return true;

	/*
	 * The A31 companion pmic (axp221) does not generate vbus change
	 * interrupts when the board is driving vbus, so we must poll
	 * when using the pmic for vbus-det _and_ we're driving vbus.
	 */
	if (data->cfg->type == sun6i_a31_phy &&
	    data->vbus_power_supply && data->phys[0].regulator_on)
		return true;

	return false;
}

static int sun4i_usb_phy_power_on(struct phy *_phy)
{
	struct sun4i_usb_phy *phy = phy_get_drvdata(_phy);
	struct sun4i_usb_phy_data *data = to_sun4i_usb_phy_data(phy);
	int ret;

	if (!phy->vbus || phy->regulator_on)
		return 0;

	/* For phy0 only turn on Vbus if we don't have an ext. Vbus */
	if (phy->index == 0 && sun4i_usb_phy0_have_vbus_det(data) &&
				data->vbus_det)
		return 0;

	ret = regulator_enable(phy->vbus);
	if (ret)
		return ret;

	phy->regulator_on = true;

	/* We must report Vbus high within OTG_TIME_A_WAIT_VRISE msec. */
	if (phy->index == 0 && sun4i_usb_phy0_poll(data))
		mod_delayed_work(system_wq, &data->detect, DEBOUNCE_TIME);

	return 0;
}

static int sun4i_usb_phy_power_off(struct phy *_phy)
{
	struct sun4i_usb_phy *phy = phy_get_drvdata(_phy);
	struct sun4i_usb_phy_data *data = to_sun4i_usb_phy_data(phy);

	if (!phy->vbus || !phy->regulator_on)
		return 0;

	regulator_disable(phy->vbus);
	phy->regulator_on = false;

	/*
	 * phy0 vbus typically slowly discharges, sometimes this causes the
	 * Vbus gpio to not trigger an edge irq on Vbus off, so force a rescan.
	 */
	if (phy->index == 0 && !sun4i_usb_phy0_poll(data))
		mod_delayed_work(system_wq, &data->detect, POLL_TIME);

	return 0;
}

void sun4i_usb_phy_set_squelch_detect(struct phy *_phy, bool enabled)
{
	struct sun4i_usb_phy *phy = phy_get_drvdata(_phy);

	sun4i_usb_phy_write(phy, PHY_SQUELCH_DETECT, enabled ? 0 : 2, 2);
}
EXPORT_SYMBOL_GPL(sun4i_usb_phy_set_squelch_detect);

static const struct phy_ops sun4i_usb_phy_ops = {
	.init		= sun4i_usb_phy_init,
	.exit		= sun4i_usb_phy_exit,
	.power_on	= sun4i_usb_phy_power_on,
	.power_off	= sun4i_usb_phy_power_off,
	.owner		= THIS_MODULE,
};

static void sun4i_usb_phy0_id_vbus_det_scan(struct work_struct *work)
{
	struct sun4i_usb_phy_data *data =
		container_of(work, struct sun4i_usb_phy_data, detect.work);
	struct phy *phy0 = data->phys[0].phy;
	int id_det, vbus_det, id_notify = 0, vbus_notify = 0;

	id_det = gpiod_get_value_cansleep(data->id_det_gpio);
	vbus_det = sun4i_usb_phy0_get_vbus_det(data);

	mutex_lock(&phy0->mutex);

	if (!data->phy0_init) {
		mutex_unlock(&phy0->mutex);
		return;
	}

	if (id_det != data->id_det) {
		/*
		 * When a host cable (id == 0) gets plugged in on systems
		 * without vbus detection report vbus low for long enough for
		 * the musb-ip to end the current device session.
		 */
		if (!sun4i_usb_phy0_have_vbus_det(data) && id_det == 0) {
			sun4i_usb_phy0_set_vbus_detect(phy0, 0);
			msleep(200);
			sun4i_usb_phy0_set_vbus_detect(phy0, 1);
		}
		sun4i_usb_phy0_set_id_detect(phy0, id_det);
		data->id_det = id_det;
		id_notify = 1;
	}

	if (vbus_det != data->vbus_det) {
		sun4i_usb_phy0_set_vbus_detect(phy0, vbus_det);
		data->vbus_det = vbus_det;
		vbus_notify = 1;
	}

	mutex_unlock(&phy0->mutex);

	if (id_notify) {
		extcon_set_cable_state_(data->extcon, EXTCON_USB_HOST,
					!id_det);
		/*
		 * When a host cable gets unplugged (id == 1) on systems
		 * without vbus detection report vbus low for long enough to
		 * the musb-ip to end the current host session.
		 */
		if (!sun4i_usb_phy0_have_vbus_det(data) && id_det == 1) {
			mutex_lock(&phy0->mutex);
			sun4i_usb_phy0_set_vbus_detect(phy0, 0);
			msleep(1000);
			sun4i_usb_phy0_set_vbus_detect(phy0, 1);
			mutex_unlock(&phy0->mutex);
		}
	}

	if (vbus_notify)
		extcon_set_cable_state_(data->extcon, EXTCON_USB, vbus_det);

	if (sun4i_usb_phy0_poll(data))
		queue_delayed_work(system_wq, &data->detect, POLL_TIME);
}

static irqreturn_t sun4i_usb_phy0_id_vbus_det_irq(int irq, void *dev_id)
{
	struct sun4i_usb_phy_data *data = dev_id;

	/* vbus or id changed, let the pins settle and then scan them */
	mod_delayed_work(system_wq, &data->detect, DEBOUNCE_TIME);

	return IRQ_HANDLED;
}

static int sun4i_usb_phy0_vbus_notify(struct notifier_block *nb,
				      unsigned long val, void *v)
{
	struct sun4i_usb_phy_data *data =
		container_of(nb, struct sun4i_usb_phy_data, vbus_power_nb);
	struct power_supply *psy = v;

	/* Properties on the vbus_power_supply changed, scan vbus_det */
	if (val == PSY_EVENT_PROP_CHANGED && psy == data->vbus_power_supply)
		mod_delayed_work(system_wq, &data->detect, DEBOUNCE_TIME);

	return NOTIFY_OK;
}

static struct phy *sun4i_usb_phy_xlate(struct device *dev,
					struct of_phandle_args *args)
{
	struct sun4i_usb_phy_data *data = dev_get_drvdata(dev);

	if (args->args[0] >= data->cfg->num_phys)
		return ERR_PTR(-ENODEV);

	return data->phys[args->args[0]].phy;
}

static int sun4i_usb_phy_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sun4i_usb_phy_data *data = dev_get_drvdata(dev);

	if (data->vbus_power_nb_registered)
		power_supply_unreg_notifier(&data->vbus_power_nb);
	if (data->id_det_irq > 0)
		devm_free_irq(dev, data->id_det_irq, data);
	if (data->vbus_det_irq > 0)
		devm_free_irq(dev, data->vbus_det_irq, data);

	cancel_delayed_work_sync(&data->detect);

	return 0;
}

static const unsigned int sun4i_usb_phy0_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static int sun4i_usb_phy_probe(struct platform_device *pdev)
{
	struct sun4i_usb_phy_data *data;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct phy_provider *phy_provider;
	struct resource *res;
	int i, ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->mutex);
	INIT_DELAYED_WORK(&data->detect, sun4i_usb_phy0_id_vbus_det_scan);
	dev_set_drvdata(dev, data);
	data->cfg = of_device_get_match_data(dev);
	if (!data->cfg)
		return -EINVAL;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_ctrl");
	data->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	data->id_det_gpio = devm_gpiod_get_optional(dev, "usb0_id_det",
						    GPIOD_IN);
	if (IS_ERR(data->id_det_gpio))
		return PTR_ERR(data->id_det_gpio);

	data->vbus_det_gpio = devm_gpiod_get_optional(dev, "usb0_vbus_det",
						      GPIOD_IN);
	if (IS_ERR(data->vbus_det_gpio))
		return PTR_ERR(data->vbus_det_gpio);

	if (of_find_property(np, "usb0_vbus_power-supply", NULL)) {
		data->vbus_power_supply = devm_power_supply_get_by_phandle(dev,
						     "usb0_vbus_power-supply");
		if (IS_ERR(data->vbus_power_supply))
			return PTR_ERR(data->vbus_power_supply);

		if (!data->vbus_power_supply)
			return -EPROBE_DEFER;
	}

	/* vbus_det without id_det makes no sense, and is not supported */
	if (sun4i_usb_phy0_have_vbus_det(data) && !data->id_det_gpio) {
		dev_err(dev, "usb0_id_det missing or invalid\n");
		return -ENODEV;
	}

	if (data->id_det_gpio) {
		data->extcon = devm_extcon_dev_allocate(dev,
							sun4i_usb_phy0_cable);
		if (IS_ERR(data->extcon))
			return PTR_ERR(data->extcon);

		ret = devm_extcon_dev_register(dev, data->extcon);
		if (ret) {
			dev_err(dev, "failed to register extcon: %d\n", ret);
			return ret;
		}
	}

	for (i = 0; i < data->cfg->num_phys; i++) {
		struct sun4i_usb_phy *phy = data->phys + i;
		char name[16];

		snprintf(name, sizeof(name), "usb%d_vbus", i);
		phy->vbus = devm_regulator_get_optional(dev, name);
		if (IS_ERR(phy->vbus)) {
			if (PTR_ERR(phy->vbus) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			phy->vbus = NULL;
		}

		if (data->cfg->dedicated_clocks)
			snprintf(name, sizeof(name), "usb%d_phy", i);
		else
			strlcpy(name, "usb_phy", sizeof(name));

		phy->clk = devm_clk_get(dev, name);
		if (IS_ERR(phy->clk)) {
			dev_err(dev, "failed to get clock %s\n", name);
			return PTR_ERR(phy->clk);
		}

		snprintf(name, sizeof(name), "usb%d_reset", i);
		phy->reset = devm_reset_control_get(dev, name);
		if (IS_ERR(phy->reset)) {
			dev_err(dev, "failed to get reset %s\n", name);
			return PTR_ERR(phy->reset);
		}

		if (i) { /* No pmu for usbc0 */
			snprintf(name, sizeof(name), "pmu%d", i);
			res = platform_get_resource_byname(pdev,
							IORESOURCE_MEM, name);
			phy->pmu = devm_ioremap_resource(dev, res);
			if (IS_ERR(phy->pmu))
				return PTR_ERR(phy->pmu);
		}

		phy->phy = devm_phy_create(dev, NULL, &sun4i_usb_phy_ops);
		if (IS_ERR(phy->phy)) {
			dev_err(dev, "failed to create PHY %d\n", i);
			return PTR_ERR(phy->phy);
		}

		phy->index = i;
		phy_set_drvdata(phy->phy, &data->phys[i]);
	}

	data->id_det_irq = gpiod_to_irq(data->id_det_gpio);
	if (data->id_det_irq > 0) {
		ret = devm_request_irq(dev, data->id_det_irq,
				sun4i_usb_phy0_id_vbus_det_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"usb0-id-det", data);
		if (ret) {
			dev_err(dev, "Err requesting id-det-irq: %d\n", ret);
			return ret;
		}
	}

	data->vbus_det_irq = gpiod_to_irq(data->vbus_det_gpio);
	if (data->vbus_det_irq > 0) {
		ret = devm_request_irq(dev, data->vbus_det_irq,
				sun4i_usb_phy0_id_vbus_det_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"usb0-vbus-det", data);
		if (ret) {
			dev_err(dev, "Err requesting vbus-det-irq: %d\n", ret);
			data->vbus_det_irq = -1;
			sun4i_usb_phy_remove(pdev); /* Stop detect work */
			return ret;
		}
	}

	if (data->vbus_power_supply) {
		data->vbus_power_nb.notifier_call = sun4i_usb_phy0_vbus_notify;
		data->vbus_power_nb.priority = 0;
		ret = power_supply_reg_notifier(&data->vbus_power_nb);
		if (ret) {
			sun4i_usb_phy_remove(pdev); /* Stop detect work */
			return ret;
		}
		data->vbus_power_nb_registered = true;
	}

	phy_provider = devm_of_phy_provider_register(dev, sun4i_usb_phy_xlate);
	if (IS_ERR(phy_provider)) {
		sun4i_usb_phy_remove(pdev); /* Stop detect work */
		return PTR_ERR(phy_provider);
	}

	return 0;
}

static const struct sun4i_usb_phy_cfg sun4i_a10_cfg = {
	.num_phys = 3,
	.type = sun4i_a10_phy,
	.disc_thresh = 3,
	.phyctl_offset = REG_PHYCTL_A10,
	.dedicated_clocks = false,
};

static const struct sun4i_usb_phy_cfg sun5i_a13_cfg = {
	.num_phys = 2,
	.type = sun4i_a10_phy,
	.disc_thresh = 2,
	.phyctl_offset = REG_PHYCTL_A10,
	.dedicated_clocks = false,
};

static const struct sun4i_usb_phy_cfg sun6i_a31_cfg = {
	.num_phys = 3,
	.type = sun6i_a31_phy,
	.disc_thresh = 3,
	.phyctl_offset = REG_PHYCTL_A10,
	.dedicated_clocks = true,
};

static const struct sun4i_usb_phy_cfg sun7i_a20_cfg = {
	.num_phys = 3,
	.type = sun4i_a10_phy,
	.disc_thresh = 2,
	.phyctl_offset = REG_PHYCTL_A10,
	.dedicated_clocks = false,
};

static const struct sun4i_usb_phy_cfg sun8i_a23_cfg = {
	.num_phys = 2,
	.type = sun4i_a10_phy,
	.disc_thresh = 3,
	.phyctl_offset = REG_PHYCTL_A10,
	.dedicated_clocks = true,
};

static const struct sun4i_usb_phy_cfg sun8i_a33_cfg = {
	.num_phys = 2,
	.type = sun8i_a33_phy,
	.disc_thresh = 3,
	.phyctl_offset = REG_PHYCTL_A33,
	.dedicated_clocks = true,
};

static const struct sun4i_usb_phy_cfg sun8i_h3_cfg = {
	.num_phys = 4,
	.type = sun8i_h3_phy,
	.disc_thresh = 3,
	.dedicated_clocks = true,
};

static const struct of_device_id sun4i_usb_phy_of_match[] = {
	{ .compatible = "allwinner,sun4i-a10-usb-phy", .data = &sun4i_a10_cfg },
	{ .compatible = "allwinner,sun5i-a13-usb-phy", .data = &sun5i_a13_cfg },
	{ .compatible = "allwinner,sun6i-a31-usb-phy", .data = &sun6i_a31_cfg },
	{ .compatible = "allwinner,sun7i-a20-usb-phy", .data = &sun7i_a20_cfg },
	{ .compatible = "allwinner,sun8i-a23-usb-phy", .data = &sun8i_a23_cfg },
	{ .compatible = "allwinner,sun8i-a33-usb-phy", .data = &sun8i_a33_cfg },
	{ .compatible = "allwinner,sun8i-h3-usb-phy", .data = &sun8i_h3_cfg },
	{ },
};
MODULE_DEVICE_TABLE(of, sun4i_usb_phy_of_match);

static struct platform_driver sun4i_usb_phy_driver = {
	.probe	= sun4i_usb_phy_probe,
	.remove	= sun4i_usb_phy_remove,
	.driver = {
		.of_match_table	= sun4i_usb_phy_of_match,
		.name  = "sun4i-usb-phy",
	}
};
module_platform_driver(sun4i_usb_phy_driver);

MODULE_DESCRIPTION("Allwinner sun4i USB phy driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL v2");
