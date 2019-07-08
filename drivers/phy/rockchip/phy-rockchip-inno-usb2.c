// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rockchip USB2.0 PHY with Innosilicon IP block driver
 *
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/extcon-provider.h>
#include <linux/interrupt.h>
#include <linux/io.h>
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
#include <linux/mfd/syscon.h>
#include <linux/usb/of.h>
#include <linux/usb/otg.h>

#define BIT_WRITEABLE_SHIFT	16
#define SCHEDULE_DELAY		(60 * HZ)
#define OTG_SCHEDULE_DELAY	(2 * HZ)

enum rockchip_usb2phy_port_id {
	USB2PHY_PORT_OTG,
	USB2PHY_PORT_HOST,
	USB2PHY_NUM_PORTS,
};

enum rockchip_usb2phy_host_state {
	PHY_STATE_HS_ONLINE	= 0,
	PHY_STATE_DISCONNECT	= 1,
	PHY_STATE_CONNECT	= 2,
	PHY_STATE_FS_LS_ONLINE	= 4,
};

/**
 * enum usb_chg_state - Different states involved in USB charger detection.
 * @USB_CHG_STATE_UNDEFINED:	USB charger is not connected or detection
 *				process is not yet started.
 * @USB_CHG_STATE_WAIT_FOR_DCD:	Waiting for Data pins contact.
 * @USB_CHG_STATE_DCD_DONE:	Data pin contact is detected.
 * @USB_CHG_STATE_PRIMARY_DONE:	Primary detection is completed (Detects
 *				between SDP and DCP/CDP).
 * @USB_CHG_STATE_SECONDARY_DONE: Secondary detection is completed (Detects
 *				  between DCP and CDP).
 * @USB_CHG_STATE_DETECTED:	USB charger type is determined.
 */
enum usb_chg_state {
	USB_CHG_STATE_UNDEFINED = 0,
	USB_CHG_STATE_WAIT_FOR_DCD,
	USB_CHG_STATE_DCD_DONE,
	USB_CHG_STATE_PRIMARY_DONE,
	USB_CHG_STATE_SECONDARY_DONE,
	USB_CHG_STATE_DETECTED,
};

static const unsigned int rockchip_usb2phy_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_CDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_SLOW,
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
 * struct rockchip_chg_det_reg - usb charger detect registers
 * @cp_det: charging port detected successfully.
 * @dcp_det: dedicated charging port detected successfully.
 * @dp_det: assert data pin connect successfully.
 * @idm_sink_en: open dm sink curren.
 * @idp_sink_en: open dp sink current.
 * @idp_src_en: open dm source current.
 * @rdm_pdwn_en: open dm pull down resistor.
 * @vdm_src_en: open dm voltage source.
 * @vdp_src_en: open dp voltage source.
 * @opmode: utmi operational mode.
 */
struct rockchip_chg_det_reg {
	struct usb2phy_reg	cp_det;
	struct usb2phy_reg	dcp_det;
	struct usb2phy_reg	dp_det;
	struct usb2phy_reg	idm_sink_en;
	struct usb2phy_reg	idp_sink_en;
	struct usb2phy_reg	idp_src_en;
	struct usb2phy_reg	rdm_pdwn_en;
	struct usb2phy_reg	vdm_src_en;
	struct usb2phy_reg	vdp_src_en;
	struct usb2phy_reg	opmode;
};

/**
 * struct rockchip_usb2phy_port_cfg - usb-phy port configuration.
 * @phy_sus: phy suspend register.
 * @bvalid_det_en: vbus valid rise detection enable register.
 * @bvalid_det_st: vbus valid rise detection status register.
 * @bvalid_det_clr: vbus valid rise detection clear register.
 * @ls_det_en: linestate detection enable register.
 * @ls_det_st: linestate detection state register.
 * @ls_det_clr: linestate detection clear register.
 * @utmi_avalid: utmi vbus avalid status register.
 * @utmi_bvalid: utmi vbus bvalid status register.
 * @utmi_ls: utmi linestate state register.
 * @utmi_hstdet: utmi host disconnect register.
 */
struct rockchip_usb2phy_port_cfg {
	struct usb2phy_reg	phy_sus;
	struct usb2phy_reg	bvalid_det_en;
	struct usb2phy_reg	bvalid_det_st;
	struct usb2phy_reg	bvalid_det_clr;
	struct usb2phy_reg	ls_det_en;
	struct usb2phy_reg	ls_det_st;
	struct usb2phy_reg	ls_det_clr;
	struct usb2phy_reg	utmi_avalid;
	struct usb2phy_reg	utmi_bvalid;
	struct usb2phy_reg	utmi_ls;
	struct usb2phy_reg	utmi_hstdet;
};

/**
 * struct rockchip_usb2phy_cfg - usb-phy configuration.
 * @reg: the address offset of grf for usb-phy config.
 * @num_ports: specify how many ports that the phy has.
 * @clkout_ctl: keep on/turn off output clk of phy.
 * @port_cfgs: usb-phy port configurations.
 * @chg_det: charger detection registers.
 */
struct rockchip_usb2phy_cfg {
	unsigned int	reg;
	unsigned int	num_ports;
	struct usb2phy_reg	clkout_ctl;
	const struct rockchip_usb2phy_port_cfg	port_cfgs[USB2PHY_NUM_PORTS];
	const struct rockchip_chg_det_reg	chg_det;
};

/**
 * struct rockchip_usb2phy_port - usb-phy port data.
 * @phy: generic phy.
 * @port_id: flag for otg port or host port.
 * @suspended: phy suspended flag.
 * @vbus_attached: otg device vbus status.
 * @bvalid_irq: IRQ number assigned for vbus valid rise detection.
 * @ls_irq: IRQ number assigned for linestate detection.
 * @otg_mux_irq: IRQ number which multiplex otg-id/otg-bvalid/linestate
 *		 irqs to one irq in otg-port.
 * @mutex: for register updating in sm_work.
 * @chg_work: charge detect work.
 * @otg_sm_work: OTG state machine work.
 * @sm_work: HOST state machine work.
 * @port_cfg: port register configuration, assigned by driver data.
 * @event_nb: hold event notification callback.
 * @state: define OTG enumeration states before device reset.
 * @mode: the dr_mode of the controller.
 */
struct rockchip_usb2phy_port {
	struct phy	*phy;
	unsigned int	port_id;
	bool		suspended;
	bool		vbus_attached;
	int		bvalid_irq;
	int		ls_irq;
	int		otg_mux_irq;
	struct mutex	mutex;
	struct		delayed_work chg_work;
	struct		delayed_work otg_sm_work;
	struct		delayed_work sm_work;
	const struct	rockchip_usb2phy_port_cfg *port_cfg;
	struct notifier_block	event_nb;
	enum usb_otg_state	state;
	enum usb_dr_mode	mode;
};

/**
 * struct rockchip_usb2phy - usb2.0 phy driver data.
 * @dev: pointer to device.
 * @grf: General Register Files regmap.
 * @usbgrf: USB General Register Files regmap.
 * @clk: clock struct of phy input clk.
 * @clk480m: clock struct of phy output clk.
 * @clk480m_hw: clock struct of phy output clk management.
 * @chg_state: states involved in USB charger detection.
 * @chg_type: USB charger types.
 * @dcd_retries: The retry count used to track Data contact
 *		 detection process.
 * @edev: extcon device for notification registration
 * @phy_cfg: phy register configuration, assigned by driver data.
 * @ports: phy port instance.
 */
struct rockchip_usb2phy {
	struct device	*dev;
	struct regmap	*grf;
	struct regmap	*usbgrf;
	struct clk	*clk;
	struct clk	*clk480m;
	struct clk_hw	clk480m_hw;
	enum usb_chg_state	chg_state;
	enum power_supply_type	chg_type;
	u8			dcd_retries;
	struct extcon_dev	*edev;
	const struct rockchip_usb2phy_cfg	*phy_cfg;
	struct rockchip_usb2phy_port	ports[USB2PHY_NUM_PORTS];
};

static inline struct regmap *get_reg_base(struct rockchip_usb2phy *rphy)
{
	return rphy->usbgrf == NULL ? rphy->grf : rphy->usbgrf;
}

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
	struct regmap *base = get_reg_base(rphy);
	int ret;

	/* turn on 480m clk output if it is off */
	if (!property_enabled(base, &rphy->phy_cfg->clkout_ctl)) {
		ret = property_enable(base, &rphy->phy_cfg->clkout_ctl, true);
		if (ret)
			return ret;

		/* waiting for the clk become stable */
		usleep_range(1200, 1300);
	}

	return 0;
}

static void rockchip_usb2phy_clk480m_unprepare(struct clk_hw *hw)
{
	struct rockchip_usb2phy *rphy =
		container_of(hw, struct rockchip_usb2phy, clk480m_hw);
	struct regmap *base = get_reg_base(rphy);

	/* turn off 480m clk output */
	property_enable(base, &rphy->phy_cfg->clkout_ctl, false);
}

static int rockchip_usb2phy_clk480m_prepared(struct clk_hw *hw)
{
	struct rockchip_usb2phy *rphy =
		container_of(hw, struct rockchip_usb2phy, clk480m_hw);
	struct regmap *base = get_reg_base(rphy);

	return property_enabled(base, &rphy->phy_cfg->clkout_ctl);
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
	struct clk_init_data init;
	const char *clk_name;
	int ret;

	init.flags = 0;
	init.name = "clk_usbphy_480m";
	init.ops = &rockchip_usb2phy_clkout_ops;

	/* optional override of the clockname */
	of_property_read_string(node, "clock-output-names", &init.name);

	if (rphy->clk) {
		clk_name = __clk_get_name(rphy->clk);
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

static int rockchip_usb2phy_extcon_register(struct rockchip_usb2phy *rphy)
{
	int ret;
	struct device_node *node = rphy->dev->of_node;
	struct extcon_dev *edev;

	if (of_property_read_bool(node, "extcon")) {
		edev = extcon_get_edev_by_phandle(rphy->dev, 0);
		if (IS_ERR(edev)) {
			if (PTR_ERR(edev) != -EPROBE_DEFER)
				dev_err(rphy->dev, "Invalid or missing extcon\n");
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
			dev_err(rphy->dev, "failed to register extcon device\n");
			return ret;
		}
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

	if (rport->port_id == USB2PHY_PORT_OTG) {
		if (rport->mode != USB_DR_MODE_HOST &&
		    rport->mode != USB_DR_MODE_UNKNOWN) {
			/* clear bvalid status and enable bvalid detect irq */
			ret = property_enable(rphy->grf,
					      &rport->port_cfg->bvalid_det_clr,
					      true);
			if (ret)
				goto out;

			ret = property_enable(rphy->grf,
					      &rport->port_cfg->bvalid_det_en,
					      true);
			if (ret)
				goto out;

			schedule_delayed_work(&rport->otg_sm_work,
					      OTG_SCHEDULE_DELAY * 3);
		} else {
			/* If OTG works in host only mode, do nothing. */
			dev_dbg(&rport->phy->dev, "mode %d\n", rport->mode);
		}
	} else if (rport->port_id == USB2PHY_PORT_HOST) {
		/* clear linestate and enable linestate detect irq */
		ret = property_enable(rphy->grf,
				      &rport->port_cfg->ls_det_clr, true);
		if (ret)
			goto out;

		ret = property_enable(rphy->grf,
				      &rport->port_cfg->ls_det_en, true);
		if (ret)
			goto out;

		schedule_delayed_work(&rport->sm_work, SCHEDULE_DELAY);
	}

out:
	mutex_unlock(&rport->mutex);
	return ret;
}

static int rockchip_usb2phy_power_on(struct phy *phy)
{
	struct rockchip_usb2phy_port *rport = phy_get_drvdata(phy);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(phy->dev.parent);
	struct regmap *base = get_reg_base(rphy);
	int ret;

	dev_dbg(&rport->phy->dev, "port power on\n");

	if (!rport->suspended)
		return 0;

	ret = clk_prepare_enable(rphy->clk480m);
	if (ret)
		return ret;

	ret = property_enable(base, &rport->port_cfg->phy_sus, false);
	if (ret)
		return ret;

	/* waiting for the utmi_clk to become stable */
	usleep_range(1500, 2000);

	rport->suspended = false;
	return 0;
}

static int rockchip_usb2phy_power_off(struct phy *phy)
{
	struct rockchip_usb2phy_port *rport = phy_get_drvdata(phy);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(phy->dev.parent);
	struct regmap *base = get_reg_base(rphy);
	int ret;

	dev_dbg(&rport->phy->dev, "port power off\n");

	if (rport->suspended)
		return 0;

	ret = property_enable(base, &rport->port_cfg->phy_sus, true);
	if (ret)
		return ret;

	rport->suspended = true;
	clk_disable_unprepare(rphy->clk480m);

	return 0;
}

static int rockchip_usb2phy_exit(struct phy *phy)
{
	struct rockchip_usb2phy_port *rport = phy_get_drvdata(phy);

	if (rport->port_id == USB2PHY_PORT_OTG &&
	    rport->mode != USB_DR_MODE_HOST &&
	    rport->mode != USB_DR_MODE_UNKNOWN) {
		cancel_delayed_work_sync(&rport->otg_sm_work);
		cancel_delayed_work_sync(&rport->chg_work);
	} else if (rport->port_id == USB2PHY_PORT_HOST)
		cancel_delayed_work_sync(&rport->sm_work);

	return 0;
}

static const struct phy_ops rockchip_usb2phy_ops = {
	.init		= rockchip_usb2phy_init,
	.exit		= rockchip_usb2phy_exit,
	.power_on	= rockchip_usb2phy_power_on,
	.power_off	= rockchip_usb2phy_power_off,
	.owner		= THIS_MODULE,
};

static void rockchip_usb2phy_otg_sm_work(struct work_struct *work)
{
	struct rockchip_usb2phy_port *rport =
		container_of(work, struct rockchip_usb2phy_port,
			     otg_sm_work.work);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);
	static unsigned int cable;
	unsigned long delay;
	bool vbus_attach, sch_work, notify_charger;

	vbus_attach = property_enabled(rphy->grf,
				       &rport->port_cfg->utmi_bvalid);

	sch_work = false;
	notify_charger = false;
	delay = OTG_SCHEDULE_DELAY;
	dev_dbg(&rport->phy->dev, "%s otg sm work\n",
		usb_otg_state_string(rport->state));

	switch (rport->state) {
	case OTG_STATE_UNDEFINED:
		rport->state = OTG_STATE_B_IDLE;
		if (!vbus_attach)
			rockchip_usb2phy_power_off(rport->phy);
		/* fall through */
	case OTG_STATE_B_IDLE:
		if (extcon_get_state(rphy->edev, EXTCON_USB_HOST) > 0) {
			dev_dbg(&rport->phy->dev, "usb otg host connect\n");
			rport->state = OTG_STATE_A_HOST;
			rockchip_usb2phy_power_on(rport->phy);
			return;
		} else if (vbus_attach) {
			dev_dbg(&rport->phy->dev, "vbus_attach\n");
			switch (rphy->chg_state) {
			case USB_CHG_STATE_UNDEFINED:
				schedule_delayed_work(&rport->chg_work, 0);
				return;
			case USB_CHG_STATE_DETECTED:
				switch (rphy->chg_type) {
				case POWER_SUPPLY_TYPE_USB:
					dev_dbg(&rport->phy->dev, "sdp cable is connected\n");
					rockchip_usb2phy_power_on(rport->phy);
					rport->state = OTG_STATE_B_PERIPHERAL;
					notify_charger = true;
					sch_work = true;
					cable = EXTCON_CHG_USB_SDP;
					break;
				case POWER_SUPPLY_TYPE_USB_DCP:
					dev_dbg(&rport->phy->dev, "dcp cable is connected\n");
					rockchip_usb2phy_power_off(rport->phy);
					notify_charger = true;
					sch_work = true;
					cable = EXTCON_CHG_USB_DCP;
					break;
				case POWER_SUPPLY_TYPE_USB_CDP:
					dev_dbg(&rport->phy->dev, "cdp cable is connected\n");
					rockchip_usb2phy_power_on(rport->phy);
					rport->state = OTG_STATE_B_PERIPHERAL;
					notify_charger = true;
					sch_work = true;
					cable = EXTCON_CHG_USB_CDP;
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
		} else {
			notify_charger = true;
			rphy->chg_state = USB_CHG_STATE_UNDEFINED;
			rphy->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		}

		if (rport->vbus_attached != vbus_attach) {
			rport->vbus_attached = vbus_attach;

			if (notify_charger && rphy->edev) {
				extcon_set_state_sync(rphy->edev,
							cable, vbus_attach);
				if (cable == EXTCON_CHG_USB_SDP)
					extcon_set_state_sync(rphy->edev,
							      EXTCON_USB,
							      vbus_attach);
			}
		}
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (!vbus_attach) {
			dev_dbg(&rport->phy->dev, "usb disconnect\n");
			rphy->chg_state = USB_CHG_STATE_UNDEFINED;
			rphy->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
			rport->state = OTG_STATE_B_IDLE;
			delay = 0;
			rockchip_usb2phy_power_off(rport->phy);
		}
		sch_work = true;
		break;
	case OTG_STATE_A_HOST:
		if (extcon_get_state(rphy->edev, EXTCON_USB_HOST) == 0) {
			dev_dbg(&rport->phy->dev, "usb otg host disconnect\n");
			rport->state = OTG_STATE_B_IDLE;
			rockchip_usb2phy_power_off(rport->phy);
		}
		break;
	default:
		break;
	}

	if (sch_work)
		schedule_delayed_work(&rport->otg_sm_work, delay);
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

static void rockchip_chg_enable_dcd(struct rockchip_usb2phy *rphy,
				    bool en)
{
	struct regmap *base = get_reg_base(rphy);

	property_enable(base, &rphy->phy_cfg->chg_det.rdm_pdwn_en, en);
	property_enable(base, &rphy->phy_cfg->chg_det.idp_src_en, en);
}

static void rockchip_chg_enable_primary_det(struct rockchip_usb2phy *rphy,
					    bool en)
{
	struct regmap *base = get_reg_base(rphy);

	property_enable(base, &rphy->phy_cfg->chg_det.vdp_src_en, en);
	property_enable(base, &rphy->phy_cfg->chg_det.idm_sink_en, en);
}

static void rockchip_chg_enable_secondary_det(struct rockchip_usb2phy *rphy,
					      bool en)
{
	struct regmap *base = get_reg_base(rphy);

	property_enable(base, &rphy->phy_cfg->chg_det.vdm_src_en, en);
	property_enable(base, &rphy->phy_cfg->chg_det.idp_sink_en, en);
}

#define CHG_DCD_POLL_TIME	(100 * HZ / 1000)
#define CHG_DCD_MAX_RETRIES	6
#define CHG_PRIMARY_DET_TIME	(40 * HZ / 1000)
#define CHG_SECONDARY_DET_TIME	(40 * HZ / 1000)
static void rockchip_chg_detect_work(struct work_struct *work)
{
	struct rockchip_usb2phy_port *rport =
		container_of(work, struct rockchip_usb2phy_port, chg_work.work);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);
	struct regmap *base = get_reg_base(rphy);
	bool is_dcd, tmout, vout;
	unsigned long delay;

	dev_dbg(&rport->phy->dev, "chg detection work state = %d\n",
		rphy->chg_state);
	switch (rphy->chg_state) {
	case USB_CHG_STATE_UNDEFINED:
		if (!rport->suspended)
			rockchip_usb2phy_power_off(rport->phy);
		/* put the controller in non-driving mode */
		property_enable(base, &rphy->phy_cfg->chg_det.opmode, false);
		/* Start DCD processing stage 1 */
		rockchip_chg_enable_dcd(rphy, true);
		rphy->chg_state = USB_CHG_STATE_WAIT_FOR_DCD;
		rphy->dcd_retries = 0;
		delay = CHG_DCD_POLL_TIME;
		break;
	case USB_CHG_STATE_WAIT_FOR_DCD:
		/* get data contact detection status */
		is_dcd = property_enabled(rphy->grf,
					  &rphy->phy_cfg->chg_det.dp_det);
		tmout = ++rphy->dcd_retries == CHG_DCD_MAX_RETRIES;
		/* stage 2 */
		if (is_dcd || tmout) {
			/* stage 4 */
			/* Turn off DCD circuitry */
			rockchip_chg_enable_dcd(rphy, false);
			/* Voltage Source on DP, Probe on DM */
			rockchip_chg_enable_primary_det(rphy, true);
			delay = CHG_PRIMARY_DET_TIME;
			rphy->chg_state = USB_CHG_STATE_DCD_DONE;
		} else {
			/* stage 3 */
			delay = CHG_DCD_POLL_TIME;
		}
		break;
	case USB_CHG_STATE_DCD_DONE:
		vout = property_enabled(rphy->grf,
					&rphy->phy_cfg->chg_det.cp_det);
		rockchip_chg_enable_primary_det(rphy, false);
		if (vout) {
			/* Voltage Source on DM, Probe on DP  */
			rockchip_chg_enable_secondary_det(rphy, true);
			delay = CHG_SECONDARY_DET_TIME;
			rphy->chg_state = USB_CHG_STATE_PRIMARY_DONE;
		} else {
			if (rphy->dcd_retries == CHG_DCD_MAX_RETRIES) {
				/* floating charger found */
				rphy->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
				rphy->chg_state = USB_CHG_STATE_DETECTED;
				delay = 0;
			} else {
				rphy->chg_type = POWER_SUPPLY_TYPE_USB;
				rphy->chg_state = USB_CHG_STATE_DETECTED;
				delay = 0;
			}
		}
		break;
	case USB_CHG_STATE_PRIMARY_DONE:
		vout = property_enabled(rphy->grf,
					&rphy->phy_cfg->chg_det.dcp_det);
		/* Turn off voltage source */
		rockchip_chg_enable_secondary_det(rphy, false);
		if (vout)
			rphy->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		else
			rphy->chg_type = POWER_SUPPLY_TYPE_USB_CDP;
		/* fall through */
	case USB_CHG_STATE_SECONDARY_DONE:
		rphy->chg_state = USB_CHG_STATE_DETECTED;
		delay = 0;
		/* fall through */
	case USB_CHG_STATE_DETECTED:
		/* put the controller in normal mode */
		property_enable(base, &rphy->phy_cfg->chg_det.opmode, true);
		rockchip_usb2phy_otg_sm_work(&rport->otg_sm_work.work);
		dev_info(&rport->phy->dev, "charger = %s\n",
			 chg_to_string(rphy->chg_type));
		return;
	default:
		return;
	}

	schedule_delayed_work(&rport->chg_work, delay);
}

/*
 * The function manage host-phy port state and suspend/resume phy port
 * to save power.
 *
 * we rely on utmi_linestate and utmi_hostdisconnect to identify whether
 * devices is disconnect or not. Besides, we do not need care it is FS/LS
 * disconnected or HS disconnected, actually, we just only need get the
 * device is disconnected at last through rearm the delayed work,
 * to suspend the phy port in _PHY_STATE_DISCONNECT_ case.
 *
 * NOTE: It may invoke *phy_powr_off or *phy_power_on which will invoke
 * some clk related APIs, so do not invoke it from interrupt context directly.
 */
static void rockchip_usb2phy_sm_work(struct work_struct *work)
{
	struct rockchip_usb2phy_port *rport =
		container_of(work, struct rockchip_usb2phy_port, sm_work.work);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);
	unsigned int sh = rport->port_cfg->utmi_hstdet.bitend -
			  rport->port_cfg->utmi_hstdet.bitstart + 1;
	unsigned int ul, uhd, state;
	unsigned int ul_mask, uhd_mask;
	int ret;

	mutex_lock(&rport->mutex);

	ret = regmap_read(rphy->grf, rport->port_cfg->utmi_ls.offset, &ul);
	if (ret < 0)
		goto next_schedule;

	ret = regmap_read(rphy->grf, rport->port_cfg->utmi_hstdet.offset, &uhd);
	if (ret < 0)
		goto next_schedule;

	uhd_mask = GENMASK(rport->port_cfg->utmi_hstdet.bitend,
			   rport->port_cfg->utmi_hstdet.bitstart);
	ul_mask = GENMASK(rport->port_cfg->utmi_ls.bitend,
			  rport->port_cfg->utmi_ls.bitstart);

	/* stitch on utmi_ls and utmi_hstdet as phy state */
	state = ((uhd & uhd_mask) >> rport->port_cfg->utmi_hstdet.bitstart) |
		(((ul & ul_mask) >> rport->port_cfg->utmi_ls.bitstart) << sh);

	switch (state) {
	case PHY_STATE_HS_ONLINE:
		dev_dbg(&rport->phy->dev, "HS online\n");
		break;
	case PHY_STATE_FS_LS_ONLINE:
		/*
		 * For FS/LS device, the online state share with connect state
		 * from utmi_ls and utmi_hstdet register, so we distinguish
		 * them via suspended flag.
		 *
		 * Plus, there are two cases, one is D- Line pull-up, and D+
		 * line pull-down, the state is 4; another is D+ line pull-up,
		 * and D- line pull-down, the state is 2.
		 */
		if (!rport->suspended) {
			/* D- line pull-up, D+ line pull-down */
			dev_dbg(&rport->phy->dev, "FS/LS online\n");
			break;
		}
		/* fall through */
	case PHY_STATE_CONNECT:
		if (rport->suspended) {
			dev_dbg(&rport->phy->dev, "Connected\n");
			rockchip_usb2phy_power_on(rport->phy);
			rport->suspended = false;
		} else {
			/* D+ line pull-up, D- line pull-down */
			dev_dbg(&rport->phy->dev, "FS/LS online\n");
		}
		break;
	case PHY_STATE_DISCONNECT:
		if (!rport->suspended) {
			dev_dbg(&rport->phy->dev, "Disconnected\n");
			rockchip_usb2phy_power_off(rport->phy);
			rport->suspended = true;
		}

		/*
		 * activate the linestate detection to get the next device
		 * plug-in irq.
		 */
		property_enable(rphy->grf, &rport->port_cfg->ls_det_clr, true);
		property_enable(rphy->grf, &rport->port_cfg->ls_det_en, true);

		/*
		 * we don't need to rearm the delayed work when the phy port
		 * is suspended.
		 */
		mutex_unlock(&rport->mutex);
		return;
	default:
		dev_dbg(&rport->phy->dev, "unknown phy state\n");
		break;
	}

next_schedule:
	mutex_unlock(&rport->mutex);
	schedule_delayed_work(&rport->sm_work, SCHEDULE_DELAY);
}

static irqreturn_t rockchip_usb2phy_linestate_irq(int irq, void *data)
{
	struct rockchip_usb2phy_port *rport = data;
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);

	if (!property_enabled(rphy->grf, &rport->port_cfg->ls_det_st))
		return IRQ_NONE;

	mutex_lock(&rport->mutex);

	/* disable linestate detect irq and clear its status */
	property_enable(rphy->grf, &rport->port_cfg->ls_det_en, false);
	property_enable(rphy->grf, &rport->port_cfg->ls_det_clr, true);

	mutex_unlock(&rport->mutex);

	/*
	 * In this case for host phy port, a new device is plugged in,
	 * meanwhile, if the phy port is suspended, we need rearm the work to
	 * resume it and mange its states; otherwise, we do nothing about that.
	 */
	if (rport->suspended && rport->port_id == USB2PHY_PORT_HOST)
		rockchip_usb2phy_sm_work(&rport->sm_work.work);

	return IRQ_HANDLED;
}

static irqreturn_t rockchip_usb2phy_bvalid_irq(int irq, void *data)
{
	struct rockchip_usb2phy_port *rport = data;
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);

	if (!property_enabled(rphy->grf, &rport->port_cfg->bvalid_det_st))
		return IRQ_NONE;

	mutex_lock(&rport->mutex);

	/* clear bvalid detect irq pending status */
	property_enable(rphy->grf, &rport->port_cfg->bvalid_det_clr, true);

	mutex_unlock(&rport->mutex);

	rockchip_usb2phy_otg_sm_work(&rport->otg_sm_work.work);

	return IRQ_HANDLED;
}

static irqreturn_t rockchip_usb2phy_otg_mux_irq(int irq, void *data)
{
	struct rockchip_usb2phy_port *rport = data;
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);

	if (property_enabled(rphy->grf, &rport->port_cfg->bvalid_det_st))
		return rockchip_usb2phy_bvalid_irq(irq, data);
	else
		return IRQ_NONE;
}

static int rockchip_usb2phy_host_port_init(struct rockchip_usb2phy *rphy,
					   struct rockchip_usb2phy_port *rport,
					   struct device_node *child_np)
{
	int ret;

	rport->port_id = USB2PHY_PORT_HOST;
	rport->port_cfg = &rphy->phy_cfg->port_cfgs[USB2PHY_PORT_HOST];
	rport->suspended = true;

	mutex_init(&rport->mutex);
	INIT_DELAYED_WORK(&rport->sm_work, rockchip_usb2phy_sm_work);

	rport->ls_irq = of_irq_get_byname(child_np, "linestate");
	if (rport->ls_irq < 0) {
		dev_err(rphy->dev, "no linestate irq provided\n");
		return rport->ls_irq;
	}

	ret = devm_request_threaded_irq(rphy->dev, rport->ls_irq, NULL,
					rockchip_usb2phy_linestate_irq,
					IRQF_ONESHOT,
					"rockchip_usb2phy", rport);
	if (ret) {
		dev_err(rphy->dev, "failed to request linestate irq handle\n");
		return ret;
	}

	return 0;
}

static int rockchip_otg_event(struct notifier_block *nb,
			      unsigned long event, void *ptr)
{
	struct rockchip_usb2phy_port *rport =
		container_of(nb, struct rockchip_usb2phy_port, event_nb);

	schedule_delayed_work(&rport->otg_sm_work, OTG_SCHEDULE_DELAY);

	return NOTIFY_DONE;
}

static int rockchip_usb2phy_otg_port_init(struct rockchip_usb2phy *rphy,
					  struct rockchip_usb2phy_port *rport,
					  struct device_node *child_np)
{
	int ret;

	rport->port_id = USB2PHY_PORT_OTG;
	rport->port_cfg = &rphy->phy_cfg->port_cfgs[USB2PHY_PORT_OTG];
	rport->state = OTG_STATE_UNDEFINED;

	/*
	 * set suspended flag to true, but actually don't
	 * put phy in suspend mode, it aims to enable usb
	 * phy and clock in power_on() called by usb controller
	 * driver during probe.
	 */
	rport->suspended = true;
	rport->vbus_attached = false;

	mutex_init(&rport->mutex);

	rport->mode = of_usb_get_dr_mode_by_phy(child_np, -1);
	if (rport->mode == USB_DR_MODE_HOST ||
	    rport->mode == USB_DR_MODE_UNKNOWN) {
		ret = 0;
		goto out;
	}

	INIT_DELAYED_WORK(&rport->chg_work, rockchip_chg_detect_work);
	INIT_DELAYED_WORK(&rport->otg_sm_work, rockchip_usb2phy_otg_sm_work);

	/*
	 * Some SoCs use one interrupt with otg-id/otg-bvalid/linestate
	 * interrupts muxed together, so probe the otg-mux interrupt first,
	 * if not found, then look for the regular interrupts one by one.
	 */
	rport->otg_mux_irq = of_irq_get_byname(child_np, "otg-mux");
	if (rport->otg_mux_irq > 0) {
		ret = devm_request_threaded_irq(rphy->dev, rport->otg_mux_irq,
						NULL,
						rockchip_usb2phy_otg_mux_irq,
						IRQF_ONESHOT,
						"rockchip_usb2phy_otg",
						rport);
		if (ret) {
			dev_err(rphy->dev,
				"failed to request otg-mux irq handle\n");
			goto out;
		}
	} else {
		rport->bvalid_irq = of_irq_get_byname(child_np, "otg-bvalid");
		if (rport->bvalid_irq < 0) {
			dev_err(rphy->dev, "no vbus valid irq provided\n");
			ret = rport->bvalid_irq;
			goto out;
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
			goto out;
		}
	}

	if (!IS_ERR(rphy->edev)) {
		rport->event_nb.notifier_call = rockchip_otg_event;

		ret = devm_extcon_register_notifier(rphy->dev, rphy->edev,
					EXTCON_USB_HOST, &rport->event_nb);
		if (ret)
			dev_err(rphy->dev, "register USB HOST notifier failed\n");
	}

out:
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
	int index, ret;

	rphy = devm_kzalloc(dev, sizeof(*rphy), GFP_KERNEL);
	if (!rphy)
		return -ENOMEM;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "phy configs are not assigned!\n");
		return -EINVAL;
	}

	if (!dev->parent || !dev->parent->of_node)
		return -EINVAL;

	rphy->grf = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(rphy->grf))
		return PTR_ERR(rphy->grf);

	if (of_device_is_compatible(np, "rockchip,rv1108-usb2phy")) {
		rphy->usbgrf =
			syscon_regmap_lookup_by_phandle(dev->of_node,
							"rockchip,usbgrf");
		if (IS_ERR(rphy->usbgrf))
			return PTR_ERR(rphy->usbgrf);
	} else {
		rphy->usbgrf = NULL;
	}

	if (of_property_read_u32(np, "reg", &reg)) {
		dev_err(dev, "the reg property is not assigned in %pOFn node\n",
			np);
		return -EINVAL;
	}

	rphy->dev = dev;
	phy_cfgs = match->data;
	rphy->chg_state = USB_CHG_STATE_UNDEFINED;
	rphy->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
	platform_set_drvdata(pdev, rphy);

	ret = rockchip_usb2phy_extcon_register(rphy);
	if (ret)
		return ret;

	/* find out a proper config which can be matched with dt. */
	index = 0;
	while (phy_cfgs[index].reg) {
		if (phy_cfgs[index].reg == reg) {
			rphy->phy_cfg = &phy_cfgs[index];
			break;
		}

		++index;
	}

	if (!rphy->phy_cfg) {
		dev_err(dev, "no phy-config can be matched with %pOFn node\n",
			np);
		return -EINVAL;
	}

	rphy->clk = of_clk_get_by_name(np, "phyclk");
	if (!IS_ERR(rphy->clk)) {
		clk_prepare_enable(rphy->clk);
	} else {
		dev_info(&pdev->dev, "no phyclk specified\n");
		rphy->clk = NULL;
	}

	ret = rockchip_usb2phy_clk480m_register(rphy);
	if (ret) {
		dev_err(dev, "failed to register 480m output clock\n");
		goto disable_clks;
	}

	index = 0;
	for_each_available_child_of_node(np, child_np) {
		struct rockchip_usb2phy_port *rport = &rphy->ports[index];
		struct phy *phy;

		/* This driver aims to support both otg-port and host-port */
		if (!of_node_name_eq(child_np, "host-port") &&
		    !of_node_name_eq(child_np, "otg-port"))
			goto next_child;

		phy = devm_phy_create(dev, child_np, &rockchip_usb2phy_ops);
		if (IS_ERR(phy)) {
			dev_err(dev, "failed to create phy\n");
			ret = PTR_ERR(phy);
			goto put_child;
		}

		rport->phy = phy;
		phy_set_drvdata(rport->phy, rport);

		/* initialize otg/host port separately */
		if (of_node_name_eq(child_np, "host-port")) {
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
	return PTR_ERR_OR_ZERO(provider);

put_child:
	of_node_put(child_np);
disable_clks:
	if (rphy->clk) {
		clk_disable_unprepare(rphy->clk);
		clk_put(rphy->clk);
	}
	return ret;
}

static const struct rockchip_usb2phy_cfg rk3228_phy_cfgs[] = {
	{
		.reg = 0x760,
		.num_ports	= 2,
		.clkout_ctl	= { 0x0768, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x0760, 15, 0, 0, 0x1d1 },
				.bvalid_det_en	= { 0x0680, 3, 3, 0, 1 },
				.bvalid_det_st	= { 0x0690, 3, 3, 0, 1 },
				.bvalid_det_clr	= { 0x06a0, 3, 3, 0, 1 },
				.ls_det_en	= { 0x0680, 2, 2, 0, 1 },
				.ls_det_st	= { 0x0690, 2, 2, 0, 1 },
				.ls_det_clr	= { 0x06a0, 2, 2, 0, 1 },
				.utmi_bvalid	= { 0x0480, 4, 4, 0, 1 },
				.utmi_ls	= { 0x0480, 3, 2, 0, 1 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0764, 15, 0, 0, 0x1d1 },
				.ls_det_en	= { 0x0680, 4, 4, 0, 1 },
				.ls_det_st	= { 0x0690, 4, 4, 0, 1 },
				.ls_det_clr	= { 0x06a0, 4, 4, 0, 1 }
			}
		},
		.chg_det = {
			.opmode		= { 0x0760, 3, 0, 5, 1 },
			.cp_det		= { 0x0884, 4, 4, 0, 1 },
			.dcp_det	= { 0x0884, 3, 3, 0, 1 },
			.dp_det		= { 0x0884, 5, 5, 0, 1 },
			.idm_sink_en	= { 0x0768, 8, 8, 0, 1 },
			.idp_sink_en	= { 0x0768, 7, 7, 0, 1 },
			.idp_src_en	= { 0x0768, 9, 9, 0, 1 },
			.rdm_pdwn_en	= { 0x0768, 10, 10, 0, 1 },
			.vdm_src_en	= { 0x0768, 12, 12, 0, 1 },
			.vdp_src_en	= { 0x0768, 11, 11, 0, 1 },
		},
	},
	{
		.reg = 0x800,
		.num_ports	= 2,
		.clkout_ctl	= { 0x0808, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x800, 15, 0, 0, 0x1d1 },
				.ls_det_en	= { 0x0684, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0694, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x06a4, 0, 0, 0, 1 }
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x804, 15, 0, 0, 0x1d1 },
				.ls_det_en	= { 0x0684, 1, 1, 0, 1 },
				.ls_det_st	= { 0x0694, 1, 1, 0, 1 },
				.ls_det_clr	= { 0x06a4, 1, 1, 0, 1 }
			}
		},
	},
	{ /* sentinel */ }
};

static const struct rockchip_usb2phy_cfg rk3328_phy_cfgs[] = {
	{
		.reg = 0x100,
		.num_ports	= 2,
		.clkout_ctl	= { 0x108, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x0100, 15, 0, 0, 0x1d1 },
				.bvalid_det_en	= { 0x0110, 2, 2, 0, 1 },
				.bvalid_det_st	= { 0x0114, 2, 2, 0, 1 },
				.bvalid_det_clr = { 0x0118, 2, 2, 0, 1 },
				.ls_det_en	= { 0x0110, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0114, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0118, 0, 0, 0, 1 },
				.utmi_avalid	= { 0x0120, 10, 10, 0, 1 },
				.utmi_bvalid	= { 0x0120, 9, 9, 0, 1 },
				.utmi_ls	= { 0x0120, 5, 4, 0, 1 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x104, 15, 0, 0, 0x1d1 },
				.ls_det_en	= { 0x110, 1, 1, 0, 1 },
				.ls_det_st	= { 0x114, 1, 1, 0, 1 },
				.ls_det_clr	= { 0x118, 1, 1, 0, 1 },
				.utmi_ls	= { 0x120, 17, 16, 0, 1 },
				.utmi_hstdet	= { 0x120, 19, 19, 0, 1 }
			}
		},
		.chg_det = {
			.opmode		= { 0x0100, 3, 0, 5, 1 },
			.cp_det		= { 0x0120, 24, 24, 0, 1 },
			.dcp_det	= { 0x0120, 23, 23, 0, 1 },
			.dp_det		= { 0x0120, 25, 25, 0, 1 },
			.idm_sink_en	= { 0x0108, 8, 8, 0, 1 },
			.idp_sink_en	= { 0x0108, 7, 7, 0, 1 },
			.idp_src_en	= { 0x0108, 9, 9, 0, 1 },
			.rdm_pdwn_en	= { 0x0108, 10, 10, 0, 1 },
			.vdm_src_en	= { 0x0108, 12, 12, 0, 1 },
			.vdp_src_en	= { 0x0108, 11, 11, 0, 1 },
		},
	},
	{ /* sentinel */ }
};

static const struct rockchip_usb2phy_cfg rk3366_phy_cfgs[] = {
	{
		.reg = 0x700,
		.num_ports	= 2,
		.clkout_ctl	= { 0x0724, 15, 15, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0728, 15, 0, 0, 0x1d1 },
				.ls_det_en	= { 0x0680, 4, 4, 0, 1 },
				.ls_det_st	= { 0x0690, 4, 4, 0, 1 },
				.ls_det_clr	= { 0x06a0, 4, 4, 0, 1 },
				.utmi_ls	= { 0x049c, 14, 13, 0, 1 },
				.utmi_hstdet	= { 0x049c, 12, 12, 0, 1 }
			}
		},
	},
	{ /* sentinel */ }
};

static const struct rockchip_usb2phy_cfg rk3399_phy_cfgs[] = {
	{
		.reg		= 0xe450,
		.num_ports	= 2,
		.clkout_ctl	= { 0xe450, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0xe454, 1, 0, 2, 1 },
				.bvalid_det_en	= { 0xe3c0, 3, 3, 0, 1 },
				.bvalid_det_st	= { 0xe3e0, 3, 3, 0, 1 },
				.bvalid_det_clr	= { 0xe3d0, 3, 3, 0, 1 },
				.utmi_avalid	= { 0xe2ac, 7, 7, 0, 1 },
				.utmi_bvalid	= { 0xe2ac, 12, 12, 0, 1 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0xe458, 1, 0, 0x2, 0x1 },
				.ls_det_en	= { 0xe3c0, 6, 6, 0, 1 },
				.ls_det_st	= { 0xe3e0, 6, 6, 0, 1 },
				.ls_det_clr	= { 0xe3d0, 6, 6, 0, 1 },
				.utmi_ls	= { 0xe2ac, 22, 21, 0, 1 },
				.utmi_hstdet	= { 0xe2ac, 23, 23, 0, 1 }
			}
		},
		.chg_det = {
			.opmode		= { 0xe454, 3, 0, 5, 1 },
			.cp_det		= { 0xe2ac, 2, 2, 0, 1 },
			.dcp_det	= { 0xe2ac, 1, 1, 0, 1 },
			.dp_det		= { 0xe2ac, 0, 0, 0, 1 },
			.idm_sink_en	= { 0xe450, 8, 8, 0, 1 },
			.idp_sink_en	= { 0xe450, 7, 7, 0, 1 },
			.idp_src_en	= { 0xe450, 9, 9, 0, 1 },
			.rdm_pdwn_en	= { 0xe450, 10, 10, 0, 1 },
			.vdm_src_en	= { 0xe450, 12, 12, 0, 1 },
			.vdp_src_en	= { 0xe450, 11, 11, 0, 1 },
		},
	},
	{
		.reg		= 0xe460,
		.num_ports	= 2,
		.clkout_ctl	= { 0xe460, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus        = { 0xe464, 1, 0, 2, 1 },
				.bvalid_det_en  = { 0xe3c0, 8, 8, 0, 1 },
				.bvalid_det_st  = { 0xe3e0, 8, 8, 0, 1 },
				.bvalid_det_clr = { 0xe3d0, 8, 8, 0, 1 },
				.utmi_avalid	= { 0xe2ac, 10, 10, 0, 1 },
				.utmi_bvalid    = { 0xe2ac, 16, 16, 0, 1 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0xe468, 1, 0, 0x2, 0x1 },
				.ls_det_en	= { 0xe3c0, 11, 11, 0, 1 },
				.ls_det_st	= { 0xe3e0, 11, 11, 0, 1 },
				.ls_det_clr	= { 0xe3d0, 11, 11, 0, 1 },
				.utmi_ls	= { 0xe2ac, 26, 25, 0, 1 },
				.utmi_hstdet	= { 0xe2ac, 27, 27, 0, 1 }
			}
		},
	},
	{ /* sentinel */ }
};

static const struct rockchip_usb2phy_cfg rv1108_phy_cfgs[] = {
	{
		.reg = 0x100,
		.num_ports	= 2,
		.clkout_ctl	= { 0x108, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x0100, 15, 0, 0, 0x1d1 },
				.bvalid_det_en	= { 0x0680, 3, 3, 0, 1 },
				.bvalid_det_st	= { 0x0690, 3, 3, 0, 1 },
				.bvalid_det_clr = { 0x06a0, 3, 3, 0, 1 },
				.ls_det_en	= { 0x0680, 2, 2, 0, 1 },
				.ls_det_st	= { 0x0690, 2, 2, 0, 1 },
				.ls_det_clr	= { 0x06a0, 2, 2, 0, 1 },
				.utmi_bvalid	= { 0x0804, 10, 10, 0, 1 },
				.utmi_ls	= { 0x0804, 13, 12, 0, 1 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0104, 15, 0, 0, 0x1d1 },
				.ls_det_en	= { 0x0680, 4, 4, 0, 1 },
				.ls_det_st	= { 0x0690, 4, 4, 0, 1 },
				.ls_det_clr	= { 0x06a0, 4, 4, 0, 1 },
				.utmi_ls	= { 0x0804, 9, 8, 0, 1 },
				.utmi_hstdet	= { 0x0804, 7, 7, 0, 1 }
			}
		},
		.chg_det = {
			.opmode		= { 0x0100, 3, 0, 5, 1 },
			.cp_det		= { 0x0804, 1, 1, 0, 1 },
			.dcp_det	= { 0x0804, 0, 0, 0, 1 },
			.dp_det		= { 0x0804, 2, 2, 0, 1 },
			.idm_sink_en	= { 0x0108, 8, 8, 0, 1 },
			.idp_sink_en	= { 0x0108, 7, 7, 0, 1 },
			.idp_src_en	= { 0x0108, 9, 9, 0, 1 },
			.rdm_pdwn_en	= { 0x0108, 10, 10, 0, 1 },
			.vdm_src_en	= { 0x0108, 12, 12, 0, 1 },
			.vdp_src_en	= { 0x0108, 11, 11, 0, 1 },
		},
	},
	{ /* sentinel */ }
};

static const struct of_device_id rockchip_usb2phy_dt_match[] = {
	{ .compatible = "rockchip,rk3228-usb2phy", .data = &rk3228_phy_cfgs },
	{ .compatible = "rockchip,rk3328-usb2phy", .data = &rk3328_phy_cfgs },
	{ .compatible = "rockchip,rk3366-usb2phy", .data = &rk3366_phy_cfgs },
	{ .compatible = "rockchip,rk3399-usb2phy", .data = &rk3399_phy_cfgs },
	{ .compatible = "rockchip,rv1108-usb2phy", .data = &rv1108_phy_cfgs },
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_usb2phy_dt_match);

static struct platform_driver rockchip_usb2phy_driver = {
	.probe		= rockchip_usb2phy_probe,
	.driver		= {
		.name	= "rockchip-usb2phy",
		.of_match_table = rockchip_usb2phy_dt_match,
	},
};
module_platform_driver(rockchip_usb2phy_driver);

MODULE_AUTHOR("Frank Wang <frank.wang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USB2.0 PHY driver");
MODULE_LICENSE("GPL v2");
