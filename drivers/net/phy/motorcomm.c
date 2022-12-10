// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Motorcomm PHYs
 *
 * Author: Peter Geis <pgwipeout@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/phy.h>

#define PHY_ID_YT8511		0x0000010a
#define PHY_ID_YT8512		0x00000118
#define PHY_ID_YT8512B		0x00000128
#define PHY_ID_YT8531S		0x4f51e91a
#define PHY_ID_YT8531		0x4f51e91b

#define YT8511_PAGE_SELECT	0x1e
#define YT8511_PAGE		0x1f
#define YT8511_EXT_CLK_GATE	0x0c
#define YT8511_EXT_DELAY_DRIVE	0x0d
#define YT8511_EXT_SLEEP_CTRL	0x27

/* 2b00 25m from pll
 * 2b01 25m from xtl *default*
 * 2b10 62.m from pll
 * 2b11 125m from pll
 */
#define YT8511_CLK_125M		(BIT(2) | BIT(1))
#define YT8511_PLLON_SLP	BIT(14)

/* RX Delay enabled = 1.8ns 1000T, 8ns 10/100T */
#define YT8511_DELAY_RX		BIT(0)

/* TX Gig-E Delay is bits 7:4, default 0x5
 * TX Fast-E Delay is bits 15:12, default 0xf
 * Delay = 150ps * N - 250ps
 * On = 2000ps, off = 50ps
 */
#define YT8511_DELAY_GE_TX_EN	(0xf << 4)
#define YT8511_DELAY_GE_TX_DIS	(0x2 << 4)
#define YT8511_DELAY_FE_TX_EN	(0xf << 12)
#define YT8511_DELAY_FE_TX_DIS	(0x2 << 12)

#define YT8512_EXTREG_AFE_PLL		0x50
#define YT8512_EXTREG_EXTEND_COMBO	0x4000
#define YT8512_EXTREG_LED0		0x40c0
#define YT8512_EXTREG_LED1		0x40c3

#define YT8512_EXTREG_SLEEP_CONTROL1	0x2027

#define YT_SOFTWARE_RESET		0x8000

#define YT8512_CONFIG_PLL_REFCLK_SEL_EN	0x0040
#define YT8512_CONTROL1_RMII_EN		0x0001
#define YT8512_LED0_ACT_BLK_IND		0x1000
#define YT8512_LED0_DIS_LED_AN_TRY	0x0001
#define YT8512_LED0_BT_BLK_EN		0x0002
#define YT8512_LED0_HT_BLK_EN		0x0004
#define YT8512_LED0_COL_BLK_EN		0x0008
#define YT8512_LED0_BT_ON_EN		0x0010
#define YT8512_LED1_BT_ON_EN		0x0010
#define YT8512_LED1_TXACT_BLK_EN	0x0100
#define YT8512_LED1_RXACT_BLK_EN	0x0200
#define YT8512_SPEED_MODE		0xc000
#define YT8512_DUPLEX			0x2000

#define YT8512_SPEED_MODE_BIT		14
#define YT8512_DUPLEX_BIT		13
#define YT8512_EN_SLEEP_SW_BIT		15

/* if system depends on ethernet packet to restore from sleep,
 * please define this macro to 1 otherwise, define it to 0.
 */
#define SYS_WAKEUP_BASED_ON_ETH_PKT	1

/* to enable system WOL feature of phy, please define this macro to 1
 * otherwise, define it to 0.
 */
#define YTPHY_WOL_FEATURE_ENABLE	0

#if (YTPHY_WOL_FEATURE_ENABLE)
#undef SYS_WAKEUP_BASED_ON_ETH_PKT
#define SYS_WAKEUP_BASED_ON_ETH_PKT	1
#endif

/* for YT8531 package A xtal init config */
#define YTPHY8531A_XTAL_INIT		0

#define REG_PHY_SPEC_STATUS		0x11
#define REG_DEBUG_ADDR_OFFSET		0x1e
#define REG_DEBUG_DATA			0x1f

#define YT8521_EXTREG_SLEEP_CONTROL1	0x27
#define YT8521_EN_SLEEP_SW_BIT		15

#define YT8521_SPEED_MODE		0xc000
#define YT8521_DUPLEX			0x2000
#define YT8521_SPEED_MODE_BIT		14
#define YT8521_DUPLEX_BIT		13
#define YT8521_LINK_STATUS_BIT		10

/* YT8521 polling mode */
#define YT8521_PHY_MODE_FIBER		1 /* fiber mode only */
#define YT8521_PHY_MODE_UTP		2 /* utp mode only */
#define YT8521_PHY_MODE_POLL		3 /* fiber and utp, poll mode */

static int yt8521_hw_strap_polling(struct phy_device *phydev);
#define YT8521_PHY_MODE_CURR		yt8521_hw_strap_polling(phydev)

static int yt8511_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, YT8511_PAGE_SELECT);
};

static int yt8511_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, YT8511_PAGE_SELECT, page);
};

static int yt8511_config_init(struct phy_device *phydev)
{
	int oldpage, ret = 0;
	unsigned int ge, fe;

	oldpage = phy_select_page(phydev, YT8511_EXT_CLK_GATE);
	if (oldpage < 0)
		goto err_restore_page;

	/* set rgmii delay mode */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		ge = YT8511_DELAY_GE_TX_DIS;
		fe = YT8511_DELAY_FE_TX_DIS;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		ge = YT8511_DELAY_RX | YT8511_DELAY_GE_TX_DIS;
		fe = YT8511_DELAY_FE_TX_DIS;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		ge = YT8511_DELAY_GE_TX_EN;
		fe = YT8511_DELAY_FE_TX_EN;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		ge = YT8511_DELAY_RX | YT8511_DELAY_GE_TX_EN;
		fe = YT8511_DELAY_FE_TX_EN;
		break;
	default: /* do not support other modes */
		ret = -EOPNOTSUPP;
		goto err_restore_page;
	}

	ret = __phy_modify(phydev, YT8511_PAGE, (YT8511_DELAY_RX | YT8511_DELAY_GE_TX_EN), ge);
	if (ret < 0)
		goto err_restore_page;

	/* set clock mode to 125mhz */
	ret = __phy_modify(phydev, YT8511_PAGE, 0, YT8511_CLK_125M);
	if (ret < 0)
		goto err_restore_page;

	/* fast ethernet delay is in a separate page */
	ret = __phy_write(phydev, YT8511_PAGE_SELECT, YT8511_EXT_DELAY_DRIVE);
	if (ret < 0)
		goto err_restore_page;

	ret = __phy_modify(phydev, YT8511_PAGE, YT8511_DELAY_FE_TX_EN, fe);
	if (ret < 0)
		goto err_restore_page;

	/* leave pll enabled in sleep */
	ret = __phy_write(phydev, YT8511_PAGE_SELECT, YT8511_EXT_SLEEP_CTRL);
	if (ret < 0)
		goto err_restore_page;

	ret = __phy_modify(phydev, YT8511_PAGE, 0, YT8511_PLLON_SLP);
	if (ret < 0)
		goto err_restore_page;

err_restore_page:
	return phy_restore_page(phydev, oldpage, ret);
}

static u32 ytphy_read_ext(struct phy_device *phydev, u32 regnum)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __phy_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		goto err_handle;

	ret = __phy_read(phydev, REG_DEBUG_DATA);
	if (ret < 0)
		goto err_handle;

err_handle:
	phy_unlock_mdio_bus(phydev);
	return ret;
}

static int ytphy_write_ext(struct phy_device *phydev, u32 regnum, u16 val)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __phy_write(phydev, REG_DEBUG_ADDR_OFFSET, regnum);
	if (ret < 0)
		goto err_handle;

	ret = __phy_write(phydev, REG_DEBUG_DATA, val);
	if (ret < 0)
		goto err_handle;

err_handle:
	phy_unlock_mdio_bus(phydev);
	return ret;
}

static int ytphy_soft_reset(struct phy_device *phydev)
{
	int ret = 0, val = 0;

	val = phy_read(phydev, MII_BMCR);
	if (val < 0)
		return val;

	ret = phy_write(phydev, MII_BMCR, val | BMCR_RESET);
	if (ret < 0)
		return ret;

	return ret;
}

static int yt8512_clk_init(struct phy_device *phydev)
{
	int ret;
	int val;

	val = ytphy_read_ext(phydev, YT8512_EXTREG_AFE_PLL);
	if (val < 0)
		return val;

	val |= YT8512_CONFIG_PLL_REFCLK_SEL_EN;

	ret = ytphy_write_ext(phydev, YT8512_EXTREG_AFE_PLL, val);
	if (ret < 0)
		return ret;

	val = ytphy_read_ext(phydev, YT8512_EXTREG_EXTEND_COMBO);
	if (val < 0)
		return val;

	val |= YT8512_CONTROL1_RMII_EN;

	ret = ytphy_write_ext(phydev, YT8512_EXTREG_EXTEND_COMBO, val);
	if (ret < 0)
		return ret;

	val = phy_read(phydev, MII_BMCR);
	if (val < 0)
		return val;

	val |= YT_SOFTWARE_RESET;
	ret = phy_write(phydev, MII_BMCR, val);

	return ret;
}

static int yt8512_led_init(struct phy_device *phydev)
{
	int ret;
	int val;
	int mask;

	val = ytphy_read_ext(phydev, YT8512_EXTREG_LED0);
	if (val < 0)
		return val;

	val |= YT8512_LED0_ACT_BLK_IND;

	mask = YT8512_LED0_DIS_LED_AN_TRY | YT8512_LED0_BT_BLK_EN |
		YT8512_LED0_HT_BLK_EN | YT8512_LED0_COL_BLK_EN |
		YT8512_LED0_BT_ON_EN;
	val &= ~mask;

	ret = ytphy_write_ext(phydev, YT8512_EXTREG_LED0, val);
	if (ret < 0)
		return ret;

	val = ytphy_read_ext(phydev, YT8512_EXTREG_LED1);
	if (val < 0)
		return val;

	val |= YT8512_LED1_BT_ON_EN;

	mask = YT8512_LED1_TXACT_BLK_EN | YT8512_LED1_RXACT_BLK_EN;
	val &= ~mask;

	ret = ytphy_write_ext(phydev, YT8512_LED1_BT_ON_EN, val);

	return ret;
}

static int yt8512_config_init(struct phy_device *phydev)
{
	int ret;
	int val;

	ret = yt8512_clk_init(phydev);
	if (ret < 0)
		return ret;

	ret = yt8512_led_init(phydev);
	if (ret < 0)
		return ret;

	/* disable auto sleep */
	val = ytphy_read_ext(phydev, YT8512_EXTREG_SLEEP_CONTROL1);
	if (val < 0)
		return val;

	val &= (~BIT(YT8512_EN_SLEEP_SW_BIT));

	ret = ytphy_write_ext(phydev, YT8512_EXTREG_SLEEP_CONTROL1, val);
	if (ret < 0)
		return ret;

	return ret;
}

static int yt8512_read_status(struct phy_device *phydev)
{
	int ret;
	int val;
	int speed, speed_mode, duplex;

	ret = genphy_update_link(phydev);
	if (ret)
		return ret;

	val = phy_read(phydev, REG_PHY_SPEC_STATUS);
	if (val < 0)
		return val;

	duplex = (val & YT8512_DUPLEX) >> YT8512_DUPLEX_BIT;
	speed_mode = (val & YT8512_SPEED_MODE) >> YT8512_SPEED_MODE_BIT;
	switch (speed_mode) {
	case 0:
		speed = SPEED_10;
		break;
	case 1:
		speed = SPEED_100;
		break;
	case 2:
	case 3:
	default:
		speed = SPEED_UNKNOWN;
		break;
	}

	phydev->speed = speed;
	phydev->duplex = duplex;

	return 0;
}

static int yt8521_soft_reset(struct phy_device *phydev)
{
	int ret = 0, val;

	if (YT8521_PHY_MODE_CURR == YT8521_PHY_MODE_UTP) {
		ytphy_write_ext(phydev, 0xa000, 0);
		ret = ytphy_soft_reset(phydev);
		if (ret < 0)
			return ret;
	}

	if (YT8521_PHY_MODE_CURR == YT8521_PHY_MODE_FIBER) {
		ytphy_write_ext(phydev, 0xa000, 2);
		ret = ytphy_soft_reset(phydev);
		if (ret < 0)
			return ret;

		ytphy_write_ext(phydev, 0xa000, 0);
	}

	if (YT8521_PHY_MODE_CURR == YT8521_PHY_MODE_POLL) {
		val = ytphy_read_ext(phydev, 0xa001);
		ytphy_write_ext(phydev, 0xa001, (val & ~0x8000));

		ytphy_write_ext(phydev, 0xa000, 0);
		ret = ytphy_soft_reset(phydev);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int yt8521_hw_strap_polling(struct phy_device *phydev)
{
	int val = 0;

	val = ytphy_read_ext(phydev, 0xa001) & 0x7;
	switch (val) {
	case 1:
	case 4:
	case 5:
		return YT8521_PHY_MODE_FIBER;
	case 2:
	case 6:
	case 7:
		return YT8521_PHY_MODE_POLL;
	case 3:
	case 0:
	default:
		return YT8521_PHY_MODE_UTP;
	}
}

static int yt8521_config_init(struct phy_device *phydev)
{
	int ret, hw_strap_mode;
	int val;

#if (YTPHY_WOL_FEATURE_ENABLE)
	struct ethtool_wolinfo wol;

	/* set phy wol enable */
	memset(&wol, 0x0, sizeof(struct ethtool_wolinfo));
	wol.wolopts |= WAKE_MAGIC;
	ytphy_wol_feature_set(phydev, &wol);
#endif

	phydev->irq = PHY_POLL;
	/* NOTE: this function should not be called more than one for each chip. */
	hw_strap_mode = ytphy_read_ext(phydev, 0xa001) & 0x7;

	ytphy_write_ext(phydev, 0xa000, 0);

	/* disable auto sleep */
	val = ytphy_read_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1);
	if (val < 0)
		return val;

	val &= (~BIT(YT8521_EN_SLEEP_SW_BIT));
	ret = ytphy_write_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1, val);
	if (ret < 0)
		return ret;

	/* enable RXC clock when no wire plug */
	val = ytphy_read_ext(phydev, 0xc);
	if (val < 0)
		return val;
	val &= ~(1 << 12);
	ret = ytphy_write_ext(phydev, 0xc, val);
	if (ret < 0)
		return ret;

	netdev_info(phydev->attached_dev, "%s done, phy addr: %d, strap mode = %d, polling mode = %d\n",
		    __func__, phydev->mdio.addr, hw_strap_mode, yt8521_hw_strap_polling(phydev));

	return ret;
}

/* for fiber mode, there is no 10M speed mode and
 * this function is for this purpose.
 */
static int yt8521_adjust_status(struct phy_device *phydev, int val, int is_utp)
{
	int speed = SPEED_UNKNOWN;
	int speed_mode, duplex;

	if (is_utp)
		duplex = (val & YT8521_DUPLEX) >> YT8521_DUPLEX_BIT;
	else
		duplex = 1;
	speed_mode = (val & YT8521_SPEED_MODE) >> YT8521_SPEED_MODE_BIT;
	switch (speed_mode) {
	case 0:
		if (is_utp)
			speed = SPEED_10;
		break;
	case 1:
		speed = SPEED_100;
		break;
	case 2:
		speed = SPEED_1000;
		break;
	case 3:
		break;
	default:
		speed = SPEED_UNKNOWN;
		break;
	}

	phydev->speed = speed;
	phydev->duplex = duplex;

	return 0;
}

/* for fiber mode, when speed is 100M, there is no definition for
 * autonegotiation, and this function handles this case and return
 * 1 per linux kernel's polling.
 */
static int yt8521_aneg_done(struct phy_device *phydev)
{
	int link_fiber = 0, link_utp = 0;

	/* reading Fiber */
	ytphy_write_ext(phydev, 0xa000, 2);
	link_fiber = !!(phy_read(phydev, REG_PHY_SPEC_STATUS) & (BIT(YT8521_LINK_STATUS_BIT)));

	/* reading UTP */
	ytphy_write_ext(phydev, 0xa000, 0);
	if (!link_fiber)
		link_utp = !!(phy_read(phydev, REG_PHY_SPEC_STATUS) & (BIT(YT8521_LINK_STATUS_BIT)));

	netdev_info(phydev->attached_dev, "%s, phy addr: %d, link_fiber: %d, link_utp: %d\n",
		    __func__, phydev->mdio.addr, link_fiber, link_utp);
	return !!(link_fiber | link_utp);
}

static int yt8521_read_status(struct phy_device *phydev)
{
	int link_utp = 0, link_fiber = 0;
	int yt8521_fiber_latch_val;
	int yt8521_fiber_curr_val;
	int link, ret;
	int val;

	if (YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_FIBER) {
		/* reading UTP */
		ret = ytphy_write_ext(phydev, 0xa000, 0);
		if (ret < 0)
			return ret;

		val = phy_read(phydev, REG_PHY_SPEC_STATUS);
		if (val < 0)
			return val;

		link = val & (BIT(YT8521_LINK_STATUS_BIT));
		if (link) {
			link_utp = 1;
			yt8521_adjust_status(phydev, val, 1);
		} else {
			link_utp = 0;
		}
	}

	if (YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_UTP) {
		/* reading Fiber */
		ret = ytphy_write_ext(phydev, 0xa000, 2);
		if (ret < 0)
			return ret;

		val = phy_read(phydev, REG_PHY_SPEC_STATUS);
		if (val < 0)
			return val;

		/* note: below debug information is used to check multiple PHy ports. */

		/* for fiber, from 1000m to 100m, there is not link down from 0x11,
		 * and check reg 1 to identify such case this is important for Linux
		 * kernel for that, missing linkdown event will cause problem.
		 */
		yt8521_fiber_latch_val = phy_read(phydev, MII_BMSR);
		yt8521_fiber_curr_val = phy_read(phydev, MII_BMSR);
		link = val & (BIT(YT8521_LINK_STATUS_BIT));
		if (link && yt8521_fiber_latch_val != yt8521_fiber_curr_val) {
			link = 0;
			netdev_info(phydev->attached_dev, "%s, phy addr: %d, fiber link down detect, latch = %04x, curr = %04x\n",
				    __func__, phydev->mdio.addr, yt8521_fiber_latch_val,
				    yt8521_fiber_curr_val);
		}

		if (link) {
			link_fiber = 1;
			yt8521_adjust_status(phydev, val, 0);
		} else {
			link_fiber = 0;
		}
	}

	if (link_utp || link_fiber) {
		if (phydev->link == 0)
			netdev_info(phydev->attached_dev, "%s, phy addr: %d, link up, media: %s, mii reg 0x11 = 0x%x\n",
				    __func__, phydev->mdio.addr,
				    (link_utp && link_fiber) ? "UNKNOWN MEDIA" : (link_utp ? "UTP" : "Fiber"),
				    (unsigned int)val);
		phydev->link = 1;
	} else {
		if (phydev->link == 1)
			netdev_info(phydev->attached_dev, "%s, phy addr: %d, link down\n",
				    __func__, phydev->mdio.addr);
		phydev->link = 0;
	}

	/* utp or combo */
	if (YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_FIBER) {
		if (link_fiber)
			ytphy_write_ext(phydev, 0xa000, 2);
		if (link_utp)
			ytphy_write_ext(phydev, 0xa000, 0);
	}

	return 0;
}

static int yt8521_suspend(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int value;

	ytphy_write_ext(phydev, 0xa000, 0);
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, value | BMCR_PDOWN);

	ytphy_write_ext(phydev, 0xa000, 2);
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, value | BMCR_PDOWN);

	ytphy_write_ext(phydev, 0xa000, 0);
#endif /*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
}

static int yt8521_resume(struct phy_device *phydev)
{
	int value, ret;

	/* disable auto sleep */
	value = ytphy_read_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1);
	if (value < 0)
		return value;

	value &= (~BIT(YT8521_EN_SLEEP_SW_BIT));

	ret = ytphy_write_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1, value);
	if (ret < 0)
		return ret;

#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	if (YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_FIBER) {
		ytphy_write_ext(phydev, 0xa000, 0);
		value = phy_read(phydev, MII_BMCR);
		phy_write(phydev, MII_BMCR, value & ~BMCR_PDOWN);
	}

	if (YT8521_PHY_MODE_CURR != YT8521_PHY_MODE_UTP) {
		ytphy_write_ext(phydev, 0xa000, 2);
		value = phy_read(phydev, MII_BMCR);
		phy_write(phydev, MII_BMCR, value & ~BMCR_PDOWN);

		ytphy_write_ext(phydev, 0xa000, 0);
	}
#endif /*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
}

static int yt8531_rxclk_duty_init(struct phy_device *phydev)
{
	unsigned int value = 0x9696;
	int ret = 0;

	ret = ytphy_write_ext(phydev, 0xa040, 0xffff);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa041, 0xff);
	if (ret < 0)
		return ret;

	ret = ytphy_write_ext(phydev, 0xa039, 0xbf00);
	if (ret < 0)
		return ret;

	/* nodelay duty = 0x9696 (default)
	 * fixed delay duty = 0x4040
	 * step delay 0xf duty = 0x4041
	 */
	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID)
		value = 0x4040;

	ret = ytphy_write_ext(phydev, 0xa03a, value);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa03b, value);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa03c, value);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa03d, value);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa03e, value);
	if (ret < 0)
		return ret;
	ret = ytphy_write_ext(phydev, 0xa03f, value);
	if (ret < 0)
		return ret;

	return ret;
}

static int yt8531S_config_init(struct phy_device *phydev)
{
#if (YTPHY8531A_XTAL_INIT)
	int ret = 0;

	ret = yt8531a_xtal_init(phydev);
	if (ret < 0)
		return ret;
#endif

	return yt8521_config_init(phydev);
}

static int yt8531_config_init(struct phy_device *phydev)
{
	int ret = 0, val;

#if (YTPHY8531A_XTAL_INIT)
	ret = yt8531a_xtal_init(phydev);
	if (ret < 0)
		return ret;
#endif

	/* PHY_CLK_OUT 125M enabled (default) */
	ret = ytphy_write_ext(phydev, 0xa012, 0xd0);
	if (ret < 0)
		return ret;

	ret = yt8531_rxclk_duty_init(phydev);
	if (ret < 0)
		return ret;

	/* RXC, PHY_CLK_OUT and RXData Drive strength:
	 * Drive strength of RXC = 4, PHY_CLK_OUT = 3, RXD0 = 4 (default)
	 * If the io voltage is 3.3v, PHY_CLK_OUT = 2, set 0xa010 = 0x9acf
	 */
	ret = ytphy_write_ext(phydev, 0xa010, 0x9bcf);
	if (ret < 0)
		return ret;

	/* Change 100M default BGS voltage from 0x294c to 0x274c */
	val = ytphy_read_ext(phydev, 0x57);
	val = (val & ~(0xf << 8)) | (7 << 8);
	ret = ytphy_write_ext(phydev, 0x57, val);
	if (ret < 0)
		return ret;

	return ret;
}

static struct phy_driver motorcomm_phy_drvs[] = {
	{
		PHY_ID_MATCH_EXACT(PHY_ID_YT8511),
		.name		= "YT8511 Gigabit Ethernet",
		.config_init	= yt8511_config_init,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= yt8511_read_page,
		.write_page	= yt8511_write_page,
	}, {
		PHY_ID_MATCH_EXACT(PHY_ID_YT8512),
		.name		= "YT8512 Ethernet",
		.config_init	= yt8512_config_init,
		.read_status	= yt8512_read_status,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	}, {
		PHY_ID_MATCH_EXACT(PHY_ID_YT8512B),
		.name		= "YT8512B Ethernet",
		.config_init	= yt8512_config_init,
		.read_status	= yt8512_read_status,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	}, {
		/* same as 8521 */
		PHY_ID_MATCH_EXACT(PHY_ID_YT8531S),
		.name          = "YT8531S Gigabit Ethernet",
		.features      = PHY_GBIT_FEATURES,
		.soft_reset    = yt8521_soft_reset,
		.aneg_done     = yt8521_aneg_done,
		.config_init   = yt8531S_config_init,
		.read_status   = yt8521_read_status,
		.suspend       = yt8521_suspend,
		.resume        = yt8521_resume,
#if (YTPHY_WOL_FEATURE_ENABLE)
		.get_wol       = &ytphy_wol_feature_get,
		.set_wol       = &ytphy_wol_feature_set,
#endif
	}, {
		/* same as 8511 */
		PHY_ID_MATCH_EXACT(PHY_ID_YT8531),
		.name          = "YT8531 Gigabit Ethernet",
		.features      = PHY_GBIT_FEATURES,
		.config_init   = yt8531_config_init,
		.suspend       = genphy_suspend,
		.resume        = genphy_resume,
#if (YTPHY_WOL_FEATURE_ENABLE)
		.get_wol       = &ytphy_wol_feature_get,
		.set_wol       = &ytphy_wol_feature_set,
#endif
	},
};

module_phy_driver(motorcomm_phy_drvs);

MODULE_DESCRIPTION("Motorcomm PHY driver");
MODULE_AUTHOR("Peter Geis");
MODULE_LICENSE("GPL");

static const struct mdio_device_id __maybe_unused motorcomm_tbl[] = {
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8511) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8512) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8512B) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8531S) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8531) },
	{ /* sentinal */ }
};

MODULE_DEVICE_TABLE(mdio, motorcomm_tbl);
