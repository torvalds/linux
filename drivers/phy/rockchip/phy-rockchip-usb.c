/*
 * Rockchip usb PHY driver
 *
 * Copyright (C) 2014 Yunzhi Li <lyz@rock-chips.com>
 * Copyright (C) 2014 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/extcon-provider.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/usb/of.h>
#include <linux/wakelock.h>

static int enable_usb_uart;

#define HIWORD_UPDATE(val, mask) \
		((val) | (mask) << 16)

#define UOC_CON0_SIDDQ BIT(13)

#define RK3288_UOC0_CON0				0x320
#define RK3288_UOC0_CON0_COMMON_ON_N			BIT(0)
#define RK3288_UOC0_CON0_DISABLE			BIT(4)

#define RK3288_UOC0_CON2				0x328
#define RK3288_UOC0_CON2_SOFT_CON_SEL			BIT(2)
#define RK3288_UOC0_CON2_CHRGSEL			BIT(5)
#define RK3288_UOC0_CON2_VDATDETENB			BIT(6)
#define RK3288_UOC0_CON2_VDATSRCENB			BIT(7)
#define RK3288_UOC0_CON2_DCDENB				BIT(14)

#define RK3288_UOC0_CON3				0x32c
#define RK3288_UOC0_CON3_UTMI_SUSPENDN			BIT(0)
#define RK3288_UOC0_CON3_UTMI_OPMODE_NODRIVING		BIT(1)
#define RK3288_UOC0_CON3_UTMI_OPMODE_MASK		(3 << 1)
#define RK3288_UOC0_CON3_UTMI_XCVRSEELCT_FSTRANSC	BIT(3)
#define RK3288_UOC0_CON3_UTMI_XCVRSEELCT_MASK		(3 << 3)
#define RK3288_UOC0_CON3_UTMI_TERMSEL_FULLSPEED		BIT(5)
#define RK3288_UOC0_CON3_BYPASSDMEN			BIT(6)
#define RK3288_UOC0_CON3_BYPASSSEL			BIT(7)
#define RK3288_UOC0_CON3_IDDIG_SET_OTG			(0 << 12)
#define RK3288_UOC0_CON3_IDDIG_SET_HOST			(2 << 12)
#define RK3288_UOC0_CON3_IDDIG_SET_PERIPHERAL		(3 << 12)
#define RK3288_UOC0_CON3_IDDIG_SET_MASK			(3 << 12)

#define RK3288_UOC0_CON4				0x330
#define RK3288_UOC0_CON4_BVALID_IRQ_EN			BIT(2)
#define RK3288_UOC0_CON4_BVALID_IRQ_PD			BIT(3)

#define RK3288_SOC_STATUS2				0x288
#define RK3288_SOC_STATUS2_UTMISRP_BVALID		BIT(14)
#define RK3288_SOC_STATUS2_UTMIOTG_IDDIG		BIT(17)

#define RK3288_SOC_STATUS19				0x2cc
#define RK3288_SOC_STATUS19_CHGDET			BIT(23)
#define RK3288_SOC_STATUS19_FSVPLUS			BIT(24)
#define RK3288_SOC_STATUS19_FSVMINUS			BIT(25)

#define OTG_SCHEDULE_DELAY				(1 * HZ)
#define CHG_DCD_POLL_TIME				(100 * HZ / 1000)
#define CHG_DCD_MAX_RETRIES				6
#define CHG_PRIMARY_DET_TIME				(40 * HZ / 1000)
#define CHG_SECONDARY_DET_TIME				(40 * HZ / 1000)

enum usb_chg_state {
	USB_CHG_STATE_UNDEFINED = 0,
	USB_CHG_STATE_WAIT_FOR_DCD,
	USB_CHG_STATE_DCD_DONE,
	USB_CHG_STATE_PRIMARY_DONE,
	USB_CHG_STATE_SECONDARY_DONE,
	USB_CHG_STATE_DETECTED,
};

static const unsigned int rockchip_usb_phy_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_USB_VBUS_EN,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_CDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_NONE,
};

struct rockchip_usb_phys {
	int reg;
	const char *pll_name;
};

struct rockchip_usb_phy_base;
struct rockchip_usb_phy_pdata {
	struct rockchip_usb_phys *phys;
	int (*init_usb_uart)(struct regmap *grf);
	int usb_uart_phy;
};

struct rockchip_usb_phy_base {
	struct device *dev;
	struct regmap *reg_base;
	struct extcon_dev *edev;
	const struct rockchip_usb_phy_pdata *pdata;
};

struct rockchip_usb_phy {
	struct rockchip_usb_phy_base *base;
	struct device_node	*np;
	unsigned int		reg_offset;
	struct clk		*clk;
	struct clk		*clk480m;
	struct clk_hw		clk480m_hw;
	struct phy		*phy;
	bool			uart_enabled;
	int			bvalid_irq;
	struct reset_control	*reset;
	struct regulator	*vbus;
	struct mutex		mutex; /* protects registers of phy */
	struct delayed_work	chg_work;
	struct delayed_work	otg_sm_work;
	struct wake_lock	wakelock;
	enum usb_chg_state	chg_state;
	enum power_supply_type	chg_type;
	enum usb_dr_mode	mode;
};

static ssize_t otg_mode_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rockchip_usb_phy *rk_phy = dev_get_drvdata(dev);

	if (!rk_phy) {
		dev_err(dev, "Fail to get otg phy.\n");
		return -EINVAL;
	}

	switch (rk_phy->mode) {
	case USB_DR_MODE_HOST:
		return sprintf(buf, "host\n");
	case USB_DR_MODE_PERIPHERAL:
		return sprintf(buf, "peripheral\n");
	case USB_DR_MODE_OTG:
		return sprintf(buf, "otg\n");
	case USB_DR_MODE_UNKNOWN:
		return sprintf(buf, "UNKNOWN\n");
	default:
		break;
	}

	return -EINVAL;
}

static ssize_t otg_mode_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct rockchip_usb_phy *rk_phy = dev_get_drvdata(dev);
	enum usb_dr_mode new_dr_mode;
	int ret = count;
	int val = 0;

	if (!rk_phy) {
		dev_err(dev, "Fail to get otg phy.\n");
		return -EINVAL;
	}

	mutex_lock(&rk_phy->mutex);

	if (!strncmp(buf, "0", 1) || !strncmp(buf, "otg", 3)) {
		new_dr_mode = USB_DR_MODE_OTG;
	} else if (!strncmp(buf, "1", 1) || !strncmp(buf, "host", 4)) {
		new_dr_mode = USB_DR_MODE_HOST;
	} else if (!strncmp(buf, "2", 1) || !strncmp(buf, "peripheral", 10)) {
		new_dr_mode = USB_DR_MODE_PERIPHERAL;
	} else {
		dev_err(&rk_phy->phy->dev, "Error mode! Input 'otg' or 'host' or 'peripheral'\n");
		ret = -EINVAL;
		goto out_unlock;
	}

	if (rk_phy->mode == new_dr_mode) {
		dev_warn(&rk_phy->phy->dev, "Same as current mode.\n");
		goto out_unlock;
	}

	rk_phy->mode = new_dr_mode;

	switch (rk_phy->mode) {
	case USB_DR_MODE_HOST:
		val = HIWORD_UPDATE(RK3288_UOC0_CON3_IDDIG_SET_HOST,
				    RK3288_UOC0_CON3_IDDIG_SET_MASK);
		break;
	case USB_DR_MODE_PERIPHERAL:
		val = HIWORD_UPDATE(RK3288_UOC0_CON3_IDDIG_SET_PERIPHERAL,
				    RK3288_UOC0_CON3_IDDIG_SET_MASK);
		break;
	case USB_DR_MODE_OTG:
		val = HIWORD_UPDATE(RK3288_UOC0_CON3_IDDIG_SET_OTG,
				    RK3288_UOC0_CON3_IDDIG_SET_MASK);
		break;
	default:
		break;
	}

	regmap_write(rk_phy->base->reg_base, RK3288_UOC0_CON3, val);

out_unlock:
	mutex_unlock(&rk_phy->mutex);

	return ret;
}

static DEVICE_ATTR_RW(otg_mode);

/* Group all the usb2 phy attributes */
static struct attribute *usb2_phy_attrs[] = {
	&dev_attr_otg_mode.attr,
	NULL,
};

static struct attribute_group usb2_phy_attr_group = {
	.name = NULL, /* we want them in the same directory */
	.attrs = usb2_phy_attrs,
};

static int rockchip_usb_phy_power(struct rockchip_usb_phy *phy,
					   bool siddq)
{
	u32 val = HIWORD_UPDATE(siddq ? UOC_CON0_SIDDQ : 0, UOC_CON0_SIDDQ);

	return regmap_write(phy->base->reg_base, phy->reg_offset, val);
}

static unsigned long rockchip_usb_phy480m_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	return 480000000;
}

static void rockchip_usb_phy480m_disable(struct clk_hw *hw)
{
	struct rockchip_usb_phy *phy = container_of(hw,
						    struct rockchip_usb_phy,
						    clk480m_hw);

	if (phy->vbus)
		regulator_disable(phy->vbus);

	/* Power down usb phy analog blocks by set siddq 1 */
	rockchip_usb_phy_power(phy, 1);
}

static int rockchip_usb_phy480m_enable(struct clk_hw *hw)
{
	struct rockchip_usb_phy *phy = container_of(hw,
						    struct rockchip_usb_phy,
						    clk480m_hw);

	/* Power up usb phy analog blocks by set siddq 0 */
	return rockchip_usb_phy_power(phy, 0);
}

static int rockchip_usb_phy480m_is_enabled(struct clk_hw *hw)
{
	struct rockchip_usb_phy *phy = container_of(hw,
						    struct rockchip_usb_phy,
						    clk480m_hw);
	int ret;
	u32 val;

	ret = regmap_read(phy->base->reg_base, phy->reg_offset, &val);
	if (ret < 0)
		return ret;

	return (val & UOC_CON0_SIDDQ) ? 0 : 1;
}

static const struct clk_ops rockchip_usb_phy480m_ops = {
	.enable = rockchip_usb_phy480m_enable,
	.disable = rockchip_usb_phy480m_disable,
	.is_enabled = rockchip_usb_phy480m_is_enabled,
	.recalc_rate = rockchip_usb_phy480m_recalc_rate,
};

static int rk3288_usb_phy_init(struct phy *_phy)
{
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);
	int ret = 0;
	unsigned int val;

	if (phy->bvalid_irq > 0) {
		mutex_lock(&phy->mutex);

		/* clear bvalid status and enable bvalid detect irq */
		val = HIWORD_UPDATE(RK3288_UOC0_CON4_BVALID_IRQ_EN
					| RK3288_UOC0_CON4_BVALID_IRQ_PD,
				    RK3288_UOC0_CON4_BVALID_IRQ_EN
					| RK3288_UOC0_CON4_BVALID_IRQ_PD);
		ret = regmap_write(phy->base->reg_base, RK3288_UOC0_CON4, val);
		if (ret) {
			dev_err(phy->base->dev,
				"failed to enable bvalid irq\n");
			goto out;
		}

		schedule_delayed_work(&phy->otg_sm_work, OTG_SCHEDULE_DELAY);

out:
		mutex_unlock(&phy->mutex);
	}

	return ret;
}

static int rk3288_usb_phy_exit(struct phy *_phy)
{
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);

	if (phy->bvalid_irq > 0)
		flush_delayed_work(&phy->otg_sm_work);

	return 0;
}

static int rockchip_usb_phy_power_off(struct phy *_phy)
{
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);

	if (phy->uart_enabled)
		return -EBUSY;

	clk_disable_unprepare(phy->clk480m);

	return 0;
}

static int rockchip_usb_phy_power_on(struct phy *_phy)
{
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);

	if (phy->uart_enabled)
		return -EBUSY;

	if (phy->vbus) {
		int ret;

		ret = regulator_enable(phy->vbus);
		if (ret)
			return ret;
	}

	return clk_prepare_enable(phy->clk480m);
}

static int rockchip_usb_phy_reset(struct phy *_phy)
{
	struct rockchip_usb_phy *phy = phy_get_drvdata(_phy);

	if (phy->reset) {
		reset_control_assert(phy->reset);
		udelay(10);
		reset_control_deassert(phy->reset);
	}

	return 0;
}

static struct phy_ops ops = {
	.power_on	= rockchip_usb_phy_power_on,
	.power_off	= rockchip_usb_phy_power_off,
	.reset		= rockchip_usb_phy_reset,
	.owner		= THIS_MODULE,
};

static void rockchip_usb_phy_action(void *data)
{
	struct rockchip_usb_phy *rk_phy = data;

	if (!rk_phy->uart_enabled) {
		of_clk_del_provider(rk_phy->np);
		clk_unregister(rk_phy->clk480m);
	}

	if (rk_phy->clk)
		clk_put(rk_phy->clk);
}

static int rockchip_usb_phy_extcon_register(struct rockchip_usb_phy_base *base)
{
	int ret;
	struct device_node *node = base->dev->of_node;
	struct extcon_dev *edev;

	if (of_property_read_bool(node, "extcon")) {
		edev = extcon_get_edev_by_phandle(base->dev, 0);
		if (IS_ERR(edev)) {
			if (PTR_ERR(edev) != -EPROBE_DEFER)
				dev_err(base->dev,
					"Invalid or missing extcon\n");
			return PTR_ERR(edev);
		}
	} else {
		/* Initialize extcon device */
		edev = devm_extcon_dev_allocate(base->dev,
						rockchip_usb_phy_extcon_cable);

		if (IS_ERR(edev))
			return -ENOMEM;

		ret = devm_extcon_dev_register(base->dev, edev);
		if (ret) {
			dev_err(base->dev,
				"failed to register extcon device\n");
			return ret;
		}
	}

	base->edev = edev;

	return 0;
}

static void rk3288_usb_phy_otg_sm_work(struct work_struct *work)
{
	struct rockchip_usb_phy *rk_phy = container_of(work,
						       struct rockchip_usb_phy,
						       otg_sm_work.work);
	unsigned int val;
	static unsigned int cable;
	static bool chg_det_completed;
	bool sch_work;
	bool vbus_attached;
	bool id;

	mutex_lock(&rk_phy->mutex);

	sch_work = false;

	regmap_read(rk_phy->base->reg_base, RK3288_SOC_STATUS2, &val);
	id = (val & RK3288_SOC_STATUS2_UTMIOTG_IDDIG) ? true : false;

	regmap_read(rk_phy->base->reg_base, RK3288_SOC_STATUS2, &val);
	vbus_attached =
		(val & RK3288_SOC_STATUS2_UTMISRP_BVALID) ? true : false;

	if (!vbus_attached || !id || rk_phy->mode == USB_DR_MODE_HOST) {
		dev_dbg(&rk_phy->phy->dev, "peripheral disconnected\n");
		wake_unlock(&rk_phy->wakelock);
		extcon_set_state_sync(rk_phy->base->edev, cable, false);
		rk_phy->chg_state = USB_CHG_STATE_UNDEFINED;
		chg_det_completed = false;
		goto out;
	}

	if (chg_det_completed) {
		sch_work = true;
		goto out;
	}

	switch (rk_phy->chg_state) {
	case USB_CHG_STATE_UNDEFINED:
		mutex_unlock(&rk_phy->mutex);
		schedule_delayed_work(&rk_phy->chg_work, 0);
		return;
	case USB_CHG_STATE_DETECTED:
		switch (rk_phy->chg_type) {
		case POWER_SUPPLY_TYPE_USB:
			dev_dbg(&rk_phy->phy->dev, "sdp cable is connected\n");
			wake_lock(&rk_phy->wakelock);
			cable = EXTCON_CHG_USB_SDP;
			sch_work = true;
			break;
		case POWER_SUPPLY_TYPE_USB_DCP:
			dev_dbg(&rk_phy->phy->dev, "dcp cable is connected\n");
			cable = EXTCON_CHG_USB_DCP;
			sch_work = true;
			break;
		case POWER_SUPPLY_TYPE_USB_CDP:
			dev_dbg(&rk_phy->phy->dev, "cdp cable is connected\n");
			wake_lock(&rk_phy->wakelock);
			cable = EXTCON_CHG_USB_CDP;
			sch_work = true;
			break;
		default:
			break;
		}
		chg_det_completed = true;
		break;
	default:
		break;
	}

	if (extcon_get_state(rk_phy->base->edev, cable) != vbus_attached)
		extcon_set_state_sync(rk_phy->base->edev, cable,
				      vbus_attached);

out:
	if (sch_work)
		schedule_delayed_work(&rk_phy->otg_sm_work, OTG_SCHEDULE_DELAY);

	mutex_unlock(&rk_phy->mutex);
}

static const char *chg_to_string(enum power_supply_type chg_type)
{
	switch (chg_type) {
	case POWER_SUPPLY_TYPE_USB:
		return "USB_SDP_CHARGER";
	case POWER_SUPPLY_TYPE_USB_DCP:
		return "USB_DCP_CHARGER";
	case POWER_SUPPLY_TYPE_USB_CDP:
		return "USB_CDP_CHARGER";
	default:
		return "INVALID_CHARGER";
	}
}

static void rk3288_chg_detect_work(struct work_struct *work)
{
	struct rockchip_usb_phy *rk_phy =
		container_of(work, struct rockchip_usb_phy, chg_work.work);
	unsigned int val;
	static int dcd_retries;
	static int primary_retries;
	unsigned long delay;
	bool fsvplus;
	bool vout;
	bool tmout;

	dev_dbg(&rk_phy->phy->dev, "chg detection work state = %d\n",
		rk_phy->chg_state);

	switch (rk_phy->chg_state) {
	case USB_CHG_STATE_UNDEFINED:
		mutex_lock(&rk_phy->mutex);
		/* put the controller in non-driving mode */
		val = HIWORD_UPDATE(RK3288_UOC0_CON2_SOFT_CON_SEL,
				    RK3288_UOC0_CON2_SOFT_CON_SEL);
		regmap_write(rk_phy->base->reg_base, RK3288_UOC0_CON2, val);
		val = HIWORD_UPDATE(RK3288_UOC0_CON3_UTMI_OPMODE_NODRIVING,
				    RK3288_UOC0_CON3_UTMI_SUSPENDN
					| RK3288_UOC0_CON3_UTMI_OPMODE_MASK);
		regmap_write(rk_phy->base->reg_base, RK3288_UOC0_CON3, val);
		/* Start DCD processing stage 1 */
		val = HIWORD_UPDATE(RK3288_UOC0_CON2_DCDENB,
				    RK3288_UOC0_CON2_DCDENB);
		regmap_write(rk_phy->base->reg_base, RK3288_UOC0_CON2, val);
		rk_phy->chg_state = USB_CHG_STATE_WAIT_FOR_DCD;
		dcd_retries = 0;
		primary_retries = 0;
		delay = CHG_DCD_POLL_TIME;
		break;
	case USB_CHG_STATE_WAIT_FOR_DCD:
		/* get data contact detection status */
		regmap_read(rk_phy->base->reg_base, RK3288_SOC_STATUS19, &val);
		fsvplus = (val & RK3288_SOC_STATUS19_FSVPLUS) ? true : false;
		tmout = ++dcd_retries == CHG_DCD_MAX_RETRIES;
		/* stage 2 */
		if (!fsvplus || tmout) {
vdpsrc:
			/* stage 4 */
			/* Turn off DCD circuitry */
			val = HIWORD_UPDATE(0, RK3288_UOC0_CON2_DCDENB);
			regmap_write(rk_phy->base->reg_base,
				     RK3288_UOC0_CON2, val);
			/* Voltage Source on DP, Probe on DM */
			val = HIWORD_UPDATE(RK3288_UOC0_CON2_VDATSRCENB
						| RK3288_UOC0_CON2_VDATDETENB,
					    RK3288_UOC0_CON2_VDATSRCENB
						| RK3288_UOC0_CON2_VDATDETENB
						| RK3288_UOC0_CON2_CHRGSEL);
			regmap_write(rk_phy->base->reg_base,
				     RK3288_UOC0_CON2, val);
			delay = CHG_PRIMARY_DET_TIME;
			rk_phy->chg_state = USB_CHG_STATE_DCD_DONE;
		} else {
			/* stage 3 */
			delay = CHG_DCD_POLL_TIME;
		}
		break;
	case USB_CHG_STATE_DCD_DONE:
		regmap_read(rk_phy->base->reg_base, RK3288_SOC_STATUS19, &val);
		vout = (val & RK3288_SOC_STATUS19_CHGDET) ? true : false;

		val = HIWORD_UPDATE(0, RK3288_UOC0_CON2_VDATSRCENB
					| RK3288_UOC0_CON2_VDATDETENB);
		regmap_write(rk_phy->base->reg_base, RK3288_UOC0_CON2, val);
		if (vout) {
			/* Voltage Source on DM, Probe on DP  */
			val = HIWORD_UPDATE(RK3288_UOC0_CON2_VDATSRCENB
						| RK3288_UOC0_CON2_VDATDETENB
						| RK3288_UOC0_CON2_CHRGSEL,
					    RK3288_UOC0_CON2_VDATSRCENB
						| RK3288_UOC0_CON2_VDATDETENB
						| RK3288_UOC0_CON2_CHRGSEL);
			regmap_write(rk_phy->base->reg_base,
				     RK3288_UOC0_CON2, val);
			delay = CHG_SECONDARY_DET_TIME;
			rk_phy->chg_state = USB_CHG_STATE_PRIMARY_DONE;
		} else {
			if (dcd_retries == CHG_DCD_MAX_RETRIES) {
				/* floating charger found */
				rk_phy->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
				rk_phy->chg_state = USB_CHG_STATE_DETECTED;
				delay = 0;
			} else if (primary_retries < 2) {
				primary_retries++;
				goto vdpsrc;
			} else {
				rk_phy->chg_type = POWER_SUPPLY_TYPE_USB;
				rk_phy->chg_state = USB_CHG_STATE_DETECTED;
				delay = 0;
			}
		}
		break;
	case USB_CHG_STATE_PRIMARY_DONE:
		regmap_read(rk_phy->base->reg_base, RK3288_SOC_STATUS19, &val);
		vout = (val & RK3288_SOC_STATUS19_CHGDET) ? true : false;

		/* Turn off voltage source */
		val = HIWORD_UPDATE(0, RK3288_UOC0_CON2_VDATSRCENB
					| RK3288_UOC0_CON2_VDATDETENB
					| RK3288_UOC0_CON2_CHRGSEL);
		regmap_write(rk_phy->base->reg_base, RK3288_UOC0_CON2, val);
		if (vout)
			rk_phy->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		else
			rk_phy->chg_type = POWER_SUPPLY_TYPE_USB_CDP;
		/* fall through */
	case USB_CHG_STATE_SECONDARY_DONE:
		rk_phy->chg_state = USB_CHG_STATE_DETECTED;
		/* fall through */
	case USB_CHG_STATE_DETECTED:
		/* put the controller in normal mode */
		val = HIWORD_UPDATE(0, RK3288_UOC0_CON2_SOFT_CON_SEL);
		regmap_write(rk_phy->base->reg_base, RK3288_UOC0_CON2, val);
		val = HIWORD_UPDATE(RK3288_UOC0_CON3_UTMI_SUSPENDN,
				    RK3288_UOC0_CON3_UTMI_SUSPENDN
					| RK3288_UOC0_CON3_UTMI_OPMODE_MASK);
		regmap_write(rk_phy->base->reg_base, RK3288_UOC0_CON3, val);
		mutex_unlock(&rk_phy->mutex);
		rk3288_usb_phy_otg_sm_work(&rk_phy->otg_sm_work.work);
		dev_info(&rk_phy->phy->dev, "charger = %s\n",
			 chg_to_string(rk_phy->chg_type));
		return;
	default:
		mutex_unlock(&rk_phy->mutex);
		return;
	}

	/*
	 * Hold the mutex lock during the whole charger
	 * detection stage, and release it after detect
	 * the charger type.
	 */
	schedule_delayed_work(&rk_phy->chg_work, delay);
}

static irqreturn_t rk3288_usb_phy_bvalid_irq(int irq, void *data)
{
	struct rockchip_usb_phy *rk_phy = data;
	int ret;
	unsigned int val;

	ret = regmap_read(rk_phy->base->reg_base, RK3288_UOC0_CON4, &val);
	if (ret < 0 || !(val & RK3288_UOC0_CON4_BVALID_IRQ_PD))
		return IRQ_NONE;

	mutex_lock(&rk_phy->mutex);

	/* clear bvalid detect irq pending status */
	val = HIWORD_UPDATE(RK3288_UOC0_CON4_BVALID_IRQ_PD,
			    RK3288_UOC0_CON4_BVALID_IRQ_PD);
	regmap_write(rk_phy->base->reg_base, RK3288_UOC0_CON4, val);

	mutex_unlock(&rk_phy->mutex);

	if (rk_phy->uart_enabled)
		goto out;

	cancel_delayed_work_sync(&rk_phy->otg_sm_work);
	rk3288_usb_phy_otg_sm_work(&rk_phy->otg_sm_work.work);
out:
	return IRQ_HANDLED;
}

static int rk3288_usb_phy_probe_init(struct rockchip_usb_phy *rk_phy)
{
	int ret = 0;
	unsigned int val;

	if (rk_phy->reg_offset == 0x320) {
		/* Enable Bvalid interrupt and charge detection */
		ops.init = rk3288_usb_phy_init;
		ops.exit = rk3288_usb_phy_exit;
		rk_phy->bvalid_irq = of_irq_get_byname(rk_phy->np,
						       "otg-bvalid");
		regmap_read(rk_phy->base->reg_base, RK3288_UOC0_CON4, &val);
		if (rk_phy->bvalid_irq <= 0) {
			dev_err(&rk_phy->phy->dev,
				"no vbus valid irq provided\n");
			ret = -EINVAL;
			goto out;
		}

		ret = devm_request_threaded_irq(rk_phy->base->dev,
						rk_phy->bvalid_irq,
						NULL,
						rk3288_usb_phy_bvalid_irq,
						IRQF_ONESHOT,
						"rockchip_usb_phy_bvalid",
						rk_phy);
		if (ret) {
			dev_err(&rk_phy->phy->dev,
				"failed to request otg-bvalid irq handle\n");
			goto out;
		}

		rk_phy->chg_state = USB_CHG_STATE_UNDEFINED;
		wake_lock_init(&rk_phy->wakelock, WAKE_LOCK_SUSPEND,
			       "rockchip_otg");
		INIT_DELAYED_WORK(&rk_phy->chg_work, rk3288_chg_detect_work);
		INIT_DELAYED_WORK(&rk_phy->otg_sm_work,
				  rk3288_usb_phy_otg_sm_work);

		rk_phy->mode = of_usb_get_dr_mode_by_phy(rk_phy->np, -1);
		if (rk_phy->mode == USB_DR_MODE_OTG ||
		    rk_phy->mode == USB_DR_MODE_UNKNOWN) {
			ret = sysfs_create_group(&rk_phy->phy->dev.kobj,
						 &usb2_phy_attr_group);
			if (ret) {
				dev_err(&rk_phy->phy->dev,
					"Cannot create sysfs group\n");
				goto out;
			}
		}
	} else if (rk_phy->reg_offset == 0x334) {
		/*
		 * Setting the COMMONONN to 1'b0 for EHCI PHY on RK3288 SoC.
		 *
		 * EHCI (auto) suspend causes the corresponding usb-phy into
		 * suspend mode which would power down the inner PLL blocks in
		 * usb-phy if the COMMONONN is set to 1'b1. The PLL output
		 * clocks contained CLK480M, CLK12MOHCI, CLK48MOHCI, PHYCLOCK0
		 * and so on, these clocks are not only supplied for EHCI and
		 * OHCI, but also supplied for GPU and other external modules,
		 * so setting COMMONONN to 1'b0 to keep the inner PLL blocks in
		 * usb-phy always powered.
		 */
		regmap_write(rk_phy->base->reg_base, rk_phy->reg_offset,
			     BIT(16));
	}
out:
	return ret;
}

static int rockchip_usb_phy_init(struct rockchip_usb_phy_base *base,
				 struct device_node *child)
{
	struct device_node *np = base->dev->of_node;
	struct rockchip_usb_phy *rk_phy;
	unsigned int reg_offset;
	const char *clk_name;
	struct clk_init_data init = {};
	int err, i;

	rk_phy = devm_kzalloc(base->dev, sizeof(*rk_phy), GFP_KERNEL);
	if (!rk_phy)
		return -ENOMEM;

	rk_phy->base = base;
	rk_phy->np = child;
	mutex_init(&rk_phy->mutex);

	if (of_property_read_u32(child, "reg", &reg_offset)) {
		dev_err(base->dev, "missing reg property in node %s\n",
			child->name);
		return -EINVAL;
	}

	rk_phy->reset = of_reset_control_get(child, "phy-reset");
	if (IS_ERR(rk_phy->reset))
		rk_phy->reset = NULL;

	rk_phy->reg_offset = reg_offset;

	rk_phy->clk = of_clk_get_by_name(child, "phyclk");
	if (IS_ERR(rk_phy->clk))
		rk_phy->clk = NULL;

	i = 0;
	init.name = NULL;
	while (base->pdata->phys[i].reg) {
		if (base->pdata->phys[i].reg == reg_offset) {
			init.name = base->pdata->phys[i].pll_name;
			break;
		}
		i++;
	}

	if (!init.name) {
		dev_err(base->dev, "phy data not found\n");
		return -EINVAL;
	}

	if (enable_usb_uart && base->pdata->usb_uart_phy == i) {
		dev_dbg(base->dev, "phy%d used as uart output\n", i);
		rk_phy->uart_enabled = true;
	} else {
		if (rk_phy->clk) {
			clk_name = __clk_get_name(rk_phy->clk);
			init.flags = 0;
			init.parent_names = &clk_name;
			init.num_parents = 1;
		} else {
			init.flags = 0;
			init.parent_names = NULL;
			init.num_parents = 0;
		}

		init.ops = &rockchip_usb_phy480m_ops;
		rk_phy->clk480m_hw.init = &init;

		rk_phy->clk480m = clk_register(base->dev, &rk_phy->clk480m_hw);
		if (IS_ERR(rk_phy->clk480m)) {
			err = PTR_ERR(rk_phy->clk480m);
			goto err_clk;
		}

		err = of_clk_add_provider(child, of_clk_src_simple_get,
					rk_phy->clk480m);
		if (err < 0)
			goto err_clk_prov;
	}

	err = devm_add_action_or_reset(base->dev, rockchip_usb_phy_action,
				       rk_phy);
	if (err)
		return err;

	rk_phy->phy = devm_phy_create(base->dev, child, &ops);
	if (IS_ERR(rk_phy->phy)) {
		dev_err(base->dev, "failed to create PHY\n");
		return PTR_ERR(rk_phy->phy);
	}
	phy_set_drvdata(rk_phy->phy, rk_phy);

	if (of_device_is_compatible(np, "rockchip,rk3288-usb-phy")) {
		err = rk3288_usb_phy_probe_init(rk_phy);
		if (err)
			return err;
	}

	rk_phy->vbus = devm_regulator_get_optional(&rk_phy->phy->dev, "vbus");
	if (IS_ERR(rk_phy->vbus)) {
		if (PTR_ERR(rk_phy->vbus) == -EPROBE_DEFER)
			return PTR_ERR(rk_phy->vbus);
		rk_phy->vbus = NULL;
	}

	/*
	 * When acting as uart-pipe, just keep clock on otherwise
	 * only power up usb phy when it use, so disable it when init
	 */
	if (rk_phy->uart_enabled)
		return clk_prepare_enable(rk_phy->clk);
	else
		return rockchip_usb_phy_power(rk_phy, 1);

err_clk_prov:
	if (!rk_phy->uart_enabled)
		clk_unregister(rk_phy->clk480m);
err_clk:
	if (rk_phy->clk)
		clk_put(rk_phy->clk);
	return err;
}

static const struct rockchip_usb_phy_pdata rk3066a_pdata = {
	.phys = (struct rockchip_usb_phys[]){
		{ .reg = 0x17c, .pll_name = "sclk_otgphy0_480m" },
		{ .reg = 0x188, .pll_name = "sclk_otgphy1_480m" },
		{ /* sentinel */ }
	},
};

static const struct rockchip_usb_phy_pdata rk3188_pdata = {
	.phys = (struct rockchip_usb_phys[]){
		{ .reg = 0x10c, .pll_name = "sclk_otgphy0_480m" },
		{ .reg = 0x11c, .pll_name = "sclk_otgphy1_480m" },
		{ /* sentinel */ }
	},
};

/*
 * Enable the bypass of uart2 data through the otg usb phy.
 * Original description in the TRM.
 * 1. Disable the OTG block by setting OTGDISABLE0 to 1’b1.
 * 2. Disable the pull-up resistance on the D+ line by setting
 *    OPMODE0[1:0] to 2’b01.
 * 3. To ensure that the XO, Bias, and PLL blocks are powered down in Suspend
 *    mode, set COMMONONN to 1’b1.
 * 4. Place the USB PHY in Suspend mode by setting SUSPENDM0 to 1’b0.
 * 5. Set BYPASSSEL0 to 1’b1.
 * 6. To transmit data, controls BYPASSDMEN0, and BYPASSDMDATA0.
 * To receive data, monitor FSVPLUS0.
 *
 * The actual code in the vendor kernel does some things differently.
 */
static int __init rk3288_init_usb_uart(struct regmap *grf)
{
	u32 val;
	int ret;

	/*
	 * COMMON_ON and DISABLE settings are described in the TRM,
	 * but were not present in the original code.
	 * Also disable the analog phy components to save power.
	 */
	val = HIWORD_UPDATE(RK3288_UOC0_CON0_COMMON_ON_N
				| RK3288_UOC0_CON0_DISABLE
				| UOC_CON0_SIDDQ,
			    RK3288_UOC0_CON0_COMMON_ON_N
				| RK3288_UOC0_CON0_DISABLE
				| UOC_CON0_SIDDQ);
	ret = regmap_write(grf, RK3288_UOC0_CON0, val);
	if (ret)
		return ret;

	val = HIWORD_UPDATE(RK3288_UOC0_CON2_SOFT_CON_SEL,
			    RK3288_UOC0_CON2_SOFT_CON_SEL);
	ret = regmap_write(grf, RK3288_UOC0_CON2, val);
	if (ret)
		return ret;

	val = HIWORD_UPDATE(RK3288_UOC0_CON3_UTMI_OPMODE_NODRIVING
				| RK3288_UOC0_CON3_UTMI_XCVRSEELCT_FSTRANSC
				| RK3288_UOC0_CON3_UTMI_TERMSEL_FULLSPEED,
			    RK3288_UOC0_CON3_UTMI_SUSPENDN
				| RK3288_UOC0_CON3_UTMI_OPMODE_MASK
				| RK3288_UOC0_CON3_UTMI_XCVRSEELCT_MASK
				| RK3288_UOC0_CON3_UTMI_TERMSEL_FULLSPEED);
	ret = regmap_write(grf, RK3288_UOC0_CON3, val);
	if (ret)
		return ret;

	val = HIWORD_UPDATE(RK3288_UOC0_CON3_BYPASSSEL
				| RK3288_UOC0_CON3_BYPASSDMEN,
			    RK3288_UOC0_CON3_BYPASSSEL
				| RK3288_UOC0_CON3_BYPASSDMEN);
	ret = regmap_write(grf, RK3288_UOC0_CON3, val);
	if (ret)
		return ret;

	return 0;
}

static const struct rockchip_usb_phy_pdata rk3288_pdata = {
	.phys = (struct rockchip_usb_phys[]){
		{ .reg = 0x320, .pll_name = "sclk_otgphy0_480m" },
		{ .reg = 0x334, .pll_name = "sclk_otgphy1_480m" },
		{ .reg = 0x348, .pll_name = "sclk_otgphy2_480m" },
		{ /* sentinel */ }
	},
	.init_usb_uart = rk3288_init_usb_uart,
	.usb_uart_phy = 0,
};

static int rockchip_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_usb_phy_base *phy_base;
	struct phy_provider *phy_provider;
	const struct of_device_id *match;
	struct device_node *child;
	int err;

	phy_base = devm_kzalloc(dev, sizeof(*phy_base), GFP_KERNEL);
	if (!phy_base)
		return -ENOMEM;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "missing phy data\n");
		return -EINVAL;
	}

	phy_base->pdata = match->data;

	phy_base->dev = dev;
	phy_base->reg_base = ERR_PTR(-ENODEV);
	if (dev->parent && dev->parent->of_node)
		phy_base->reg_base = syscon_node_to_regmap(
						dev->parent->of_node);
	if (IS_ERR(phy_base->reg_base))
		phy_base->reg_base = syscon_regmap_lookup_by_phandle(
						dev->of_node, "rockchip,grf");
	if (IS_ERR(phy_base->reg_base)) {
		dev_err(&pdev->dev, "Missing rockchip,grf property\n");
		return PTR_ERR(phy_base->reg_base);
	}

	err = rockchip_usb_phy_extcon_register(phy_base);
	if (err)
		return err;

	for_each_available_child_of_node(dev->of_node, child) {
		err = rockchip_usb_phy_init(phy_base, child);
		if (err) {
			of_node_put(child);
			return err;
		}
	}

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id rockchip_usb_phy_dt_ids[] = {
	{ .compatible = "rockchip,rk3066a-usb-phy", .data = &rk3066a_pdata },
	{ .compatible = "rockchip,rk3188-usb-phy", .data = &rk3188_pdata },
	{ .compatible = "rockchip,rk3288-usb-phy", .data = &rk3288_pdata },
	{}
};

MODULE_DEVICE_TABLE(of, rockchip_usb_phy_dt_ids);

static struct platform_driver rockchip_usb_driver = {
	.probe		= rockchip_usb_phy_probe,
	.driver		= {
		.name	= "rockchip-usb-phy",
		.of_match_table = rockchip_usb_phy_dt_ids,
	},
};

module_platform_driver(rockchip_usb_driver);

#ifndef MODULE
static int __init rockchip_init_usb_uart(void)
{
	const struct of_device_id *match;
	const struct rockchip_usb_phy_pdata *data;
	struct device_node *np;
	struct regmap *grf;
	int ret;

	if (!enable_usb_uart)
		return 0;

	np = of_find_matching_node_and_match(NULL, rockchip_usb_phy_dt_ids,
					     &match);
	if (!np) {
		pr_err("%s: failed to find usbphy node\n", __func__);
		return -ENOTSUPP;
	}

	pr_debug("%s: using settings for %s\n", __func__, match->compatible);
	data = match->data;

	if (!data->init_usb_uart) {
		pr_err("%s: usb-uart not available on %s\n",
		       __func__, match->compatible);
		return -ENOTSUPP;
	}

	grf = ERR_PTR(-ENODEV);
	if (np->parent)
		grf = syscon_node_to_regmap(np->parent);
	if (IS_ERR(grf))
		grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(grf)) {
		pr_err("%s: Missing rockchip,grf property, %lu\n",
		       __func__, PTR_ERR(grf));
		return PTR_ERR(grf);
	}

	ret = data->init_usb_uart(grf);
	if (ret) {
		pr_err("%s: could not init usb_uart, %d\n", __func__, ret);
		enable_usb_uart = 0;
		return ret;
	}

	return 0;
}
early_initcall(rockchip_init_usb_uart);

static int __init rockchip_usb_uart(char *buf)
{
	enable_usb_uart = true;
	return 0;
}
early_param("rockchip.usb_uart", rockchip_usb_uart);
#endif

MODULE_AUTHOR("Yunzhi Li <lyz@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USB 2.0 PHY driver");
MODULE_LICENSE("GPL v2");
