// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Motorcomm PHYs
 *
 * Author: Peter Geis <pgwipeout@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>

#define PHY_ID_YT8511		0x0000010a
#define PHY_ID_YT8521 		0x0000011a
#define MOTORCOMM_PHY_ID_MASK 	0x00000fff

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

#define YT8521_SLEEP_SW_EN 	BIT(15)
#define YT8521_LINK_STATUS 	BIT(10)
#define YT8521_DUPLEX 		0x2000
#define YT8521_SPEED_MODE 	0xc000
#define YTPHY_REG_SPACE_UTP 	0
#define YTPHY_REG_SPACE_FIBER 	2
#define REG_PHY_SPEC_STATUS 	0x11
/* based on yt8521 wol config register */
#define YTPHY_UTP_INTR_REG 	0x12

#define SYS_WAKEUP_BASED_ON_ETH_PKT 	0

/* to enable system WOL of phy, please define this macro to 1
 * otherwise, define it to 0.
 */
#define YTPHY_ENABLE_WOL 	0

#if (YTPHY_ENABLE_WOL)
	#undef SYS_WAKEUP_BASED_ON_ETH_PKT
	#define SYS_WAKEUP_BASED_ON_ETH_PKT     1
#endif

#if (YTPHY_ENABLE_WOL)
enum ytphy_wol_type_e {
	YTPHY_WOL_TYPE_LEVEL,
	YTPHY_WOL_TYPE_PULSE,
	YTPHY_WOL_TYPE_MAX
};
typedef enum ytphy_wol_type_e ytphy_wol_type_t;

enum ytphy_wol_width_e {
	YTPHY_WOL_WIDTH_84MS,
	YTPHY_WOL_WIDTH_168MS,
	YTPHY_WOL_WIDTH_336MS,
	YTPHY_WOL_WIDTH_672MS,
	YTPHY_WOL_WIDTH_MAX
};
typedef enum ytphy_wol_width_e ytphy_wol_width_t;

struct ytphy_wol_cfg_s {
	int enable;
	int type;
	int width;
};
typedef struct ytphy_wol_cfg_s ytphy_wol_cfg_t;
#endif /*(YTPHY_ENABLE_WOL)*/

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

int genphy_config_init(struct phy_device *phydev)
{
	return genphy_read_abilities(phydev);
}

static int ytphy_read_ext(struct phy_device *phydev, u32 regnum)
{
	int ret;
	int val;

	ret = phy_write(phydev, YT8511_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	val = phy_read(phydev, YT8511_PAGE);

	return val;
}

static int ytphy_write_ext(struct phy_device *phydev, u32 regnum, u16 val)
{
	int ret;

	ret = phy_write(phydev, YT8511_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, YT8511_PAGE, val);

	return ret;
}

int yt8521_soft_reset(struct phy_device *phydev)
{
	int ret, val;

	val = ytphy_read_ext(phydev, 0xa001);
	ytphy_write_ext(phydev, 0xa001, (val & ~0x8000));

	ret = genphy_soft_reset(phydev);
	if (ret < 0)
		return ret;

	return 0;
}

#if (YTPHY_ENABLE_WOL)
static int ytphy_switch_reg_space(struct phy_device *phydev, int space)
{
	int ret;

	if (space == YTPHY_REG_SPACE_UTP)
		ret = ytphy_write_ext(phydev, 0xa000, 0);
	else
		ret = ytphy_write_ext(phydev, 0xa000, 2);

	return ret;
}

static int ytphy_wol_en_cfg(struct phy_device *phydev, ytphy_wol_cfg_t wol_cfg)
{
	int ret=0;
	int val=0;

	val = ytphy_read_ext(phydev, YTPHY_WOL_CFG_REG);
	if (val < 0)
		return val;

	if(wol_cfg.enable) {
		val |= YTPHY_WOL_CFG_EN;

		if(wol_cfg.type == YTPHY_WOL_TYPE_LEVEL) {
			val &= ~YTPHY_WOL_CFG_TYPE;
			val &= ~YTPHY_WOL_CFG_INTR_SEL;
		} else if(wol_cfg.type == YTPHY_WOL_TYPE_PULSE) {
			val |= YTPHY_WOL_CFG_TYPE;
			val |= YTPHY_WOL_CFG_INTR_SEL;

			if(wol_cfg.width == YTPHY_WOL_WIDTH_84MS) {
				val &= ~YTPHY_WOL_CFG_WIDTH1;
				val &= ~YTPHY_WOL_CFG_WIDTH2;
			} else if(wol_cfg.width == YTPHY_WOL_WIDTH_168MS) {
				val |= YTPHY_WOL_CFG_WIDTH1;
				val &= ~YTPHY_WOL_CFG_WIDTH2;
			} else if(wol_cfg.width == YTPHY_WOL_WIDTH_336MS) {
				val &= ~YTPHY_WOL_CFG_WIDTH1;
				val |= YTPHY_WOL_CFG_WIDTH2;
			} else if(wol_cfg.width == YTPHY_WOL_WIDTH_672MS) {
				val |= YTPHY_WOL_CFG_WIDTH1;
				val |= YTPHY_WOL_CFG_WIDTH2;
			}
		}
	} else {
		val &= ~YTPHY_WOL_CFG_EN;
		val &= ~YTPHY_WOL_CFG_INTR_SEL;
	}

	ret = ytphy_write_ext(phydev, YTPHY_WOL_CFG_REG, val);
	if (ret < 0)
		return ret;

	return 0;
}

static void ytphy_get_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	int val = 0;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	val = ytphy_read_ext(phydev, YTPHY_WOL_CFG_REG);
	if (val < 0)
		return;

	if (val & YTPHY_WOL_CFG_EN)
		wol->wolopts |= WAKE_MAGIC;

	return;
}

static int ytphy_set_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	int ret, pre_page, val;
	ytphy_wol_cfg_t wol_cfg;
	struct net_device *p_attached_dev = phydev->attached_dev;

	memset(&wol_cfg,0,sizeof(ytphy_wol_cfg_t));
	pre_page = ytphy_read_ext(phydev, 0xa000);
	if (pre_page < 0)
		return pre_page;

	/* Switch to phy UTP page */
	ret = ytphy_switch_reg_space(phydev, YTPHY_REG_SPACE_UTP);
	if (ret < 0)
		return ret;

	if (wol->wolopts & WAKE_MAGIC) {
		/* Enable the WOL interrupt */
		val = phy_read(phydev, YTPHY_UTP_INTR_REG);
		val |= YTPHY_WOL_INTR;
		ret = phy_write(phydev, YTPHY_UTP_INTR_REG, val);
		if (ret < 0)
			return ret;

		/* Set the WOL config */
		wol_cfg.enable = 1; //enable
		wol_cfg.type= YTPHY_WOL_TYPE_PULSE;
		wol_cfg.width= YTPHY_WOL_WIDTH_672MS;
		ret = ytphy_wol_en_cfg(phydev, wol_cfg);
		if (ret < 0)
			return ret;

		/* Store the device address for the magic packet */
		ret = ytphy_write_ext(phydev, YTPHY_MAGIC_PACKET_MAC_ADDR2,
				((p_attached_dev->dev_addr[0] << 8) |
				 p_attached_dev->dev_addr[1]));
		if (ret < 0)
			return ret;
		ret = ytphy_write_ext(phydev, YTPHY_MAGIC_PACKET_MAC_ADDR1,
				((p_attached_dev->dev_addr[2] << 8) |
				 p_attached_dev->dev_addr[3]));
		if (ret < 0)
			return ret;
		ret = ytphy_write_ext(phydev, YTPHY_MAGIC_PACKET_MAC_ADDR0,
				((p_attached_dev->dev_addr[4] << 8) |
				 p_attached_dev->dev_addr[5]));
		if (ret < 0)
			return ret;
	} else {
		wol_cfg.enable = 0; //disable
		wol_cfg.type= YTPHY_WOL_TYPE_MAX;
		wol_cfg.width= YTPHY_WOL_WIDTH_MAX;
		ret = ytphy_wol_en_cfg(phydev, wol_cfg);
		if (ret < 0)
			return ret;
	}

	/* Recover to previous register space page */
	ret = ytphy_switch_reg_space(phydev, pre_page);
	if (ret < 0)
		return ret;

	return 0;
}
#endif /*(YTPHY_ENABLE_WOL)*/

static int yt8521_config_init(struct phy_device *phydev)
{
	int ret;
	int val;

	phydev->irq = PHY_POLL;

	ytphy_write_ext(phydev, 0xa000, 0);

	ret = genphy_config_init(phydev);
	if (ret < 0)
		return ret;

	/* disable auto sleep */
	val = ytphy_read_ext(phydev, YT8511_EXT_SLEEP_CTRL);
	if (val < 0)
		return val;

	val &= ~YT8521_SLEEP_SW_EN;

	ret = ytphy_write_ext(phydev, YT8511_EXT_SLEEP_CTRL, val);
	if (ret < 0)
		return ret;

	/*  enable tx delay 450ps per step */
	val = ytphy_read_ext(phydev, 0xa003);
	if (val < 0) {
		printk(KERN_INFO "yt8521_config: read 0xa003 error!\n");
		return val;
	}

	val &= ~0x3CFF;
	val |= 0x5f;
	ret = ytphy_write_ext(phydev, 0xa003, val);
	if (ret < 0) {
		printk(KERN_INFO "yt8521_config: set 0xa003 error!\n");
		return ret;
	}

	/* disable rx delay */
	val = ytphy_read_ext(phydev, 0xa001);
	if (val < 0) {
		printk(KERN_INFO "yt8521_config: read 0xa001 error!\n");
		return val;
	}
	val &= ~(1<<8);
	val |= BIT(8);
	ret = ytphy_write_ext(phydev, 0xa001, val);
	if (ret < 0) {
		printk(KERN_INFO "yt8521_config: failed to disable rx_delay!\n");
		return ret;
	}

	/* enable RXC clock when no wire plug */
	ret = ytphy_write_ext(phydev, 0xa000, 0);
	if (ret < 0)
		return ret;

	val = ytphy_read_ext(phydev, YT8511_EXT_CLK_GATE);
	if (val < 0)
		return val;
	val &= ~(1 << 12);
	ret = ytphy_write_ext(phydev, YT8511_EXT_CLK_GATE, val);
	if (ret < 0)
		return ret;

	return ret;
}

/*
 * for fiber mode, there is no 10M speed mode and
 * this function is for this purpose.
 */
static int yt8521_adjust_status(struct phy_device *phydev, int val, int is_utp)
{
	int speed_mode, duplex;
	int speed = SPEED_UNKNOWN;

	duplex = (val & YT8521_DUPLEX) >> 13;
	speed_mode = (val & YT8521_SPEED_MODE) >> 14;
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

static int yt8521_read_status(struct phy_device *phydev)
{
	int ret;
	volatile int val;
	volatile int link;
	int link_utp = 0;

	/* reading UTP */
	ret = ytphy_write_ext(phydev, 0xa000, 0);
	if (ret < 0)
		return ret;

	val = phy_read(phydev, REG_PHY_SPEC_STATUS);
	if (val < 0)
		return val;

	link = val & YT8521_LINK_STATUS;
	if (link) {
		link_utp = 1;
		yt8521_adjust_status(phydev, val, 1);
	} else {
		link_utp = 0;
	}

	if (link_utp) {
		phydev->link = 1;
		ytphy_write_ext(phydev, 0xa000, 0);
	} else {
		phydev->link = 0;
	}

	return 0;
}

int yt8521_suspend(struct phy_device *phydev)
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

int yt8521_resume(struct phy_device *phydev)
{
#if !(SYS_WAKEUP_BASED_ON_ETH_PKT)
	int value;
	int ret;

	ytphy_write_ext(phydev, 0xa000, 0);
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, value & ~BMCR_PDOWN);

	/* disable auto sleep */
	value = ytphy_read_ext(phydev, YT8511_EXT_SLEEP_CTRL);
	if (value < 0)
		return value;

	value &= ~YT8521_SLEEP_SW_EN;
	ret = ytphy_write_ext(phydev, YT8511_EXT_SLEEP_CTRL, value);
	if (ret < 0)
		return ret;

	/* enable RXC clock when no wire plug */
	value = ytphy_read_ext(phydev, YT8511_EXT_CLK_GATE);
	if (value < 0)
		return value;
	value &= ~(1 << 12);
	ret = ytphy_write_ext(phydev, YT8511_EXT_CLK_GATE, value);
	if (ret < 0)
		return ret;

	ytphy_write_ext(phydev, 0xa000, 2);
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, value & ~BMCR_PDOWN);

	ytphy_write_ext(phydev, 0xa000, 0);

#endif /*!(SYS_WAKEUP_BASED_ON_ETH_PKT)*/

	return 0;
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
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_YT8521),
		.name 		= "YT8521 Gigabit Ethernet",
		.phy_id_mask 	= MOTORCOMM_PHY_ID_MASK,
		.flags 		= PHY_POLL,
		.soft_reset	= yt8521_soft_reset,
		.config_aneg 	= genphy_config_aneg,
		.aneg_done	= genphy_aneg_done,
		.config_init 	= yt8521_config_init,
		.read_status 	= yt8521_read_status,
		.suspend 	= yt8521_suspend,
		.resume 	= yt8521_resume,
#if (YTPHY_ENABLE_WOL)
		.get_wol	= &ytphy_get_wol,
		.set_wol	= &ytphy_set_wol,
#endif
	},
};

module_phy_driver(motorcomm_phy_drvs);

MODULE_DESCRIPTION("Motorcomm PHY driver");
MODULE_AUTHOR("Peter Geis");
MODULE_AUTHOR("Walker Chen <walker.chen@starfivetech.com>");
MODULE_LICENSE("GPL");

static const struct mdio_device_id __maybe_unused motorcomm_tbl[] = {
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8511) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8521) },
	{ /* sentinal */ }
};

MODULE_DEVICE_TABLE(mdio, motorcomm_tbl);
