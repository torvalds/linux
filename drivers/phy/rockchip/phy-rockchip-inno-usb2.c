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
#include <linux/pm_runtime.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/rockchip/cpu.h>
#include <linux/mfd/syscon.h>
#include <linux/usb/of.h>
#include <linux/usb/otg.h>
#include <linux/usb/role.h>
#include <linux/usb/typec_mux.h>
#include <linux/wakelock.h>

#define BIT_WRITEABLE_SHIFT	16
#define SCHEDULE_DELAY		(60 * HZ)
#define OTG_SCHEDULE_DELAY	(1 * HZ)
#define BYPASS_SCHEDULE_DELAY	(2 * HZ)
#define FILTER_COUNTER		0xF4240

struct rockchip_usb2phy;

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
	EXTCON_USB_VBUS_EN,
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
 * @chg_mode: set phy in charge detection mode.
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
	struct usb2phy_reg	chg_mode;
};

/**
 * struct rockchip_usb2phy_port_cfg - usb-phy port configuration.
 * @phy_sus: phy suspend register.
 * @pipe_phystatus: select pipe phystatus from grf or phy.
 * @bvalid_det_en: vbus valid rise detection enable register.
 * @bvalid_det_st: vbus valid rise detection status register.
 * @bvalid_det_clr: vbus valid rise detection clear register.
 * @bvalid_grf_con: vbus valid software control.
 * @bvalid_grf_sel: vbus valid software control select.
 * @bvalid_phy_con: vbus valid external select and enable.
 * @bypass_dm_en: usb bypass uart DM enable register.
 * @bypass_sel: usb bypass uart select register.
 * @bypass_iomux: usb bypass uart GRF iomux register.
 * @bypass_bc: bypass battery charging module.
 * @bypass_otg: bypass otg module.
 * @bypass_host: bypass host module.
 * @disfall_en: host disconnect fall edge detection enable.
 * @disfall_st: host disconnect fall edge detection state.
 * @disfall_clr: host disconnect fall edge detection clear.
 * @disrise_en: host disconnect rise edge detection enable.
 * @disrise_st: host disconnect rise edge detection state.
 * @disrise_clr: host disconnect rise edge detection clear.
 * @ls_det_en: linestate detection enable register.
 * @ls_det_st: linestate detection state register.
 * @ls_det_clr: linestate detection clear register.
 * @iddig_output: iddig output from grf.
 * @iddig_en: utmi iddig select between grf and phy,
 *	      0: from phy; 1: from grf
 * @idfall_det_en: id fall detection enable register.
 * @idfall_det_st: id fall detection state register.
 * @idfall_det_clr: id fall detection clear register.
 * @idrise_det_en: id rise detection enable register.
 * @idrise_det_st: id rise detection state register.
 * @idrise_det_clr: id rise detection clear register.
 * @utmi_avalid: utmi vbus avalid status register.
 * @utmi_bvalid: utmi vbus bvalid status register.
 * @utmi_iddig: otg port id pin status register.
 * @utmi_ls: utmi linestate state register.
 * @utmi_hstdet: utmi host disconnect register.
 * @vbus_det_en: vbus detect function power down register.
 */
struct rockchip_usb2phy_port_cfg {
	struct usb2phy_reg	phy_sus;
	struct usb2phy_reg	pipe_phystatus;
	struct usb2phy_reg	bvalid_det_en;
	struct usb2phy_reg	bvalid_det_st;
	struct usb2phy_reg	bvalid_det_clr;
	struct usb2phy_reg	bvalid_grf_con;
	struct usb2phy_reg	bvalid_grf_sel;
	struct usb2phy_reg	bvalid_phy_con;
	struct usb2phy_reg	bypass_dm_en;
	struct usb2phy_reg	bypass_sel;
	struct usb2phy_reg	bypass_iomux;
	struct usb2phy_reg	bypass_bc;
	struct usb2phy_reg	bypass_otg;
	struct usb2phy_reg	bypass_host;
	struct usb2phy_reg	disfall_en;
	struct usb2phy_reg	disfall_st;
	struct usb2phy_reg	disfall_clr;
	struct usb2phy_reg	disrise_en;
	struct usb2phy_reg	disrise_st;
	struct usb2phy_reg	disrise_clr;
	struct usb2phy_reg	ls_det_en;
	struct usb2phy_reg	ls_det_st;
	struct usb2phy_reg	ls_det_clr;
	struct usb2phy_reg	iddig_output;
	struct usb2phy_reg	iddig_en;
	struct usb2phy_reg	idfall_det_en;
	struct usb2phy_reg	idfall_det_st;
	struct usb2phy_reg	idfall_det_clr;
	struct usb2phy_reg	idrise_det_en;
	struct usb2phy_reg	idrise_det_st;
	struct usb2phy_reg	idrise_det_clr;
	struct usb2phy_reg	utmi_avalid;
	struct usb2phy_reg	utmi_bvalid;
	struct usb2phy_reg	utmi_iddig;
	struct usb2phy_reg	utmi_ls;
	struct usb2phy_reg	utmi_hstdet;
	struct usb2phy_reg	vbus_det_en;
};

/**
 * struct rockchip_usb2phy_cfg - usb-phy configuration.
 * @reg: the address offset of grf for usb-phy config.
 * @num_ports: specify how many ports that the phy has.
 * @phy_tuning: phy default parameters tuning.
 * @vbus_detect: vbus voltage level detection function.
 * @clkout_ctl: keep on/turn off output clk of phy.
 * @ls_filter_con: set linestate filter time.
 * @port_cfgs: usb-phy port configurations.
 * @ls_filter_con: set linestate filter time.
 * @chg_det: charger detection registers.
 */
struct rockchip_usb2phy_cfg {
	unsigned int	reg;
	unsigned int	num_ports;
	int (*phy_tuning)(struct rockchip_usb2phy *rphy);
	int (*vbus_detect)(struct rockchip_usb2phy *rphy, bool en);
	struct usb2phy_reg	clkout_ctl;
	struct usb2phy_reg	ls_filter_con;
	const struct rockchip_usb2phy_port_cfg	port_cfgs[USB2PHY_NUM_PORTS];
	const struct rockchip_chg_det_reg	chg_det;
};

/**
 * struct rockchip_usb2phy_port - usb-phy port data.
 * @phy: generic phy.
 * @port_id: flag for otg port or host port.
 * @low_power_en: enable enter low power when suspend.
 * @perip_connected: flag for periphyeral connect status.
 * @prev_iddig: previous otg port id pin status.
 * @sel_pipe_phystatus: select pipe phystatus from grf.
 * @suspended: phy suspended flag.
 * @typec_vbus_det: Type-C otg vbus detect.
 * @utmi_avalid: utmi avalid status usage flag.
 *	true	- use avalid to get vbus status
 *	false	- use bvalid to get vbus status
 * @vbus_attached: otg device vbus status.
 * @vbus_always_on: otg vbus is always powered on.
 * @vbus_enabled: vbus regulator status.
 * @bypass_uart_en: usb bypass uart enable, passed from DT.
 * @host_disconnect: usb host disconnect status.
 * @dis_u2_susphy: disable usb2 phy suspend.
 * @bvalid_irq: IRQ number assigned for vbus valid rise detection.
 * @ls_irq: IRQ number assigned for linestate detection.
 * @id_irq: IRQ number assigned for id fall or rise detection.
 * @otg_mux_irq: IRQ number which multiplex otg-id/otg-bvalid/linestate
 *		 irqs to one irq in otg-port.
 * @mutex: for register updating in sm_work.
 * @chg_work: charge detect work.
 * @bypass_uart_work: usb bypass uart work.
 * @otg_sm_work: OTG state machine work.
 * @sm_work: HOST state machine work.
 * @vbus: vbus regulator supply on few rockchip boards.
 * @sw: orientation switch, communicate with TCPM (Type-C Port Manager).
 * @port_cfg: port register configuration, assigned by driver data.
 * @event_nb: hold event notification callback.
 * @state: define OTG enumeration states before device reset.
 * @mode: the dr_mode of the controller.
 */
struct rockchip_usb2phy_port {
	struct phy	*phy;
	unsigned int	port_id;
	bool		low_power_en;
	bool		perip_connected;
	bool		prev_iddig;
	bool		sel_pipe_phystatus;
	bool		suspended;
	bool		typec_vbus_det;
	bool		utmi_avalid;
	bool		vbus_attached;
	bool		vbus_always_on;
	bool		vbus_enabled;
	bool		bypass_uart_en;
	bool		host_disconnect;
	bool		dis_u2_susphy;
	int		bvalid_irq;
	int		ls_irq;
	int             id_irq;
	int		otg_mux_irq;
	struct mutex	mutex;
	struct		delayed_work bypass_uart_work;
	struct		delayed_work chg_work;
	struct		delayed_work otg_sm_work;
	struct		delayed_work sm_work;
	struct		regulator *vbus;
	struct		typec_switch *sw;
	const struct	rockchip_usb2phy_port_cfg *port_cfg;
	struct notifier_block	event_nb;
	struct wake_lock	wakelock;
	enum usb_otg_state	state;
	enum usb_dr_mode	mode;
};

/**
 * struct rockchip_usb2phy - usb2.0 phy driver data.
 * @dev: pointer to device.
 * @grf: General Register Files regmap.
 * @usbgrf: USB General Register Files regmap.
 * @usbctrl_grf: USB Controller General Register Files regmap.
 * *phy_base: the base address of USB PHY.
 * @phy_reset: phy reset control.
 * @clks: array of phy input clocks.
 * @clk480m: clock struct of phy output clk.
 * @clk480m_hw: clock struct of phy output clk management.
 * @num_clks: number of phy input clocks.
 * @chg_state: states involved in USB charger detection.
 * @chg_type: USB charger types.
 * @dcd_retries: The retry count used to track Data contact
 *		 detection process.
 * @primary_retries: The retry count used for charger
 *		     detection primary phase.
 * @phy_sus_cfg: Store the phy current suspend configuration.
 * @edev_self: represent the source of extcon.
 * @irq: IRQ number assigned for phy which combined irqs of
 *	 otg port and host port.
 * @edev: extcon device for notification registration
 * @phy_cfg: phy register configuration, assigned by driver data.
 * @ports: phy port instance.
 */
struct rockchip_usb2phy {
	struct device	*dev;
	struct regmap	*grf;
	struct regmap	*usbgrf;
	struct regmap	*usbctrl_grf;
	void __iomem	*phy_base;
	struct reset_control	*phy_reset;
	struct clk_bulk_data	*clks;
	struct clk	*clk480m;
	struct clk_hw	clk480m_hw;
	int		num_clks;
	enum usb_chg_state	chg_state;
	enum power_supply_type	chg_type;
	u8			dcd_retries;
	u8			primary_retries;
	unsigned int		phy_sus_cfg;
	bool			edev_self;
	int			irq;
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

static inline void phy_clear_bits(void __iomem *reg, u32 bits)
{
	u32 tmp = readl(reg);

	tmp &= ~bits;
	writel(tmp, reg);
}

static inline void phy_set_bits(void __iomem *reg, u32 bits)
{
	u32 tmp = readl(reg);

	tmp |= bits;
	writel(tmp, reg);
}

static inline void phy_update_bits(void __iomem *reg, u32 mask, u32 val)
{
	u32 tmp = readl(reg);

	tmp &= ~mask;
	tmp |= val & mask;
	writel(tmp, reg);
}

static int rockchip_usb2phy_reset(struct rockchip_usb2phy *rphy)
{
	int ret;

	if (!rphy->phy_reset)
		return 0;

	ret = reset_control_assert(rphy->phy_reset);
	if (ret)
		return ret;

	udelay(10);

	ret = reset_control_deassert(rphy->phy_reset);
	if (ret)
		return ret;

	usleep_range(100, 200);

	return 0;
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

		rphy->edev_self = true;
	}

	rphy->edev = edev;

	return 0;
}

/* The caller must hold rport->mutex lock */
static int rockchip_usb2phy_enable_id_irq(struct rockchip_usb2phy *rphy,
					  struct rockchip_usb2phy_port *rport,
					  bool en)
{
	int ret;

	ret = property_enable(rphy->grf, &rport->port_cfg->idfall_det_clr, true);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf, &rport->port_cfg->idfall_det_en, en);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf, &rport->port_cfg->idrise_det_clr, true);
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

	ret = property_enable(rphy->grf, &rport->port_cfg->bvalid_det_clr, true);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf, &rport->port_cfg->bvalid_det_en, en);
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

static int rockchip_usb2phy_enable_host_disc_irq(struct rockchip_usb2phy *rphy,
						 struct rockchip_usb2phy_port *rport,
						 bool en)
{
	int ret;

	ret = property_enable(rphy->grf, &rport->port_cfg->disfall_clr, true);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf, &rport->port_cfg->disfall_en, en);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf, &rport->port_cfg->disrise_clr, true);
	if (ret)
		goto out;

	ret = property_enable(rphy->grf, &rport->port_cfg->disrise_en, en);
out:
	return ret;
}

static int rockchip_usb_bypass_uart(struct rockchip_usb2phy_port *rport,
				    bool en)
{
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);
	const struct usb2phy_reg *iomux = &rport->port_cfg->bypass_iomux;
	struct regmap *base = get_reg_base(rphy);
	int ret = 0;

	mutex_lock(&rport->mutex);

	if (en == property_enabled(base, &rport->port_cfg->bypass_sel)) {
		dev_info(&rport->phy->dev,
			 "bypass uart %s is already set\n", en ? "on" : "off");
		goto unlock;
	}

	dev_info(&rport->phy->dev, "bypass uart %s\n", en ? "on" : "off");

	if (en) {
		/*
		 * To use UART function:
		 * 1. Put the USB PHY in suspend mode and opmode is normal;
		 * 2. Set bypasssel to 1'b1 and bypassdmen to 1'b1;
		 *
		 * Note: Although the datasheet requires that put USB PHY
		 * in non-driving mode to disable resistance when use USB
		 * bypass UART function, but actually we find that if we
		 * set phy in non-driving mode, it will cause UART to print
		 * random codes. So just put USB PHY in normal mode.
		 */
		ret |= property_enable(base, &rport->port_cfg->bypass_sel,
				       true);
		ret |= property_enable(base, &rport->port_cfg->bypass_dm_en,
				       true);

		/* Some platforms required to set iomux of bypass uart */
		if (iomux->offset)
			ret |= property_enable(rphy->grf, iomux, true);
	} else {
		/* just disable bypass, and resume phy in phy power_on later */
		ret |= property_enable(base, &rport->port_cfg->bypass_sel,
				       false);
		ret |= property_enable(base, &rport->port_cfg->bypass_dm_en,
				       false);

		/* Some platforms required to set iomux of bypass uart */
		if (iomux->offset)
			ret |= property_enable(rphy->grf, iomux, false);
	}

unlock:
	mutex_unlock(&rport->mutex);

	return ret;
}

static void rockchip_usb_bypass_uart_work(struct work_struct *work)
{
	struct rockchip_usb2phy_port *rport =
		container_of(work, struct rockchip_usb2phy_port,
			     bypass_uart_work.work);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);
	bool vbus, iddig;
	int ret;

	mutex_lock(&rport->mutex);

	iddig = property_enabled(rphy->grf, &rport->port_cfg->utmi_iddig);

	if (rport->utmi_avalid)
		vbus = property_enabled(rphy->grf, &rport->port_cfg->utmi_avalid);
	else
		vbus = property_enabled(rphy->grf, &rport->port_cfg->utmi_bvalid);

	mutex_unlock(&rport->mutex);

	/*
	 * If the vbus is low and iddig is high, it indicates that usb
	 * otg is not working, then we can enable usb to bypass uart,
	 * otherwise schedule the work until the conditions (vbus is low
	 * and iddig is high) are matched.
	 */
	if (!vbus && iddig) {
		ret = rockchip_usb_bypass_uart(rport, true);
		if (ret)
			dev_warn(&rport->phy->dev,
				 "failed to enable bypass uart\n");
	} else {
		schedule_delayed_work(&rport->bypass_uart_work,
				      BYPASS_SCHEDULE_DELAY);
	}
}

static int rockchip_usb2phy_init(struct phy *phy)
{
	struct rockchip_usb2phy_port *rport = phy_get_drvdata(phy);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(phy->dev.parent);
	int ret = 0;
	unsigned int ul, ul_mask;

	mutex_lock(&rport->mutex);

	if (rport->port_id == USB2PHY_PORT_OTG &&
	    (rport->mode == USB_DR_MODE_PERIPHERAL ||
	     rport->mode == USB_DR_MODE_OTG)) {
		/* clear id status and enable id detect irq */
		if (rport->id_irq > 0 || rport->otg_mux_irq > 0 ||
		    rphy->irq > 0) {
			ret = rockchip_usb2phy_enable_id_irq(rphy, rport,
							     true);
			if (ret) {
				dev_err(rphy->dev,
					"failed to enable id irq\n");
				goto out;
			}
		}

		/* clear bvalid status and enable bvalid detect irq */
		if ((rport->bvalid_irq > 0 || rport->otg_mux_irq > 0 ||
		    rphy->irq > 0) && !rport->vbus_always_on) {
			ret = rockchip_usb2phy_enable_vbus_irq(rphy, rport,
							       true);
			if (ret) {
				dev_err(rphy->dev,
					"failed to enable bvalid irq\n");
				goto out;
			}
			schedule_delayed_work(&rport->otg_sm_work,
					      rport->typec_vbus_det ? 0 : OTG_SCHEDULE_DELAY);
		}
	} else if (rport->port_id == USB2PHY_PORT_HOST) {
		if (rport->port_cfg->disfall_en.offset) {
			ret = regmap_read(rphy->grf, rport->port_cfg->utmi_ls.offset, &ul);
			if (ret < 0)
				goto out;
			ul_mask = GENMASK(rport->port_cfg->utmi_ls.bitend,
					  rport->port_cfg->utmi_ls.bitstart);
			rport->host_disconnect = (ul & ul_mask) == 0 ? true : false;
			ret = rockchip_usb2phy_enable_host_disc_irq(rphy, rport, true);
			if (ret) {
				dev_err(rphy->dev, "failed to enable disconnect irq\n");
				goto out;
			}
		}

		/* clear linestate and enable linestate detect irq */
		ret = rockchip_usb2phy_enable_line_irq(rphy, rport, true);
		if (ret) {
			dev_err(rphy->dev, "failed to enable linestate irq\n");
			goto out;
		}

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

	if (rport->bypass_uart_en) {
		ret = rockchip_usb_bypass_uart(rport, false);
		if (ret) {
			dev_warn(&rport->phy->dev,
				 "failed to disable bypass uart\n");
			goto exit;
		}
	}

	mutex_lock(&rport->mutex);

	if (!rport->suspended) {
		ret = 0;
		goto unlock;
	}

	ret = clk_prepare_enable(rphy->clk480m);
	if (ret)
		goto unlock;

	if (rport->sel_pipe_phystatus)
		property_enable(rphy->usbctrl_grf,
				&rport->port_cfg->pipe_phystatus, true);

	ret = property_enable(base, &rport->port_cfg->phy_sus, false);
	if (ret)
		goto unlock;

	/*
	 * For rk3588, it needs to reset phy when exit from
	 * suspend mode with common_on_n 1'b1(aka REFCLK_LOGIC,
	 * Bias, and PLL blocks are powered down) for lower
	 * power consumption. If you don't want to reset phy,
	 * please keep the common_on_n 1'b0 to set these blocks
	 * remain powered.
	 */
	if (rport->port_id == USB2PHY_PORT_OTG &&
	    of_device_is_compatible(rphy->dev->of_node, "rockchip,rk3588-usb2phy")) {
		ret = rockchip_usb2phy_reset(rphy);
		if (ret)
			goto unlock;
	}

	/* waiting for the utmi_clk to become stable */
	usleep_range(1500, 2000);

	rport->suspended = false;

unlock:
	mutex_unlock(&rport->mutex);

	/* Enable bypass uart in the bypass_uart_work. */
	if (rport->bypass_uart_en)
		schedule_delayed_work(&rport->bypass_uart_work, 0);

exit:
	return ret;
}

static int rockchip_usb2phy_power_off(struct phy *phy)
{
	struct rockchip_usb2phy_port *rport = phy_get_drvdata(phy);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(phy->dev.parent);
	struct regmap *base = get_reg_base(rphy);
	int ret;

	dev_dbg(&rport->phy->dev, "port power off\n");

	mutex_lock(&rport->mutex);

	if (rport->suspended) {
		ret = 0;
		goto unlock;
	}

	ret = property_enable(base, &rport->port_cfg->phy_sus, true);
	if (ret)
		goto unlock;

	rport->suspended = true;
	clk_disable_unprepare(rphy->clk480m);

unlock:
	mutex_unlock(&rport->mutex);

	/* Enable bypass uart in the bypass_uart_work. */
	if (rport->bypass_uart_en)
		schedule_delayed_work(&rport->bypass_uart_work, 0);

	return ret;
}

static int rockchip_usb2phy_exit(struct phy *phy)
{
	struct rockchip_usb2phy_port *rport = phy_get_drvdata(phy);

	if (rport->port_id == USB2PHY_PORT_HOST)
		cancel_delayed_work_sync(&rport->sm_work);
	else if (rport->port_id == USB2PHY_PORT_OTG &&
		 rport->otg_sm_work.work.func)
		flush_delayed_work(&rport->otg_sm_work);

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
	bool vbus_det_en;
	int ret = 0;

	if (rport->port_id != USB2PHY_PORT_OTG)
		return ret;

	switch (mode) {
	case PHY_MODE_USB_OTG:
		if (rphy->edev_self && submode) {
			if (submode == USB_ROLE_HOST) {
				extcon_set_state(rphy->edev, EXTCON_USB_HOST, true);
				extcon_set_state(rphy->edev, EXTCON_USB, false);
			} else if (submode == USB_ROLE_DEVICE) {
				extcon_set_state(rphy->edev, EXTCON_USB_HOST, false);
				extcon_set_state(rphy->edev, EXTCON_USB, true);
			}

			return ret;
		}

		/*
		 * In case of using vbus to detect connect state by u2phy,
		 * enable vbus detect on otg mode.
		 */
		fallthrough;
	case PHY_MODE_USB_DEVICE:
		/* Disable VBUS supply */
		rockchip_set_vbus_power(rport, false);
		extcon_set_state_sync(rphy->edev, EXTCON_USB_VBUS_EN, false);
		/* For vbus always on, set EXTCON_USB to true. */
		if (rport->vbus_always_on)
			extcon_set_state(rphy->edev, EXTCON_USB, true);
		rport->perip_connected = true;
		vbus_det_en = true;
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
		/* For vbus always on, deinit EXTCON_USB to false. */
		if (rport->vbus_always_on)
			extcon_set_state(rphy->edev, EXTCON_USB, false);
		rport->perip_connected = false;
		fallthrough;
	case PHY_MODE_INVALID:
		vbus_det_en = false;
		break;
	default:
		dev_info(&rport->phy->dev, "illegal mode\n");
		return ret;
	}

	if (rphy->phy_cfg->vbus_detect)
		rphy->phy_cfg->vbus_detect(rphy, vbus_det_en);
	else
		ret = property_enable(rphy->grf, &rport->port_cfg->vbus_det_en,
				      vbus_det_en);

	return ret;
}

static const struct phy_ops rockchip_usb2phy_ops = {
	.init		= rockchip_usb2phy_init,
	.exit		= rockchip_usb2phy_exit,
	.power_on	= rockchip_usb2phy_power_on,
	.power_off	= rockchip_usb2phy_power_off,
	.set_mode	= rockchip_usb2phy_set_mode,
	.owner		= THIS_MODULE,
};

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
	}

	return -EINVAL;
}

static ssize_t otg_mode_store(struct device *device,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct rockchip_usb2phy *rphy = dev_get_drvdata(device);
	struct rockchip_usb2phy_port *rport = NULL;
	struct regmap *base = get_reg_base(rphy);
	enum usb_dr_mode new_dr_mode;
	unsigned int index;
	int rc = count;

	for (index = 0; index < rphy->phy_cfg->num_ports; index++) {
		rport = &rphy->ports[index];
		if (rport->port_id == USB2PHY_PORT_OTG)
			break;
	}

	if (!rport) {
		dev_err(rphy->dev, "Fail to get otg port\n");
		rc = -EINVAL;
		goto err0;
	} else if (rport->port_id != USB2PHY_PORT_OTG ||
		   rport->mode == USB_DR_MODE_UNKNOWN) {
		dev_err(rphy->dev, "No support otg\n");
		rc = -EINVAL;
		goto err0;
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
		goto err1;
	}

	if (rport->mode == new_dr_mode) {
		dev_warn(rphy->dev, "Same as current mode\n");
		goto err1;
	}

	rport->mode = new_dr_mode;

	switch (rport->mode) {
	case USB_DR_MODE_HOST:
		rockchip_usb2phy_set_mode(rport->phy, PHY_MODE_USB_HOST, 0);
		property_enable(base, &rport->port_cfg->iddig_output, false);
		property_enable(base, &rport->port_cfg->iddig_en, true);
		break;
	case USB_DR_MODE_PERIPHERAL:
		rockchip_usb2phy_set_mode(rport->phy, PHY_MODE_USB_DEVICE, 0);
		property_enable(base, &rport->port_cfg->iddig_output, true);
		property_enable(base, &rport->port_cfg->iddig_en, true);
		break;
	case USB_DR_MODE_OTG:
		rockchip_usb2phy_set_mode(rport->phy, PHY_MODE_USB_OTG, 0);
		property_enable(base, &rport->port_cfg->iddig_output, false);
		property_enable(base, &rport->port_cfg->iddig_en, false);
		break;
	default:
		break;
	}

err1:
	mutex_unlock(&rport->mutex);

err0:
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

static void rockchip_usb2phy_otg_sm_work(struct work_struct *work)
{
	struct rockchip_usb2phy_port *rport =
		container_of(work, struct rockchip_usb2phy_port,
			     otg_sm_work.work);
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);
	static unsigned int cable;
	unsigned long delay;
	bool sch_work;

	mutex_lock(&rport->mutex);

	if (rport->port_cfg->bvalid_grf_con.enable && rport->typec_vbus_det)
		rport->vbus_attached =
			property_enabled(rphy->grf, &rport->port_cfg->bvalid_grf_con);
	else if (rport->utmi_avalid)
		rport->vbus_attached =
			property_enabled(rphy->grf, &rport->port_cfg->utmi_avalid);
	else
		rport->vbus_attached =
			property_enabled(rphy->grf, &rport->port_cfg->utmi_bvalid);

	sch_work = false;
	delay = OTG_SCHEDULE_DELAY;

	dev_dbg(&rport->phy->dev, "%s otg sm work\n",
		usb_otg_state_string(rport->state));

	switch (rport->state) {
	case OTG_STATE_UNDEFINED:
		rport->state = OTG_STATE_B_IDLE;
		if (!rport->vbus_attached) {
			mutex_unlock(&rport->mutex);
			if (!rport->dis_u2_susphy)
				rockchip_usb2phy_power_off(rport->phy);
			mutex_lock(&rport->mutex);
		}
		fallthrough;
	case OTG_STATE_B_IDLE:
		if (extcon_get_state(rphy->edev, EXTCON_USB_HOST) > 0 ||
		    extcon_get_state(rphy->edev, EXTCON_USB_VBUS_EN) > 0) {
			dev_dbg(&rport->phy->dev, "usb otg host connect\n");
			rport->state = OTG_STATE_A_HOST;
			rphy->chg_state = USB_CHG_STATE_UNDEFINED;
			rphy->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
			mutex_unlock(&rport->mutex);
			rockchip_usb2phy_power_on(rport->phy);
			return;
		} else if (rport->vbus_attached) {
			dev_dbg(&rport->phy->dev, "vbus_attach\n");
			switch (rphy->chg_state) {
			case USB_CHG_STATE_UNDEFINED:
				mutex_unlock(&rport->mutex);
				schedule_delayed_work(&rport->chg_work, 0);
				return;
			case USB_CHG_STATE_DETECTED:
				switch (rphy->chg_type) {
				case POWER_SUPPLY_TYPE_USB:
					dev_dbg(&rport->phy->dev, "sdp cable is connected\n");
					wake_lock(&rport->wakelock);
					cable = EXTCON_CHG_USB_SDP;
					mutex_unlock(&rport->mutex);
					rockchip_usb2phy_power_on(rport->phy);
					mutex_lock(&rport->mutex);
					rport->state = OTG_STATE_B_PERIPHERAL;
					rport->perip_connected = true;
					sch_work = true;
					break;
				case POWER_SUPPLY_TYPE_USB_DCP:
					dev_dbg(&rport->phy->dev, "dcp cable is connected\n");
					cable = EXTCON_CHG_USB_DCP;
					sch_work = true;
					break;
				case POWER_SUPPLY_TYPE_USB_CDP:
					dev_dbg(&rport->phy->dev, "cdp cable is connected\n");
					wake_lock(&rport->wakelock);
					cable = EXTCON_CHG_USB_CDP;
					mutex_unlock(&rport->mutex);
					rockchip_usb2phy_power_on(rport->phy);
					mutex_lock(&rport->mutex);
					rport->state = OTG_STATE_B_PERIPHERAL;
					rport->perip_connected = true;
					sch_work = true;
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
		} else {
			rphy->chg_state = USB_CHG_STATE_UNDEFINED;
			rphy->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
			mutex_unlock(&rport->mutex);
			if (!rport->dis_u2_susphy)
				rockchip_usb2phy_power_off(rport->phy);
			mutex_lock(&rport->mutex);
		}
		break;
	case OTG_STATE_B_PERIPHERAL:
		sch_work = true;

		if (extcon_get_state(rphy->edev, EXTCON_USB_HOST) > 0 ||
		    extcon_get_state(rphy->edev,
					    EXTCON_USB_VBUS_EN) > 0) {
			dev_dbg(&rport->phy->dev, "usb otg host connect\n");
			rport->state = OTG_STATE_A_HOST;
			rphy->chg_state = USB_CHG_STATE_UNDEFINED;
			rphy->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
			rport->perip_connected = false;
			sch_work = false;
			wake_unlock(&rport->wakelock);
		} else if (!rport->vbus_attached) {
			dev_dbg(&rport->phy->dev, "usb disconnect\n");
			rport->state = OTG_STATE_B_IDLE;
			rport->perip_connected = false;
			rphy->chg_state = USB_CHG_STATE_UNDEFINED;
			rphy->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
			delay = OTG_SCHEDULE_DELAY;
			wake_unlock(&rport->wakelock);
		}
		break;
	case OTG_STATE_A_HOST:
		if (extcon_get_state(rphy->edev, EXTCON_USB_HOST) == 0) {
			dev_dbg(&rport->phy->dev, "usb otg host disconnect\n");
			rport->state = OTG_STATE_B_IDLE;
			sch_work = true;
		} else {
			mutex_unlock(&rport->mutex);
			return;
		}
		break;
	default:
		mutex_unlock(&rport->mutex);
		return;
	}

	if (extcon_get_state(rphy->edev, cable) != rport->vbus_attached) {
		extcon_set_state_sync(rphy->edev,
					cable, rport->vbus_attached);

		if (!rport->vbus_attached)
			cable = EXTCON_NONE;
	} else if (rport->state == OTG_STATE_A_HOST &&
		 extcon_get_state(rphy->edev, cable)) {
		/*
		 * If plug in OTG host cable when the rport state is
		 * OTG_STATE_B_PERIPHERAL, the vbus voltage will stay
		 * in high, so the rport->vbus_attached may not be
		 * changed. We need to set cable state here.
		 */
		extcon_set_state_sync(rphy->edev, cable, false);
		cable = EXTCON_NONE;
	}

	if (rphy->edev_self &&
	    (extcon_get_state(rphy->edev, EXTCON_USB) !=
	     rport->perip_connected)) {
		extcon_set_state_sync(rphy->edev,
					EXTCON_USB,
					rport->perip_connected);
		extcon_sync(rphy->edev, EXTCON_USB_HOST);
	}
	if (sch_work)
		schedule_delayed_work(&rport->otg_sm_work, delay);

	mutex_unlock(&rport->mutex);
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
	const struct usb2phy_reg *phy_sus_reg;
	bool is_dcd, tmout, vout;
	unsigned long delay;
	unsigned int mask;
	int ret;

	dev_dbg(&rport->phy->dev, "chg detection work state = %d\n",
		rphy->chg_state);

	/*
	 * The conditions for charger detection:
	 * 1. Set the PHY in normal mode to keep the UTMI_CLK on.
	 * 2. Set the utmi_opmode in non-driving mode.
	 * 3. Set the utmi_xcvrselect to FS speed.
	 * 4. Set the utmi_termselect to FS speed.
	 * 5. Enable the DP/DM pulldown resistor.
	 */
	switch (rphy->chg_state) {
	case USB_CHG_STATE_UNDEFINED:
		mutex_lock(&rport->mutex);
		/* Store the PHY current suspend configuration */
		phy_sus_reg = &rport->port_cfg->phy_sus;
		ret = regmap_read(base, phy_sus_reg->offset,
				  &rphy->phy_sus_cfg);
		if (ret) {
			dev_err(&rport->phy->dev,
				"Fail to read phy_sus reg offset 0x%x, ret %d\n",
				phy_sus_reg->offset, ret);
			mutex_unlock(&rport->mutex);
			return;
		}

		/* Set the PHY in charger detection mode */
		property_enable(base, &rphy->phy_cfg->chg_det.chg_mode, true);
		/* Start DCD processing stage 1 */
		rockchip_chg_enable_dcd(rphy, true);
		rphy->chg_state = USB_CHG_STATE_WAIT_FOR_DCD;
		rphy->dcd_retries = 0;
		rphy->primary_retries = 0;
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
				if (rphy->primary_retries < 2) {
					/* Turn off DCD circuitry */
					rockchip_chg_enable_dcd(rphy, false);
					/* Voltage Source on DP, Probe on DM */
					rockchip_chg_enable_primary_det(rphy,
									true);
					delay = CHG_PRIMARY_DET_TIME;
					rphy->chg_state =
						USB_CHG_STATE_DCD_DONE;
					rphy->primary_retries++;
					/* break USB_CHG_STATE_DCD_DONE */
					break;
				}
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
		fallthrough;
	case USB_CHG_STATE_SECONDARY_DONE:
		rphy->chg_state = USB_CHG_STATE_DETECTED;
		fallthrough;
	case USB_CHG_STATE_DETECTED:
		if (rphy->phy_cfg->chg_det.chg_mode.offset !=
		    rport->port_cfg->phy_sus.offset)
			property_enable(base, &rphy->phy_cfg->chg_det.chg_mode, false);

		/* Restore the PHY suspend configuration */
		phy_sus_reg = &rport->port_cfg->phy_sus;
		mask = GENMASK(phy_sus_reg->bitend, phy_sus_reg->bitstart);
		ret = regmap_write(base, phy_sus_reg->offset,
				   (rphy->phy_sus_cfg | (mask << BIT_WRITEABLE_SHIFT)));
		if (ret)
			dev_err(&rport->phy->dev,
				"Fail to set phy_sus reg offset 0x%x, ret %d\n",
				phy_sus_reg->offset, ret);
		mutex_unlock(&rport->mutex);
		rockchip_usb2phy_otg_sm_work(&rport->otg_sm_work.work);
		dev_dbg(&rport->phy->dev, "charger = %s\n",
			 chg_to_string(rphy->chg_type));
		return;
	default:
		mutex_unlock(&rport->mutex);
		return;
	}

	/*
	 * Hold the mutex lock during the whole charger
	 * detection stage, and release it after detect
	 * the charger type.
	 */
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
	unsigned int sh, ul, uhd, state;
	unsigned int ul_mask, uhd_mask;
	int ret;

	if (!rport->port_cfg->utmi_ls.offset ||
	    (!rport->port_cfg->utmi_hstdet.offset &&
	     !rport->port_cfg->disfall_en.offset)) {
		dev_dbg(&rport->phy->dev, "some property may not be specified\n");
		return;
	}

	mutex_lock(&rport->mutex);

	ret = regmap_read(rphy->grf, rport->port_cfg->utmi_ls.offset, &ul);
	if (ret < 0)
		goto next_schedule;

	ul_mask = GENMASK(rport->port_cfg->utmi_ls.bitend,
			  rport->port_cfg->utmi_ls.bitstart);

	if (rport->port_cfg->utmi_hstdet.offset) {
		ret = regmap_read(rphy->grf, rport->port_cfg->utmi_hstdet.offset, &uhd);
		if (ret < 0)
			goto next_schedule;

		uhd_mask = GENMASK(rport->port_cfg->utmi_hstdet.bitend,
				   rport->port_cfg->utmi_hstdet.bitstart);

		sh = rport->port_cfg->utmi_hstdet.bitend -
		     rport->port_cfg->utmi_hstdet.bitstart + 1;
		/* stitch on utmi_ls and utmi_hstdet as phy state */
		state = ((uhd & uhd_mask) >> rport->port_cfg->utmi_hstdet.bitstart) |
			(((ul & ul_mask) >> rport->port_cfg->utmi_ls.bitstart) << sh);
	} else {
		state = ((ul & ul_mask) >> rport->port_cfg->utmi_ls.bitstart) << 1 |
			rport->host_disconnect;
	}

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
		fallthrough;
	case PHY_STATE_CONNECT:
		if (rport->suspended) {
			dev_dbg(&rport->phy->dev, "Connected\n");
			mutex_unlock(&rport->mutex);
			rockchip_usb2phy_power_on(rport->phy);
			mutex_lock(&rport->mutex);
			rport->suspended = false;
		} else {
			/* D+ line pull-up, D- line pull-down */
			dev_dbg(&rport->phy->dev, "FS/LS online\n");
		}
		break;
	case PHY_STATE_DISCONNECT:
		if (!rport->suspended) {
			dev_dbg(&rport->phy->dev, "Disconnected\n");
			mutex_unlock(&rport->mutex);
			rockchip_usb2phy_power_off(rport->phy);
			mutex_lock(&rport->mutex);
			rport->suspended = true;
		}

		/*
		 * activate the linestate detection to get the next device
		 * plug-in irq.
		 */
		rockchip_usb2phy_enable_line_irq(rphy, rport, true);

		/*
		 * we don't need to rearm the delayed work when the phy port
		 * is suspended.
		 */
		mutex_unlock(&rport->mutex);
		return;
	default:
		dev_dbg(&rport->phy->dev, "unknown phy state %d\n", state);
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

	if (!property_enabled(rphy->grf, &rport->port_cfg->ls_det_st) ||
	    !property_enabled(rphy->grf, &rport->port_cfg->ls_det_en))
		return IRQ_NONE;

	dev_dbg(&rport->phy->dev, "linestate interrupt\n");

	mutex_lock(&rport->mutex);

	/* disable linestate detect irq and clear its status */
	rockchip_usb2phy_enable_line_irq(rphy, rport, false);

	/*
	 * For host port, it may miss disc irq when device is connected,
	 * in this case, we can clear host_disconnect state depend on
	 * the linestate irq.
	 */
	if (rport->port_id == USB2PHY_PORT_HOST && rport->port_cfg->disfall_en.offset)
		rport->host_disconnect = false;

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

	if (rport->bypass_uart_en)
		rockchip_usb_bypass_uart(rport, false);

	if (rport->otg_sm_work.work.func) {
		cancel_delayed_work_sync(&rport->otg_sm_work);
		rockchip_usb2phy_otg_sm_work(&rport->otg_sm_work.work);
	}

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
	} else if (property_enabled(rphy->grf, &rport->port_cfg->idrise_det_st)) {
		property_enable(rphy->grf, &rport->port_cfg->idrise_det_clr,
				true);
		cable_vbus_state = false;
	}

	extcon_set_state(rphy->edev, EXTCON_USB_HOST, cable_vbus_state);
	extcon_set_state(rphy->edev, EXTCON_USB_VBUS_EN, cable_vbus_state);

	extcon_sync(rphy->edev, EXTCON_USB_HOST);
	extcon_sync(rphy->edev, EXTCON_USB_VBUS_EN);

	rockchip_set_vbus_power(rport, cable_vbus_state);

	mutex_unlock(&rport->mutex);

	return IRQ_HANDLED;
}

static irqreturn_t rockchip_usb2phy_host_disc_irq(int irq, void *data)
{
	struct rockchip_usb2phy_port *rport = data;
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);

	if (!property_enabled(rphy->grf, &rport->port_cfg->disfall_st) &&
	    !property_enabled(rphy->grf, &rport->port_cfg->disrise_st))
		return IRQ_NONE;

	mutex_lock(&rport->mutex);

	/* clear disconnect fall or rise detect irq pending status */
	if (property_enabled(rphy->grf, &rport->port_cfg->disfall_st)) {
		property_enable(rphy->grf, &rport->port_cfg->disfall_clr,
				true);
		rport->host_disconnect = false;
	} else if (property_enabled(rphy->grf, &rport->port_cfg->disrise_st)) {
		property_enable(rphy->grf, &rport->port_cfg->disrise_clr,
				true);
		rport->host_disconnect = true;
	}

	mutex_unlock(&rport->mutex);

	return IRQ_HANDLED;
}

static irqreturn_t rockchip_usb2phy_otg_mux_irq(int irq, void *data)
{
	irqreturn_t ret = IRQ_NONE;

	ret  = rockchip_usb2phy_id_irq(irq, data);
	ret |= rockchip_usb2phy_bvalid_irq(irq, data);
	ret |= rockchip_usb2phy_linestate_irq(irq, data);

	return ret;
}

static irqreturn_t rockchip_usb2phy_irq(int irq, void *data)
{
	struct rockchip_usb2phy *rphy = data;
	struct rockchip_usb2phy_port *rport;
	irqreturn_t ret = IRQ_NONE;
	unsigned int index;
	bool force_mode;

	for (index = 0; index < rphy->phy_cfg->num_ports; index++) {
		rport = &rphy->ports[index];
		if (!rport->phy)
			continue;

		/*
		 * Handle disc irq before linestate irq to set the disc
		 * state for sm work scheduled in the linestate irq handler.
		 */
		if (rport->port_id == USB2PHY_PORT_HOST &&
		    rport->port_cfg->disfall_en.offset)
			ret |= rockchip_usb2phy_host_disc_irq(irq, rport);

		/* Handle linestate irq for both otg port and host port */
		ret |= rockchip_usb2phy_linestate_irq(irq, rport);

		/*
		 * Handle bvalid irq and id irq for otg port which
		 * is assigned to otg controller.
		 */
		if (rport->port_id == USB2PHY_PORT_OTG &&
		    rport->mode != USB_DR_MODE_UNKNOWN) {
			if (rport->mode == USB_DR_MODE_HOST) {
				/*
				 * If otg port work as usb host mode and
				 * force_mode is true, it means that the
				 * otg port is forced to host mode by the
				 * grf plug iddig indicator via the sys
				 * interface "otg_mode". We need to handle
				 * the bvalid irq and id irq in this case.
				 */
				force_mode = property_enabled(rphy->grf,
						&rport->port_cfg->iddig_en);
				if (!force_mode)
					continue;
			}

			if (!rport->vbus_always_on)
				ret |= rockchip_usb2phy_bvalid_irq(irq, rport);

			ret |= rockchip_usb2phy_id_irq(irq, rport);
		}
	}

	return ret;
}

static int rockchip_usb2phy_port_irq_init(struct rockchip_usb2phy *rphy,
					  struct rockchip_usb2phy_port *rport,
					  struct device_node *child_np)
{
	int ret;

	/*
	 * If the usb2 phy used combined irq for otg and host port,
	 * don't need to init otg and host port irq separately.
	 */
	if (rphy->irq > 0)
		return 0;

	/*
	 * Some SoCs (e.g. RV1108) use one combined irq for all of
	 * the irqs of otg port. So probe the otg-mux interrupt first,
	 * if not found, then init the regular irqs one by one.
	 */
	rport->otg_mux_irq = of_irq_get_byname(child_np, "otg-mux");
	if (rport->otg_mux_irq > 0) {
		ret = devm_request_threaded_irq(rphy->dev, rport->otg_mux_irq,
						NULL,
						rockchip_usb2phy_otg_mux_irq,
						IRQF_ONESHOT,
						"rockchip_usb2phy_otg",
						rport);
		if (ret)
			dev_err(rphy->dev,
				"failed to request otg-mux irq handle\n");

		return ret;
	}

	/* Init linestate irq for both otg port and host port */
	rport->ls_irq = of_irq_get_byname(child_np, "linestate");
	if (rport->ls_irq <= 0) {
		dev_err(rphy->dev, "no linestate irq provided\n");
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(rphy->dev, rport->ls_irq, NULL,
					rockchip_usb2phy_linestate_irq,
					IRQF_ONESHOT,
					"rockchip_usb2phy_ls", rport);
	if (ret) {
		dev_err(rphy->dev, "failed to request linestate irq handle\n");
		return ret;
	}

	/*
	 * If it's host port or it's otg port but only support
	 * host mode, return immediately without init the bvalid
	 * and id irqs/
	 */
	if (rport->port_id == USB2PHY_PORT_HOST ||
	    rport->mode == USB_DR_MODE_HOST ||
	    rport->mode == USB_DR_MODE_UNKNOWN)
		return ret;

	/* Init the bvalid irq for otg port */
	if (!rport->vbus_always_on) {
		rport->bvalid_irq = of_irq_get_byname(child_np,
						      "otg-bvalid");
		if (rport->bvalid_irq <= 0) {
			dev_err(rphy->dev, "no bvalid irq provided\n");
			return -EINVAL;
		}

		ret = devm_request_threaded_irq(rphy->dev,
						rport->bvalid_irq,
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
	}

	/* Init the id irq for otg port */
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
	}

	return ret;
}

static void rockchip_usb2phy_usb_bvalid_enable(struct rockchip_usb2phy_port *rport,
					       u8 enable)
{
	struct rockchip_usb2phy *rphy = dev_get_drvdata(rport->phy->dev.parent);
	const struct rockchip_usb2phy_port_cfg *cfg = rport->port_cfg;

	if (cfg->bvalid_phy_con.enable)
		property_enable(rphy->grf, &cfg->bvalid_phy_con, enable);

	if (cfg->bvalid_grf_con.enable)
		property_enable(rphy->grf, &cfg->bvalid_grf_con, enable);
}

static int rockchip_usb2phy_orien_sw_set(struct typec_switch *sw,
					 enum typec_orientation orien)
{
	struct rockchip_usb2phy_port *rport = typec_switch_get_drvdata(sw);

	dev_dbg(&rport->phy->dev, "type-c orientation: %d\n", orien);

	mutex_lock(&rport->mutex);
	rockchip_usb2phy_usb_bvalid_enable(rport, orien != TYPEC_ORIENTATION_NONE);
	mutex_unlock(&rport->mutex);

	return 0;
}

static int
rockchip_usb2phy_setup_orien_switch(struct rockchip_usb2phy *rphy,
				    struct rockchip_usb2phy_port *rport)
{
	struct typec_switch_desc sw_desc = { };
	struct device *dev = rphy->dev;

	sw_desc.drvdata = rport;
	sw_desc.fwnode = dev_fwnode(dev);
	sw_desc.set = rockchip_usb2phy_orien_sw_set;

	rport->sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(rport->sw)) {
		dev_err(dev, "Error register typec orientation switch: %ld\n",
			PTR_ERR(rport->sw));
		return PTR_ERR(rport->sw);
	}

	return 0;
}

static void rockchip_usb2phy_orien_switch_unregister(void *data)
{
	struct rockchip_usb2phy_port *rport = data;

	typec_switch_unregister(rport->sw);
}

static int rockchip_usb2phy_host_port_init(struct rockchip_usb2phy *rphy,
					   struct rockchip_usb2phy_port *rport,
					   struct device_node *child_np)
{
	int ret;
	struct regmap *base = get_reg_base(rphy);

	rport->port_id = USB2PHY_PORT_HOST;
	rport->port_cfg = &rphy->phy_cfg->port_cfgs[USB2PHY_PORT_HOST];

	/* enter lower power state when suspend */
	rport->low_power_en =
		of_property_read_bool(child_np, "rockchip,low-power-mode");

	mutex_init(&rport->mutex);
	INIT_DELAYED_WORK(&rport->sm_work, rockchip_usb2phy_sm_work);

	ret = rockchip_usb2phy_port_irq_init(rphy, rport, child_np);
	if (ret) {
		dev_err(rphy->dev, "failed to init irq for host port\n");
		return ret;
	}

	/*
	 * Let us put phy-port into suspend mode here for saving power
	 * consumption, and usb controller will resume it during probe
	 * time if needed.
	 */
	ret = property_enable(base, &rport->port_cfg->phy_sus, true);
	if (ret)
		return ret;
	rport->suspended = true;

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
	int iddig;
	struct regmap *base = get_reg_base(rphy);

	rport->port_id = USB2PHY_PORT_OTG;
	rport->port_cfg = &rphy->phy_cfg->port_cfgs[USB2PHY_PORT_OTG];
	rport->state = OTG_STATE_UNDEFINED;
	rport->vbus_attached = false;
	rport->vbus_enabled = false;
	rport->perip_connected = false;
	rport->prev_iddig = true;

	mutex_init(&rport->mutex);

	/* bypass uart function is only used in debug stage. */
	rport->bypass_uart_en =
		of_property_read_bool(child_np, "rockchip,bypass-uart");
	rport->vbus_always_on =
		of_property_read_bool(child_np, "rockchip,vbus-always-on");
	rport->utmi_avalid =
		of_property_read_bool(child_np, "rockchip,utmi-avalid");
	rport->dis_u2_susphy =
		of_property_read_bool(child_np, "rockchip,dis-u2-susphy");

	/* enter lower power state when suspend */
	rport->low_power_en =
		of_property_read_bool(child_np, "rockchip,low-power-mode");

	/* For type-c with vbus_det always pull up */
	rport->typec_vbus_det =
		of_property_read_bool(child_np, "rockchip,typec-vbus-det");

	rport->sel_pipe_phystatus =
		of_property_read_bool(child_np, "rockchip,sel-pipe-phystatus");

	if (rport->sel_pipe_phystatus) {
		rphy->usbctrl_grf =
			syscon_regmap_lookup_by_phandle(rphy->dev->of_node,
							"rockchip,usbctrl-grf");
		if (IS_ERR(rphy->usbctrl_grf)) {
			dev_err(rphy->dev, "Failed to map usbctrl-grf\n");
			return PTR_ERR(rphy->usbctrl_grf);
		}
	}

	/* Get Vbus regulators */
	rport->vbus = devm_regulator_get_optional(&rport->phy->dev, "vbus");
	if (IS_ERR(rport->vbus)) {
		ret = PTR_ERR(rport->vbus);
		if (ret == -EPROBE_DEFER)
			return ret;

		if (rport->mode == USB_DR_MODE_OTG)
			dev_warn(&rport->phy->dev, "No vbus specified for otg port\n");
		rport->vbus = NULL;
	}

	rport->mode = of_usb_get_dr_mode_by_phy(child_np, -1);
	iddig = property_enabled(rphy->grf, &rport->port_cfg->utmi_iddig);
	if (rphy->edev_self && (rport->mode == USB_DR_MODE_HOST ||
	    rport->mode == USB_DR_MODE_UNKNOWN || !iddig)) {
		/* Enable VBUS supply for otg port */
		extcon_set_state(rphy->edev, EXTCON_USB, false);
		extcon_set_state(rphy->edev, EXTCON_USB_HOST, true);
		extcon_set_state(rphy->edev, EXTCON_USB_VBUS_EN, true);
		ret = rockchip_set_vbus_power(rport, true);
		if (ret)
			return ret;
	}

	ret = rockchip_usb2phy_port_irq_init(rphy, rport, child_np);
	if (ret) {
		dev_err(rphy->dev, "failed to init irq for otg port\n");
		return ret;
	}

	if (IS_REACHABLE(CONFIG_TYPEC) &&
	    device_property_present(rphy->dev, "orientation-switch")) {
		ret = rockchip_usb2phy_setup_orien_switch(rphy, rport);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(rphy->dev,
					       rockchip_usb2phy_orien_switch_unregister,
					       rport);
		if (ret)
			return ret;
	}

	/*
	 * Set the utmi bvalid come from the usb phy or grf.
	 * For most of Rockchip SoCs, them have VBUSDET pin
	 * for the usb phy to detect the USB VBUS and set
	 * the bvalid signal, so select the bvalid from the
	 * usb phy by default. And for those SoCs which don't
	 * have VBUSDET pin (e.g. RV1103), it needs to select
	 * the bvaid from the grf and set bvalid to be valid
	 * (high) by default.
	 */
	if (rport->port_cfg->bvalid_grf_sel.enable != 0) {
		if (of_machine_is_compatible("rockchip,rv1103"))
			property_enable(base, &rport->port_cfg->bvalid_grf_sel, true);
		else
			property_enable(base, &rport->port_cfg->bvalid_grf_sel, false);
	}

	if (rport->vbus_always_on)
		extcon_set_state(rphy->edev, EXTCON_USB, true);

	if (rport->vbus_always_on || rport->mode == USB_DR_MODE_HOST ||
	    rport->mode == USB_DR_MODE_UNKNOWN)
		goto out;

	wake_lock_init(&rport->wakelock, WAKE_LOCK_SUSPEND, "rockchip_otg");
	INIT_DELAYED_WORK(&rport->bypass_uart_work,
			  rockchip_usb_bypass_uart_work);
	INIT_DELAYED_WORK(&rport->chg_work, rockchip_chg_detect_work);
	INIT_DELAYED_WORK(&rport->otg_sm_work, rockchip_usb2phy_otg_sm_work);

	if (!IS_ERR(rphy->edev)) {
		rport->event_nb.notifier_call = rockchip_otg_event;

		ret = devm_extcon_register_notifier(rphy->dev, rphy->edev,
					EXTCON_USB_HOST, &rport->event_nb);
		if (ret) {
			dev_err(rphy->dev, "register USB HOST notifier failed\n");
			goto err;
		}
	}

out:
	/*
	 * Let us put phy-port into suspend mode here for saving power
	 * consumption, and usb controller will resume it during probe
	 * time if needed.
	 */
	ret = property_enable(base, &rport->port_cfg->phy_sus, true);
	if (ret)
		return ret;
	rport->suspended = true;

	return 0;

err:
	wake_lock_destroy(&rport->wakelock);
	return ret;
}

static int rockchip_usb2phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child_np;
	struct phy_provider *provider;
	struct rockchip_usb2phy *rphy;
	struct resource *res;
	const struct rockchip_usb2phy_cfg *phy_cfgs;
	const struct of_device_id *match;
	unsigned int reg;
	unsigned int index;
	int ret;

	rphy = devm_kzalloc(dev, sizeof(*rphy), GFP_KERNEL);
	if (!rphy)
		return -ENOMEM;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "phy configs are not assigned!\n");
		return -EINVAL;
	}

	if (!dev->parent || !dev->parent->of_node) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res) {
			dev_err(dev, "missing memory resource\n");
			return -ENODEV;
		}

		rphy->phy_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(rphy->phy_base))
			return PTR_ERR(rphy->phy_base);

		rphy->grf = syscon_regmap_lookup_by_phandle(np,
							    "rockchip,usbgrf");
		if (IS_ERR(rphy->grf))
			return PTR_ERR(rphy->grf);

		reg = res->start;
	} else {
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
			dev_err(dev, "missing reg property in %s node\n",
				np->name);
			return -EINVAL;
		}
	}

	rphy->dev = dev;
	phy_cfgs = match->data;
	rphy->chg_state = USB_CHG_STATE_UNDEFINED;
	rphy->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
	rphy->edev_self = false;
	rphy->irq = platform_get_irq(pdev, 0);
	platform_set_drvdata(pdev, rphy);

	ret = rockchip_usb2phy_extcon_register(rphy);
	if (ret)
		return ret;

	/* find out a proper config which can be matched with dt. */
	index = 0;
	do {
		if (phy_cfgs[index].reg == reg) {
			rphy->phy_cfg = &phy_cfgs[index];
			break;
		}

		++index;
	} while (phy_cfgs[index].reg);

	if (!rphy->phy_cfg) {
		dev_err(dev, "no phy-config can be matched with %pOFn node\n",
			np);
		return -EINVAL;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);


	rphy->phy_reset = devm_reset_control_get_optional(dev, "phy");
	if (IS_ERR(rphy->phy_reset))
		return PTR_ERR(rphy->phy_reset);

	ret = devm_clk_bulk_get_all(dev, &rphy->clks);
	if (ret == -EPROBE_DEFER)
		return ret;

	/* Clocks are optional */
	if (ret < 0)
		rphy->num_clks = 0;
	else
		rphy->num_clks = ret;

	ret = clk_bulk_prepare_enable(rphy->num_clks, rphy->clks);
	if (ret)
		return ret;

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
	if (IS_ERR(provider)) {
		dev_err(dev, "Failed to register phy provider\n");
		ret = PTR_ERR(provider);
		goto put_child;
	}

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

	if (rphy->irq > 0) {
		ret = devm_request_threaded_irq(rphy->dev, rphy->irq, NULL,
						rockchip_usb2phy_irq,
						IRQF_ONESHOT,
						"rockchip_usb2phy",
						rphy);
		if (ret) {
			dev_err(rphy->dev,
				"failed to request usb2 phy irq handle\n");
			goto put_child;
		}
	}

	if (of_property_read_bool(np, "wakeup-source"))
		device_init_wakeup(rphy->dev, true);
	else
		device_init_wakeup(rphy->dev, false);

	return 0;

put_child:
	of_node_put(child_np);
disable_clks:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	clk_bulk_disable_unprepare(rphy->num_clks, rphy->clks);
	return ret;
}

static int __maybe_unused
rockchip_usb2phy_low_power_enable(struct rockchip_usb2phy *rphy,
				  struct rockchip_usb2phy_port *rport,
				  bool value)
{
	int ret = 0;

	if (!rport->low_power_en)
		return ret;

	if (rport->port_id == USB2PHY_PORT_OTG) {
		dev_info(&rport->phy->dev, "set otg port low power state %d\n",
			 value);
		ret = property_enable(rphy->grf, &rport->port_cfg->bypass_bc,
				      value);
		if (ret)
			return ret;

		ret = property_enable(rphy->grf, &rport->port_cfg->bypass_otg,
				      value);
		if (ret)
			return ret;

		ret = property_enable(rphy->grf, &rport->port_cfg->vbus_det_en,
				      !value);
	} else if (rport->port_id == USB2PHY_PORT_HOST) {
		dev_info(&rport->phy->dev, "set host port low power state %d\n",
			 value);

		ret = property_enable(rphy->grf, &rport->port_cfg->bypass_host,
				      value);
	}

	return ret;
}

static int rk312x_usb2phy_tuning(struct rockchip_usb2phy *rphy)
{
	int ret;

	/* Turn off differential receiver in suspend mode */
	ret = regmap_write(rphy->grf, 0x298, 0x00040000);
	if (ret)
		return ret;

	return 0;
}

static int rk3228_usb2phy_tuning(struct rockchip_usb2phy *rphy)
{
	int ret = 0;

	/* Open pre-emphasize in non-chirp state for PHY0 otg port */
	if (rphy->phy_cfg->reg == 0x760)
		ret = regmap_write(rphy->grf, 0x76c, 0x00070004);

	return ret;
}

static int rk3308_usb2phy_tuning(struct rockchip_usb2phy *rphy)
{
	int ret;

	if (soc_is_rk3308bs()) {
		/* Turn off differential receiver in suspend mode */
		ret = regmap_update_bits(rphy->grf, 0x30, BIT(2), 0);
		if (ret)
			return ret;

		/* Enable otg port pre-emphasis during non-chirp phase */
		ret = regmap_update_bits(rphy->grf, 0, GENMASK(2, 0), BIT(2));
		if (ret)
			return ret;

		/* Set otg port squelch trigger point configure to 100mv */
		ret = regmap_update_bits(rphy->grf, 0x004, GENMASK(7, 5), 0x40);
		if (ret)
			return ret;

		ret = regmap_update_bits(rphy->grf, 0x008, BIT(0), 0x1);
		if (ret)
			return ret;

		/* Enable host port pre-emphasis during non-chirp phase */
		ret = regmap_update_bits(rphy->grf, 0x400, GENMASK(2, 0), BIT(2));
		if (ret)
			return ret;

		/* Set host port squelch trigger point configure to 100mv */
		ret = regmap_update_bits(rphy->grf, 0x404, GENMASK(7, 5), 0x40);
		if (ret)
			return ret;

		ret = regmap_update_bits(rphy->grf, 0x408, BIT(0), 0x1);
		if (ret)
			return ret;
	} else {
		/* Open pre-emphasize in non-chirp state for otg port */
		ret = regmap_write(rphy->grf, 0x0, 0x00070004);
		if (ret)
			return ret;

		/* Open pre-emphasize in non-chirp state for host port */
		ret = regmap_write(rphy->grf, 0x30, 0x00070004);
		if (ret)
			return ret;

		/* Turn off differential receiver in suspend mode */
		ret = regmap_write(rphy->grf, 0x18, 0x00040000);
		if (ret)
			return ret;
	}

	return 0;
}

static int rk3328_usb2phy_tuning(struct rockchip_usb2phy *rphy)
{
	int ret;

	if (soc_is_px30s()) {
		/* Enable otg port pre-emphasis during non-chirp phase */
		ret = regmap_update_bits(rphy->grf, 0x8000, GENMASK(2, 0), BIT(2));
		if (ret)
			return ret;

		/* Set otg port squelch trigger point configure to 100mv */
		ret = regmap_update_bits(rphy->grf, 0x8004, GENMASK(7, 5), 0x40);
		if (ret)
			return ret;

		ret = regmap_update_bits(rphy->grf, 0x8008, BIT(0), 0x1);
		if (ret)
			return ret;

		/* Turn off otg port differential reciver in suspend mode */
		ret = regmap_update_bits(rphy->grf, 0x8030, BIT(2), 0);
		if (ret)
			return ret;

		/* Enable host port pre-emphasis during non-chirp phase */
		ret = regmap_update_bits(rphy->grf, 0x8400, GENMASK(2, 0), BIT(2));
		if (ret)
			return ret;

		/* Set host port squelch trigger point configure to 100mv */
		ret = regmap_update_bits(rphy->grf, 0x8404, GENMASK(7, 5), 0x40);
		if (ret)
			return ret;

		ret = regmap_update_bits(rphy->grf, 0x8408, BIT(0), 0x1);
		if (ret)
			return ret;

		/* Turn off host port differential reciver in suspend mode */
		ret = regmap_update_bits(rphy->grf, 0x8430, BIT(2), 0);
		if (ret)
			return ret;
	} else {
		/* Open debug mode for tuning */
		ret = regmap_write(rphy->grf, 0x2c, 0xffff0400);
		if (ret)
			return ret;

		/* Open pre-emphasize in non-chirp state for otg port */
		ret = regmap_write(rphy->grf, 0x0, 0x00070004);
		if (ret)
			return ret;

		/* Open pre-emphasize in non-chirp state for host port */
		ret = regmap_write(rphy->grf, 0x30, 0x00070004);
		if (ret)
			return ret;

		/* Turn off differential receiver in suspend mode */
		ret = regmap_write(rphy->grf, 0x18, 0x00040000);
		if (ret)
			return ret;
	}
	return 0;
}

static int rk3366_usb2phy_tuning(struct rockchip_usb2phy *rphy)
{
	unsigned int open_pre_emphasize = 0xffff851f;
	unsigned int eye_height_tuning = 0xffff68c8;
	unsigned int compensation_tuning = 0xffff026e;
	int ret = 0;

	/* open HS pre-emphasize to expand HS slew rate for each port. */
	ret |= regmap_write(rphy->grf, 0x0780, open_pre_emphasize);
	ret |= regmap_write(rphy->grf, 0x079c, eye_height_tuning);
	ret |= regmap_write(rphy->grf, 0x07b0, open_pre_emphasize);
	ret |= regmap_write(rphy->grf, 0x07cc, eye_height_tuning);

	/* compensate default tuning reference relate to ODT and etc. */
	ret |= regmap_write(rphy->grf, 0x078c, compensation_tuning);

	return ret;
}

static int rk3399_usb2phy_tuning(struct rockchip_usb2phy *rphy)
{
	struct device_node *node = rphy->dev->of_node;
	int ret = 0;

	if (rphy->phy_cfg->reg == 0xe450) {
		/*
		 * Disable the pre-emphasize in eop state
		 * and chirp state to avoid mis-trigger the
		 * disconnect detection and also avoid hs
		 * handshake fail for PHY0.
		 */
		ret |= regmap_write(rphy->grf, 0x4480,
				    GENMASK(17, 16) | 0x0);
		ret |= regmap_write(rphy->grf, 0x44b4,
				    GENMASK(17, 16) | 0x0);
	} else {
		/*
		 * Disable the pre-emphasize in eop state
		 * and chirp state to avoid mis-trigger the
		 * disconnect detection and also avoid hs
		 * handshake fail for PHY1.
		 */
		ret |= regmap_write(rphy->grf, 0x4500,
				    GENMASK(17, 16) | 0x0);
		ret |= regmap_write(rphy->grf, 0x4534,
				    GENMASK(17, 16) | 0x0);
	}

	if (!of_property_read_bool(node, "rockchip,u2phy-tuning"))
		return ret;

	if (rphy->phy_cfg->reg == 0xe450) {
		/*
		 * Set max ODT compensation voltage and
		 * current tuning reference for PHY0.
		 */
		ret |= regmap_write(rphy->grf, 0x448c,
				    GENMASK(23, 16) | 0xe3);

		/* Set max pre-emphasis level for PHY0 */
		ret |= regmap_write(rphy->grf, 0x44b0,
				    GENMASK(18, 16) | 0x07);

		/*
		 * Set PHY0 A port squelch trigger point to 125mv
		 */
		ret |= regmap_write(rphy->grf, 0x4480,
				    GENMASK(30, 30) | 0x4000);
	} else {
		/*
		 * Set max ODT compensation voltage and
		 * current tuning reference for PHY1.
		 */
		ret |= regmap_write(rphy->grf, 0x450c,
				    GENMASK(23, 16) | 0xe3);

		/* Set max pre-emphasis level for PHY1 */
		ret |= regmap_write(rphy->grf, 0x4530,
				    GENMASK(18, 16) | 0x07);

		/*
		 * Set PHY1 A port squelch trigger point to 125mv
		 */
		ret |= regmap_write(rphy->grf, 0x4500,
				    GENMASK(30, 30) | 0x4000);
	}

	return ret;
}

static int rk3568_usb2phy_tuning(struct rockchip_usb2phy *rphy)
{
	int ret = 0;

	/* Turn off differential receiver by default to save power */
	phy_clear_bits(rphy->phy_base + 0x30, BIT(2));

	/* Enable otg port pre-emphasis during non-chirp phase */
	phy_update_bits(rphy->phy_base, GENMASK(2, 0), 0x04);

	/* Enable host port pre-emphasis during non-chirp phase */
	phy_update_bits(rphy->phy_base + 0x0400, GENMASK(2, 0), 0x04);

	if (rphy->phy_cfg->reg == 0xfe8a0000) {
		/* Set otg port HS eye height to 437.5mv(default is 400mv) */
		phy_update_bits(rphy->phy_base + 0x30, GENMASK(6, 4), (0x06 << 4));

		/*
		 * Set the bvalid filter time to 10ms
		 * based on the usb2 phy grf pclk 100MHz.
		 */
		ret |= regmap_write(rphy->grf, 0x0048, FILTER_COUNTER);

		/*
		 * Set the id filter time to 10ms based
		 * on the usb2 phy grf pclk 100MHz.
		 */
		ret |= regmap_write(rphy->grf, 0x004c, FILTER_COUNTER);
	}

	/* Enable host port (usb3 host1 and usb2 host1) wakeup irq */
	ret |= regmap_write(rphy->grf, 0x000c, 0x80008000);

	return ret;
}

static int rv1106_usb2phy_tuning(struct rockchip_usb2phy *rphy)
{
	/* Always enable pre-emphasis in SOF & EOP & chirp & non-chirp state */
	phy_update_bits(rphy->phy_base + 0x30, GENMASK(2, 0), 0x07);

	if (rockchip_get_cpu_version()) {
		/* Set Tx HS pre_emphasize strength to 3'b001 */
		phy_update_bits(rphy->phy_base + 0x40, GENMASK(5, 3), (0x01 << 3));
	} else {
		/* Set Tx HS pre_emphasize strength to 3'b011 */
		phy_update_bits(rphy->phy_base + 0x40, GENMASK(5, 3), (0x03 << 3));
	}

	/* Set RX Squelch trigger point configure to 4'b0000(112.5 mV) */
	phy_update_bits(rphy->phy_base + 0x64, GENMASK(6, 3), (0x00 << 3));

	/* Turn off differential receiver by default to save power */
	phy_clear_bits(rphy->phy_base + 0x100, BIT(6));

	/* Set 45ohm HS ODT value to 5'b10111 to increase driver strength */
	phy_update_bits(rphy->phy_base + 0x11c, GENMASK(4, 0), 0x17);

	/* Set Tx HS eye height tuning to 3'b011(462 mV)*/
	phy_update_bits(rphy->phy_base + 0x124, GENMASK(4, 2), (0x03 << 2));

	/* Bypass Squelch detector calibration */
	phy_update_bits(rphy->phy_base + 0x1a4, GENMASK(7, 4), (0x01 << 4));
	phy_update_bits(rphy->phy_base + 0x1b4, GENMASK(7, 4), (0x01 << 4));

	return 0;
}

static int rk3568_vbus_detect_control(struct rockchip_usb2phy *rphy, bool en)
{
	if (en) {
		/* Enable vbus voltage level detection function */
		phy_clear_bits(rphy->phy_base + 0x3c, BIT(7));
	} else {
		/* Disable vbus voltage level detection function */
		phy_set_bits(rphy->phy_base + 0x3c, BIT(7));
	}

	return 0;
}

static int rk3588_usb2phy_tuning(struct rockchip_usb2phy *rphy)
{
	unsigned int reg;
	int ret = 0;

	/* Read the SIDDQ control register */
	ret = regmap_read(rphy->grf, 0x0008, &reg);
	if (ret)
		return ret;

	if (reg & BIT(13)) {
		/* Deassert SIDDQ to power on analog block */
		ret = regmap_write(rphy->grf, 0x0008,
				   GENMASK(29, 29) | 0x0000);
		if (ret)
			return ret;

		/* Do reset after exit IDDQ mode */
		ret = rockchip_usb2phy_reset(rphy);
		if (ret)
			return ret;
	}

	if (rphy->phy_cfg->reg == 0x0000) {
		/*
		 * Set USB2 PHY0 suspend configuration for USB3_0
		 * 1. Set utmi_termselect to 1'b1 (en FS terminations)
		 * 2. Set utmi_xcvrselect to 2'b01 (FS transceiver)
		 * 3. Set utmi_opmode to 2'b01 (no-driving)
		 */
		ret |= regmap_write(rphy->grf, 0x000c,
				    GENMASK(20, 16) | 0x0015);

		/* HS DC Voltage Level Adjustment 4'b1001 : +5.89% */
		ret |= regmap_write(rphy->grf, 0x0004,
				   GENMASK(27, 24) | 0x0900);

		/* HS Transmitter Pre-Emphasis Current Control 2'b10 : 2x */
		ret |= regmap_write(rphy->grf, 0x0008,
				   GENMASK(20, 19) | 0x0010);

		/* Pullup iddig pin for USB3_0 OTG mode */
		ret |= regmap_write(rphy->grf, 0x0010,
				    GENMASK(17, 16) | 0x0003);
	} else if (rphy->phy_cfg->reg == 0x4000) {
		/*
		 * Set USB2 PHY1 suspend configuration for USB3_1
		 * 1. Set utmi_termselect to 1'b1 (en FS terminations)
		 * 2. Set utmi_xcvrselect to 2'b01(FS transceiver)
		 * 3. Set utmi_opmode to 2'b01 (no-driving)
		 */
		ret |= regmap_write(rphy->grf, 0x000c,
				    GENMASK(20, 16) | 0x0015);

		/* HS DC Voltage Level Adjustment 4'b1001 : +5.89% */
		ret |= regmap_write(rphy->grf, 0x0004,
				   GENMASK(27, 24) | 0x0900);

		/* HS Transmitter Pre-Emphasis Current Control 2'b10 : 2x */
		ret |= regmap_write(rphy->grf, 0x0008,
				   GENMASK(20, 19) | 0x0010);

		/* Pullup iddig pin for USB3_1 OTG mode */
		ret |= regmap_write(rphy->grf, 0x0010,
				    GENMASK(17, 16) | 0x0003);
	} else if (rphy->phy_cfg->reg == 0x8000) {
		/*
		 * Set USB2 PHY2 suspend configuration for USB2_0
		 * 1. Set utmi_termselect to 1'b1 (en FS terminations)
		 * 2. Set utmi_xcvrselect to 2'b01(FS transceiver)
		 * 3. Set utmi_opmode to 2'b00 (normal)
		 */
		ret |= regmap_write(rphy->grf, 0x000c,
				    GENMASK(20, 16) | 0x0014);

		/* HS DC Voltage Level Adjustment 4'b1001 : +5.89% */
		ret |= regmap_write(rphy->grf, 0x0004,
				   GENMASK(27, 24) | 0x0900);

		/* HS Transmitter Pre-Emphasis Current Control 2'b10 : 2x */
		ret |= regmap_write(rphy->grf, 0x0008,
				   GENMASK(20, 19) | 0x0010);
	} else if (rphy->phy_cfg->reg == 0xc000) {
		/*
		 * Set USB2 PHY3 suspend configuration for USB2_1
		 * 1. Set utmi_termselect to 1'b1 (en FS terminations)
		 * 2. Set utmi_xcvrselect to 2'b01(FS transceiver)
		 * 3. Set utmi_opmode to 2'b00 (normal)
		 */
		ret |= regmap_write(rphy->grf, 0x000c,
				    GENMASK(20, 16) | 0x0014);

		/* HS DC Voltage Level Adjustment 4'b1001 : +5.89% */
		ret |= regmap_write(rphy->grf, 0x0004,
				   GENMASK(27, 24) | 0x0900);

		/* HS Transmitter Pre-Emphasis Current Control 2'b10 : 2x */
		ret |= regmap_write(rphy->grf, 0x0008,
				   GENMASK(20, 19) | 0x0010);
	}

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int rockchip_usb2phy_pm_suspend(struct device *dev)
{
	struct rockchip_usb2phy *rphy = dev_get_drvdata(dev);
	const struct rockchip_usb2phy_cfg *phy_cfg = rphy->phy_cfg;
	struct rockchip_usb2phy_port *rport;
	unsigned int index;
	int ret = 0;
	bool wakeup_enable = false;

	if (device_may_wakeup(rphy->dev))
		wakeup_enable = true;

	/*
	 * Set the linestate filter time to 1ms based
	 * on the usb2 phy grf pclk 32KHz on suspend.
	 */
	if (phy_cfg->ls_filter_con.enable) {
		ret = regmap_write(rphy->grf, phy_cfg->ls_filter_con.offset,
				   phy_cfg->ls_filter_con.enable);
		if (ret)
			dev_err(rphy->dev, "failed to set ls filter %d\n", ret);
	}

	for (index = 0; index < phy_cfg->num_ports; index++) {
		rport = &rphy->ports[index];
		if (!rport->phy)
			continue;

		if (rport->port_id == USB2PHY_PORT_OTG &&
		    (rport->id_irq > 0 || rphy->irq > 0)) {
			mutex_lock(&rport->mutex);
			rport->prev_iddig = property_enabled(rphy->grf,
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

		/* activate the linestate to detect the next interrupt. */
		mutex_lock(&rport->mutex);
		ret = rockchip_usb2phy_enable_line_irq(rphy, rport, true);
		mutex_unlock(&rport->mutex);
		if (ret) {
			dev_err(rphy->dev, "failed to enable linestate irq\n");
			return ret;
		}

		if (wakeup_enable && rport->ls_irq > 0)
			enable_irq_wake(rport->ls_irq);

		/* enter low power state */
		rockchip_usb2phy_low_power_enable(rphy, rport, true);
	}

	if (wakeup_enable && rphy->irq > 0)
		enable_irq_wake(rphy->irq);

	return ret;
}

static int rockchip_usb2phy_pm_resume(struct device *dev)
{
	struct rockchip_usb2phy *rphy = dev_get_drvdata(dev);
	const struct rockchip_usb2phy_cfg *phy_cfg = rphy->phy_cfg;
	struct rockchip_usb2phy_port *rport;
	unsigned int index;
	bool iddig;
	int ret = 0;
	bool wakeup_enable = false;

	if (device_may_wakeup(rphy->dev))
		wakeup_enable = true;

	/*
	 * PHY lost power in suspend, it needs to reset
	 * PHY to recovery clock to usb controller.
	 */
	if (!wakeup_enable)
		rockchip_usb2phy_reset(rphy);

	if (phy_cfg->phy_tuning)
		ret = phy_cfg->phy_tuning(rphy);

	if (phy_cfg->ls_filter_con.disable) {
		ret = regmap_write(rphy->grf, phy_cfg->ls_filter_con.offset,
				   phy_cfg->ls_filter_con.disable);
		if (ret)
			dev_err(rphy->dev, "failed to set ls filter %d\n", ret);
	}

	for (index = 0; index < phy_cfg->num_ports; index++) {
		rport = &rphy->ports[index];
		if (!rport->phy)
			continue;

		if (rport->port_id == USB2PHY_PORT_OTG &&
		    (rport->id_irq > 0 || rphy->irq > 0)) {
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

		/* exit low power state */
		rockchip_usb2phy_low_power_enable(rphy, rport, false);
	}

	if (wakeup_enable && rphy->irq > 0)
		disable_irq_wake(rphy->irq);

	return ret;
}

static const struct dev_pm_ops rockchip_usb2phy_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_usb2phy_pm_suspend,
				rockchip_usb2phy_pm_resume)
};

#define ROCKCHIP_USB2PHY_DEV_PM	(&rockchip_usb2phy_dev_pm_ops)
#else
#define ROCKCHIP_USB2PHY_DEV_PM	NULL
#endif /* CONFIG_PM_SLEEP */

static const struct rockchip_usb2phy_cfg rk1808_phy_cfgs[] = {
	{
		.reg = 0x100,
		.num_ports	= 2,
		.clkout_ctl	= { 0x108, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x0100, 8, 0, 0, 0x1d1 },
				.bvalid_det_en	= { 0x0110, 2, 2, 0, 1 },
				.bvalid_det_st	= { 0x0114, 2, 2, 0, 1 },
				.bvalid_det_clr = { 0x0118, 2, 2, 0, 1 },
				.bypass_dm_en	= { 0x0108, 2, 2, 0, 1},
				.bypass_sel	= { 0x0108, 3, 3, 0, 1},
				.iddig_output	= { 0x0100, 10, 10, 0, 1 },
				.iddig_en	= { 0x0100, 9, 9, 0, 1 },
				.idfall_det_en	= { 0x0110, 5, 5, 0, 1 },
				.idfall_det_st	= { 0x0114, 5, 5, 0, 1 },
				.idfall_det_clr = { 0x0118, 5, 5, 0, 1 },
				.idrise_det_en	= { 0x0110, 4, 4, 0, 1 },
				.idrise_det_st	= { 0x0114, 4, 4, 0, 1 },
				.idrise_det_clr = { 0x0118, 4, 4, 0, 1 },
				.ls_det_en	= { 0x0110, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0114, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0118, 0, 0, 0, 1 },
				.utmi_avalid	= { 0x0120, 10, 10, 0, 1 },
				.utmi_bvalid	= { 0x0120, 9, 9, 0, 1 },
				.utmi_iddig	= { 0x0120, 6, 6, 0, 1 },
				.utmi_ls	= { 0x0120, 5, 4, 0, 1 },
				.vbus_det_en	= { 0x001c, 15, 15, 1, 0 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x104, 8, 0, 0, 0x1d1 },
				.ls_det_en	= { 0x110, 1, 1, 0, 1 },
				.ls_det_st	= { 0x114, 1, 1, 0, 1 },
				.ls_det_clr	= { 0x118, 1, 1, 0, 1 },
				.utmi_ls	= { 0x120, 17, 16, 0, 1 },
				.utmi_hstdet	= { 0x120, 19, 19, 0, 1 }
			}
		},
		.chg_det = {
			.chg_mode	= { 0x0100, 8, 0, 0, 0x1d7 },
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

static const struct rockchip_usb2phy_cfg rk312x_phy_cfgs[] = {
	{
		.reg = 0x17c,
		.num_ports	= 2,
		.phy_tuning	= rk312x_usb2phy_tuning,
		.clkout_ctl	= { 0x0190, 15, 15, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x017c, 8, 0, 0, 0x1d1 },
				.bvalid_det_en	= { 0x017c, 14, 14, 0, 1 },
				.bvalid_det_st	= { 0x017c, 15, 15, 0, 1 },
				.bvalid_det_clr	= { 0x017c, 15, 15, 0, 1 },
				.bypass_dm_en	= { 0x0190, 12, 12, 0, 1},
				.bypass_sel	= { 0x0190, 13, 13, 0, 1},
				.iddig_output	= { 0x017c, 10, 10, 0, 1 },
				.iddig_en	= { 0x017c, 9, 9, 0, 1 },
				.idfall_det_en  = { 0x01a0, 2, 2, 0, 1 },
				.idfall_det_st  = { 0x01a0, 3, 3, 0, 1 },
				.idfall_det_clr = { 0x01a0, 3, 3, 0, 1 },
				.idrise_det_en  = { 0x01a0, 0, 0, 0, 1 },
				.idrise_det_st  = { 0x01a0, 1, 1, 0, 1 },
				.idrise_det_clr = { 0x01a0, 1, 1, 0, 1 },
				.ls_det_en	= { 0x017c, 12, 12, 0, 1 },
				.ls_det_st	= { 0x017c, 13, 13, 0, 1 },
				.ls_det_clr	= { 0x017c, 13, 13, 0, 1 },
				.utmi_bvalid	= { 0x014c, 5, 5, 0, 1 },
				.utmi_iddig	= { 0x014c, 8, 8, 0, 1 },
				.utmi_ls	= { 0x014c, 7, 6, 0, 1 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0194, 8, 0, 0, 0x1d1 },
				.ls_det_en	= { 0x0194, 14, 14, 0, 1 },
				.ls_det_st	= { 0x0194, 15, 15, 0, 1 },
				.ls_det_clr	= { 0x0194, 15, 15, 0, 1 }
			}
		},
		.chg_det = {
			.chg_mode	= { 0x017c, 8, 0, 0, 0x1d7 },
			.cp_det		= { 0x02c0, 6, 6, 0, 1 },
			.dcp_det	= { 0x02c0, 5, 5, 0, 1 },
			.dp_det		= { 0x02c0, 7, 7, 0, 1 },
			.idm_sink_en	= { 0x0184, 8, 8, 0, 1 },
			.idp_sink_en	= { 0x0184, 7, 7, 0, 1 },
			.idp_src_en	= { 0x0184, 9, 9, 0, 1 },
			.rdm_pdwn_en	= { 0x0184, 10, 10, 0, 1 },
			.vdm_src_en	= { 0x0184, 12, 12, 0, 1 },
			.vdp_src_en	= { 0x0184, 11, 11, 0, 1 },
		},
	},
	{ /* sentinel */ }
};

static const struct rockchip_usb2phy_cfg rk3228_phy_cfgs[] = {
	{
		.reg = 0x760,
		.num_ports	= 2,
		.phy_tuning	= rk3228_usb2phy_tuning,
		.clkout_ctl	= { 0x0768, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x0760, 8, 0, 0, 0x1d1 },
				.bvalid_det_en	= { 0x0680, 3, 3, 0, 1 },
				.bvalid_det_st	= { 0x0690, 3, 3, 0, 1 },
				.bvalid_det_clr	= { 0x06a0, 3, 3, 0, 1 },
				.iddig_output   = { 0x0760, 10, 10, 0, 1 },
				.iddig_en       = { 0x0760, 9, 9, 0, 1 },
				.idfall_det_en	= { 0x0680, 6, 6, 0, 1 },
				.idfall_det_st	= { 0x0690, 6, 6, 0, 1 },
				.idfall_det_clr	= { 0x06a0, 6, 6, 0, 1 },
				.idrise_det_en	= { 0x0680, 5, 5, 0, 1 },
				.idrise_det_st	= { 0x0690, 5, 5, 0, 1 },
				.idrise_det_clr	= { 0x06a0, 5, 5, 0, 1 },
				.ls_det_en	= { 0x0680, 2, 2, 0, 1 },
				.ls_det_st	= { 0x0690, 2, 2, 0, 1 },
				.ls_det_clr	= { 0x06a0, 2, 2, 0, 1 },
				.utmi_bvalid	= { 0x0480, 4, 4, 0, 1 },
				.utmi_iddig	= { 0x0480, 1, 1, 0, 1 },
				.utmi_ls	= { 0x0480, 3, 2, 0, 1 },
				.vbus_det_en	= { 0x0788, 15, 15, 1, 0 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0764, 8, 0, 0, 0x1d1 },
				.ls_det_en	= { 0x0680, 4, 4, 0, 1 },
				.ls_det_st	= { 0x0690, 4, 4, 0, 1 },
				.ls_det_clr	= { 0x06a0, 4, 4, 0, 1 }
			}
		},
		.chg_det = {
			.chg_mode	= { 0x0760, 8, 0, 0, 0x1d7 },
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
				.phy_sus	= { 0x804, 8, 0, 0, 0x1d1 },
				.ls_det_en	= { 0x0684, 1, 1, 0, 1 },
				.ls_det_st	= { 0x0694, 1, 1, 0, 1 },
				.ls_det_clr	= { 0x06a4, 1, 1, 0, 1 }
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x800, 8, 0, 0, 0x1d1 },
				.ls_det_en	= { 0x0684, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0694, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x06a4, 0, 0, 0, 1 }
			}
		},
	},
	{ /* sentinel */ }
};

static const struct rockchip_usb2phy_cfg rk3308_phy_cfgs[] = {
	{
		.reg = 0x100,
		.num_ports	= 2,
		.phy_tuning	= rk3308_usb2phy_tuning,
		.clkout_ctl	= { 0x0108, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x0100, 8, 0, 0, 0x1d1 },
				.bvalid_det_en	= { 0x3020, 2, 2, 0, 1 },
				.bvalid_det_st	= { 0x3024, 2, 2, 0, 1 },
				.bvalid_det_clr = { 0x3028, 2, 2, 0, 1 },
				.iddig_output	= { 0x0100, 10, 10, 0, 1 },
				.iddig_en	= { 0x0100, 9, 9, 0, 1 },
				.idfall_det_en	= { 0x3020, 5, 5, 0, 1 },
				.idfall_det_st	= { 0x3024, 5, 5, 0, 1 },
				.idfall_det_clr = { 0x3028, 5, 5, 0, 1 },
				.idrise_det_en	= { 0x3020, 4, 4, 0, 1 },
				.idrise_det_st	= { 0x3024, 4, 4, 0, 1 },
				.idrise_det_clr = { 0x3028, 4, 4, 0, 1 },
				.ls_det_en	= { 0x3020, 0, 0, 0, 1 },
				.ls_det_st	= { 0x3024, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x3028, 0, 0, 0, 1 },
				.utmi_avalid	= { 0x0120, 10, 10, 0, 1 },
				.utmi_bvalid	= { 0x0120, 9, 9, 0, 1 },
				.utmi_iddig	= { 0x0120, 6, 6, 0, 1 },
				.utmi_ls	= { 0x0120, 5, 4, 0, 1 },
				.vbus_det_en	= { 0x001c, 15, 15, 1, 0 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0104, 8, 0, 0, 0x1d1 },
				.ls_det_en	= { 0x3020, 1, 1, 0, 1 },
				.ls_det_st	= { 0x3024, 1, 1, 0, 1 },
				.ls_det_clr	= { 0x3028, 1, 1, 0, 1 },
				.utmi_ls	= { 0x120, 17, 16, 0, 1 },
				.utmi_hstdet	= { 0x120, 19, 19, 0, 1 }
			}
		},
		.chg_det = {
			.chg_mode	= { 0x0100, 8, 0, 0, 0x1d7 },
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

static const struct rockchip_usb2phy_cfg rk3328_phy_cfgs[] = {
	{
		.reg = 0x100,
		.num_ports	= 2,
		.phy_tuning = rk3328_usb2phy_tuning,
		.clkout_ctl	= { 0x108, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x0100, 8, 0, 0, 0x1d1 },
				.bvalid_det_en	= { 0x0110, 2, 2, 0, 1 },
				.bvalid_det_st	= { 0x0114, 2, 2, 0, 1 },
				.bvalid_det_clr = { 0x0118, 2, 2, 0, 1 },
				.bypass_bc      = { 0x0008, 14, 14, 0, 1 },
				.bypass_otg     = { 0x0018, 15, 15, 1, 0 },
				.iddig_output   = { 0x0100, 10, 10, 0, 1 },
				.iddig_en       = { 0x0100, 9, 9, 0, 1 },
				.idfall_det_en	= { 0x0110, 5, 5, 0, 1 },
				.idfall_det_st	= { 0x0114, 5, 5, 0, 1 },
				.idfall_det_clr = { 0x0118, 5, 5, 0, 1 },
				.idrise_det_en	= { 0x0110, 4, 4, 0, 1 },
				.idrise_det_st	= { 0x0114, 4, 4, 0, 1 },
				.idrise_det_clr = { 0x0118, 4, 4, 0, 1 },
				.ls_det_en	= { 0x0110, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0114, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0118, 0, 0, 0, 1 },
				.utmi_avalid	= { 0x0120, 10, 10, 0, 1 },
				.utmi_bvalid	= { 0x0120, 9, 9, 0, 1 },
				.utmi_iddig	= { 0x0120, 6, 6, 0, 1 },
				.utmi_ls	= { 0x0120, 5, 4, 0, 1 },
				.vbus_det_en	= { 0x001c, 15, 15, 1, 0 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x104, 8, 0, 0, 0x1d1 },
				.bypass_host	= { 0x048, 15, 15, 1, 0 },
				.ls_det_en	= { 0x110, 1, 1, 0, 1 },
				.ls_det_st	= { 0x114, 1, 1, 0, 1 },
				.ls_det_clr	= { 0x118, 1, 1, 0, 1 },
				.utmi_ls	= { 0x120, 17, 16, 0, 1 },
				.utmi_hstdet	= { 0x120, 19, 19, 0, 1 }
			}
		},
		.chg_det = {
			.chg_mode	= { 0x0100, 8, 0, 0, 0x1d7 },
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
		.phy_tuning	= rk3366_usb2phy_tuning,
		.clkout_ctl	= { 0x0724, 15, 15, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0728, 8, 0, 0, 0x1d1 },
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

static const struct rockchip_usb2phy_cfg rk3368_phy_cfgs[] = {
	{
		.reg = 0x700,
		.num_ports	= 2,
		.clkout_ctl	= { 0x0724, 15, 15, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x0700, 8, 0, 0, 0x1d1 },
				.bvalid_det_en	= { 0x0680, 3, 3, 0, 1 },
				.bvalid_det_st	= { 0x0690, 3, 3, 0, 1 },
				.bvalid_det_clr = { 0x06a0, 3, 3, 0, 1 },
				.iddig_output	= { 0x0700, 10, 10, 0, 1 },
				.iddig_en	= { 0x0700, 9, 9, 0, 1 },
				.idfall_det_en	= { 0x0680, 6, 6, 0, 1 },
				.idfall_det_st	= { 0x0690, 6, 6, 0, 1 },
				.idfall_det_clr	= { 0x06a0, 6, 6, 0, 1 },
				.idrise_det_en	= { 0x0680, 5, 5, 0, 1 },
				.idrise_det_st	= { 0x0690, 5, 5, 0, 1 },
				.idrise_det_clr	= { 0x06a0, 5, 5, 0, 1 },
				.ls_det_en	= { 0x0680, 2, 2, 0, 1 },
				.ls_det_st	= { 0x0690, 2, 2, 0, 1 },
				.ls_det_clr	= { 0x06a0, 2, 2, 0, 1 },
				.utmi_bvalid	= { 0x04bc, 23, 23, 0, 1 },
				.utmi_iddig     = { 0x04bc, 26, 26, 0, 1 },
				.utmi_ls	= { 0x04bc, 25, 24, 0, 1 },
				.vbus_det_en    = { 0x079c, 15, 15, 1, 0 },
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0728, 15, 0, 0, 0x1d1 },
				.ls_det_en	= { 0x0680, 4, 4, 0, 1 },
				.ls_det_st	= { 0x0690, 4, 4, 0, 1 },
				.ls_det_clr	= { 0x06a0, 4, 4, 0, 1 }
			}
		},
		.chg_det = {
			.chg_mode	= { 0x0700, 8, 0, 0, 0x1d7 },
			.cp_det		= { 0x04b8, 30, 30, 0, 1 },
			.dcp_det	= { 0x04b8, 29, 29, 0, 1 },
			.dp_det		= { 0x04b8, 31, 31, 0, 1 },
			.idm_sink_en	= { 0x0718, 8, 8, 0, 1 },
			.idp_sink_en	= { 0x0718, 7, 7, 0, 1 },
			.idp_src_en	= { 0x0718, 9, 9, 0, 1 },
			.rdm_pdwn_en	= { 0x0718, 10, 10, 0, 1 },
			.vdm_src_en	= { 0x0718, 12, 12, 0, 1 },
			.vdp_src_en	= { 0x0718, 11, 11, 0, 1 },
		},
	},
	{ /* sentinel */ }
};

static const struct rockchip_usb2phy_cfg rk3399_phy_cfgs[] = {
	{
		.reg		= 0xe450,
		.num_ports	= 2,
		.phy_tuning	= rk3399_usb2phy_tuning,
		.clkout_ctl	= { 0xe450, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus = { 0xe454, 8, 0, 0x052, 0x1d1 },
				.bvalid_det_en	= { 0xe3c0, 3, 3, 0, 1 },
				.bvalid_det_st	= { 0xe3e0, 3, 3, 0, 1 },
				.bvalid_det_clr	= { 0xe3d0, 3, 3, 0, 1 },
				.bypass_dm_en   = { 0xe450, 2, 2, 0, 1 },
				.bypass_sel     = { 0xe450, 3, 3, 0, 1 },
				.iddig_output	= { 0xe454, 10, 10, 0, 1 },
				.iddig_en	= { 0xe454, 9, 9, 0, 1 },
				.idfall_det_en	= { 0xe3c0, 5, 5, 0, 1 },
				.idfall_det_st	= { 0xe3e0, 5, 5, 0, 1 },
				.idfall_det_clr	= { 0xe3d0, 5, 5, 0, 1 },
				.idrise_det_en	= { 0xe3c0, 4, 4, 0, 1 },
				.idrise_det_st	= { 0xe3e0, 4, 4, 0, 1 },
				.idrise_det_clr	= { 0xe3d0, 4, 4, 0, 1 },
				.ls_det_en	= { 0xe3c0, 2, 2, 0, 1 },
				.ls_det_st	= { 0xe3e0, 2, 2, 0, 1 },
				.ls_det_clr	= { 0xe3d0, 2, 2, 0, 1 },
				.utmi_avalid	= { 0xe2ac, 7, 7, 0, 1 },
				.utmi_bvalid	= { 0xe2ac, 12, 12, 0, 1 },
				.utmi_iddig     = { 0xe2ac, 8, 8, 0, 1 },
				.utmi_ls	= { 0xe2ac, 14, 13, 0, 1 },
				.vbus_det_en    = { 0x449c, 15, 15, 1, 0 },
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
			.chg_mode	= { 0xe454, 8, 0, 0, 0x1d7 },
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
		.phy_tuning	= rk3399_usb2phy_tuning,
		.clkout_ctl	= { 0xe460, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus = { 0xe464, 8, 0, 0x052, 0x1d1 },
				.bvalid_det_en  = { 0xe3c0, 8, 8, 0, 1 },
				.bvalid_det_st  = { 0xe3e0, 8, 8, 0, 1 },
				.bvalid_det_clr = { 0xe3d0, 8, 8, 0, 1 },
				.iddig_output	= { 0xe464, 10, 10, 0, 1 },
				.iddig_en	= { 0xe464, 9, 9, 0, 1 },
				.idfall_det_en	= { 0xe3c0, 10, 10, 0, 1 },
				.idfall_det_st	= { 0xe3e0, 10, 10, 0, 1 },
				.idfall_det_clr	= { 0xe3d0, 10, 10, 0, 1 },
				.idrise_det_en	= { 0xe3c0, 9, 9, 0, 1 },
				.idrise_det_st	= { 0xe3e0, 9, 9, 0, 1 },
				.idrise_det_clr	= { 0xe3d0, 9, 9, 0, 1 },
				.ls_det_en	= { 0xe3c0, 7, 7, 0, 1 },
				.ls_det_st	= { 0xe3e0, 7, 7, 0, 1 },
				.ls_det_clr	= { 0xe3d0, 7, 7, 0, 1 },
				.utmi_avalid	= { 0xe2ac, 10, 10, 0, 1 },
				.utmi_bvalid    = { 0xe2ac, 16, 16, 0, 1 },
				.utmi_iddig     = { 0xe2ac, 11, 11, 0, 1 },
				.utmi_ls	= { 0xe2ac, 18, 17, 0, 1 },
				.vbus_det_en    = { 0x451c, 15, 15, 1, 0 },
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
		.chg_det = {
			.chg_mode	= { 0xe464, 8, 0, 0, 0x1d7 },
			.cp_det		= { 0xe2ac, 5, 5, 0, 1 },
			.dcp_det	= { 0xe2ac, 4, 4, 0, 1 },
			.dp_det		= { 0xe2ac, 3, 3, 0, 1 },
			.idm_sink_en	= { 0xe460, 8, 8, 0, 1 },
			.idp_sink_en	= { 0xe460, 7, 7, 0, 1 },
			.idp_src_en	= { 0xe460, 9, 9, 0, 1 },
			.rdm_pdwn_en	= { 0xe460, 10, 10, 0, 1 },
			.vdm_src_en	= { 0xe460, 12, 12, 0, 1 },
			.vdp_src_en	= { 0xe460, 11, 11, 0, 1 },
		},
	},
	{ /* sentinel */ }
};

static const struct rockchip_usb2phy_cfg rk3568_phy_cfgs[] = {
	{
		.reg = 0xfe8a0000,
		.num_ports	= 2,
		.phy_tuning	= rk3568_usb2phy_tuning,
		.vbus_detect	= rk3568_vbus_detect_control,
		.clkout_ctl	= { 0x0008, 4, 4, 1, 0 },
		.ls_filter_con	= { 0x0040, 19, 0, 0x30100, 0x00020 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x0000, 8, 0, 0, 0x1d1 },
				.bvalid_det_en	= { 0x0080, 2, 2, 0, 1 },
				.bvalid_det_st	= { 0x0084, 2, 2, 0, 1 },
				.bvalid_det_clr = { 0x0088, 2, 2, 0, 1 },
				.bvalid_grf_sel	= { 0x0008, 15, 14, 0, 3 },
				.bypass_dm_en	= { 0x0008, 2, 2, 0, 1},
				.bypass_sel	= { 0x0008, 3, 3, 0, 1},
				.iddig_output	= { 0x0000, 10, 10, 0, 1 },
				.iddig_en	= { 0x0000, 9, 9, 0, 1 },
				.idfall_det_en	= { 0x0080, 5, 5, 0, 1 },
				.idfall_det_st	= { 0x0084, 5, 5, 0, 1 },
				.idfall_det_clr = { 0x0088, 5, 5, 0, 1 },
				.idrise_det_en	= { 0x0080, 4, 4, 0, 1 },
				.idrise_det_st	= { 0x0084, 4, 4, 0, 1 },
				.idrise_det_clr = { 0x0088, 4, 4, 0, 1 },
				.ls_det_en	= { 0x0080, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0084, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0088, 0, 0, 0, 1 },
				.utmi_avalid	= { 0x00c0, 10, 10, 0, 1 },
				.utmi_bvalid	= { 0x00c0, 9, 9, 0, 1 },
				.utmi_iddig	= { 0x00c0, 6, 6, 0, 1 },
				.utmi_ls	= { 0x00c0, 5, 4, 0, 1 },
			},
			[USB2PHY_PORT_HOST] = {
				/* Select suspend control from controller */
				.phy_sus	= { 0x0004, 8, 0, 0x1d2, 0x1d2 },
				.ls_det_en	= { 0x0080, 1, 1, 0, 1 },
				.ls_det_st	= { 0x0084, 1, 1, 0, 1 },
				.ls_det_clr	= { 0x0088, 1, 1, 0, 1 },
				.utmi_ls	= { 0x00c0, 17, 16, 0, 1 },
				.utmi_hstdet	= { 0x00c0, 19, 19, 0, 1 }
			}
		},
		.chg_det = {
			.chg_mode	= { 0x0000, 8, 0, 0, 0x1d7 },
			.cp_det		= { 0x00c0, 24, 24, 0, 1 },
			.dcp_det	= { 0x00c0, 23, 23, 0, 1 },
			.dp_det		= { 0x00c0, 25, 25, 0, 1 },
			.idm_sink_en	= { 0x0008, 8, 8, 0, 1 },
			.idp_sink_en	= { 0x0008, 7, 7, 0, 1 },
			.idp_src_en	= { 0x0008, 9, 9, 0, 1 },
			.rdm_pdwn_en	= { 0x0008, 10, 10, 0, 1 },
			.vdm_src_en	= { 0x0008, 12, 12, 0, 1 },
			.vdp_src_en	= { 0x0008, 11, 11, 0, 1 },
		},
	},
	{
		.reg = 0xfe8b0000,
		.num_ports	= 2,
		.phy_tuning	= rk3568_usb2phy_tuning,
		.clkout_ctl	= { 0x0008, 4, 4, 1, 0 },
		.ls_filter_con	= { 0x0040, 19, 0, 0x30100, 0x00020 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x0000, 8, 0, 0x1d2, 0x1d1 },
				.ls_det_en	= { 0x0080, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0084, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0088, 0, 0, 0, 1 },
				.utmi_ls	= { 0x00c0, 5, 4, 0, 1 },
				.utmi_hstdet	= { 0x00c0, 7, 7, 0, 1 }
			},
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0004, 8, 0, 0x1d2, 0x1d1 },
				.ls_det_en	= { 0x0080, 1, 1, 0, 1 },
				.ls_det_st	= { 0x0084, 1, 1, 0, 1 },
				.ls_det_clr	= { 0x0088, 1, 1, 0, 1 },
				.utmi_ls	= { 0x00c0, 17, 16, 0, 1 },
				.utmi_hstdet	= { 0x00c0, 19, 19, 0, 1 }
			}
		},
	},
	{ /* sentinel */ }
};

static const struct rockchip_usb2phy_cfg rk3588_phy_cfgs[] = {
	{
		.reg = 0x0000,
		.num_ports	= 1,
		.phy_tuning	= rk3588_usb2phy_tuning,
		.clkout_ctl	= { 0x0000, 0, 0, 1, 0 },
		.ls_filter_con	= { 0x0040, 19, 0, 0x30100, 0x00020 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x000c, 11, 11, 0, 1 },
				.pipe_phystatus	= { 0x001c, 3, 2, 0, 2 },
				.bvalid_det_en	= { 0x0080, 1, 1, 0, 1 },
				.bvalid_det_st	= { 0x0084, 1, 1, 0, 1 },
				.bvalid_det_clr = { 0x0088, 1, 1, 0, 1 },
				.bvalid_grf_sel	= { 0x0010, 3, 3, 0, 1 },
				.bvalid_grf_con	= { 0x0010, 3, 2, 2, 3 },
				.bvalid_phy_con	= { 0x0008, 1, 0, 2, 3 },
				.bypass_dm_en	= { 0x000c, 5, 5, 0, 1 },
				.bypass_sel	= { 0x000c, 6, 6, 0, 1 },
				.iddig_output	= { 0x0010, 0, 0, 0, 1 },
				.iddig_en	= { 0x0010, 1, 1, 0, 1 },
				.idfall_det_en	= { 0x0080, 4, 4, 0, 1 },
				.idfall_det_st	= { 0x0084, 4, 4, 0, 1 },
				.idfall_det_clr = { 0x0088, 4, 4, 0, 1 },
				.idrise_det_en	= { 0x0080, 3, 3, 0, 1 },
				.idrise_det_st	= { 0x0084, 3, 3, 0, 1 },
				.idrise_det_clr = { 0x0088, 3, 3, 0, 1 },
				.ls_det_en	= { 0x0080, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0084, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0088, 0, 0, 0, 1 },
				.disfall_en	= { 0x0080, 6, 6, 0, 1 },
				.disfall_st	= { 0x0084, 6, 6, 0, 1 },
				.disfall_clr	= { 0x0088, 6, 6, 0, 1 },
				.disrise_en	= { 0x0080, 5, 5, 0, 1 },
				.disrise_st	= { 0x0084, 5, 5, 0, 1 },
				.disrise_clr	= { 0x0088, 5, 5, 0, 1 },
				.utmi_avalid	= { 0x00c0, 7, 7, 0, 1 },
				.utmi_bvalid	= { 0x00c0, 6, 6, 0, 1 },
				.utmi_iddig	= { 0x00c0, 5, 5, 0, 1 },
				.utmi_ls	= { 0x00c0, 10, 9, 0, 1 },
			}
		},
		.chg_det = {
			.chg_mode	= { 0x0008, 2, 2, 0, 1 },
			.cp_det		= { 0x00c0, 0, 0, 0, 1 },
			.dcp_det	= { 0x00c0, 0, 0, 0, 1 },
			.dp_det		= { 0x00c0, 1, 1, 1, 0 },
			.idm_sink_en	= { 0x0008, 5, 5, 1, 0 },
			.idp_sink_en	= { 0x0008, 5, 5, 0, 1 },
			.idp_src_en	= { 0x0008, 14, 14, 0, 1 },
			.rdm_pdwn_en	= { 0x0008, 14, 14, 0, 1 },
			.vdm_src_en	= { 0x0008, 7, 6, 0, 3 },
			.vdp_src_en	= { 0x0008, 7, 6, 0, 3 },
		},
	},
	{
		.reg = 0x4000,
		.num_ports	= 1,
		.phy_tuning	= rk3588_usb2phy_tuning,
		.clkout_ctl	= { 0x0000, 0, 0, 1, 0 },
		.ls_filter_con	= { 0x0040, 19, 0, 0x30100, 0x00020 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x000c, 11, 11, 0, 1 },
				.pipe_phystatus	= { 0x0034, 3, 2, 0, 2 },
				.bvalid_det_en	= { 0x0080, 1, 1, 0, 1 },
				.bvalid_det_st	= { 0x0084, 1, 1, 0, 1 },
				.bvalid_det_clr = { 0x0088, 1, 1, 0, 1 },
				.bvalid_grf_sel	= { 0x0010, 3, 3, 0, 1 },
				.bvalid_grf_con	= { 0x0010, 3, 2, 2, 3 },
				.bvalid_phy_con = { 0x0008, 1, 0, 2, 3 },
				.bypass_dm_en	= { 0x000c, 5, 5, 0, 1 },
				.bypass_sel	= { 0x000c, 6, 6, 0, 1 },
				.iddig_output	= { 0x0010, 0, 0, 0, 1 },
				.iddig_en	= { 0x0010, 1, 1, 0, 1 },
				.idfall_det_en	= { 0x0080, 4, 4, 0, 1 },
				.idfall_det_st	= { 0x0084, 4, 4, 0, 1 },
				.idfall_det_clr = { 0x0088, 4, 4, 0, 1 },
				.idrise_det_en	= { 0x0080, 3, 3, 0, 1 },
				.idrise_det_st	= { 0x0084, 3, 3, 0, 1 },
				.idrise_det_clr = { 0x0088, 3, 3, 0, 1 },
				.ls_det_en	= { 0x0080, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0084, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0088, 0, 0, 0, 1 },
				.disfall_en	= { 0x0080, 6, 6, 0, 1 },
				.disfall_st	= { 0x0084, 6, 6, 0, 1 },
				.disfall_clr	= { 0x0088, 6, 6, 0, 1 },
				.disrise_en	= { 0x0080, 5, 5, 0, 1 },
				.disrise_st	= { 0x0084, 5, 5, 0, 1 },
				.disrise_clr	= { 0x0088, 5, 5, 0, 1 },
				.utmi_avalid	= { 0x00c0, 7, 7, 0, 1 },
				.utmi_bvalid	= { 0x00c0, 6, 6, 0, 1 },
				.utmi_iddig	= { 0x00c0, 5, 5, 0, 1 },
				.utmi_ls	= { 0x00c0, 10, 9, 0, 1 },
			}
		},
		.chg_det = {
			.chg_mode	= { 0x0008, 2, 2, 0, 1 },
			.cp_det		= { 0x00c0, 0, 0, 0, 1 },
			.dcp_det	= { 0x00c0, 0, 0, 0, 1 },
			.dp_det		= { 0x00c0, 1, 1, 1, 0 },
			.idm_sink_en	= { 0x0008, 5, 5, 1, 0 },
			.idp_sink_en	= { 0x0008, 5, 5, 0, 1 },
			.idp_src_en	= { 0x0008, 14, 14, 0, 1 },
			.rdm_pdwn_en	= { 0x0008, 14, 14, 0, 1 },
			.vdm_src_en	= { 0x0008, 7, 6, 0, 3 },
			.vdp_src_en	= { 0x0008, 7, 6, 0, 3 },
		},
	},
	{
		.reg = 0x8000,
		.num_ports	= 1,
		.phy_tuning	= rk3588_usb2phy_tuning,
		.clkout_ctl	= { 0x0000, 0, 0, 0, 0 },
		.ls_filter_con	= { 0x0040, 19, 0, 0x30100, 0x00020 },
		.port_cfgs	= {
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0008, 2, 2, 0, 1 },
				.ls_det_en	= { 0x0080, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0084, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0088, 0, 0, 0, 1 },
				.disfall_en	= { 0x0080, 6, 6, 0, 1 },
				.disfall_st	= { 0x0084, 6, 6, 0, 1 },
				.disfall_clr	= { 0x0088, 6, 6, 0, 1 },
				.disrise_en	= { 0x0080, 5, 5, 0, 1 },
				.disrise_st	= { 0x0084, 5, 5, 0, 1 },
				.disrise_clr	= { 0x0088, 5, 5, 0, 1 },
				.utmi_ls	= { 0x00c0, 10, 9, 0, 1 },
			}
		},
	},
	{
		.reg = 0xc000,
		.num_ports	= 1,
		.phy_tuning	= rk3588_usb2phy_tuning,
		.clkout_ctl	= { 0x0000, 0, 0, 0, 0 },
		.ls_filter_con	= { 0x0040, 19, 0, 0x30100, 0x00020 },
		.port_cfgs	= {
			[USB2PHY_PORT_HOST] = {
				.phy_sus	= { 0x0008, 2, 2, 0, 1 },
				.ls_det_en	= { 0x0080, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0084, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0088, 0, 0, 0, 1 },
				.disfall_en	= { 0x0080, 6, 6, 0, 1 },
				.disfall_st	= { 0x0084, 6, 6, 0, 1 },
				.disfall_clr	= { 0x0088, 6, 6, 0, 1 },
				.disrise_en	= { 0x0080, 5, 5, 0, 1 },
				.disrise_st	= { 0x0084, 5, 5, 0, 1 },
				.disrise_clr	= { 0x0088, 5, 5, 0, 1 },
				.utmi_ls	= { 0x00c0, 10, 9, 0, 1 },
			}
		},
	},
	{ /* sentinel */ }
};

static const struct rockchip_usb2phy_cfg rv1106_phy_cfgs[] = {
	{
		.reg = 0xff3e0000,
		.num_ports	= 1,
		.phy_tuning	= rv1106_usb2phy_tuning,
		.clkout_ctl	= { 0x0058, 4, 4, 1, 0 },
		.port_cfgs	= {
			[USB2PHY_PORT_OTG] = {
				.phy_sus	= { 0x0050, 8, 0, 0, 0x1d1 },
				.bvalid_det_en	= { 0x0100, 2, 2, 0, 1 },
				.bvalid_det_st	= { 0x0104, 2, 2, 0, 1 },
				.bvalid_det_clr = { 0x0108, 2, 2, 0, 1 },
				.bvalid_grf_sel	= { 0x0058, 15, 14, 0, 3 },
				.iddig_output	= { 0x0050, 10, 10, 0, 1 },
				.iddig_en	= { 0x0050, 9, 9, 0, 1 },
				.idfall_det_en	= { 0x0100, 5, 5, 0, 1 },
				.idfall_det_st	= { 0x0104, 5, 5, 0, 1 },
				.idfall_det_clr = { 0x0108, 5, 5, 0, 1 },
				.idrise_det_en	= { 0x0100, 4, 4, 0, 1 },
				.idrise_det_st	= { 0x0104, 4, 4, 0, 1 },
				.idrise_det_clr = { 0x0108, 4, 4, 0, 1 },
				.ls_det_en	= { 0x0100, 0, 0, 0, 1 },
				.ls_det_st	= { 0x0104, 0, 0, 0, 1 },
				.ls_det_clr	= { 0x0108, 0, 0, 0, 1 },
				.utmi_avalid	= { 0x0060, 10, 10, 0, 1 },
				.utmi_bvalid	= { 0x0060, 9, 9, 0, 1 },
				.utmi_iddig	= { 0x0060, 6, 6, 0, 1 },
				.utmi_ls	= { 0x0060, 5, 4, 0, 1 },
			},
		},
		.chg_det = {
			.chg_mode	= { 0x0050, 8, 0, 0, 0x1d7 },
			.cp_det		= { 0x0060, 13, 13, 0, 1 },
			.dcp_det	= { 0x0060, 12, 12, 0, 1 },
			.dp_det		= { 0x0060, 14, 14, 0, 1 },
			.idm_sink_en	= { 0x0058, 8, 8, 0, 1 },
			.idp_sink_en	= { 0x0058, 7, 7, 0, 1 },
			.idp_src_en	= { 0x0058, 9, 9, 0, 1 },
			.rdm_pdwn_en	= { 0x0058, 10, 10, 0, 1 },
			.vdm_src_en	= { 0x0058, 12, 12, 0, 1 },
			.vdp_src_en	= { 0x0058, 11, 11, 0, 1 },
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
			.chg_mode	= { 0x0100, 8, 0, 0, 0x1d7 },
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
#ifdef CONFIG_CPU_PX30
	{ .compatible = "rockchip,px30-usb2phy", .data = &rk3328_phy_cfgs },
#endif
#ifdef CONFIG_CPU_RK1808
	{ .compatible = "rockchip,rk1808-usb2phy", .data = &rk1808_phy_cfgs },
#endif
#ifdef CONFIG_CPU_RK312X
	{ .compatible = "rockchip,rk3128-usb2phy", .data = &rk312x_phy_cfgs },
#endif
#ifdef CONFIG_CPU_RK322X
	{ .compatible = "rockchip,rk3228-usb2phy", .data = &rk3228_phy_cfgs },
#endif
#ifdef CONFIG_CPU_RK3308
	{ .compatible = "rockchip,rk3308-usb2phy", .data = &rk3308_phy_cfgs },
#endif
#ifdef CONFIG_CPU_RK3328
	{ .compatible = "rockchip,rk3328-usb2phy", .data = &rk3328_phy_cfgs },
#endif
#ifdef CONFIG_CPU_RK3366
	{ .compatible = "rockchip,rk3366-usb2phy", .data = &rk3366_phy_cfgs },
#endif
#ifdef CONFIG_CPU_RK3368
	{ .compatible = "rockchip,rk3368-usb2phy", .data = &rk3368_phy_cfgs },
#endif
#ifdef CONFIG_CPU_RK3399
	{ .compatible = "rockchip,rk3399-usb2phy", .data = &rk3399_phy_cfgs },
#endif
#ifdef CONFIG_CPU_RK3568
	{ .compatible = "rockchip,rk3568-usb2phy", .data = &rk3568_phy_cfgs },
#endif
#ifdef CONFIG_CPU_RK3588
	{ .compatible = "rockchip,rk3588-usb2phy", .data = &rk3588_phy_cfgs },
#endif
#ifdef CONFIG_CPU_RV1106
	{ .compatible = "rockchip,rv1106-usb2phy", .data = &rv1106_phy_cfgs },
#endif
#ifdef CONFIG_CPU_RV1108
	{ .compatible = "rockchip,rv1108-usb2phy", .data = &rv1108_phy_cfgs },
#endif
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_usb2phy_dt_match);

static struct platform_driver rockchip_usb2phy_driver = {
	.probe		= rockchip_usb2phy_probe,
	.driver		= {
		.name	= "rockchip-usb2phy",
		.pm	= ROCKCHIP_USB2PHY_DEV_PM,
		.of_match_table = rockchip_usb2phy_dt_match,
	},
};
module_platform_driver(rockchip_usb2phy_driver);

MODULE_AUTHOR("Frank Wang <frank.wang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USB2.0 PHY driver");
MODULE_LICENSE("GPL v2");
