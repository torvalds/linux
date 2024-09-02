// SPDX-License-Identifier: GPL-2.0+
/* drivers/net/phy/realtek.c
 *
 * Driver for Realtek PHYs
 *
 * Author: Johnson Leung <r58129@freescale.com>
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 */
#include <linux/bitops.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/clk.h>

#define RTL821x_PHYSR				0x11
#define RTL821x_PHYSR_DUPLEX			BIT(13)
#define RTL821x_PHYSR_SPEED			GENMASK(15, 14)

#define RTL821x_INER				0x12
#define RTL8211B_INER_INIT			0x6400
#define RTL8211E_INER_LINK_STATUS		BIT(10)
#define RTL8211F_INER_LINK_STATUS		BIT(4)

#define RTL821x_INSR				0x13

#define RTL821x_EXT_PAGE_SELECT			0x1e
#define RTL821x_PAGE_SELECT			0x1f

#define RTL8211F_PHYCR1				0x18
#define RTL8211F_PHYCR2				0x19
#define RTL8211F_INSR				0x1d

#define RTL8211F_LEDCR				0x10
#define RTL8211F_LEDCR_MODE			BIT(15)
#define RTL8211F_LEDCR_ACT_TXRX			BIT(4)
#define RTL8211F_LEDCR_LINK_1000		BIT(3)
#define RTL8211F_LEDCR_LINK_100			BIT(1)
#define RTL8211F_LEDCR_LINK_10			BIT(0)
#define RTL8211F_LEDCR_MASK			GENMASK(4, 0)
#define RTL8211F_LEDCR_SHIFT			5

#define RTL8211F_TX_DELAY			BIT(8)
#define RTL8211F_RX_DELAY			BIT(3)

#define RTL8211F_ALDPS_PLL_OFF			BIT(1)
#define RTL8211F_ALDPS_ENABLE			BIT(2)
#define RTL8211F_ALDPS_XTAL_OFF			BIT(12)

#define RTL8211E_CTRL_DELAY			BIT(13)
#define RTL8211E_TX_DELAY			BIT(12)
#define RTL8211E_RX_DELAY			BIT(11)

#define RTL8211F_CLKOUT_EN			BIT(0)

#define RTL8201F_ISR				0x1e
#define RTL8201F_ISR_ANERR			BIT(15)
#define RTL8201F_ISR_DUPLEX			BIT(13)
#define RTL8201F_ISR_LINK			BIT(11)
#define RTL8201F_ISR_MASK			(RTL8201F_ISR_ANERR | \
						 RTL8201F_ISR_DUPLEX | \
						 RTL8201F_ISR_LINK)
#define RTL8201F_IER				0x13

#define RTL822X_VND1_SERDES_OPTION			0x697a
#define RTL822X_VND1_SERDES_OPTION_MODE_MASK		GENMASK(5, 0)
#define RTL822X_VND1_SERDES_OPTION_MODE_2500BASEX_SGMII		0
#define RTL822X_VND1_SERDES_OPTION_MODE_2500BASEX		2

#define RTL822X_VND1_SERDES_CTRL3			0x7580
#define RTL822X_VND1_SERDES_CTRL3_MODE_MASK		GENMASK(5, 0)
#define RTL822X_VND1_SERDES_CTRL3_MODE_SGMII			0x02
#define RTL822X_VND1_SERDES_CTRL3_MODE_2500BASEX		0x16

/* RTL822X_VND2_XXXXX registers are only accessible when phydev->is_c45
 * is set, they cannot be accessed by C45-over-C22.
 */
#define RTL822X_VND2_GBCR				0xa412

#define RTL822X_VND2_GANLPAR				0xa414

#define RTL822X_VND2_PHYSR				0xa434

#define RTL8366RB_POWER_SAVE			0x15
#define RTL8366RB_POWER_SAVE_ON			BIT(12)

#define RTL9000A_GINMR				0x14
#define RTL9000A_GINMR_LINK_STATUS		BIT(4)

#define RTLGEN_SPEED_MASK			0x0630

#define RTL_GENERIC_PHYID			0x001cc800
#define RTL_8211FVD_PHYID			0x001cc878
#define RTL_8221B_VB_CG				0x001cc849
#define RTL_8221B_VN_CG				0x001cc84a
#define RTL_8251B				0x001cc862

#define RTL8211F_LED_COUNT			3

MODULE_DESCRIPTION("Realtek PHY driver");
MODULE_AUTHOR("Johnson Leung");
MODULE_LICENSE("GPL");

struct rtl821x_priv {
	u16 phycr1;
	u16 phycr2;
	bool has_phycr2;
	struct clk *clk;
};

static int rtl821x_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, RTL821x_PAGE_SELECT);
}

static int rtl821x_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, RTL821x_PAGE_SELECT, page);
}

static int rtl821x_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct rtl821x_priv *priv;
	u32 phy_id = phydev->drv->phy_id;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get_optional_enabled(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "failed to get phy clock\n");

	ret = phy_read_paged(phydev, 0xa43, RTL8211F_PHYCR1);
	if (ret < 0)
		return ret;

	priv->phycr1 = ret & (RTL8211F_ALDPS_PLL_OFF | RTL8211F_ALDPS_ENABLE | RTL8211F_ALDPS_XTAL_OFF);
	if (of_property_read_bool(dev->of_node, "realtek,aldps-enable"))
		priv->phycr1 |= RTL8211F_ALDPS_PLL_OFF | RTL8211F_ALDPS_ENABLE | RTL8211F_ALDPS_XTAL_OFF;

	priv->has_phycr2 = !(phy_id == RTL_8211FVD_PHYID);
	if (priv->has_phycr2) {
		ret = phy_read_paged(phydev, 0xa43, RTL8211F_PHYCR2);
		if (ret < 0)
			return ret;

		priv->phycr2 = ret & RTL8211F_CLKOUT_EN;
		if (of_property_read_bool(dev->of_node, "realtek,clkout-disable"))
			priv->phycr2 &= ~RTL8211F_CLKOUT_EN;
	}

	phydev->priv = priv;

	return 0;
}

static int rtl8201_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL8201F_ISR);

	return (err < 0) ? err : 0;
}

static int rtl821x_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL821x_INSR);

	return (err < 0) ? err : 0;
}

static int rtl8211f_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read_paged(phydev, 0xa43, RTL8211F_INSR);

	return (err < 0) ? err : 0;
}

static int rtl8201_config_intr(struct phy_device *phydev)
{
	u16 val;
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = rtl8201_ack_interrupt(phydev);
		if (err)
			return err;

		val = BIT(13) | BIT(12) | BIT(11);
		err = phy_write_paged(phydev, 0x7, RTL8201F_IER, val);
	} else {
		val = 0;
		err = phy_write_paged(phydev, 0x7, RTL8201F_IER, val);
		if (err)
			return err;

		err = rtl8201_ack_interrupt(phydev);
	}

	return err;
}

static int rtl8211b_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = rtl821x_ack_interrupt(phydev);
		if (err)
			return err;

		err = phy_write(phydev, RTL821x_INER,
				RTL8211B_INER_INIT);
	} else {
		err = phy_write(phydev, RTL821x_INER, 0);
		if (err)
			return err;

		err = rtl821x_ack_interrupt(phydev);
	}

	return err;
}

static int rtl8211e_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = rtl821x_ack_interrupt(phydev);
		if (err)
			return err;

		err = phy_write(phydev, RTL821x_INER,
				RTL8211E_INER_LINK_STATUS);
	} else {
		err = phy_write(phydev, RTL821x_INER, 0);
		if (err)
			return err;

		err = rtl821x_ack_interrupt(phydev);
	}

	return err;
}

static int rtl8211f_config_intr(struct phy_device *phydev)
{
	u16 val;
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = rtl8211f_ack_interrupt(phydev);
		if (err)
			return err;

		val = RTL8211F_INER_LINK_STATUS;
		err = phy_write_paged(phydev, 0xa42, RTL821x_INER, val);
	} else {
		val = 0;
		err = phy_write_paged(phydev, 0xa42, RTL821x_INER, val);
		if (err)
			return err;

		err = rtl8211f_ack_interrupt(phydev);
	}

	return err;
}

static irqreturn_t rtl8201_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read(phydev, RTL8201F_ISR);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & RTL8201F_ISR_MASK))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static irqreturn_t rtl821x_handle_interrupt(struct phy_device *phydev)
{
	int irq_status, irq_enabled;

	irq_status = phy_read(phydev, RTL821x_INSR);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	irq_enabled = phy_read(phydev, RTL821x_INER);
	if (irq_enabled < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & irq_enabled))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static irqreturn_t rtl8211f_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read_paged(phydev, 0xa43, RTL8211F_INSR);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & RTL8211F_INER_LINK_STATUS))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int rtl8211_config_aneg(struct phy_device *phydev)
{
	int ret;

	ret = genphy_config_aneg(phydev);
	if (ret < 0)
		return ret;

	/* Quirk was copied from vendor driver. Unfortunately it includes no
	 * description of the magic numbers.
	 */
	if (phydev->speed == SPEED_100 && phydev->autoneg == AUTONEG_DISABLE) {
		phy_write(phydev, 0x17, 0x2138);
		phy_write(phydev, 0x0e, 0x0260);
	} else {
		phy_write(phydev, 0x17, 0x2108);
		phy_write(phydev, 0x0e, 0x0000);
	}

	return 0;
}

static int rtl8211c_config_init(struct phy_device *phydev)
{
	/* RTL8211C has an issue when operating in Gigabit slave mode */
	return phy_set_bits(phydev, MII_CTRL1000,
			    CTL1000_ENABLE_MASTER | CTL1000_AS_MASTER);
}

static int rtl8211f_config_init(struct phy_device *phydev)
{
	struct rtl821x_priv *priv = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	u16 val_txdly, val_rxdly;
	int ret;

	ret = phy_modify_paged_changed(phydev, 0xa43, RTL8211F_PHYCR1,
				       RTL8211F_ALDPS_PLL_OFF | RTL8211F_ALDPS_ENABLE | RTL8211F_ALDPS_XTAL_OFF,
				       priv->phycr1);
	if (ret < 0) {
		dev_err(dev, "aldps mode  configuration failed: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		val_txdly = 0;
		val_rxdly = 0;
		break;

	case PHY_INTERFACE_MODE_RGMII_RXID:
		val_txdly = 0;
		val_rxdly = RTL8211F_RX_DELAY;
		break;

	case PHY_INTERFACE_MODE_RGMII_TXID:
		val_txdly = RTL8211F_TX_DELAY;
		val_rxdly = 0;
		break;

	case PHY_INTERFACE_MODE_RGMII_ID:
		val_txdly = RTL8211F_TX_DELAY;
		val_rxdly = RTL8211F_RX_DELAY;
		break;

	default: /* the rest of the modes imply leaving delay as is. */
		return 0;
	}

	ret = phy_modify_paged_changed(phydev, 0xd08, 0x11, RTL8211F_TX_DELAY,
				       val_txdly);
	if (ret < 0) {
		dev_err(dev, "Failed to update the TX delay register\n");
		return ret;
	} else if (ret) {
		dev_dbg(dev,
			"%s 2ns TX delay (and changing the value from pin-strapping RXD1 or the bootloader)\n",
			val_txdly ? "Enabling" : "Disabling");
	} else {
		dev_dbg(dev,
			"2ns TX delay was already %s (by pin-strapping RXD1 or bootloader configuration)\n",
			val_txdly ? "enabled" : "disabled");
	}

	ret = phy_modify_paged_changed(phydev, 0xd08, 0x15, RTL8211F_RX_DELAY,
				       val_rxdly);
	if (ret < 0) {
		dev_err(dev, "Failed to update the RX delay register\n");
		return ret;
	} else if (ret) {
		dev_dbg(dev,
			"%s 2ns RX delay (and changing the value from pin-strapping RXD0 or the bootloader)\n",
			val_rxdly ? "Enabling" : "Disabling");
	} else {
		dev_dbg(dev,
			"2ns RX delay was already %s (by pin-strapping RXD0 or bootloader configuration)\n",
			val_rxdly ? "enabled" : "disabled");
	}

	if (priv->has_phycr2) {
		ret = phy_modify_paged(phydev, 0xa43, RTL8211F_PHYCR2,
				       RTL8211F_CLKOUT_EN, priv->phycr2);
		if (ret < 0) {
			dev_err(dev, "clkout configuration failed: %pe\n",
				ERR_PTR(ret));
			return ret;
		}

		return genphy_soft_reset(phydev);
	}

	return 0;
}

static int rtl821x_suspend(struct phy_device *phydev)
{
	struct rtl821x_priv *priv = phydev->priv;
	int ret = 0;

	if (!phydev->wol_enabled) {
		ret = genphy_suspend(phydev);

		if (ret)
			return ret;

		clk_disable_unprepare(priv->clk);
	}

	return ret;
}

static int rtl821x_resume(struct phy_device *phydev)
{
	struct rtl821x_priv *priv = phydev->priv;
	int ret;

	if (!phydev->wol_enabled)
		clk_prepare_enable(priv->clk);

	ret = genphy_resume(phydev);
	if (ret < 0)
		return ret;

	msleep(20);

	return 0;
}

static int rtl8211f_led_hw_is_supported(struct phy_device *phydev, u8 index,
					unsigned long rules)
{
	const unsigned long mask = BIT(TRIGGER_NETDEV_LINK_10) |
				   BIT(TRIGGER_NETDEV_LINK_100) |
				   BIT(TRIGGER_NETDEV_LINK_1000) |
				   BIT(TRIGGER_NETDEV_RX) |
				   BIT(TRIGGER_NETDEV_TX);

	/* The RTL8211F PHY supports these LED settings on up to three LEDs:
	 * - Link: Configurable subset of 10/100/1000 link rates
	 * - Active: Blink on activity, RX or TX is not differentiated
	 * The Active option has two modes, A and B:
	 * - A: Link and Active indication at configurable, but matching,
	 *      subset of 10/100/1000 link rates
	 * - B: Link indication at configurable subset of 10/100/1000 link
	 *      rates and Active indication always at all three 10+100+1000
	 *      link rates.
	 * This code currently uses mode B only.
	 */

	if (index >= RTL8211F_LED_COUNT)
		return -EINVAL;

	/* Filter out any other unsupported triggers. */
	if (rules & ~mask)
		return -EOPNOTSUPP;

	/* RX and TX are not differentiated, either both are set or not set. */
	if (!(rules & BIT(TRIGGER_NETDEV_RX)) ^ !(rules & BIT(TRIGGER_NETDEV_TX)))
		return -EOPNOTSUPP;

	return 0;
}

static int rtl8211f_led_hw_control_get(struct phy_device *phydev, u8 index,
				       unsigned long *rules)
{
	int val;

	val = phy_read_paged(phydev, 0xd04, RTL8211F_LEDCR);
	if (val < 0)
		return val;

	val >>= RTL8211F_LEDCR_SHIFT * index;
	val &= RTL8211F_LEDCR_MASK;

	if (val & RTL8211F_LEDCR_LINK_10)
		set_bit(TRIGGER_NETDEV_LINK_10, rules);

	if (val & RTL8211F_LEDCR_LINK_100)
		set_bit(TRIGGER_NETDEV_LINK_100, rules);

	if (val & RTL8211F_LEDCR_LINK_1000)
		set_bit(TRIGGER_NETDEV_LINK_1000, rules);

	if (val & RTL8211F_LEDCR_ACT_TXRX) {
		set_bit(TRIGGER_NETDEV_RX, rules);
		set_bit(TRIGGER_NETDEV_TX, rules);
	}

	return 0;
}

static int rtl8211f_led_hw_control_set(struct phy_device *phydev, u8 index,
				       unsigned long rules)
{
	const u16 mask = RTL8211F_LEDCR_MASK << (RTL8211F_LEDCR_SHIFT * index);
	u16 reg = 0;

	if (index >= RTL8211F_LED_COUNT)
		return -EINVAL;

	if (test_bit(TRIGGER_NETDEV_LINK_10, &rules))
		reg |= RTL8211F_LEDCR_LINK_10;

	if (test_bit(TRIGGER_NETDEV_LINK_100, &rules))
		reg |= RTL8211F_LEDCR_LINK_100;

	if (test_bit(TRIGGER_NETDEV_LINK_1000, &rules))
		reg |= RTL8211F_LEDCR_LINK_1000;

	if (test_bit(TRIGGER_NETDEV_RX, &rules) ||
	    test_bit(TRIGGER_NETDEV_TX, &rules)) {
		reg |= RTL8211F_LEDCR_ACT_TXRX;
	}

	reg <<= RTL8211F_LEDCR_SHIFT * index;
	reg |= RTL8211F_LEDCR_MODE;	 /* Mode B */

	return phy_modify_paged(phydev, 0xd04, RTL8211F_LEDCR, mask, reg);
}

static int rtl8211e_config_init(struct phy_device *phydev)
{
	int ret = 0, oldpage;
	u16 val;

	/* enable TX/RX delay for rgmii-* modes, and disable them for rgmii. */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		val = RTL8211E_CTRL_DELAY | 0;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		val = RTL8211E_CTRL_DELAY | RTL8211E_TX_DELAY | RTL8211E_RX_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		val = RTL8211E_CTRL_DELAY | RTL8211E_RX_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = RTL8211E_CTRL_DELAY | RTL8211E_TX_DELAY;
		break;
	default: /* the rest of the modes imply leaving delays as is. */
		return 0;
	}

	/* According to a sample driver there is a 0x1c config register on the
	 * 0xa4 extension page (0x7) layout. It can be used to disable/enable
	 * the RX/TX delays otherwise controlled by RXDLY/TXDLY pins.
	 * The configuration register definition:
	 * 14 = reserved
	 * 13 = Force Tx RX Delay controlled by bit12 bit11,
	 * 12 = RX Delay, 11 = TX Delay
	 * 10:0 = Test && debug settings reserved by realtek
	 */
	oldpage = phy_select_page(phydev, 0x7);
	if (oldpage < 0)
		goto err_restore_page;

	ret = __phy_write(phydev, RTL821x_EXT_PAGE_SELECT, 0xa4);
	if (ret)
		goto err_restore_page;

	ret = __phy_modify(phydev, 0x1c, RTL8211E_CTRL_DELAY
			   | RTL8211E_TX_DELAY | RTL8211E_RX_DELAY,
			   val);

err_restore_page:
	return phy_restore_page(phydev, oldpage, ret);
}

static int rtl8211b_suspend(struct phy_device *phydev)
{
	phy_write(phydev, MII_MMD_DATA, BIT(9));

	return genphy_suspend(phydev);
}

static int rtl8211b_resume(struct phy_device *phydev)
{
	phy_write(phydev, MII_MMD_DATA, 0);

	return genphy_resume(phydev);
}

static int rtl8366rb_config_init(struct phy_device *phydev)
{
	int ret;

	ret = phy_set_bits(phydev, RTL8366RB_POWER_SAVE,
			   RTL8366RB_POWER_SAVE_ON);
	if (ret) {
		dev_err(&phydev->mdio.dev,
			"error enabling power management\n");
	}

	return ret;
}

/* get actual speed to cover the downshift case */
static void rtlgen_decode_speed(struct phy_device *phydev, int val)
{
	switch (val & RTLGEN_SPEED_MASK) {
	case 0x0000:
		phydev->speed = SPEED_10;
		break;
	case 0x0010:
		phydev->speed = SPEED_100;
		break;
	case 0x0020:
		phydev->speed = SPEED_1000;
		break;
	case 0x0200:
		phydev->speed = SPEED_10000;
		break;
	case 0x0210:
		phydev->speed = SPEED_2500;
		break;
	case 0x0220:
		phydev->speed = SPEED_5000;
		break;
	default:
		break;
	}
}

static int rtlgen_read_status(struct phy_device *phydev)
{
	int ret, val;

	ret = genphy_read_status(phydev);
	if (ret < 0)
		return ret;

	if (!phydev->link)
		return 0;

	val = phy_read_paged(phydev, 0xa43, 0x12);
	if (val < 0)
		return val;

	rtlgen_decode_speed(phydev, val);

	return 0;
}

static int rtlgen_read_mmd(struct phy_device *phydev, int devnum, u16 regnum)
{
	int ret;

	if (devnum == MDIO_MMD_PCS && regnum == MDIO_PCS_EEE_ABLE) {
		rtl821x_write_page(phydev, 0xa5c);
		ret = __phy_read(phydev, 0x12);
		rtl821x_write_page(phydev, 0);
	} else if (devnum == MDIO_MMD_AN && regnum == MDIO_AN_EEE_ADV) {
		rtl821x_write_page(phydev, 0xa5d);
		ret = __phy_read(phydev, 0x10);
		rtl821x_write_page(phydev, 0);
	} else if (devnum == MDIO_MMD_AN && regnum == MDIO_AN_EEE_LPABLE) {
		rtl821x_write_page(phydev, 0xa5d);
		ret = __phy_read(phydev, 0x11);
		rtl821x_write_page(phydev, 0);
	} else {
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int rtlgen_write_mmd(struct phy_device *phydev, int devnum, u16 regnum,
			    u16 val)
{
	int ret;

	if (devnum == MDIO_MMD_AN && regnum == MDIO_AN_EEE_ADV) {
		rtl821x_write_page(phydev, 0xa5d);
		ret = __phy_write(phydev, 0x10, val);
		rtl821x_write_page(phydev, 0);
	} else {
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int rtl822x_read_mmd(struct phy_device *phydev, int devnum, u16 regnum)
{
	int ret = rtlgen_read_mmd(phydev, devnum, regnum);

	if (ret != -EOPNOTSUPP)
		return ret;

	if (devnum == MDIO_MMD_PCS && regnum == MDIO_PCS_EEE_ABLE2) {
		rtl821x_write_page(phydev, 0xa6e);
		ret = __phy_read(phydev, 0x16);
		rtl821x_write_page(phydev, 0);
	} else if (devnum == MDIO_MMD_AN && regnum == MDIO_AN_EEE_ADV2) {
		rtl821x_write_page(phydev, 0xa6d);
		ret = __phy_read(phydev, 0x12);
		rtl821x_write_page(phydev, 0);
	} else if (devnum == MDIO_MMD_AN && regnum == MDIO_AN_EEE_LPABLE2) {
		rtl821x_write_page(phydev, 0xa6d);
		ret = __phy_read(phydev, 0x10);
		rtl821x_write_page(phydev, 0);
	}

	return ret;
}

static int rtl822x_write_mmd(struct phy_device *phydev, int devnum, u16 regnum,
			     u16 val)
{
	int ret = rtlgen_write_mmd(phydev, devnum, regnum, val);

	if (ret != -EOPNOTSUPP)
		return ret;

	if (devnum == MDIO_MMD_AN && regnum == MDIO_AN_EEE_ADV2) {
		rtl821x_write_page(phydev, 0xa6d);
		ret = __phy_write(phydev, 0x12, val);
		rtl821x_write_page(phydev, 0);
	}

	return ret;
}

static int rtl822xb_config_init(struct phy_device *phydev)
{
	bool has_2500, has_sgmii;
	u16 mode;
	int ret;

	has_2500 = test_bit(PHY_INTERFACE_MODE_2500BASEX,
			    phydev->host_interfaces) ||
		   phydev->interface == PHY_INTERFACE_MODE_2500BASEX;

	has_sgmii = test_bit(PHY_INTERFACE_MODE_SGMII,
			     phydev->host_interfaces) ||
		    phydev->interface == PHY_INTERFACE_MODE_SGMII;

	/* fill in possible interfaces */
	__assign_bit(PHY_INTERFACE_MODE_2500BASEX, phydev->possible_interfaces,
		     has_2500);
	__assign_bit(PHY_INTERFACE_MODE_SGMII, phydev->possible_interfaces,
		     has_sgmii);

	if (!has_2500 && !has_sgmii)
		return 0;

	/* determine SerDes option mode */
	if (has_2500 && !has_sgmii) {
		mode = RTL822X_VND1_SERDES_OPTION_MODE_2500BASEX;
		phydev->rate_matching = RATE_MATCH_PAUSE;
	} else {
		mode = RTL822X_VND1_SERDES_OPTION_MODE_2500BASEX_SGMII;
		phydev->rate_matching = RATE_MATCH_NONE;
	}

	/* the following sequence with magic numbers sets up the SerDes
	 * option mode
	 */
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x75f3, 0);
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd_changed(phydev, MDIO_MMD_VEND1,
				     RTL822X_VND1_SERDES_OPTION,
				     RTL822X_VND1_SERDES_OPTION_MODE_MASK,
				     mode);
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x6a04, 0x0503);
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x6f10, 0xd455);
	if (ret < 0)
		return ret;

	return phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x6f11, 0x8020);
}

static int rtl822xb_get_rate_matching(struct phy_device *phydev,
				      phy_interface_t iface)
{
	int val;

	/* Only rate matching at 2500base-x */
	if (iface != PHY_INTERFACE_MODE_2500BASEX)
		return RATE_MATCH_NONE;

	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, RTL822X_VND1_SERDES_OPTION);
	if (val < 0)
		return val;

	if ((val & RTL822X_VND1_SERDES_OPTION_MODE_MASK) ==
	    RTL822X_VND1_SERDES_OPTION_MODE_2500BASEX)
		return RATE_MATCH_PAUSE;

	/* RTL822X_VND1_SERDES_OPTION_MODE_2500BASEX_SGMII */
	return RATE_MATCH_NONE;
}

static int rtl822x_get_features(struct phy_device *phydev)
{
	int val;

	val = phy_read_paged(phydev, 0xa61, 0x13);
	if (val < 0)
		return val;

	linkmode_mod_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
			 phydev->supported, val & MDIO_PMA_SPEED_2_5G);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_5000baseT_Full_BIT,
			 phydev->supported, val & MDIO_PMA_SPEED_5G);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
			 phydev->supported, val & MDIO_SPEED_10G);

	return genphy_read_abilities(phydev);
}

static int rtl822x_config_aneg(struct phy_device *phydev)
{
	int ret = 0;

	if (phydev->autoneg == AUTONEG_ENABLE) {
		u16 adv = linkmode_adv_to_mii_10gbt_adv_t(phydev->advertising);

		ret = phy_modify_paged_changed(phydev, 0xa5d, 0x12,
					       MDIO_AN_10GBT_CTRL_ADV2_5G |
					       MDIO_AN_10GBT_CTRL_ADV5G,
					       adv);
		if (ret < 0)
			return ret;
	}

	return __genphy_config_aneg(phydev, ret);
}

static void rtl822xb_update_interface(struct phy_device *phydev)
{
	int val;

	if (!phydev->link)
		return;

	/* Change interface according to serdes mode */
	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, RTL822X_VND1_SERDES_CTRL3);
	if (val < 0)
		return;

	switch (val & RTL822X_VND1_SERDES_CTRL3_MODE_MASK) {
	case RTL822X_VND1_SERDES_CTRL3_MODE_2500BASEX:
		phydev->interface = PHY_INTERFACE_MODE_2500BASEX;
		break;
	case RTL822X_VND1_SERDES_CTRL3_MODE_SGMII:
		phydev->interface = PHY_INTERFACE_MODE_SGMII;
		break;
	}
}

static int rtl822x_read_status(struct phy_device *phydev)
{
	if (phydev->autoneg == AUTONEG_ENABLE) {
		int lpadv = phy_read_paged(phydev, 0xa5d, 0x13);

		if (lpadv < 0)
			return lpadv;

		mii_10gbt_stat_mod_linkmode_lpa_t(phydev->lp_advertising,
						  lpadv);
	}

	return rtlgen_read_status(phydev);
}

static int rtl822xb_read_status(struct phy_device *phydev)
{
	int ret;

	ret = rtl822x_read_status(phydev);
	if (ret < 0)
		return ret;

	rtl822xb_update_interface(phydev);

	return 0;
}

static int rtl822x_c45_get_features(struct phy_device *phydev)
{
	linkmode_set_bit(ETHTOOL_LINK_MODE_TP_BIT,
			 phydev->supported);

	return genphy_c45_pma_read_abilities(phydev);
}

static int rtl822x_c45_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	int ret, val;

	if (phydev->autoneg == AUTONEG_DISABLE)
		return genphy_c45_pma_setup_forced(phydev);

	ret = genphy_c45_an_config_aneg(phydev);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	val = linkmode_adv_to_mii_ctrl1000_t(phydev->advertising);

	/* Vendor register as C45 has no standardized support for 1000BaseT */
	ret = phy_modify_mmd_changed(phydev, MDIO_MMD_VEND2, RTL822X_VND2_GBCR,
				     ADVERTISE_1000FULL, val);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	return genphy_c45_check_and_restart_aneg(phydev, changed);
}

static int rtl822x_c45_read_status(struct phy_device *phydev)
{
	int ret, val;

	ret = genphy_c45_read_status(phydev);
	if (ret < 0)
		return ret;

	/* Vendor register as C45 has no standardized support for 1000BaseT */
	if (phydev->autoneg == AUTONEG_ENABLE) {
		val = phy_read_mmd(phydev, MDIO_MMD_VEND2,
				   RTL822X_VND2_GANLPAR);
		if (val < 0)
			return val;

		mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising, val);
	}

	if (!phydev->link)
		return 0;

	/* Read actual speed from vendor register. */
	val = phy_read_mmd(phydev, MDIO_MMD_VEND2, RTL822X_VND2_PHYSR);
	if (val < 0)
		return val;

	rtlgen_decode_speed(phydev, val);

	return 0;
}

static int rtl822xb_c45_read_status(struct phy_device *phydev)
{
	int ret;

	ret = rtl822x_c45_read_status(phydev);
	if (ret < 0)
		return ret;

	rtl822xb_update_interface(phydev);

	return 0;
}

static bool rtlgen_supports_2_5gbps(struct phy_device *phydev)
{
	int val;

	phy_write(phydev, RTL821x_PAGE_SELECT, 0xa61);
	val = phy_read(phydev, 0x13);
	phy_write(phydev, RTL821x_PAGE_SELECT, 0);

	return val >= 0 && val & MDIO_PMA_SPEED_2_5G;
}

static int rtlgen_match_phy_device(struct phy_device *phydev)
{
	return phydev->phy_id == RTL_GENERIC_PHYID &&
	       !rtlgen_supports_2_5gbps(phydev);
}

static int rtl8226_match_phy_device(struct phy_device *phydev)
{
	return phydev->phy_id == RTL_GENERIC_PHYID &&
	       rtlgen_supports_2_5gbps(phydev);
}

static int rtlgen_is_c45_match(struct phy_device *phydev, unsigned int id,
			       bool is_c45)
{
	if (phydev->is_c45)
		return is_c45 && (id == phydev->c45_ids.device_ids[1]);
	else
		return !is_c45 && (id == phydev->phy_id);
}

static int rtl8221b_vb_cg_c22_match_phy_device(struct phy_device *phydev)
{
	return rtlgen_is_c45_match(phydev, RTL_8221B_VB_CG, false);
}

static int rtl8221b_vb_cg_c45_match_phy_device(struct phy_device *phydev)
{
	return rtlgen_is_c45_match(phydev, RTL_8221B_VB_CG, true);
}

static int rtl8221b_vn_cg_c22_match_phy_device(struct phy_device *phydev)
{
	return rtlgen_is_c45_match(phydev, RTL_8221B_VN_CG, false);
}

static int rtl8221b_vn_cg_c45_match_phy_device(struct phy_device *phydev)
{
	return rtlgen_is_c45_match(phydev, RTL_8221B_VN_CG, true);
}

static int rtlgen_resume(struct phy_device *phydev)
{
	int ret = genphy_resume(phydev);

	/* Internal PHY's from RTL8168h up may not be instantly ready */
	msleep(20);

	return ret;
}

static int rtlgen_c45_resume(struct phy_device *phydev)
{
	int ret = genphy_c45_pma_resume(phydev);

	msleep(20);

	return ret;
}

static int rtl9000a_config_init(struct phy_device *phydev)
{
	phydev->autoneg = AUTONEG_DISABLE;
	phydev->speed = SPEED_100;
	phydev->duplex = DUPLEX_FULL;

	return 0;
}

static int rtl9000a_config_aneg(struct phy_device *phydev)
{
	int ret;
	u16 ctl = 0;

	switch (phydev->master_slave_set) {
	case MASTER_SLAVE_CFG_MASTER_FORCE:
		ctl |= CTL1000_AS_MASTER;
		break;
	case MASTER_SLAVE_CFG_SLAVE_FORCE:
		break;
	case MASTER_SLAVE_CFG_UNKNOWN:
	case MASTER_SLAVE_CFG_UNSUPPORTED:
		return 0;
	default:
		phydev_warn(phydev, "Unsupported Master/Slave mode\n");
		return -EOPNOTSUPP;
	}

	ret = phy_modify_changed(phydev, MII_CTRL1000, CTL1000_AS_MASTER, ctl);
	if (ret == 1)
		ret = genphy_soft_reset(phydev);

	return ret;
}

static int rtl9000a_read_status(struct phy_device *phydev)
{
	int ret;

	phydev->master_slave_get = MASTER_SLAVE_CFG_UNKNOWN;
	phydev->master_slave_state = MASTER_SLAVE_STATE_UNKNOWN;

	ret = genphy_update_link(phydev);
	if (ret)
		return ret;

	ret = phy_read(phydev, MII_CTRL1000);
	if (ret < 0)
		return ret;
	if (ret & CTL1000_AS_MASTER)
		phydev->master_slave_get = MASTER_SLAVE_CFG_MASTER_FORCE;
	else
		phydev->master_slave_get = MASTER_SLAVE_CFG_SLAVE_FORCE;

	ret = phy_read(phydev, MII_STAT1000);
	if (ret < 0)
		return ret;
	if (ret & LPA_1000MSRES)
		phydev->master_slave_state = MASTER_SLAVE_STATE_MASTER;
	else
		phydev->master_slave_state = MASTER_SLAVE_STATE_SLAVE;

	return 0;
}

static int rtl9000a_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL8211F_INSR);

	return (err < 0) ? err : 0;
}

static int rtl9000a_config_intr(struct phy_device *phydev)
{
	u16 val;
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = rtl9000a_ack_interrupt(phydev);
		if (err)
			return err;

		val = (u16)~RTL9000A_GINMR_LINK_STATUS;
		err = phy_write_paged(phydev, 0xa42, RTL9000A_GINMR, val);
	} else {
		val = ~0;
		err = phy_write_paged(phydev, 0xa42, RTL9000A_GINMR, val);
		if (err)
			return err;

		err = rtl9000a_ack_interrupt(phydev);
	}

	return phy_write_paged(phydev, 0xa42, RTL9000A_GINMR, val);
}

static irqreturn_t rtl9000a_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read(phydev, RTL8211F_INSR);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & RTL8211F_INER_LINK_STATUS))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static struct phy_driver realtek_drvs[] = {
	{
		PHY_ID_MATCH_EXACT(0x00008201),
		.name           = "RTL8201CP Ethernet",
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc816),
		.name		= "RTL8201F Fast Ethernet",
		.config_intr	= &rtl8201_config_intr,
		.handle_interrupt = rtl8201_handle_interrupt,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_MODEL(0x001cc880),
		.name		= "RTL8208 Fast Ethernet",
		.read_mmd	= genphy_read_mmd_unsupported,
		.write_mmd	= genphy_write_mmd_unsupported,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc910),
		.name		= "RTL8211 Gigabit Ethernet",
		.config_aneg	= rtl8211_config_aneg,
		.read_mmd	= &genphy_read_mmd_unsupported,
		.write_mmd	= &genphy_write_mmd_unsupported,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc912),
		.name		= "RTL8211B Gigabit Ethernet",
		.config_intr	= &rtl8211b_config_intr,
		.handle_interrupt = rtl821x_handle_interrupt,
		.read_mmd	= &genphy_read_mmd_unsupported,
		.write_mmd	= &genphy_write_mmd_unsupported,
		.suspend	= rtl8211b_suspend,
		.resume		= rtl8211b_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc913),
		.name		= "RTL8211C Gigabit Ethernet",
		.config_init	= rtl8211c_config_init,
		.read_mmd	= &genphy_read_mmd_unsupported,
		.write_mmd	= &genphy_write_mmd_unsupported,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc914),
		.name		= "RTL8211DN Gigabit Ethernet",
		.config_intr	= rtl8211e_config_intr,
		.handle_interrupt = rtl821x_handle_interrupt,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc915),
		.name		= "RTL8211E Gigabit Ethernet",
		.config_init	= &rtl8211e_config_init,
		.config_intr	= &rtl8211e_config_intr,
		.handle_interrupt = rtl821x_handle_interrupt,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc916),
		.name		= "RTL8211F Gigabit Ethernet",
		.probe		= rtl821x_probe,
		.config_init	= &rtl8211f_config_init,
		.read_status	= rtlgen_read_status,
		.config_intr	= &rtl8211f_config_intr,
		.handle_interrupt = rtl8211f_handle_interrupt,
		.suspend	= rtl821x_suspend,
		.resume		= rtl821x_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
		.flags		= PHY_ALWAYS_CALL_SUSPEND,
		.led_hw_is_supported = rtl8211f_led_hw_is_supported,
		.led_hw_control_get = rtl8211f_led_hw_control_get,
		.led_hw_control_set = rtl8211f_led_hw_control_set,
	}, {
		PHY_ID_MATCH_EXACT(RTL_8211FVD_PHYID),
		.name		= "RTL8211F-VD Gigabit Ethernet",
		.probe		= rtl821x_probe,
		.config_init	= &rtl8211f_config_init,
		.read_status	= rtlgen_read_status,
		.config_intr	= &rtl8211f_config_intr,
		.handle_interrupt = rtl8211f_handle_interrupt,
		.suspend	= rtl821x_suspend,
		.resume		= rtl821x_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
		.flags		= PHY_ALWAYS_CALL_SUSPEND,
	}, {
		.name		= "Generic FE-GE Realtek PHY",
		.match_phy_device = rtlgen_match_phy_device,
		.read_status	= rtlgen_read_status,
		.suspend	= genphy_suspend,
		.resume		= rtlgen_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
		.read_mmd	= rtlgen_read_mmd,
		.write_mmd	= rtlgen_write_mmd,
	}, {
		.name		= "RTL8226 2.5Gbps PHY",
		.match_phy_device = rtl8226_match_phy_device,
		.get_features	= rtl822x_get_features,
		.config_aneg	= rtl822x_config_aneg,
		.read_status	= rtl822x_read_status,
		.suspend	= genphy_suspend,
		.resume		= rtlgen_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
		.read_mmd	= rtl822x_read_mmd,
		.write_mmd	= rtl822x_write_mmd,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc840),
		.name		= "RTL8226B_RTL8221B 2.5Gbps PHY",
		.get_features	= rtl822x_get_features,
		.config_aneg	= rtl822x_config_aneg,
		.config_init    = rtl822xb_config_init,
		.get_rate_matching = rtl822xb_get_rate_matching,
		.read_status	= rtl822xb_read_status,
		.suspend	= genphy_suspend,
		.resume		= rtlgen_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
		.read_mmd	= rtl822x_read_mmd,
		.write_mmd	= rtl822x_write_mmd,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc838),
		.name           = "RTL8226-CG 2.5Gbps PHY",
		.get_features   = rtl822x_get_features,
		.config_aneg    = rtl822x_config_aneg,
		.read_status    = rtl822x_read_status,
		.suspend        = genphy_suspend,
		.resume         = rtlgen_resume,
		.read_page      = rtl821x_read_page,
		.write_page     = rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc848),
		.name           = "RTL8226B-CG_RTL8221B-CG 2.5Gbps PHY",
		.get_features   = rtl822x_get_features,
		.config_aneg    = rtl822x_config_aneg,
		.config_init    = rtl822xb_config_init,
		.get_rate_matching = rtl822xb_get_rate_matching,
		.read_status    = rtl822xb_read_status,
		.suspend        = genphy_suspend,
		.resume         = rtlgen_resume,
		.read_page      = rtl821x_read_page,
		.write_page     = rtl821x_write_page,
	}, {
		.match_phy_device = rtl8221b_vb_cg_c22_match_phy_device,
		.name           = "RTL8221B-VB-CG 2.5Gbps PHY (C22)",
		.get_features   = rtl822x_get_features,
		.config_aneg    = rtl822x_config_aneg,
		.config_init    = rtl822xb_config_init,
		.get_rate_matching = rtl822xb_get_rate_matching,
		.read_status    = rtl822xb_read_status,
		.suspend        = genphy_suspend,
		.resume         = rtlgen_resume,
		.read_page      = rtl821x_read_page,
		.write_page     = rtl821x_write_page,
	}, {
		.match_phy_device = rtl8221b_vb_cg_c45_match_phy_device,
		.name           = "RTL8221B-VB-CG 2.5Gbps PHY (C45)",
		.config_init    = rtl822xb_config_init,
		.get_rate_matching = rtl822xb_get_rate_matching,
		.get_features   = rtl822x_c45_get_features,
		.config_aneg    = rtl822x_c45_config_aneg,
		.read_status    = rtl822xb_c45_read_status,
		.suspend        = genphy_c45_pma_suspend,
		.resume         = rtlgen_c45_resume,
	}, {
		.match_phy_device = rtl8221b_vn_cg_c22_match_phy_device,
		.name           = "RTL8221B-VM-CG 2.5Gbps PHY (C22)",
		.get_features   = rtl822x_get_features,
		.config_aneg    = rtl822x_config_aneg,
		.config_init    = rtl822xb_config_init,
		.get_rate_matching = rtl822xb_get_rate_matching,
		.read_status    = rtl822xb_read_status,
		.suspend        = genphy_suspend,
		.resume         = rtlgen_resume,
		.read_page      = rtl821x_read_page,
		.write_page     = rtl821x_write_page,
	}, {
		.match_phy_device = rtl8221b_vn_cg_c45_match_phy_device,
		.name           = "RTL8221B-VN-CG 2.5Gbps PHY (C45)",
		.config_init    = rtl822xb_config_init,
		.get_rate_matching = rtl822xb_get_rate_matching,
		.get_features   = rtl822x_c45_get_features,
		.config_aneg    = rtl822x_c45_config_aneg,
		.read_status    = rtl822xb_c45_read_status,
		.suspend        = genphy_c45_pma_suspend,
		.resume         = rtlgen_c45_resume,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc862),
		.name           = "RTL8251B 5Gbps PHY",
		.get_features   = rtl822x_get_features,
		.config_aneg    = rtl822x_config_aneg,
		.read_status    = rtl822x_read_status,
		.suspend        = genphy_suspend,
		.resume         = rtlgen_resume,
		.read_page      = rtl821x_read_page,
		.write_page     = rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001ccad0),
		.name		= "RTL8224 2.5Gbps PHY",
		.get_features   = rtl822x_c45_get_features,
		.config_aneg    = rtl822x_c45_config_aneg,
		.read_status    = rtl822x_c45_read_status,
		.suspend        = genphy_c45_pma_suspend,
		.resume         = rtlgen_c45_resume,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc961),
		.name		= "RTL8366RB Gigabit Ethernet",
		.config_init	= &rtl8366rb_config_init,
		/* These interrupts are handled by the irq controller
		 * embedded inside the RTL8366RB, they get unmasked when the
		 * irq is requested and ACKed by reading the status register,
		 * which is done by the irqchip code.
		 */
		.config_intr	= genphy_no_config_intr,
		.handle_interrupt = genphy_handle_interrupt_no_ack,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	}, {
		PHY_ID_MATCH_EXACT(0x001ccb00),
		.name		= "RTL9000AA_RTL9000AN Ethernet",
		.features       = PHY_BASIC_T1_FEATURES,
		.config_init	= rtl9000a_config_init,
		.config_aneg	= rtl9000a_config_aneg,
		.read_status	= rtl9000a_read_status,
		.config_intr	= rtl9000a_config_intr,
		.handle_interrupt = rtl9000a_handle_interrupt,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc942),
		.name		= "RTL8365MB-VC Gigabit Ethernet",
		/* Interrupt handling analogous to RTL8366RB */
		.config_intr	= genphy_no_config_intr,
		.handle_interrupt = genphy_handle_interrupt_no_ack,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc960),
		.name		= "RTL8366S Gigabit Ethernet",
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_mmd	= genphy_read_mmd_unsupported,
		.write_mmd	= genphy_write_mmd_unsupported,
	},
};

module_phy_driver(realtek_drvs);

static const struct mdio_device_id __maybe_unused realtek_tbl[] = {
	{ PHY_ID_MATCH_VENDOR(0x001cc800) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, realtek_tbl);
