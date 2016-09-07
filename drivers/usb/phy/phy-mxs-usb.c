/*
 * Copyright 2012-2016 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Marek Vasut <marex@denx.de>
 * on behalf of DENX Software Engineering GmbH
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/usb/otg.h>
#include <linux/stmp_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/regulator/consumer.h>

#define DRIVER_NAME "mxs_phy"

#define HW_USBPHY_PWD				0x00
#define HW_USBPHY_TX				0x10
#define HW_USBPHY_CTRL				0x30
#define HW_USBPHY_CTRL_SET			0x34
#define HW_USBPHY_CTRL_CLR			0x38

#define HW_USBPHY_DEBUG_SET			0x54
#define HW_USBPHY_DEBUG_CLR			0x58

#define HW_USBPHY_IP				0x90
#define HW_USBPHY_IP_SET			0x94
#define HW_USBPHY_IP_CLR			0x98

#define HW_USBPHY_TX_D_CAL_MASK			0xf

#define BM_USBPHY_CTRL_SFTRST			BIT(31)
#define BM_USBPHY_CTRL_CLKGATE			BIT(30)
#define BM_USBPHY_CTRL_OTG_ID_VALUE		BIT(27)
#define BM_USBPHY_CTRL_ENAUTOSET_USBCLKS	BIT(26)
#define BM_USBPHY_CTRL_ENAUTOCLR_USBCLKGATE	BIT(25)
#define BM_USBPHY_CTRL_ENVBUSCHG_WKUP		BIT(23)
#define BM_USBPHY_CTRL_ENIDCHG_WKUP		BIT(22)
#define BM_USBPHY_CTRL_ENDPDMCHG_WKUP		BIT(21)
#define BM_USBPHY_CTRL_ENAUTOCLR_PHY_PWD	BIT(20)
#define BM_USBPHY_CTRL_ENAUTOCLR_CLKGATE	BIT(19)
#define BM_USBPHY_CTRL_ENAUTO_PWRON_PLL		BIT(18)
#define BM_USBPHY_CTRL_ENUTMILEVEL3		BIT(15)
#define BM_USBPHY_CTRL_ENUTMILEVEL2		BIT(14)
#define BM_USBPHY_CTRL_ENHOSTDISCONDETECT	BIT(1)

#define BM_USBPHY_IP_FIX                       (BIT(17) | BIT(18))

#define BM_USBPHY_DEBUG_CLKGATE			BIT(30)

/* Anatop Registers */
#define ANADIG_REG_1P1_SET			0x114
#define ANADIG_REG_1P1_CLR			0x118

#define ANADIG_ANA_MISC0			0x150
#define ANADIG_ANA_MISC0_SET			0x154
#define ANADIG_ANA_MISC0_CLR			0x158

#define ANADIG_USB1_VBUS_DET_STAT		0x1c0
#define ANADIG_USB2_VBUS_DET_STAT		0x220

#define ANADIG_USB1_LOOPBACK_SET		0x1e4
#define ANADIG_USB1_LOOPBACK_CLR		0x1e8
#define ANADIG_USB2_LOOPBACK_SET		0x244
#define ANADIG_USB2_LOOPBACK_CLR		0x248

#define ANADIG_USB1_MISC			0x1f0
#define ANADIG_USB2_MISC			0x250

#define BM_ANADIG_ANA_MISC0_STOP_MODE_CONFIG	BIT(12)
#define BM_ANADIG_ANA_MISC0_STOP_MODE_CONFIG_SL BIT(11)

#define BM_ANADIG_USB1_VBUS_DET_STAT_VBUS_VALID	BIT(3)
#define BM_ANADIG_USB2_VBUS_DET_STAT_VBUS_VALID	BIT(3)

#define BM_ANADIG_USB1_LOOPBACK_UTMI_DIG_TST1	BIT(2)
#define BM_ANADIG_USB1_LOOPBACK_TSTI_TX_EN	BIT(5)
#define BM_ANADIG_USB2_LOOPBACK_UTMI_DIG_TST1	BIT(2)
#define BM_ANADIG_USB2_LOOPBACK_TSTI_TX_EN	BIT(5)

#define BM_ANADIG_USB1_MISC_RX_VPIN_FS		BIT(29)
#define BM_ANADIG_USB1_MISC_RX_VMIN_FS		BIT(28)
#define BM_ANADIG_USB2_MISC_RX_VPIN_FS		BIT(29)
#define BM_ANADIG_USB2_MISC_RX_VMIN_FS		BIT(28)

#define BM_ANADIG_REG_1P1_ENABLE_WEAK_LINREG	BIT(18)
#define BM_ANADIG_REG_1P1_TRACK_VDD_SOC_CAP	BIT(19)

#define to_mxs_phy(p) container_of((p), struct mxs_phy, phy)

/* Do disconnection between PHY and controller without vbus */
#define MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS	BIT(0)

/*
 * The PHY will be in messy if there is a wakeup after putting
 * bus to suspend (set portsc.suspendM) but before setting PHY to low
 * power mode (set portsc.phcd).
 */
#define MXS_PHY_ABNORMAL_IN_SUSPEND		BIT(1)

/*
 * The SOF sends too fast after resuming, it will cause disconnection
 * between host and high speed device.
 */
#define MXS_PHY_SENDING_SOF_TOO_FAST		BIT(2)

/*
 * IC has bug fixes logic, they include
 * MXS_PHY_ABNORMAL_IN_SUSPEND and MXS_PHY_SENDING_SOF_TOO_FAST
 * which are described at above flags, the RTL will handle it
 * according to different versions.
 */
#define MXS_PHY_NEED_IP_FIX			BIT(3)

/*
 * At some versions, the PHY2's clock is controlled by hardware directly,
 * eg, according to PHY's suspend status. In these PHYs, we only need to
 * open the clock at the initialization and close it at its shutdown routine.
 * It will be benefit for remote wakeup case which needs to send resume
 * signal as soon as possible, and in this case, the resume signal can be sent
 * out without software interfere.
 */
#define MXS_PHY_HARDWARE_CONTROL_PHY2_CLK	BIT(4)

struct mxs_phy_data {
	unsigned int flags;
};

static const struct mxs_phy_data imx23_phy_data = {
	.flags = MXS_PHY_ABNORMAL_IN_SUSPEND | MXS_PHY_SENDING_SOF_TOO_FAST,
};

static const struct mxs_phy_data imx6q_phy_data = {
	.flags = MXS_PHY_SENDING_SOF_TOO_FAST |
		MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS |
		MXS_PHY_NEED_IP_FIX |
		MXS_PHY_HARDWARE_CONTROL_PHY2_CLK,
};

static const struct mxs_phy_data imx6sl_phy_data = {
	.flags = MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS |
		MXS_PHY_NEED_IP_FIX |
		MXS_PHY_HARDWARE_CONTROL_PHY2_CLK,
};

static const struct mxs_phy_data vf610_phy_data = {
	.flags = MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS |
		MXS_PHY_NEED_IP_FIX,
};

static const struct mxs_phy_data imx6sx_phy_data = {
	.flags = MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS |
		MXS_PHY_HARDWARE_CONTROL_PHY2_CLK,
};

static const struct mxs_phy_data imx6ul_phy_data = {
	.flags = MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS |
		MXS_PHY_HARDWARE_CONTROL_PHY2_CLK,
};

static const struct of_device_id mxs_phy_dt_ids[] = {
	{ .compatible = "fsl,imx6ul-usbphy", .data = &imx6ul_phy_data, },
	{ .compatible = "fsl,imx6sx-usbphy", .data = &imx6sx_phy_data, },
	{ .compatible = "fsl,imx6sl-usbphy", .data = &imx6sl_phy_data, },
	{ .compatible = "fsl,imx6q-usbphy", .data = &imx6q_phy_data, },
	{ .compatible = "fsl,imx23-usbphy", .data = &imx23_phy_data, },
	{ .compatible = "fsl,vf610-usbphy", .data = &vf610_phy_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_phy_dt_ids);

struct mxs_phy {
	struct usb_phy phy;
	struct clk *clk;
	const struct mxs_phy_data *data;
	struct regmap *regmap_anatop;
	int port_id;
	struct regulator *phy_3p0;
	bool hardware_control_phy2_clk;
	u32 tx_d_cal;
};

static inline bool is_imx6q_phy(struct mxs_phy *mxs_phy)
{
	return mxs_phy->data == &imx6q_phy_data;
}

static inline bool is_imx6sl_phy(struct mxs_phy *mxs_phy)
{
	return mxs_phy->data == &imx6sl_phy_data;
}

static inline bool is_imx6ul_phy(struct mxs_phy *mxs_phy)
{
	return mxs_phy->data == &imx6ul_phy_data;
}

/*
 * PHY needs some 32K cycles to switch from 32K clock to
 * bus (such as AHB/AXI, etc) clock.
 */
static void mxs_phy_clock_switch_delay(void)
{
	usleep_range(300, 400);
}

static int mxs_phy_hw_init(struct mxs_phy *mxs_phy)
{
	int ret;
	void __iomem *base = mxs_phy->phy.io_priv;
	u32 val;

	ret = stmp_reset_block(base + HW_USBPHY_CTRL);
	if (ret)
		return ret;

	if (mxs_phy->phy_3p0) {
		ret = regulator_enable(mxs_phy->phy_3p0);
		if (ret) {
			dev_err(mxs_phy->phy.dev,
				"Failed to enable 3p0 regulator, ret=%d\n",
				ret);
			return ret;
		}
	}

	/* Power up the PHY */
	writel(0, base + HW_USBPHY_PWD);

	/*
	 * USB PHY Ctrl Setting
	 * - Auto clock/power on
	 * - Enable full/low speed support
	 */
	writel(BM_USBPHY_CTRL_ENAUTOSET_USBCLKS |
		BM_USBPHY_CTRL_ENAUTOCLR_USBCLKGATE |
		BM_USBPHY_CTRL_ENAUTOCLR_PHY_PWD |
		BM_USBPHY_CTRL_ENAUTOCLR_CLKGATE |
		BM_USBPHY_CTRL_ENAUTO_PWRON_PLL |
		BM_USBPHY_CTRL_ENUTMILEVEL2 |
		BM_USBPHY_CTRL_ENUTMILEVEL3,
	       base + HW_USBPHY_CTRL_SET);

	if (mxs_phy->data->flags & MXS_PHY_NEED_IP_FIX)
		writel(BM_USBPHY_IP_FIX, base + HW_USBPHY_IP_SET);

	/* Change D_CAL if necessary */
	if (mxs_phy->tx_d_cal) {
		val = readl(base + HW_USBPHY_TX);
		val &= ~HW_USBPHY_TX_D_CAL_MASK;
		writel(val | mxs_phy->tx_d_cal, base + HW_USBPHY_TX);
	}

	return 0;
}

/* Return true if the vbus is there */
static bool mxs_phy_get_vbus_status(struct mxs_phy *mxs_phy)
{
	unsigned int vbus_value = 0;

	if (!mxs_phy->regmap_anatop)
		return false;

	if (mxs_phy->port_id == 0)
		regmap_read(mxs_phy->regmap_anatop,
			ANADIG_USB1_VBUS_DET_STAT,
			&vbus_value);
	else if (mxs_phy->port_id == 1)
		regmap_read(mxs_phy->regmap_anatop,
			ANADIG_USB2_VBUS_DET_STAT,
			&vbus_value);

	if (vbus_value & BM_ANADIG_USB1_VBUS_DET_STAT_VBUS_VALID)
		return true;
	else
		return false;
}

static void __mxs_phy_disconnect_line(struct mxs_phy *mxs_phy, bool disconnect)
{
	void __iomem *base = mxs_phy->phy.io_priv;
	u32 reg;

	if (disconnect)
		writel_relaxed(BM_USBPHY_DEBUG_CLKGATE,
			base + HW_USBPHY_DEBUG_CLR);

	if (mxs_phy->port_id == 0) {
		reg = disconnect ? ANADIG_USB1_LOOPBACK_SET
			: ANADIG_USB1_LOOPBACK_CLR;
		regmap_write(mxs_phy->regmap_anatop, reg,
			BM_ANADIG_USB1_LOOPBACK_UTMI_DIG_TST1 |
			BM_ANADIG_USB1_LOOPBACK_TSTI_TX_EN);
	} else if (mxs_phy->port_id == 1) {
		reg = disconnect ? ANADIG_USB2_LOOPBACK_SET
			: ANADIG_USB2_LOOPBACK_CLR;
		regmap_write(mxs_phy->regmap_anatop, reg,
			BM_ANADIG_USB2_LOOPBACK_UTMI_DIG_TST1 |
			BM_ANADIG_USB2_LOOPBACK_TSTI_TX_EN);
	}

	if (!disconnect)
		writel_relaxed(BM_USBPHY_DEBUG_CLKGATE,
			base + HW_USBPHY_DEBUG_SET);

	/* Delay some time, and let Linestate be SE0 for controller */
	if (disconnect)
		usleep_range(500, 1000);
}

static bool mxs_phy_is_otg_host(struct mxs_phy *mxs_phy)
{
	void __iomem *base = mxs_phy->phy.io_priv;
	u32 phyctrl = readl(base + HW_USBPHY_CTRL);

	if (IS_ENABLED(CONFIG_USB_OTG) &&
			!(phyctrl & BM_USBPHY_CTRL_OTG_ID_VALUE))
		return true;

	return false;
}

static void mxs_phy_disconnect_line(struct mxs_phy *mxs_phy, bool on)
{
	bool vbus_is_on = false;

	/* If the SoCs don't need to disconnect line without vbus, quit */
	if (!(mxs_phy->data->flags & MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS))
		return;

	/* If the SoCs don't have anatop, quit */
	if (!mxs_phy->regmap_anatop)
		return;

	vbus_is_on = mxs_phy_get_vbus_status(mxs_phy);

	if (on && !vbus_is_on && !mxs_phy_is_otg_host(mxs_phy))
		__mxs_phy_disconnect_line(mxs_phy, true);
	else
		__mxs_phy_disconnect_line(mxs_phy, false);

}

static int mxs_phy_init(struct usb_phy *phy)
{
	int ret;
	struct mxs_phy *mxs_phy = to_mxs_phy(phy);

	mxs_phy_clock_switch_delay();
	ret = clk_prepare_enable(mxs_phy->clk);
	if (ret)
		return ret;

	return mxs_phy_hw_init(mxs_phy);
}

static void mxs_phy_shutdown(struct usb_phy *phy)
{
	struct mxs_phy *mxs_phy = to_mxs_phy(phy);
	u32 value = BM_USBPHY_CTRL_ENVBUSCHG_WKUP |
			BM_USBPHY_CTRL_ENDPDMCHG_WKUP |
			BM_USBPHY_CTRL_ENIDCHG_WKUP |
			BM_USBPHY_CTRL_ENAUTOSET_USBCLKS |
			BM_USBPHY_CTRL_ENAUTOCLR_USBCLKGATE |
			BM_USBPHY_CTRL_ENAUTOCLR_PHY_PWD |
			BM_USBPHY_CTRL_ENAUTOCLR_CLKGATE |
			BM_USBPHY_CTRL_ENAUTO_PWRON_PLL;

	writel(value, phy->io_priv + HW_USBPHY_CTRL_CLR);
	writel(0xffffffff, phy->io_priv + HW_USBPHY_PWD);

	writel(BM_USBPHY_CTRL_CLKGATE,
	       phy->io_priv + HW_USBPHY_CTRL_SET);

	if (mxs_phy->phy_3p0)
		regulator_disable(mxs_phy->phy_3p0);

	clk_disable_unprepare(mxs_phy->clk);
}

static bool mxs_phy_is_low_speed_connection(struct mxs_phy *mxs_phy)
{
	unsigned int line_state;
	/* bit definition is the same for all controllers */
	unsigned int dp_bit = BM_ANADIG_USB1_MISC_RX_VPIN_FS,
		     dm_bit = BM_ANADIG_USB1_MISC_RX_VMIN_FS;
	unsigned int reg = ANADIG_USB1_MISC;

	/* If the SoCs don't have anatop, quit */
	if (!mxs_phy->regmap_anatop)
		return false;

	if (mxs_phy->port_id == 0)
		reg = ANADIG_USB1_MISC;
	else if (mxs_phy->port_id == 1)
		reg = ANADIG_USB2_MISC;

	regmap_read(mxs_phy->regmap_anatop, reg, &line_state);

	if ((line_state & (dp_bit | dm_bit)) ==  dm_bit)
		return true;
	else
		return false;
}

static int mxs_phy_suspend(struct usb_phy *x, int suspend)
{
	int ret;
	struct mxs_phy *mxs_phy = to_mxs_phy(x);
	bool low_speed_connection, vbus_is_on;

	low_speed_connection = mxs_phy_is_low_speed_connection(mxs_phy);
	vbus_is_on = mxs_phy_get_vbus_status(mxs_phy);

	if (suspend) {
		/*
		 * FIXME: Do not power down RXPWD1PT1 bit for low speed
		 * connect. The low speed connection will have problem at
		 * very rare cases during usb suspend and resume process.
		 */
		if (low_speed_connection & vbus_is_on) {
			/*
			 * If value to be set as pwd value is not 0xffffffff,
			 * several 32Khz cycles are needed.
			 */
			mxs_phy_clock_switch_delay();
			writel(0xffbfffff, x->io_priv + HW_USBPHY_PWD);
		} else {
			writel(0xffffffff, x->io_priv + HW_USBPHY_PWD);
		}
		writel(BM_USBPHY_CTRL_CLKGATE,
		       x->io_priv + HW_USBPHY_CTRL_SET);
		if (!(mxs_phy->port_id == 1 &&
				mxs_phy->hardware_control_phy2_clk))
			clk_disable_unprepare(mxs_phy->clk);
	} else {
		mxs_phy_clock_switch_delay();
		if (!(mxs_phy->port_id == 1 &&
				mxs_phy->hardware_control_phy2_clk)) {
			ret = clk_prepare_enable(mxs_phy->clk);
			if (ret)
				return ret;
		}
		writel(BM_USBPHY_CTRL_CLKGATE,
		       x->io_priv + HW_USBPHY_CTRL_CLR);
		writel(0, x->io_priv + HW_USBPHY_PWD);
	}

	return 0;
}

static int mxs_phy_set_wakeup(struct usb_phy *x, bool enabled)
{
	struct mxs_phy *mxs_phy = to_mxs_phy(x);
	u32 value = BM_USBPHY_CTRL_ENVBUSCHG_WKUP |
			BM_USBPHY_CTRL_ENDPDMCHG_WKUP |
				BM_USBPHY_CTRL_ENIDCHG_WKUP;
	if (enabled) {
		mxs_phy_disconnect_line(mxs_phy, true);
		writel_relaxed(value, x->io_priv + HW_USBPHY_CTRL_SET);
	} else {
		writel_relaxed(value, x->io_priv + HW_USBPHY_CTRL_CLR);
		mxs_phy_disconnect_line(mxs_phy, false);
	}

	return 0;
}

static int mxs_phy_on_connect(struct usb_phy *phy,
		enum usb_device_speed speed)
{
	dev_dbg(phy->dev, "%s device has connected\n",
		(speed == USB_SPEED_HIGH) ? "HS" : "FS/LS");

	if (speed == USB_SPEED_HIGH)
		writel(BM_USBPHY_CTRL_ENHOSTDISCONDETECT,
		       phy->io_priv + HW_USBPHY_CTRL_SET);

	return 0;
}

static int mxs_phy_on_disconnect(struct usb_phy *phy,
		enum usb_device_speed speed)
{
	dev_dbg(phy->dev, "%s device has disconnected\n",
		(speed == USB_SPEED_HIGH) ? "HS" : "FS/LS");

	/* Sometimes, the speed is not high speed when the error occurs */
	if (readl(phy->io_priv + HW_USBPHY_CTRL) &
			BM_USBPHY_CTRL_ENHOSTDISCONDETECT)
		writel(BM_USBPHY_CTRL_ENHOSTDISCONDETECT,
		       phy->io_priv + HW_USBPHY_CTRL_CLR);

	return 0;
}

static int mxs_phy_on_suspend(struct usb_phy *phy,
		enum usb_device_speed speed)
{
	struct mxs_phy *mxs_phy = to_mxs_phy(phy);

	dev_dbg(phy->dev, "%s device has suspended\n",
		(speed == USB_SPEED_HIGH) ? "HS" : "FS/LS");

	/* delay 4ms to wait bus entering idle */
	usleep_range(4000, 5000);

	if (mxs_phy->data->flags & MXS_PHY_ABNORMAL_IN_SUSPEND) {
		writel_relaxed(0xffffffff, phy->io_priv + HW_USBPHY_PWD);
		writel_relaxed(0, phy->io_priv + HW_USBPHY_PWD);
	}

	if (speed == USB_SPEED_HIGH)
		writel_relaxed(BM_USBPHY_CTRL_ENHOSTDISCONDETECT,
				phy->io_priv + HW_USBPHY_CTRL_CLR);

	return 0;
}

/*
 * The resume signal must be finished here.
 */
static int mxs_phy_on_resume(struct usb_phy *phy,
		enum usb_device_speed speed)
{
	dev_dbg(phy->dev, "%s device has resumed\n",
		(speed == USB_SPEED_HIGH) ? "HS" : "FS/LS");

	if (speed == USB_SPEED_HIGH) {
		/* Make sure the device has switched to High-Speed mode */
		udelay(500);
		writel_relaxed(BM_USBPHY_CTRL_ENHOSTDISCONDETECT,
				phy->io_priv + HW_USBPHY_CTRL_SET);
	}

	return 0;
}

static int mxs_phy_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *base;
	struct clk *clk;
	struct mxs_phy *mxs_phy;
	int ret;
	const struct of_device_id *of_id =
			of_match_device(mxs_phy_dt_ids, &pdev->dev);
	struct device_node *np = pdev->dev.of_node;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev,
			"can't get the clock, err=%ld", PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	mxs_phy = devm_kzalloc(&pdev->dev, sizeof(*mxs_phy), GFP_KERNEL);
	if (!mxs_phy)
		return -ENOMEM;

	/* Some SoCs don't have anatop registers */
	if (of_get_property(np, "fsl,anatop", NULL)) {
		mxs_phy->regmap_anatop = syscon_regmap_lookup_by_phandle
			(np, "fsl,anatop");
		if (IS_ERR(mxs_phy->regmap_anatop)) {
			dev_dbg(&pdev->dev,
				"failed to find regmap for anatop\n");
			return PTR_ERR(mxs_phy->regmap_anatop);
		}
	}

	ret = of_alias_get_id(np, "usbphy");
	if (ret < 0)
		dev_dbg(&pdev->dev, "failed to get alias id, errno %d\n", ret);
	mxs_phy->port_id = ret;
	mxs_phy->clk = clk;
	mxs_phy->data = of_id->data;

	mxs_phy->phy.io_priv		= base;
	mxs_phy->phy.dev		= &pdev->dev;
	mxs_phy->phy.label		= DRIVER_NAME;
	mxs_phy->phy.init		= mxs_phy_init;
	mxs_phy->phy.shutdown		= mxs_phy_shutdown;
	mxs_phy->phy.set_suspend	= mxs_phy_suspend;
	mxs_phy->phy.notify_connect	= mxs_phy_on_connect;
	mxs_phy->phy.notify_disconnect	= mxs_phy_on_disconnect;
	mxs_phy->phy.type		= USB_PHY_TYPE_USB2;
	mxs_phy->phy.set_wakeup		= mxs_phy_set_wakeup;
	if (mxs_phy->data->flags & MXS_PHY_SENDING_SOF_TOO_FAST) {
		mxs_phy->phy.notify_suspend = mxs_phy_on_suspend;
		mxs_phy->phy.notify_resume = mxs_phy_on_resume;
	}

	mxs_phy->phy_3p0 = devm_regulator_get(&pdev->dev, "phy-3p0");
	if (PTR_ERR(mxs_phy->phy_3p0) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (PTR_ERR(mxs_phy->phy_3p0) == -ENODEV) {
		/* not exist */
		mxs_phy->phy_3p0 = NULL;
	} else if (IS_ERR(mxs_phy->phy_3p0)) {
		dev_err(&pdev->dev, "Getting regulator error: %ld\n",
			PTR_ERR(mxs_phy->phy_3p0));
		return PTR_ERR(mxs_phy->phy_3p0);
	}
	if (mxs_phy->phy_3p0)
		regulator_set_voltage(mxs_phy->phy_3p0, 3200000, 3200000);

	if (mxs_phy->data->flags & MXS_PHY_HARDWARE_CONTROL_PHY2_CLK)
		mxs_phy->hardware_control_phy2_clk = true;

	if (of_find_property(np, "tx-d-cal", NULL)) {
		ret = of_property_read_u32(np, "tx-d-cal",
			&mxs_phy->tx_d_cal);
		if (ret) {
			dev_err(&pdev->dev,
				"failed to get tx-d-cal value\n");
			return ret;
		}
	}

	platform_set_drvdata(pdev, mxs_phy);

	device_set_wakeup_capable(&pdev->dev, true);

	ret = usb_add_phy_dev(&mxs_phy->phy);
	if (ret)
		return ret;

	return 0;
}

static int mxs_phy_remove(struct platform_device *pdev)
{
	struct mxs_phy *mxs_phy = platform_get_drvdata(pdev);

	usb_remove_phy(&mxs_phy->phy);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static void mxs_phy_enable_ldo_in_suspend(struct mxs_phy *mxs_phy, bool on)
{
	unsigned int reg;
	u32 value;

	/* If the SoCs don't have anatop, quit */
	if (!mxs_phy->regmap_anatop)
		return;

	if (is_imx6q_phy(mxs_phy)) {
		reg = on ? ANADIG_ANA_MISC0_SET : ANADIG_ANA_MISC0_CLR;
		regmap_write(mxs_phy->regmap_anatop, reg,
			BM_ANADIG_ANA_MISC0_STOP_MODE_CONFIG);
	} else if (is_imx6sl_phy(mxs_phy)) {
		reg = on ? ANADIG_ANA_MISC0_SET : ANADIG_ANA_MISC0_CLR;
		regmap_write(mxs_phy->regmap_anatop,
			reg, BM_ANADIG_ANA_MISC0_STOP_MODE_CONFIG_SL);
	} else if (is_imx6ul_phy(mxs_phy)) {
		reg = on ? ANADIG_REG_1P1_SET : ANADIG_REG_1P1_CLR;
		value = BM_ANADIG_REG_1P1_ENABLE_WEAK_LINREG |
			BM_ANADIG_REG_1P1_TRACK_VDD_SOC_CAP;
		if (mxs_phy_get_vbus_status(mxs_phy) && on)
			regmap_write(mxs_phy->regmap_anatop, reg, value);
		else if (!on)
			regmap_write(mxs_phy->regmap_anatop, reg, value);
	}
}

static int mxs_phy_system_suspend(struct device *dev)
{
	struct mxs_phy *mxs_phy = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		mxs_phy_enable_ldo_in_suspend(mxs_phy, true);

	return 0;
}

static int mxs_phy_system_resume(struct device *dev)
{
	struct mxs_phy *mxs_phy = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		mxs_phy_enable_ldo_in_suspend(mxs_phy, false);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(mxs_phy_pm, mxs_phy_system_suspend,
		mxs_phy_system_resume);

static struct platform_driver mxs_phy_driver = {
	.probe = mxs_phy_probe,
	.remove = mxs_phy_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = mxs_phy_dt_ids,
		.pm = &mxs_phy_pm,
	 },
};

static int __init mxs_phy_module_init(void)
{
	return platform_driver_register(&mxs_phy_driver);
}
postcore_initcall(mxs_phy_module_init);

static void __exit mxs_phy_module_exit(void)
{
	platform_driver_unregister(&mxs_phy_driver);
}
module_exit(mxs_phy_module_exit);

MODULE_ALIAS("platform:mxs-usb-phy");
MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_AUTHOR("Richard Zhao <richard.zhao@freescale.com>");
MODULE_DESCRIPTION("Freescale MXS USB PHY driver");
MODULE_LICENSE("GPL");
