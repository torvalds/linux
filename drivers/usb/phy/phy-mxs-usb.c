// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2012-2014 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Marek Vasut <marex@denx.de>
 * on behalf of DENX Software Engineering GmbH
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

#define GM_USBPHY_TX_TXCAL45DP(x)            (((x) & 0xf) << 16)
#define GM_USBPHY_TX_TXCAL45DN(x)            (((x) & 0xf) << 8)
#define GM_USBPHY_TX_D_CAL(x)                (((x) & 0xf) << 0)

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
#define ANADIG_ANA_MISC0			0x150
#define ANADIG_ANA_MISC0_SET			0x154
#define ANADIG_ANA_MISC0_CLR			0x158

#define ANADIG_USB1_CHRG_DETECT_SET		0x1b4
#define ANADIG_USB1_CHRG_DETECT_CLR		0x1b8
#define ANADIG_USB2_CHRG_DETECT_SET		0x214
#define ANADIG_USB1_CHRG_DETECT_EN_B		BIT(20)
#define ANADIG_USB1_CHRG_DETECT_CHK_CHRG_B	BIT(19)
#define ANADIG_USB1_CHRG_DETECT_CHK_CONTACT	BIT(18)

#define ANADIG_USB1_VBUS_DET_STAT		0x1c0
#define ANADIG_USB1_VBUS_DET_STAT_VBUS_VALID	BIT(3)

#define ANADIG_USB1_CHRG_DET_STAT		0x1d0
#define ANADIG_USB1_CHRG_DET_STAT_DM_STATE	BIT(2)
#define ANADIG_USB1_CHRG_DET_STAT_CHRG_DETECTED	BIT(1)
#define ANADIG_USB1_CHRG_DET_STAT_PLUG_CONTACT	BIT(0)

#define ANADIG_USB2_VBUS_DET_STAT		0x220

#define ANADIG_USB1_LOOPBACK_SET		0x1e4
#define ANADIG_USB1_LOOPBACK_CLR		0x1e8
#define ANADIG_USB1_LOOPBACK_UTMI_TESTSTART	BIT(0)

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

/* Minimum and maximum values for device tree entries */
#define MXS_PHY_TX_CAL45_MIN			30
#define MXS_PHY_TX_CAL45_MAX			55
#define MXS_PHY_TX_D_CAL_MIN			79
#define MXS_PHY_TX_D_CAL_MAX			119

struct mxs_phy_data {
	unsigned int flags;
};

static const struct mxs_phy_data imx23_phy_data = {
	.flags = MXS_PHY_ABNORMAL_IN_SUSPEND | MXS_PHY_SENDING_SOF_TOO_FAST,
};

static const struct mxs_phy_data imx6q_phy_data = {
	.flags = MXS_PHY_SENDING_SOF_TOO_FAST |
		MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS |
		MXS_PHY_NEED_IP_FIX,
};

static const struct mxs_phy_data imx6sl_phy_data = {
	.flags = MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS |
		MXS_PHY_NEED_IP_FIX,
};

static const struct mxs_phy_data vf610_phy_data = {
	.flags = MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS |
		MXS_PHY_NEED_IP_FIX,
};

static const struct mxs_phy_data imx6sx_phy_data = {
	.flags = MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS,
};

static const struct mxs_phy_data imx6ul_phy_data = {
	.flags = MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS,
};

static const struct of_device_id mxs_phy_dt_ids[] = {
	{ .compatible = "fsl,imx6sx-usbphy", .data = &imx6sx_phy_data, },
	{ .compatible = "fsl,imx6sl-usbphy", .data = &imx6sl_phy_data, },
	{ .compatible = "fsl,imx6q-usbphy", .data = &imx6q_phy_data, },
	{ .compatible = "fsl,imx23-usbphy", .data = &imx23_phy_data, },
	{ .compatible = "fsl,vf610-usbphy", .data = &vf610_phy_data, },
	{ .compatible = "fsl,imx6ul-usbphy", .data = &imx6ul_phy_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_phy_dt_ids);

struct mxs_phy {
	struct usb_phy phy;
	struct clk *clk;
	const struct mxs_phy_data *data;
	struct regmap *regmap_anatop;
	int port_id;
	u32 tx_reg_set;
	u32 tx_reg_mask;
};

static inline bool is_imx6q_phy(struct mxs_phy *mxs_phy)
{
	return mxs_phy->data == &imx6q_phy_data;
}

static inline bool is_imx6sl_phy(struct mxs_phy *mxs_phy)
{
	return mxs_phy->data == &imx6sl_phy_data;
}

/*
 * PHY needs some 32K cycles to switch from 32K clock to
 * bus (such as AHB/AXI, etc) clock.
 */
static void mxs_phy_clock_switch_delay(void)
{
	usleep_range(300, 400);
}

static void mxs_phy_tx_init(struct mxs_phy *mxs_phy)
{
	void __iomem *base = mxs_phy->phy.io_priv;
	u32 phytx;

	/* Update TX register if there is anything to write */
	if (mxs_phy->tx_reg_mask) {
		phytx = readl(base + HW_USBPHY_TX);
		phytx &= ~mxs_phy->tx_reg_mask;
		phytx |= mxs_phy->tx_reg_set;
		writel(phytx, base + HW_USBPHY_TX);
	}
}

static int mxs_phy_hw_init(struct mxs_phy *mxs_phy)
{
	int ret;
	void __iomem *base = mxs_phy->phy.io_priv;

	ret = stmp_reset_block(base + HW_USBPHY_CTRL);
	if (ret)
		return ret;

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

	if (mxs_phy->regmap_anatop) {
		unsigned int reg = mxs_phy->port_id ?
			ANADIG_USB1_CHRG_DETECT_SET :
			ANADIG_USB2_CHRG_DETECT_SET;
		/*
		 * The external charger detector needs to be disabled,
		 * or the signal at DP will be poor
		 */
		regmap_write(mxs_phy->regmap_anatop, reg,
			     ANADIG_USB1_CHRG_DETECT_EN_B |
			     ANADIG_USB1_CHRG_DETECT_CHK_CHRG_B);
	}

	mxs_phy_tx_init(mxs_phy);

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
		clk_disable_unprepare(mxs_phy->clk);
	} else {
		mxs_phy_clock_switch_delay();
		ret = clk_prepare_enable(mxs_phy->clk);
		if (ret)
			return ret;
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

#define MXS_USB_CHARGER_DATA_CONTACT_TIMEOUT	100
static int mxs_charger_data_contact_detect(struct mxs_phy *x)
{
	struct regmap *regmap = x->regmap_anatop;
	int i, stable_contact_count = 0;
	u32 val;

	/* Check if vbus is valid */
	regmap_read(regmap, ANADIG_USB1_VBUS_DET_STAT, &val);
	if (!(val & ANADIG_USB1_VBUS_DET_STAT_VBUS_VALID)) {
		dev_err(x->phy.dev, "vbus is not valid\n");
		return -EINVAL;
	}

	/* Enable charger detector */
	regmap_write(regmap, ANADIG_USB1_CHRG_DETECT_CLR,
				ANADIG_USB1_CHRG_DETECT_EN_B);
	/*
	 * - Do not check whether a charger is connected to the USB port
	 * - Check whether the USB plug has been in contact with each other
	 */
	regmap_write(regmap, ANADIG_USB1_CHRG_DETECT_SET,
			ANADIG_USB1_CHRG_DETECT_CHK_CONTACT |
			ANADIG_USB1_CHRG_DETECT_CHK_CHRG_B);

	/* Check if plug is connected */
	for (i = 0; i < MXS_USB_CHARGER_DATA_CONTACT_TIMEOUT; i++) {
		regmap_read(regmap, ANADIG_USB1_CHRG_DET_STAT, &val);
		if (val & ANADIG_USB1_CHRG_DET_STAT_PLUG_CONTACT) {
			stable_contact_count++;
			if (stable_contact_count > 5)
				/* Data pin makes contact */
				break;
			else
				usleep_range(5000, 10000);
		} else {
			stable_contact_count = 0;
			usleep_range(5000, 6000);
		}
	}

	if (i == MXS_USB_CHARGER_DATA_CONTACT_TIMEOUT) {
		dev_err(x->phy.dev,
			"Data pin can't make good contact.\n");
		/* Disable charger detector */
		regmap_write(regmap, ANADIG_USB1_CHRG_DETECT_SET,
				ANADIG_USB1_CHRG_DETECT_EN_B |
				ANADIG_USB1_CHRG_DETECT_CHK_CHRG_B);
		return -ENXIO;
	}

	return 0;
}

static enum usb_charger_type mxs_charger_primary_detection(struct mxs_phy *x)
{
	struct regmap *regmap = x->regmap_anatop;
	enum usb_charger_type chgr_type = UNKNOWN_TYPE;
	u32 val;

	/*
	 * - Do check whether a charger is connected to the USB port
	 * - Do not Check whether the USB plug has been in contact with
	 *   each other
	 */
	regmap_write(regmap, ANADIG_USB1_CHRG_DETECT_CLR,
			ANADIG_USB1_CHRG_DETECT_CHK_CONTACT |
			ANADIG_USB1_CHRG_DETECT_CHK_CHRG_B);

	msleep(100);

	/* Check if it is a charger */
	regmap_read(regmap, ANADIG_USB1_CHRG_DET_STAT, &val);
	if (!(val & ANADIG_USB1_CHRG_DET_STAT_CHRG_DETECTED)) {
		chgr_type = SDP_TYPE;
		dev_dbg(x->phy.dev, "It is a standard downstream port\n");
	}

	/* Disable charger detector */
	regmap_write(regmap, ANADIG_USB1_CHRG_DETECT_SET,
			ANADIG_USB1_CHRG_DETECT_EN_B |
			ANADIG_USB1_CHRG_DETECT_CHK_CHRG_B);

	return chgr_type;
}

/*
 * It must be called after DP is pulled up, which is used to
 * differentiate DCP and CDP.
 */
static enum usb_charger_type mxs_charger_secondary_detection(struct mxs_phy *x)
{
	struct regmap *regmap = x->regmap_anatop;
	int val;

	msleep(80);

	regmap_read(regmap, ANADIG_USB1_CHRG_DET_STAT, &val);
	if (val & ANADIG_USB1_CHRG_DET_STAT_DM_STATE) {
		dev_dbg(x->phy.dev, "It is a dedicate charging port\n");
		return DCP_TYPE;
	} else {
		dev_dbg(x->phy.dev, "It is a charging downstream port\n");
		return CDP_TYPE;
	}
}

static enum usb_charger_type mxs_phy_charger_detect(struct usb_phy *phy)
{
	struct mxs_phy *mxs_phy = to_mxs_phy(phy);
	struct regmap *regmap = mxs_phy->regmap_anatop;
	void __iomem *base = phy->io_priv;
	enum usb_charger_type chgr_type = UNKNOWN_TYPE;

	if (!regmap)
		return UNKNOWN_TYPE;

	if (mxs_charger_data_contact_detect(mxs_phy))
		return chgr_type;

	chgr_type = mxs_charger_primary_detection(mxs_phy);

	if (chgr_type != SDP_TYPE) {
		/* Pull up DP via test */
		writel_relaxed(BM_USBPHY_DEBUG_CLKGATE,
				base + HW_USBPHY_DEBUG_CLR);
		regmap_write(regmap, ANADIG_USB1_LOOPBACK_SET,
				ANADIG_USB1_LOOPBACK_UTMI_TESTSTART);

		chgr_type = mxs_charger_secondary_detection(mxs_phy);

		/* Stop the test */
		regmap_write(regmap, ANADIG_USB1_LOOPBACK_CLR,
				ANADIG_USB1_LOOPBACK_UTMI_TESTSTART);
		writel_relaxed(BM_USBPHY_DEBUG_CLKGATE,
				base + HW_USBPHY_DEBUG_SET);
	}

	return chgr_type;
}

static int mxs_phy_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *base;
	struct clk *clk;
	struct mxs_phy *mxs_phy;
	int ret;
	const struct of_device_id *of_id;
	struct device_node *np = pdev->dev.of_node;
	u32 val;

	of_id = of_match_device(mxs_phy_dt_ids, &pdev->dev);
	if (!of_id)
		return -ENODEV;

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

	/* Precompute which bits of the TX register are to be updated, if any */
	if (!of_property_read_u32(np, "fsl,tx-cal-45-dn-ohms", &val) &&
	    val >= MXS_PHY_TX_CAL45_MIN && val <= MXS_PHY_TX_CAL45_MAX) {
		/* Scale to a 4-bit value */
		val = (MXS_PHY_TX_CAL45_MAX - val) * 0xF
			/ (MXS_PHY_TX_CAL45_MAX - MXS_PHY_TX_CAL45_MIN);
		mxs_phy->tx_reg_mask |= GM_USBPHY_TX_TXCAL45DN(~0);
		mxs_phy->tx_reg_set  |= GM_USBPHY_TX_TXCAL45DN(val);
	}

	if (!of_property_read_u32(np, "fsl,tx-cal-45-dp-ohms", &val) &&
	    val >= MXS_PHY_TX_CAL45_MIN && val <= MXS_PHY_TX_CAL45_MAX) {
		/* Scale to a 4-bit value. */
		val = (MXS_PHY_TX_CAL45_MAX - val) * 0xF
			/ (MXS_PHY_TX_CAL45_MAX - MXS_PHY_TX_CAL45_MIN);
		mxs_phy->tx_reg_mask |= GM_USBPHY_TX_TXCAL45DP(~0);
		mxs_phy->tx_reg_set  |= GM_USBPHY_TX_TXCAL45DP(val);
	}

	if (!of_property_read_u32(np, "fsl,tx-d-cal", &val) &&
	    val >= MXS_PHY_TX_D_CAL_MIN && val <= MXS_PHY_TX_D_CAL_MAX) {
		/* Scale to a 4-bit value.  Round up the values and heavily
		 * weight the rounding by adding 2/3 of the denominator.
		 */
		val = ((MXS_PHY_TX_D_CAL_MAX - val) * 0xF
			+ (MXS_PHY_TX_D_CAL_MAX - MXS_PHY_TX_D_CAL_MIN) * 2/3)
			/ (MXS_PHY_TX_D_CAL_MAX - MXS_PHY_TX_D_CAL_MIN);
		mxs_phy->tx_reg_mask |= GM_USBPHY_TX_D_CAL(~0);
		mxs_phy->tx_reg_set  |= GM_USBPHY_TX_D_CAL(val);
	}

	ret = of_alias_get_id(np, "usbphy");
	if (ret < 0)
		dev_dbg(&pdev->dev, "failed to get alias id, errno %d\n", ret);
	mxs_phy->port_id = ret;

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
	mxs_phy->phy.charger_detect	= mxs_phy_charger_detect;

	mxs_phy->clk = clk;
	mxs_phy->data = of_id->data;

	platform_set_drvdata(pdev, mxs_phy);

	device_set_wakeup_capable(&pdev->dev, true);

	return usb_add_phy_dev(&mxs_phy->phy);
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
	unsigned int reg = on ? ANADIG_ANA_MISC0_SET : ANADIG_ANA_MISC0_CLR;

	/* If the SoCs don't have anatop, quit */
	if (!mxs_phy->regmap_anatop)
		return;

	if (is_imx6q_phy(mxs_phy))
		regmap_write(mxs_phy->regmap_anatop, reg,
			BM_ANADIG_ANA_MISC0_STOP_MODE_CONFIG);
	else if (is_imx6sl_phy(mxs_phy))
		regmap_write(mxs_phy->regmap_anatop,
			reg, BM_ANADIG_ANA_MISC0_STOP_MODE_CONFIG_SL);
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
