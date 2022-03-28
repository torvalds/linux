// SPDX-License-Identifier: GPL-2.0+
/*
 * Rockchip USB2.0 PHY with Naneng IP block driver
 *
 * Copyright (C) 2020 Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/extcon-provider.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/gpio/consumer.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/mfd/syscon.h>
#include <linux/usb/of.h>
#include <linux/usb/otg.h>
#include <linux/wakelock.h>

struct rockchip_usb2phy;

#define BIT_WRITEABLE_SHIFT	16
#define OTG_SCHEDULE_DELAY	(1 * HZ)

enum rockchip_usb2phy_port_id {
	USB2PHY_PORT_OTG,
	USB2PHY_PORT_HOST,
	USB2PHY_NUM_PORTS,
};

enum calibrate_state {
	SWING_CALIBRATION,
	CURRENT_COMPENSATION,
	CALIBRATION_DONE,
};

static const unsigned int rockchip_usb2phy_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_USB_VBUS_EN,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_CDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_NONE,
};

struct usb2phy_reg {
	unsigned int	offset;
	unsigned int	bitend;
	unsigned int	bitstart;
	unsigned int	disable;
	unsigned int	enable;
};

/**
 * struct rockchip_chg_det_reg: usb charger detect registers
 * @chg_en: charge detector enable signal.
 * @chg_rst: charge detector reset signal, active high.
 * @chg_valid: charge valid signal.
 * @phy_connect: PHY start handshake signal.
 */
struct rockchip_chg_det_reg {
	struct usb2phy_reg	chg_en;
	struct usb2phy_reg	chg_rst;
	struct usb2phy_reg	chg_valid;
	struct usb2phy_reg	phy_connect;
};

/**
 * struct rockchip_usb2phy_port_cfg: usb phy port configuration.
 * @bypass_otgsuspendm: otg-suspendm bypass control register.
 *			 0: iddig; 1: grf.
 * @bvalidfall_det_en: vbus valid fall detection enable register.
 * @bvalidfall_det_st: vbus valid fall detection status register.
 * @bvalidfall_det_clr: vbus valid fall detection clear register.
 * @bvalidrise_det_en: vbus valid rise detection enable register.
 * @bvalidrise_det_st: vbus valid rise detection status register.
 * @bvalidrise_det_clr: vbus valid rise detection clear register.
 * @disconfall_det_en: host connect detection enable register.
 * @disconfall_det_st: host connect detection status register.
 * @disconfall_det_clr: host connect detection clear register.
 * @disconrise_det_en: host disconnect detection enable register.
 * @disconrise_det_st: host disconnect detection status register.
 * @disconrise_det_clr: host disconnect detection clear register.
 * @idfall_det_en: id fall detection enable register.
 * @idfall_det_st: id fall detection state register.
 * @idfall_det_clr: id fall detection clear register.
 * @idpullup: id pin pullup or pulldown control register.
 * @iddig_output: utmi iddig value from grf output.
 * @iddig_en: select utmi iddig output from grf or phy,
 *	      0: from phy output; 1: from grf output
 * @idrise_det_en: id rise detection enable register.
 * @idrise_det_st: id rise detection state register.
 * @idrise_det_clr: id rise detection clear register.
 * @ls_det_en: linestate detection enable register.
 * @ls_det_st: linestate detection state register.
 * @ls_det_clr: linestate detection clear register.
 * @phy_sus: phy suspend register.
 * @utmi_bvalid: utmi vbus bvalid status register.
 * @utmi_iddig: otg port id pin status register.
 * @utmi_hostdet: utmi host disconnect status register.
 */
struct rockchip_usb2phy_port_cfg {
	struct usb2phy_reg	bypass_otgsuspendm;
	struct usb2phy_reg	bvalidfall_det_en;
	struct usb2phy_reg	bvalidfall_det_st;
	struct usb2phy_reg	bvalidfall_det_clr;
	struct usb2phy_reg	bvalidrise_det_en;
	struct usb2phy_reg	bvalidrise_det_st;
	struct usb2phy_reg	bvalidrise_det_clr;
	struct usb2phy_reg	disconfall_det_en;
	struct usb2phy_reg	disconfall_det_st;
	struct usb2phy_reg	disconfall_det_clr;
	struct usb2phy_reg	disconrise_det_en;
	struct usb2phy_reg	disconrise_det_st;
	struct usb2phy_reg	disconrise_det_clr;
	struct usb2phy_reg	idfall_det_en;
	struct usb2phy_reg	idfall_det_st;
	struct usb2phy_reg	idfall_det_clr;
	struct usb2phy_reg	idpullup;
	struct usb2phy_reg	iddig_output;
	struct usb2phy_reg	iddig_en;
	struct usb2phy_reg	idrise_det_en;
	struct usb2phy_reg	idrise_det_st;
	struct usb2phy_reg	idrise_det_clr;
	struct usb2phy_reg	ls_det_en;
	struct usb2phy_reg	ls_det_st;
	struct usb2phy_reg	ls_det_clr;
	struct usb2phy_reg	phy_sus;
	struct usb2phy_reg	utmi_bvalid;
	struct usb2phy_reg	utmi_iddig;
	struct usb2phy_reg	utmi_hostdet;
};

/**
 * struct rockchip_usb2phy_cfg: usb phy configuration.
 * @reg: the address offset of grf for usb-phy config.
 * @num_ports: specify how many ports that the phy has.
 * @clks: array of input clocks
 * @num_clks: number of input clocks.
 * @phy_tuning: phy default parameters tuning.
 * @phy_lowpower: phy low power mode.
 * @clkout_ctl: keep on/turn off output clk of phy.
 * @port_cfgs: ports register configuration, assigned by driver data.
 * @chg_det: charger detection registers.
 * @last: indicate the last one.
 */
struct rockchip_usb2phy_cfg {
	unsigned int	reg;
	unsigned int	num_ports;
	const struct	clk_bulk_data *clks;
	int		num_clks;
	int		(*phy_tuning)(struct rockchip_usb2phy *rphy);
	int		(*phy_lowpower)(struct rockchip_usb2phy *rphy, bool en);
	struct		usb2phy_reg clkout_ctl;
	const struct	rockchip_usb2phy_port_cfg port_cfgs[USB2PHY_NUM_PORTS];
	const struct	rockchip_chg_det_reg chg_det;
	bool		last;
};

/**
 * struct rockchip_usb2phy_port: usb phy port data.
 * @phy: the struct phy of this port.
 * @port_id: flag for otg port or host port.
 * @perip_connected: flag for periphyeral connect status.
 * @prev_iddig: previous otg port id pin status.
 * @suspended: phy suspended flag.
 * @vbus_attached: otg device vbus status.
 * @vbus_always_on: otg vbus is always powered on.
 * @vbus_enabled: vbus regulator status.
 * @bvalid_irq: IRQ number assigned for vbus valid rise detection.
 * @ls_irq: IRQ number assigned for linestate detection.
 * @disconnect_irq: IRQ number assigned for host disconnect detection.
 * @id_irq: IRQ number assigned for id fall or rise detection.
 * @mutex: for register updating in interrupt thread.
 * @otg_sm_work: OTG periphreal connect or disconnect judgement.
 * @vbus: vbus regulator supply on few rockchip boards.
 * @port_cfg: port register configuration, assigned by driver data.
 * @wakelock: wakeup source for otg-port.
 * @mode: the dr_mode of the controller.
 */
struct rockchip_usb2phy_port {
	struct phy	*phy;
	unsigned int	port_id;
	bool		perip_connected;
	bool		prev_iddig;
	bool		suspended;
	bool		vbus_attached;
	bool		vbus_always_on;
	bool		vbus_enabled;
	int		bvalid_irq;
	int		ls_irq;
	int		disconnect_irq;
	int		id_irq;
	struct mutex	mutex; /* protects register of phy */
	struct		delayed_work otg_sm_work;
	struct		regulator *vbus;
	const struct	rockchip_usb2phy_port_cfg *port_cfg;
	struct		wake_lock wakelock;
	enum		usb_dr_mode mode;
};

/**
 * struct rockchip_usb2phy: usb2.0 phy driver data.
 * @dev: pointer to our struct device.
 * @grf: General Register Files regmap.
 * @base: the base address of APB interface.
 * @reset: power reset signal for phy.
 * @clks: array of input clocks.
 * @num_clks: number of input clocks.
 * @clk480m: clock struct of phy output clk.
 * @clk480m_hw: clock struct of phy output clk management.
 * @chg_type: USB charger types.
 * @edev_self: represent the source of extcon.
 * @edev: extcon device for notification registration.
 * @vup_gpio: gpio switch for pull-up register on DM.
 * @wait_timer: hrtimer for phy calibration delay.
 * @cal_state: state of phy calibration.
 * @phy_cfg: phy register configuration, assigned by driver data.
 * @ports: phy port instance.
 */
struct rockchip_usb2phy {
	struct device		*dev;
	struct regmap		*grf;
	void __iomem		*base;
	struct reset_control	*reset;
	struct clk_bulk_data	*clks;
	int			num_clks;
	struct clk		*clk480m;
	struct clk_hw		clk480m_hw;
	enum power_supply_type	chg_type;
	bool			edev_self;
	struct extcon_dev	*edev;
	struct gpio_desc	*vup_gpio;
	struct hrtimer		wait_timer;
	enum calibrate_state	cal_state;
	const struct		rockchip_usb2phy_cfg *phy_cfg;
	struct			rockchip_usb2phy_port ports[USB2PHY_NUM_PORTS];
};

static inline int property_enable(struct regmap *base,
				  const struct usb2phy_reg *reg, bool en)
{
	unsigned int val, mask, tmp;

	tmp = en ? reg->enable : reg->disable;
	mask = GENMASK(reg->bitend, reg->bitstart);
	val = (tmp << reg->bitstart) | (mask << BIT_WRITEABLE_SHIFT);

	return regmap_write(base, reg->offset, val);
}

static inline bool property_enabled(struct regmap *base,
				    const struct usb2phy_reg *reg)
{
	int ret;
	unsigned int tmp, orig;
	unsigned int mask = GENMASK(reg->bitend, reg->bitstart);

	ret = regmap_read(base, reg->offset, &orig);
	if (ret)
		return false;

	tmp = (orig & mask) >> reg->bitstart;
	return tmp == reg->enable;
}

static int rockchip_usb2phy_clk480m_prepare(struct clk_hw *hw)
{
	struct rockchip_usb2phy *rphy =
		container_of(hw, struct rockchip_usb2phy, clk480m_hw);
	int ret;

	/* turn on 480m clk output if it is off */
	if (!property_enabled(rphy->grf, &rphy->phy_cfg->clkout_ctl)) {
		ret = property_enable(rphy->grf, &rphy->phy_cfg->clkout_ctl,
				      true);
		if (ret)
			return ret;

		/* waiting for the clk become stable */
		usleep_range(500, 600);
	}

	return 0;
}

static void rockchip_usb2phy_clk480m_unprepare(struct clk_hw *hw)
{
	struct rockchip_usb2phy *rphy =
		container_of(hw, struct rockchip_usb2phy, clk480m_hw);

	/* turn off 480m clk output */
	property_enable(rphy->grf, &rphy->phy_cfg->clkout_ctl, false);
}

static int rockchip_usb2phy_clk480m_prepared(struct clk_hw *hw)
{
	struct rockchip_usb2phy *rphy =
		container_of(hw, struct rockchip_usb2phy, clk480m_hw);

	return property_enabled(rphy->grf, &rphy->phy_cfg->clkout_ctl);
}

static unsigned long
rockchip_usb2phy_clk480m_recalc_rate(struct clk_hw *hw,
				     unsigned long parent_rate)
{
	return 480000000;
}

static const struct clk_ops rockchip_usb2phy_clkout_ops = {
	.prepare = rockchip_usb2phy_clk480m_prepare,
	.unprepare = rockchip_usb2phy_clk480m_unprepare,
	.is_prepared = rockchip_usb2phy_clk480m_prepared,
	.recalc_rate = rockchip_usb2phy_clk480m_recalc_rate,
};

static void rockchip_usb2phy_clk480m_unregister(void *data)
{
	struct rockchip_usb2phy *rphy = data;

	of_clk_del_provider(rphy->dev->of_node);
	clk_unregister(rphy->clk480m);
}

static int
rockchip_usb2phy_clk480m_register(struct rockchip_usb2phy *rphy)
{
	struct device_node *node = rphy->dev->of_node;
	struct clk_init_data init = {};
	struct clk *refclk = of_clk_get_by_name(node, "phyclk");
	const char *clk_name;
	int ret;

	init.flags = 0;
	init.name = "clk_usbphy_480m";
	init.ops = &rockchip_usb2phy_clkout_ops;

	/* optional override of the clockname */
	of_property_read_string(node, "clock-output-names", &init.name);

	if (!IS_ERR(refclk)) {
		clk_name = __clk_get_name(refclk);
		init.parent_names = &clk_name;
		init.num_parents = 1;
	} else {
		init.parent_names = NULL;
		init.num_parents = 0;
	}

	rphy->clk480m_hw.init = &init;

	/* register the clock */
	rphy->clk480m = clk_register(rphy->dev, &rphy->clk480m_hw);
	if (IS_ERR(rphy->clk480m)) {
		ret = PTR_ERR(rphy->clk480m);
		goto err_ret;
	}

	ret = of_clk_add_provider(node, of_clk_src_simple_get, rphy->clk480m);
	if (ret < 0)
		goto err_clk_provider;

	ret = devm_add_action(rphy->dev, rockchip_usb2phy_clk480m_unregister,
			      rphy);
	if (ret < 0)
		goto err_unreg_action;

	return 0;

err_unreg_action:
	of_clk_del_provider(node);
err_clk_provider:
	clk_unregister(rphy->clk480m);
err_ret:
	return ret;
}

/* The caller must hold rport->mutex lock */
static int rockchip_usb2phy_enable_id_irq(struct rockchip_usb2phy *rphy,
					  struct rockchip_usb2phy_port *rport,
					  bool en)
{
	int ret;

	ret = property_enable(rphy->grf,
			      &rport->port_cfg->idfall_det_clr, true);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf, &rport->port_cfg->idfall_det_en, en);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf,
			      &rport->port_cfg->idrise_det_clr, true);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf, &rport->port_cfg->idrise_det_en, en);
out:
	return ret;
}

/* The caller must hold rport->mutex lock */
static int rockchip_usb2phy_enable_vbus_irq(struct rockchip_usb2phy *rphy,
					    struct rockchip_usb2phy_port *rport,
					    bool en)
{
	int ret;

	ret = property_enable(rphy->grf,
			      &rport->port_cfg->bvalidfall_det_clr, true);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf,
			      &rport->port_cfg->bvalidfall_det_en, en);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf,
			      &rport->port_cfg->bvalidrise_det_clr, true);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf,
			      &rport->port_cfg->bvalidrise_det_en, en);
out:
	return ret;
}

static int rockchip_usb2phy_enable_line_irq(struct rockchip_usb2phy *rphy,
					    struct rockchip_usb2phy_port *rport,
					    bool en)
{
	int ret;

	ret = property_enable(rphy->grf, &rport->port_cfg->ls_det_clr, true);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf, &rport->port_cfg->ls_det_en, en);
out:
	return ret;
}

static int
	rockchip_usb2phy_enable_disconn_irq(struct rockchip_usb2phy *rphy,
					    struct rockchip_usb2phy_port *rport,
					    bool en)
{
	int ret;

	ret = property_enable(rphy->grf,
			      &rport->port_cfg->disconrise_det_clr, true);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf,
			      &rport->port_cfg->disconrise_det_en, en);
out:
	return ret;
}

static int rockchip_usb2phy_extcon_register(struct rockchip_usb2phy *rphy)
{
	int ret;
	struct device_node *node = rphy->dev->of_node;
	struct extcon_dev *edev;

	if (of_property_read_bool(node, "extcon")) {
		edev = extcon_get_edev_by_phandle(rphy->dev, 0);
		if (IS_ERR(edev)) {
			if (PTR_ERR(edev) != -EPROBE_DEFER)
				dev_err(rphy->dev,
					"Invalid or missing extcon\n");
			return PTR_ERR(edev);
		}
	} else {
		/* Initialize extcon device */
		edev = devm_extcon_dev_allocate(rphy->dev,
						rockchip_usb2phy_extcon_cable);

		if (IS_ERR(edev))
			return -ENOMEM;

		ret = devm_extcon_dev_register(rphy->dev, edev);
		if (ret) {
			dev_err(rphy->dev,
				"failed to register extcon device\n");
			return ret;
		}

		rphy->edev_self = true;
	}

	rphy->edev = edev;

	return 0;
}

static int rockchip_usb2phy_init(struct phy *phy)
{
	struct rockchip_usb2phy_port *rport = phy_get_drvdata(phy);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(phy->dev.parent);
	int ret = 0;

	mutex_lock(&rport->mutex);

	/* clear disconnect status and enable disconnect detect irq */
	if (rport->disconnect_irq > 0) {
		ret = rockchip_usb2phy_enable_disconn_irq(rphy, rport, true);
		if (ret) {
			dev_err(rphy->dev, "failed to enable disconnect irq\n");
			goto out;
		}
	}

	/* clear linstate status and enable linestate detect irq */
	if (rport->ls_irq > 0 &&
	    (rport->port_id == USB2PHY_PORT_HOST ||
	     rport->mode == USB_DR_MODE_HOST)) {
		ret = rockchip_usb2phy_enable_line_irq(rphy, rport, true);
		if (ret) {
			dev_err(rphy->dev, "failed to enable linestate irq\n");
			goto out;
		}
	}

	/* clear bvalid status and enable bvalid detect irq */
	if (rport->bvalid_irq > 0) {
		ret = rockchip_usb2phy_enable_vbus_irq(rphy, rport, true);
		if (ret) {
			dev_err(rphy->dev,
				"failed to enable bvalid irq\n");
			goto out;
		}

		rport->vbus_attached =
			property_enabled(rphy->grf,
					 &rport->port_cfg->utmi_bvalid);
		schedule_delayed_work(&rport->otg_sm_work, OTG_SCHEDULE_DELAY);
	}

	/* clear id status and enable id detect irq */
	if (rport->id_irq > 0) {
		ret = rockchip_usb2phy_enable_id_irq(rphy, rport,
						     true);
		if (ret) {
			dev_err(rphy->dev,
				"failed to enable id irq\n");
			goto out;
		}
	}

out:
	mutex_unlock(&rport->mutex);
	return ret;
}

static int rockchip_usb2phy_power_on(struct phy *phy)
{
	struct rockchip_usb2phy_port *rport = phy_get_drvdata(phy);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(phy->dev.parent);
	int ret;

	dev_dbg(&rport->phy->dev, "port power on\n");

	mutex_lock(&rport->mutex);

	if (!rport->suspended) {
		ret = 0;
		goto unlock;
	}

	ret = property_enable(rphy->grf, &rport->port_cfg->phy_sus, false);
	if (ret)
		goto unlock;

	/* waiting for the utmi_clk to become stable */
	usleep_range(2500, 3000);

	rport->suspended = false;

unlock:
	mutex_unlock(&rport->mutex);

	return ret;
}

static int rockchip_usb2phy_power_off(struct phy *phy)
{
	struct rockchip_usb2phy_port *rport = phy_get_drvdata(phy);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(phy->dev.parent);
	int ret;

	dev_dbg(&rport->phy->dev, "port power off\n");

	mutex_lock(&rport->mutex);

	if (rport->suspended) {
		ret = 0;
		goto unlock;
	}

	ret = property_enable(rphy->grf, &rport->port_cfg->phy_sus, true);
	if (ret)
		goto unlock;

	rport->suspended = true;

unlock:
	mutex_unlock(&rport->mutex);

	return ret;
}

static int rockchip_usb2phy_exit(struct phy *phy)
{
	return 0;
}

static int rockchip_set_vbus_power(struct rockchip_usb2phy_port *rport,
				   bool en)
{
	int ret = 0;

	if (!rport->vbus)
		return 0;

	if (en && !rport->vbus_enabled) {
		ret = regulator_enable(rport->vbus);
		if (ret)
			dev_err(&rport->phy->dev,
				"Failed to enable VBUS supply\n");
	} else if (!en && rport->vbus_enabled) {
		ret = regulator_disable(rport->vbus);
	}

	if (ret == 0)
		rport->vbus_enabled = en;

	return ret;
}

static int rockchip_usb2phy_set_mode(struct phy *phy,
				     enum phy_mode mode, int submode)
{
	struct rockchip_usb2phy_port *rport = phy_get_drvdata(phy);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(phy->dev.parent);
	int ret = 0;

	if (rport->port_id != USB2PHY_PORT_OTG)
		return ret;

	switch (mode) {
	case PHY_MODE_USB_OTG:
		/* fallthrough */
	case PHY_MODE_USB_DEVICE:
		/* Disable VBUS supply */
		rockchip_set_vbus_power(rport, false);
		extcon_set_state_sync(rphy->edev, EXTCON_USB_VBUS_EN, false);
		break;
	case PHY_MODE_USB_HOST:
		/* Enable VBUS supply */
		ret = rockchip_set_vbus_power(rport, true);
		if (ret) {
			dev_err(&rport->phy->dev,
				"Failed to set host mode\n");
			return ret;
		}

		extcon_set_state_sync(rphy->edev, EXTCON_USB_VBUS_EN, true);
		break;
	default:
		dev_info(&rport->phy->dev, "illegal mode\n");
		return ret;
	}

	return ret;
}

static enum hrtimer_restart rv1126_wait_timer_fn(struct hrtimer *t)
{
	enum hrtimer_restart ret;
	ktime_t delay;
	static u32 reg;
	struct rockchip_usb2phy *rphy = container_of(t, struct rockchip_usb2phy,
						     wait_timer);

	switch (rphy->cal_state) {
	case SWING_CALIBRATION:
		/* disable tx swing calibrate */
		writel(0x5d, rphy->base + 0x20);
		/* read the value of rsistance calibration */
		reg = readl(rphy->base + 0x10);

		/* open the pull-up resistor */
		gpiod_set_value(rphy->vup_gpio, 1);
		/* set cfg_hs_strg 0 to increase chirpk amplitude */
		writel(0x08, rphy->base + 0x00);
		/*
		 * set internal 45 Ohm resistance minimal to
		 * increase chirpk amplitude
		 */
		writel(0x7c, rphy->base + 0x10);

		delay = ktime_set(0, 1200000);
		hrtimer_forward_now(&rphy->wait_timer, delay);
		rphy->cal_state = CURRENT_COMPENSATION;
		ret = HRTIMER_RESTART;
		break;
	case CURRENT_COMPENSATION:
		/* close the pull-up resistor */
		gpiod_set_value(rphy->vup_gpio, 0);
		/*
		 * set cfg_sel_strength and cfg_sel_pw 1 to
		 * correct the effect of pull-up resistor
		 */
		writel(0xe8, rphy->base + 0x00);
		/* write the value of rsistance calibration */
		writel(reg, rphy->base + 0x10);

		delay = ktime_set(0, 1000000);
		hrtimer_forward_now(&rphy->wait_timer, delay);
		rphy->cal_state = CALIBRATION_DONE;
		ret = HRTIMER_RESTART;
		break;
	case CALIBRATION_DONE:
		/* enable tx swing calibrate */
		writel(0x4d, rphy->base + 0x20);
		/* fall through */
	default:
		ret = HRTIMER_NORESTART;
		break;
	}

	return ret;
}

static int rv1126_usb2phy_calibrate(struct phy *phy)
{
	struct rockchip_usb2phy_port *rport = phy_get_drvdata(phy);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(phy->dev.parent);
	ktime_t delay;

	if (rport->port_id != USB2PHY_PORT_OTG)
		return 0;

	delay = ktime_set(0, 500000);
	rphy->cal_state = SWING_CALIBRATION;
	hrtimer_start(&rphy->wait_timer, delay, HRTIMER_MODE_REL);

	return 0;
}

static struct phy_ops rockchip_usb2phy_ops = {
	.init			= rockchip_usb2phy_init,
	.exit			= rockchip_usb2phy_exit,
	.power_on		= rockchip_usb2phy_power_on,
	.power_off		= rockchip_usb2phy_power_off,
	.set_mode		= rockchip_usb2phy_set_mode,
	.owner			= THIS_MODULE,
};

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

static void rockchip_chg_detect(struct rockchip_usb2phy *rphy,
				struct rockchip_usb2phy_port *rport)
{
	bool chg_valid, phy_connect;
	int result;
	int cnt;

	mutex_lock(&rport->mutex);

	reset_control_assert(rphy->reset);

	/* CHG_RST is set to 1'b0 to start charge detection */
	property_enable(rphy->grf, &rphy->phy_cfg->chg_det.chg_en, true);
	property_enable(rphy->grf, &rphy->phy_cfg->chg_det.chg_rst, false);

	for (cnt = 0; cnt < 12; cnt++) {
		msleep(100);

		chg_valid = property_enabled(rphy->grf,
					     &rphy->phy_cfg->chg_det.chg_valid);
		phy_connect =
			property_enabled(rphy->grf,
					 &rphy->phy_cfg->chg_det.phy_connect);
		result = (chg_valid << 1) | phy_connect;

		if (result)
			break;
	}

	switch (result) {
	case 1:
		rphy->chg_type = POWER_SUPPLY_TYPE_USB;
		break;
	case 2:
		rphy->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	case 3:
		rphy->chg_type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case 0:
		/* fall through */
	default:
		rphy->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		break;
	}

	dev_info(&rport->phy->dev, "charger = %s\n",
		 chg_to_string(rphy->chg_type));

	usleep_range(1000, 1100);
	reset_control_deassert(rphy->reset);
	/* waiting for the utmi_clk to become stable */
	usleep_range(2500, 3000);

	/* disable the chg detection module */
	property_enable(rphy->grf, &rphy->phy_cfg->chg_det.chg_rst, true);
	property_enable(rphy->grf, &rphy->phy_cfg->chg_det.chg_en, false);

	mutex_unlock(&rport->mutex);
}

static irqreturn_t rockchip_usb2phy_disconnect_irq(int irq, void *data)
{
	struct rockchip_usb2phy_port *rport = data;
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);

	if (!property_enabled(rphy->grf, &rport->port_cfg->disconrise_det_st))
		return IRQ_NONE;

	mutex_lock(&rport->mutex);

	/* clear disconnect rise detect irq pending status */
	property_enable(rphy->grf, &rport->port_cfg->disconrise_det_clr, true);

	mutex_unlock(&rport->mutex);

	/* prevent fs/ls device disconnect before enumeration */
	msleep(200);
	if (!property_enabled(rphy->grf, &rport->port_cfg->utmi_hostdet))
		return IRQ_HANDLED;

	mutex_lock(&rport->mutex);

	/* enable linestate detect irq to detect next host connect */
	rockchip_usb2phy_enable_line_irq(rphy, rport, true);

	mutex_unlock(&rport->mutex);

	dev_dbg(&rport->phy->dev, "host disconnected\n");
	rockchip_usb2phy_power_off(rport->phy);

	return IRQ_HANDLED;
}

static irqreturn_t rockchip_usb2phy_linestate_irq(int irq, void *data)
{
	struct rockchip_usb2phy_port *rport = data;
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);

	if (!property_enabled(rphy->grf, &rport->port_cfg->ls_det_st))
		return IRQ_NONE;

	dev_dbg(&rport->phy->dev, "linestate interrupt\n");

	mutex_lock(&rport->mutex);

	/* disable linestate detect irq and clear its status */
	rockchip_usb2phy_enable_line_irq(rphy, rport, false);

	mutex_unlock(&rport->mutex);

	if (!rport->suspended)
		return IRQ_HANDLED;

	if (rport->port_id != USB2PHY_PORT_HOST &&
	    rport->mode != USB_DR_MODE_HOST)
		return IRQ_HANDLED;

	dev_dbg(&rport->phy->dev, "host connected\n");
	rockchip_usb2phy_power_on(rport->phy);

	return IRQ_HANDLED;
}

static void rockchip_usb2phy_otg_sm_work(struct work_struct *work)
{
	static unsigned int cable;
	struct rockchip_usb2phy_port *rport =
		container_of(work, struct rockchip_usb2phy_port,
			     otg_sm_work.work);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);

	if (rport->vbus_attached) {
		if (extcon_get_state(rphy->edev, EXTCON_USB_HOST) ||
		    extcon_get_state(rphy->edev, EXTCON_USB_VBUS_EN) ||
		    !property_enabled(rphy->grf, &rport->port_cfg->utmi_iddig))
			return;

		if (rport->perip_connected)
			return;

		rockchip_chg_detect(rphy, rport);

		switch (rphy->chg_type) {
		case POWER_SUPPLY_TYPE_USB:
			dev_dbg(&rport->phy->dev, "sdp cable is connected\n");
			wake_lock(&rport->wakelock);
			cable = EXTCON_CHG_USB_SDP;
			rport->perip_connected = true;
			break;
		case POWER_SUPPLY_TYPE_USB_DCP:
			dev_dbg(&rport->phy->dev, "dcp cable is connected\n");
			cable = EXTCON_CHG_USB_DCP;
			break;
		case POWER_SUPPLY_TYPE_USB_CDP:
			dev_dbg(&rport->phy->dev, "cdp cable is connected\n");
			wake_lock(&rport->wakelock);
			cable = EXTCON_CHG_USB_CDP;
			rport->perip_connected = true;
			break;
		default:
			break;
		}
	} else {
		if (!rport->perip_connected) {
			if (extcon_get_state(rphy->edev, EXTCON_CHG_USB_DCP) > 0)
				extcon_set_state_sync(rphy->edev, EXTCON_CHG_USB_DCP, 0);
			return;
		}

		dev_dbg(&rport->phy->dev, "usb peripheral disconnect\n");
		wake_unlock(&rport->wakelock);
		rport->perip_connected = false;
		rphy->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
	}

	if (extcon_get_state(rphy->edev, cable) != rport->vbus_attached) {
		extcon_set_state(rphy->edev, cable, rport->vbus_attached);
		extcon_sync(rphy->edev, cable);
	}

	if (rphy->edev_self && (extcon_get_state(rphy->edev, EXTCON_USB) !=
				rport->perip_connected)) {
		extcon_set_state(rphy->edev, EXTCON_USB,
				 rport->perip_connected);

		extcon_sync(rphy->edev, EXTCON_USB);
		extcon_sync(rphy->edev, EXTCON_USB_HOST);
	}
}

/* Show & store the current value of otg mode for otg port */
static ssize_t otg_mode_show(struct device *device,
			     struct device_attribute *attr,
			     char *buf)
{
	struct rockchip_usb2phy *rphy = dev_get_drvdata(device);
	struct rockchip_usb2phy_port *rport = NULL;
	unsigned int index;

	for (index = 0; index < rphy->phy_cfg->num_ports; index++) {
		rport = &rphy->ports[index];
		if (rport->port_id == USB2PHY_PORT_OTG)
			break;
	}

	if (!rport) {
		dev_err(rphy->dev, "Fail to get otg port\n");
		return -EINVAL;
	} else if (rport->port_id != USB2PHY_PORT_OTG) {
		dev_err(rphy->dev, "No support otg\n");
		return -EINVAL;
	}

	switch (rport->mode) {
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

static ssize_t otg_mode_store(struct device *device,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct rockchip_usb2phy *rphy = dev_get_drvdata(device);
	struct rockchip_usb2phy_port *rport = NULL;
	enum usb_dr_mode new_dr_mode;
	unsigned int index;
	int rc = count;

	for (index = 0; index < rphy->phy_cfg->num_ports; index++) {
		rport = &rphy->ports[index];
		if (rport->port_id == USB2PHY_PORT_OTG)
			break;
	}

	if (!rport) {
		dev_err(rphy->dev, "Fail to get otg port!\n");
		rc = -EINVAL;
		goto exit;
	} else if (rport->port_id != USB2PHY_PORT_OTG ||
		   rport->mode == USB_DR_MODE_UNKNOWN) {
		dev_err(rphy->dev, "No support otg!\n");
		rc = -EINVAL;
		goto exit;
	}

	mutex_lock(&rport->mutex);

	if (!strncmp(buf, "0", 1) || !strncmp(buf, "otg", 3)) {
		new_dr_mode = USB_DR_MODE_OTG;
	} else if (!strncmp(buf, "1", 1) || !strncmp(buf, "host", 4)) {
		new_dr_mode = USB_DR_MODE_HOST;
	} else if (!strncmp(buf, "2", 1) || !strncmp(buf, "peripheral", 10)) {
		new_dr_mode = USB_DR_MODE_PERIPHERAL;
	} else {
		dev_err(rphy->dev, "Error mode! Input 'otg' or 'host' or 'peripheral'\n");
		rc = -EINVAL;
		goto unlock;
	}

	if (rport->mode == new_dr_mode) {
		dev_warn(rphy->dev, "Same as current mode\n");
		goto unlock;
	}

	rport->mode = new_dr_mode;

	switch (rport->mode) {
	case USB_DR_MODE_HOST:
		rport->perip_connected = false;
		extcon_set_state(rphy->edev, EXTCON_USB, false);
		extcon_set_state(rphy->edev, EXTCON_USB_HOST, true);
		extcon_sync(rphy->edev, EXTCON_USB);
		extcon_sync(rphy->edev, EXTCON_USB_HOST);
		rockchip_usb2phy_set_mode(rport->phy, PHY_MODE_USB_HOST, 0);
		property_enable(rphy->grf, &rport->port_cfg->idpullup,
				false);
		property_enable(rphy->grf, &rport->port_cfg->iddig_output,
				false);
		property_enable(rphy->grf, &rport->port_cfg->iddig_en,
				true);
		break;
	case USB_DR_MODE_PERIPHERAL:
		rockchip_usb2phy_set_mode(rport->phy, PHY_MODE_USB_DEVICE, 0);
		property_enable(rphy->grf, &rport->port_cfg->idpullup,
				true);
		property_enable(rphy->grf, &rport->port_cfg->iddig_output,
				true);
		property_enable(rphy->grf, &rport->port_cfg->iddig_en,
				true);
		break;
	case USB_DR_MODE_OTG:
		rockchip_usb2phy_set_mode(rport->phy, PHY_MODE_USB_OTG, 0);
		property_enable(rphy->grf, &rport->port_cfg->iddig_output,
				false);
		property_enable(rphy->grf, &rport->port_cfg->iddig_en,
				false);
		break;
	default:
		break;
	}


	if ((rport->mode == USB_DR_MODE_PERIPHERAL ||
	     rport->mode == USB_DR_MODE_OTG) && property_enabled(rphy->grf,
	     &rport->port_cfg->utmi_bvalid)) {
		rport->vbus_attached = true;
		cancel_delayed_work_sync(&rport->otg_sm_work);
		schedule_delayed_work(&rport->otg_sm_work, OTG_SCHEDULE_DELAY);
	}
unlock:
	mutex_unlock(&rport->mutex);

exit:
	return rc;
}
static DEVICE_ATTR_RW(otg_mode);

/* Group all the usb2 phy attributes */
static struct attribute *usb2_phy_attrs[] = {
	&dev_attr_otg_mode.attr,
	NULL,
};

static struct attribute_group usb2_phy_attr_group = {
	.name = NULL,	/* we want them in the same directory */
	.attrs = usb2_phy_attrs,
};

static irqreturn_t rockchip_usb2phy_bvalid_irq(int irq, void *data)
{
	struct rockchip_usb2phy_port *rport = data;
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);

	if (!property_enabled(rphy->grf, &rport->port_cfg->bvalidfall_det_st) &&
	    !property_enabled(rphy->grf, &rport->port_cfg->bvalidrise_det_st))
		return IRQ_NONE;

	mutex_lock(&rport->mutex);

	/* clear bvalid fall or rise detect irq pending status */
	if (property_enabled(rphy->grf, &rport->port_cfg->bvalidfall_det_st)) {
		property_enable(rphy->grf, &rport->port_cfg->bvalidfall_det_clr,
				true);
		rport->vbus_attached = false;
	} else if (property_enabled(rphy->grf,
				    &rport->port_cfg->bvalidrise_det_st)) {
		property_enable(rphy->grf, &rport->port_cfg->bvalidrise_det_clr,
				true);
		rport->vbus_attached = true;
	}

	mutex_unlock(&rport->mutex);

	cancel_delayed_work_sync(&rport->otg_sm_work);
	rockchip_usb2phy_otg_sm_work(&rport->otg_sm_work.work);

	return IRQ_HANDLED;
}

static irqreturn_t rockchip_usb2phy_id_irq(int irq, void *data)
{
	struct rockchip_usb2phy_port *rport = data;
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);
	bool cable_vbus_state = false;

	if (!property_enabled(rphy->grf, &rport->port_cfg->idfall_det_st) &&
	    !property_enabled(rphy->grf, &rport->port_cfg->idrise_det_st))
		return IRQ_NONE;

	mutex_lock(&rport->mutex);

	/* clear id fall or rise detect irq pending status */
	if (property_enabled(rphy->grf, &rport->port_cfg->idfall_det_st)) {
		property_enable(rphy->grf, &rport->port_cfg->idfall_det_clr,
				true);
		cable_vbus_state = true;
	} else if (property_enabled(rphy->grf,
				    &rport->port_cfg->idrise_det_st)) {
		property_enable(rphy->grf, &rport->port_cfg->idrise_det_clr,
				true);
		cable_vbus_state = false;
	}

	dev_dbg(&rport->phy->dev, "id %s interrupt\n",
		cable_vbus_state ? "fall" : "rise");
	extcon_set_state(rphy->edev, EXTCON_USB_HOST, cable_vbus_state);
	extcon_set_state(rphy->edev, EXTCON_USB_VBUS_EN, cable_vbus_state);

	extcon_sync(rphy->edev, EXTCON_USB_HOST);
	extcon_sync(rphy->edev, EXTCON_USB_VBUS_EN);

	rockchip_set_vbus_power(rport, cable_vbus_state);

	mutex_unlock(&rport->mutex);

	return IRQ_HANDLED;
}

static int rockchip_usb2phy_otg_port_init(struct rockchip_usb2phy *rphy,
					  struct rockchip_usb2phy_port *rport,
					  struct device_node *child_np)
{
	int ret = 0;
	int iddig;

	mutex_init(&rport->mutex);

	rport->port_id = USB2PHY_PORT_OTG;
	rport->port_cfg = &rphy->phy_cfg->port_cfgs[USB2PHY_PORT_OTG];
	rport->vbus_attached = false;
	rport->vbus_enabled = false;
	rport->prev_iddig = true;

	rport->vbus_always_on =
		of_property_read_bool(child_np, "rockchip,vbus-always-on");

	ret = rockchip_usb2phy_extcon_register(rphy);
	if (ret)
		return ret;

	/* Get Vbus regulators */
	rport->vbus = devm_regulator_get_optional(&rport->phy->dev, "vbus");
	if (IS_ERR(rport->vbus)) {
		if (PTR_ERR(rport->vbus) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		dev_warn(&rport->phy->dev,
			 "Failed to get VBUS supply regulator\n");
		rport->vbus = NULL;
	}

	/*
	 * The default value of bypass_otgsuspendm is 1 that we must set
	 * otg_suspendm and LS_PAR_EN by software when switching drd role.
	 * So we disable the otg_suspend_bypass to let hardware auto-switch
	 * device mode or host mode.
	 */
	property_enable(rphy->grf, &rport->port_cfg->bypass_otgsuspendm,
			false);

	/* Request linstate interrupt */
	rport->ls_irq = of_irq_get_byname(child_np, "linestate");
	if (rport->ls_irq <= 0) {
		dev_err(rphy->dev, "no linestate irq provided\n");
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(rphy->dev, rport->ls_irq, NULL,
					rockchip_usb2phy_linestate_irq,
					IRQF_ONESHOT,
					"rockchip_usb2phy", rport);
	if (ret) {
		dev_err(rphy->dev, "failed to request linestate irq handle\n");
		return ret;
	}

	rport->mode = of_usb_get_dr_mode_by_phy(child_np, -1);
	if (rport->mode == USB_DR_MODE_HOST) {
		if (rphy->edev_self) {
			extcon_set_state(rphy->edev, EXTCON_USB, false);
			extcon_set_state(rphy->edev, EXTCON_USB_HOST, true);
		}
		rockchip_usb2phy_set_mode(rport->phy, PHY_MODE_USB_HOST, 0);
		/*
		 * Here set iddig to 0 by disable idpullup, the otg_suspendm
		 * will be set to 1 to enable the disconnect detection module,
		 * and the LS_PAR_EN will be set to 1 to enable low speed device
		 * enumerate.
		 */
		property_enable(rphy->grf, &rport->port_cfg->idpullup, false);

		/* Request disconnect interrupt */
		rport->disconnect_irq = of_irq_get_byname(child_np,
							  "disconnect");
		if (rport->disconnect_irq <= 0) {
			dev_err(rphy->dev, "no disconnect irq provided\n");
			return -EINVAL;
		}

		ret = devm_request_threaded_irq(rphy->dev,
						rport->disconnect_irq, NULL,
						rockchip_usb2phy_disconnect_irq,
						IRQF_ONESHOT,
						"rockchip_usb2phy", rport);
		if (ret) {
			dev_err(rphy->dev,
				"failed to request disconnect irq handle\n");
			return ret;
		}
		goto out;
	}

	/* Request otg iddig interrupt only if there is no extcon property */
	if (rphy->edev_self) {
		rport->id_irq = of_irq_get_byname(child_np, "otg-id");
		if (rport->id_irq <= 0) {
			dev_err(rphy->dev, "no otg id irq provided\n");
			return -EINVAL;
		}

		ret = devm_request_threaded_irq(rphy->dev,
						rport->id_irq, NULL,
						rockchip_usb2phy_id_irq,
						IRQF_ONESHOT,
						"rockchip_usb2phy_id",
						rport);
		if (ret) {
			dev_err(rphy->dev,
				"failed to request otg-id irq handle\n");
			return ret;
		}

		iddig = property_enabled(rphy->grf,
					 &rport->port_cfg->utmi_iddig);
		if (!iddig) {
			extcon_set_state(rphy->edev, EXTCON_USB, false);
			extcon_set_state(rphy->edev, EXTCON_USB_HOST, true);
			extcon_set_state(rphy->edev, EXTCON_USB_VBUS_EN, true);
			/* Enable VBUS supply */
			ret = rockchip_set_vbus_power(rport, true);
			if (ret)
				return ret;
		}
	}

	if (rport->vbus_always_on)
		goto out;

	/* Request otg bvalid interrupt */
	rport->bvalid_irq = of_irq_get_byname(child_np, "otg-bvalid");
	if (rport->bvalid_irq <= 0) {
		dev_err(rphy->dev, "no vbus valid irq provided\n");
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(rphy->dev, rport->bvalid_irq,
					NULL,
					rockchip_usb2phy_bvalid_irq,
					IRQF_ONESHOT,
					"rockchip_usb2phy_bvalid",
					rport);
	if (ret) {
		dev_err(rphy->dev,
			"failed to request otg-bvalid irq handle\n");
		return ret;
	}

	INIT_DELAYED_WORK(&rport->otg_sm_work, rockchip_usb2phy_otg_sm_work);

out:
	/*
	 * Let us put phy-port into suspend mode here for saving power
	 * consumption, and usb controller will resume it during probe
	 * time if needed.
	 */
	ret = property_enable(rphy->grf, &rport->port_cfg->phy_sus, true);
	if (ret)
		return ret;

	rport->suspended = true;

	wake_lock_init(&rport->wakelock, WAKE_LOCK_SUSPEND, "rockchip_otg");

	return ret;
}

static int rockchip_usb2phy_host_port_init(struct rockchip_usb2phy *rphy,
					   struct rockchip_usb2phy_port *rport,
					   struct device_node *child_np)
{
	int ret = 0;

	mutex_init(&rport->mutex);

	rport->port_id = USB2PHY_PORT_HOST;
	rport->port_cfg = &rphy->phy_cfg->port_cfgs[USB2PHY_PORT_HOST];

	/* Request disconnect interrupt */
	rport->disconnect_irq = of_irq_get_byname(child_np, "disconnect");
	if (rport->disconnect_irq <= 0) {
		dev_err(rphy->dev, "no disconnect irq provided\n");
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(rphy->dev, rport->disconnect_irq, NULL,
					rockchip_usb2phy_disconnect_irq,
					IRQF_ONESHOT,
					"rockchip_usb2phy", rport);
	if (ret) {
		dev_err(rphy->dev, "failed to request disconnect irq handle\n");
		return ret;
	}

	/* Request linstate interrupt */
	rport->ls_irq = of_irq_get_byname(child_np, "linestate");
	if (rport->ls_irq <= 0) {
		dev_err(rphy->dev, "no linestate irq provided\n");
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(rphy->dev, rport->ls_irq, NULL,
					rockchip_usb2phy_linestate_irq,
					IRQF_ONESHOT,
					"rockchip_usb2phy", rport);
	if (ret) {
		dev_err(rphy->dev, "failed to request linestate irq handle\n");
		return ret;
	}

	/*
	 * Let us put phy-port into suspend mode here for saving power
	 * consumption, and usb controller will resume it during probe
	 * time if needed.
	 */
	ret = property_enable(rphy->grf, &rport->port_cfg->phy_sus, true);
	if (ret)
		return ret;

	rport->suspended = true;

	return ret;
}

static int rockchip_usb2phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child_np;
	struct phy_provider *provider;
	struct rockchip_usb2phy *rphy;
	const struct rockchip_usb2phy_cfg *phy_cfgs;
	const struct of_device_id *match;
	unsigned int reg;
	unsigned int index;
	struct resource *res;
	int ret = 0;

	rphy = devm_kzalloc(dev, sizeof(*rphy), GFP_KERNEL);
	if (!rphy)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "missing memory resource\n");
		return -ENODEV;
	}

	rphy->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(rphy->base)) {
		dev_err(dev, "failed to remap phy regs\n");
		return PTR_ERR(rphy->base);
	}

	rphy->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(rphy->grf))
		return PTR_ERR(rphy->grf);

	/* Get PHY power reset */
	rphy->reset = devm_reset_control_get(dev, "u2phy");
	if (IS_ERR(rphy->reset))
		return PTR_ERR(rphy->reset);

	rphy->vup_gpio = devm_gpiod_get_optional(dev, "vup", GPIOD_OUT_LOW);
	if (IS_ERR(rphy->vup_gpio)) {
		ret = PTR_ERR(rphy->vup_gpio);
		dev_err(dev, "failed to get vup gpio (%d)\n", ret);
		return ret;
	}

	reset_control_assert(rphy->reset);
	udelay(1);
	reset_control_deassert(rphy->reset);

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "phy configs are not assigned!\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "reg", &reg)) {
		dev_err(dev, "the reg property is not assigned in %s node\n",
			np->name);
		return -EINVAL;
	}

	rphy->dev = dev;
	phy_cfgs = match->data;

	/* find out a proper config which can be matched with dt. */
	index = 0;
	do {
		if (phy_cfgs[index].reg == reg) {
			rphy->phy_cfg = &phy_cfgs[index];
			break;
		}
	} while (!phy_cfgs[index++].last);

	if (!rphy->phy_cfg) {
		dev_err(dev, "no phy-config can be matched with %s node\n",
			np->name);
		return -EINVAL;
	}

	rphy->num_clks = rphy->phy_cfg->num_clks;

	rphy->clks = devm_kmemdup(dev, rphy->phy_cfg->clks,
				  rphy->num_clks * sizeof(struct clk_bulk_data),
				  GFP_KERNEL);

	if (!rphy->clks)
		return -ENOMEM;

	ret = devm_clk_bulk_get(dev, rphy->num_clks, rphy->clks);
	if (ret == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (ret)
		rphy->num_clks = 0;

	ret = clk_bulk_prepare_enable(rphy->num_clks, rphy->clks);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, rphy);

	if (rphy->phy_cfg->phy_tuning) {
		ret = rphy->phy_cfg->phy_tuning(rphy);
		if (ret)
			goto disable_clks;
	}

	index = 0;
	for_each_available_child_of_node(np, child_np) {
		struct rockchip_usb2phy_port *rport = &rphy->ports[index];
		struct phy *phy;

		/* This driver aims to support both otg-port and host-port */
		if (of_node_cmp(child_np->name, "host-port") &&
		    of_node_cmp(child_np->name, "otg-port"))
			goto next_child;

		if (rphy->vup_gpio &&
		    of_device_is_compatible(np, "rockchip,rv1126-usb2phy")) {
			rockchip_usb2phy_ops.calibrate =
						rv1126_usb2phy_calibrate;
			hrtimer_init(&rphy->wait_timer, CLOCK_MONOTONIC,
				     HRTIMER_MODE_REL);
			rphy->wait_timer.function = &rv1126_wait_timer_fn;
		}

		phy = devm_phy_create(dev, child_np, &rockchip_usb2phy_ops);
		if (IS_ERR(phy)) {
			dev_err(dev, "failed to create phy\n");
			ret = PTR_ERR(phy);
			goto put_child;
		}

		rport->phy = phy;
		phy_set_drvdata(rport->phy, rport);

		/* initialize otg/host port separately */
		if (!of_node_cmp(child_np->name, "host-port")) {
			ret = rockchip_usb2phy_host_port_init(rphy, rport,
							      child_np);
			if (ret)
				goto put_child;
		} else {
			ret = rockchip_usb2phy_otg_port_init(rphy, rport,
							     child_np);
			if (ret)
				goto put_child;
		}

next_child:
		/* to prevent out of boundary */
		if (++index >= rphy->phy_cfg->num_ports)
			break;
	}

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR_OR_NULL(provider))
		goto put_child;

	/* Attributes */
	ret = sysfs_create_group(&dev->kobj, &usb2_phy_attr_group);
	if (ret) {
		dev_err(dev, "Cannot create sysfs group: %d\n", ret);
		goto put_child;
	}

	ret = rockchip_usb2phy_clk480m_register(rphy);
	if (ret) {
		dev_err(dev, "failed to register 480m output clock\n");
		goto put_child;
	}

	if (of_property_read_bool(np, "wakeup-source"))
		device_init_wakeup(rphy->dev, true);
	else
		device_init_wakeup(rphy->dev, false);

	return 0;

put_child:
	of_node_put(child_np);
disable_clks:
	clk_bulk_disable_unprepare(rphy->num_clks, rphy->clks);

	return ret;
}

static int rockchip_usb2phy_remove(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rockchip_usb2phy *rphy = platform_get_drvdata(pdev);

	if (rphy->vup_gpio &&
	    of_device_is_compatible(np, "rockchip,rv1126-usb2phy"))
		hrtimer_cancel(&rphy->wait_timer);

	return 0;
}

static int rv1126_usb2phy_tuning(struct rockchip_usb2phy *rphy)
{
	int ret = 0;
	u32 rcal, reg;

	if (rphy->phy_cfg->reg == 0xff4c0000) {
		/* set iddig interrupt filter time to 10ms */
		ret = regmap_write(rphy->grf, 0x1031c, 0x000f4240);
		if (ret)
			goto out;

		/* set pready_cnt to 1 and rden_cnt to 0 */
		ret = regmap_write(rphy->grf, 0x1027c, 0x0f0f0100);
		if (ret)
			goto out;

		reg = readl(rphy->base + 0x10);
		/* Enable Rterm self calibration and wait for rcal trim done */
		writel(reg & ~BIT(2), rphy->base + 0x10);
		/*
		 * If Rterm is disconnected, self calibration will fail and
		 * rcal trim done will be set in about 3.5 us
		 */
		udelay(10);
		if (readl(rphy->base + 0x34) & BIT(4)) {
			dev_dbg(rphy->dev, "Rterm disconnected");
		} else {
			ret = readl_poll_timeout(rphy->base + 0x34, rcal,
						 rcal & BIT(4),
						 100, 600);
			if (ret == -ETIMEDOUT)
				dev_err(rphy->dev, "Rterm calibration timeout");
			else
				/* Use rcal out calibration code */
				reg = (reg & ~(0x0f << 3)) |
				      ((rcal & 0x0f) << 3);
		}
		/* Disable Rterm self calibration */
		writel(reg | BIT(2), rphy->base + 0x10);
	}

	if (rphy->phy_cfg->reg == 0xff4c8000) {
		/* set pready_cnt to 1 and rden_cnt to 0 */
		ret = regmap_write(rphy->grf, 0x1028c, 0x0f0f0100);
		if (ret)
			goto out;
	}

out:
	return ret;
}

static int rv1126_usb2phy_low_power(struct rockchip_usb2phy *rphy, bool en)
{
	unsigned int reg;

	reg = readl(rphy->base + 0x20);
	/* bypass or enable bc detect */
	reg = en ? reg | BIT(5) : reg & ~BIT(5);
	writel(reg, rphy->base + 0x20);

	return 0;
}

static const struct clk_bulk_data rv1126_clks[] = {
	{ .id = "phyclk" },
	{ .id = "pclk" },
};

#ifdef CONFIG_PM_SLEEP
static int rockchip_usb2phy_pm_suspend(struct device *dev)
{
	int ret = 0;
	struct rockchip_usb2phy *rphy = dev_get_drvdata(dev);
	struct rockchip_usb2phy_port *rport;
	unsigned int index;
	bool wakeup_enable = false;

	if (device_may_wakeup(rphy->dev))
		wakeup_enable = true;

	for (index = 0; index < rphy->phy_cfg->num_ports; index++) {
		rport = &rphy->ports[index];
		if (!rport->phy)
			continue;

		if (rport->port_id == USB2PHY_PORT_OTG &&
		    rport->id_irq > 0) {
			mutex_lock(&rport->mutex);
			rport->prev_iddig =
				property_enabled(rphy->grf,
						 &rport->port_cfg->utmi_iddig);
			ret = rockchip_usb2phy_enable_id_irq(rphy, rport,
							     false);
			mutex_unlock(&rport->mutex);
			if (ret) {
				dev_err(rphy->dev,
					"failed to disable id irq\n");
				return ret;
			}
		}

		if (rport->port_id == USB2PHY_PORT_OTG && wakeup_enable &&
		    rport->bvalid_irq > 0)
			enable_irq_wake(rport->bvalid_irq);

		/* activate the linestate to detect the remove wakeup. */
		mutex_lock(&rport->mutex);
		ret = rockchip_usb2phy_enable_line_irq(rphy, rport, true);
		mutex_unlock(&rport->mutex);
		if (ret) {
			dev_err(rphy->dev, "failed to enable linestate irq\n");
			return ret;
		}

		if (wakeup_enable && rport->ls_irq > 0)
			enable_irq_wake(rport->ls_irq);
	}

	/* enter low power state */
	if (rphy->phy_cfg->phy_lowpower)
		ret = rphy->phy_cfg->phy_lowpower(rphy, true);

	return ret;
}

static int rockchip_usb2phy_pm_resume(struct device *dev)
{
	int ret = 0;
	struct rockchip_usb2phy *rphy = dev_get_drvdata(dev);
	struct rockchip_usb2phy_port *rport;
	unsigned int index;
	bool iddig;
	bool wakeup_enable = false;

	if (device_may_wakeup(rphy->dev))
		wakeup_enable = true;

	/* exit low power state */
	if (rphy->phy_cfg->phy_lowpower)
		ret = rphy->phy_cfg->phy_lowpower(rphy, false);

	for (index = 0; index < rphy->phy_cfg->num_ports; index++) {
		rport = &rphy->ports[index];
		if (!rport->phy)
			continue;

		if (rport->port_id == USB2PHY_PORT_OTG &&
		    rport->id_irq > 0) {
			mutex_lock(&rport->mutex);
			iddig = property_enabled(rphy->grf,
						 &rport->port_cfg->utmi_iddig);
			ret = rockchip_usb2phy_enable_id_irq(rphy, rport,
							     true);
			mutex_unlock(&rport->mutex);
			if (ret) {
				dev_err(rphy->dev,
					"failed to enable id irq\n");
				return ret;
			}

			if (iddig != rport->prev_iddig) {
				dev_dbg(&rport->phy->dev,
					"iddig changed during resume\n");
				rport->prev_iddig = iddig;
				extcon_set_state_sync(rphy->edev,
						      EXTCON_USB_HOST,
						      !iddig);
				extcon_set_state_sync(rphy->edev,
						      EXTCON_USB_VBUS_EN,
						      !iddig);
				ret = rockchip_set_vbus_power(rport, !iddig);
				if (ret)
					return ret;
			}
		}

		if (rport->port_id == USB2PHY_PORT_OTG && wakeup_enable &&
		    rport->bvalid_irq > 0)
			disable_irq_wake(rport->bvalid_irq);

		if (wakeup_enable && rport->ls_irq > 0)
			disable_irq_wake(rport->ls_irq);
	}

	return ret;
}

static const struct dev_pm_ops rockchip_usb2phy_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_usb2phy_pm_suspend,
				rockchip_usb2phy_pm_resume)
};

#define ROCKCHIP_USB2PHY_DEV_PM	(&rockchip_usb2phy_dev_pm_ops)
#else
#define ROCKCHIP_USB2PHY_DEV_PM	NULL
#endif

static const struct rockchip_usb2phy_cfg rv1126_phy_cfgs[] = {
	{
		.reg		= 0xff4c0000,
		.num_ports	= 1,
		.phy_tuning	= rv1126_usb2phy_tuning,
		.phy_lowpower	= rv1126_usb2phy_low_power,
		.num_clks	= 2,
		.clks		= rv1126_clks,
		.clkout_ctl	= { 0x10230, 14, 14, 0, 1 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.bypass_otgsuspendm = { 0x10234, 12, 12, 0, 1 },
				.bvalidfall_det_en = { 0x10300, 3, 3, 0, 1 },
				.bvalidfall_det_st = { 0x10304, 3, 3, 0, 1 },
				.bvalidfall_det_clr = { 0x10308, 3, 3, 0, 1 },
				.bvalidrise_det_en = { 0x10300, 2, 2, 0, 1 },
				.bvalidrise_det_st = { 0x10304, 2, 2, 0, 1 },
				.bvalidrise_det_clr = { 0x10308, 2, 2, 0, 1 },
				.disconfall_det_en = { 0x10300, 7, 7, 0, 1 },
				.disconfall_det_st = { 0x10304, 7, 7, 0, 1 },
				.disconfall_det_clr = { 0x10308, 7, 7, 0, 1 },
				.disconrise_det_en = { 0x10300, 6, 6, 0, 1 },
				.disconrise_det_st = { 0x10304, 6, 6, 0, 1 },
				.disconrise_det_clr = { 0x10308, 6, 6, 0, 1 },
				.idfall_det_en = { 0x10300, 5, 5, 0, 1 },
				.idfall_det_st = { 0x10304, 5, 5, 0, 1 },
				.idfall_det_clr = { 0x10308, 5, 5, 0, 1 },
				.idpullup = { 0x10230, 11, 11, 0, 1 },
				.iddig_output = { 0x10230, 10, 10, 0, 1 },
				.iddig_en = { 0x10230, 9, 9, 0, 1 },
				.idrise_det_en = { 0x10300, 4, 4, 0, 1 },
				.idrise_det_st = { 0x10304, 4, 4, 0, 1 },
				.idrise_det_clr = { 0x10308, 4, 4, 0, 1 },
				.ls_det_en = { 0x10300, 0, 0, 0, 1 },
				.ls_det_st = { 0x10304, 0, 0, 0, 1 },
				.ls_det_clr = { 0x10308, 0, 0, 0, 1 },
				.phy_sus = { 0x10230, 8, 0, 0x052, 0x1d5 },
				.utmi_bvalid = { 0x10248, 9, 9, 0, 1 },
				.utmi_iddig = { 0x10248, 6, 6, 0, 1 },
				.utmi_hostdet = { 0x10248, 7, 7, 0, 1 },
			}
		},
		.chg_det = {
			.chg_en		= { 0x10234, 14, 14, 0, 1 },
			.chg_rst	= { 0x10234, 15, 15, 0, 1 },
			.chg_valid	= { 0x10248, 12, 12, 0, 1 },
			.phy_connect	= { 0x10248, 13, 13, 0, 1 },
		},
	},
	{
		.reg		= 0xff4c8000,
		.num_ports	= 1,
		.phy_tuning	= rv1126_usb2phy_tuning,
		.phy_lowpower	= rv1126_usb2phy_low_power,
		.num_clks	= 2,
		.clks		= rv1126_clks,
		.clkout_ctl	= { 0x10238, 9, 9, 0, 1 },
		.port_cfgs	= {
			[USB2PHY_PORT_HOST] = {
				.disconfall_det_en = { 0x10300, 9, 9, 0, 1 },
				.disconfall_det_st = { 0x10304, 9, 9, 0, 1 },
				.disconfall_det_clr = { 0x10308, 9, 9, 0, 1 },
				.disconrise_det_en = { 0x10300, 8, 8, 0, 1 },
				.disconrise_det_st = { 0x10304, 8, 8, 0, 1 },
				.disconrise_det_clr = { 0x10308, 8, 8, 0, 1 },
				.ls_det_en = { 0x10300, 1, 1, 0, 1 },
				.ls_det_st = { 0x10304, 1, 1, 0, 1 },
				.ls_det_clr = { 0x10308, 1, 1, 0, 1 },
				.phy_sus = { 0x10238, 3, 0, 0x2, 0x5 },
				.utmi_hostdet = { 0x10248, 23, 23, 0, 1 },
			}
		},
		.chg_det = {
			.chg_en		= { 0x10238, 7, 7, 0, 1 },
			.chg_rst	= { 0x10238, 8, 8, 0, 1 },
			.chg_valid	= { 0x10248, 28, 28, 0, 1 },
			.phy_connect	= { 0x10248, 29, 29, 0, 1 },
		},
		.last		= true,
	},
};

static const struct of_device_id rockchip_usb2phy_dt_match[] = {
	{ .compatible = "rockchip,rv1126-usb2phy", .data = &rv1126_phy_cfgs },
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_usb2phy_dt_match);

static struct platform_driver rockchip_usb2phy_driver = {
	.probe		= rockchip_usb2phy_probe,
	.remove		= rockchip_usb2phy_remove,
	.driver		= {
		.name	= "rockchip-usb2phy-naneng",
		.pm	= ROCKCHIP_USB2PHY_DEV_PM,
		.of_match_table = rockchip_usb2phy_dt_match,
	},
};
module_platform_driver(rockchip_usb2phy_driver);

MODULE_AUTHOR("Jianing Ren <jianing.ren@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USB2.0 Naneng PHY driver");
MODULE_LICENSE("GPL v2");
