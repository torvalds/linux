// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for ICPlus PHYs
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/property.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <linux/uaccess.h>

MODULE_DESCRIPTION("ICPlus IP175C/IP101A/IP101G/IC1001 PHY drivers");
MODULE_AUTHOR("Michael Barkowski");
MODULE_LICENSE("GPL");

/* IP101A/G - IP1001 */
#define IP10XX_SPEC_CTRL_STATUS		16	/* Spec. Control Register */
#define IP1001_RXPHASE_SEL		BIT(0)	/* Add delay on RX_CLK */
#define IP1001_TXPHASE_SEL		BIT(1)	/* Add delay on TX_CLK */
#define IP1001_SPEC_CTRL_STATUS_2	20	/* IP1001 Spec. Control Reg 2 */
#define IP1001_APS_ON			11	/* IP1001 APS Mode  bit */
#define IP101A_G_APS_ON			BIT(1)	/* IP101A/G APS Mode bit */
#define IP101A_G_AUTO_MDIX_DIS		BIT(11)
#define IP101A_G_IRQ_CONF_STATUS	0x11	/* Conf Info IRQ & Status Reg */
#define	IP101A_G_IRQ_PIN_USED		BIT(15) /* INTR pin used */
#define IP101A_G_IRQ_ALL_MASK		BIT(11) /* IRQ's inactive */
#define IP101A_G_IRQ_SPEED_CHANGE	BIT(2)
#define IP101A_G_IRQ_DUPLEX_CHANGE	BIT(1)
#define IP101A_G_IRQ_LINK_CHANGE	BIT(0)
#define IP101A_G_PHY_STATUS		18
#define IP101A_G_MDIX			BIT(9)
#define IP101A_G_PHY_SPEC_CTRL		30
#define IP101A_G_FORCE_MDIX		BIT(3)

#define IP101G_PAGE_CONTROL				0x14
#define IP101G_PAGE_CONTROL_MASK			GENMASK(4, 0)
#define IP101G_DIGITAL_IO_SPEC_CTRL			0x1d
#define IP101G_DIGITAL_IO_SPEC_CTRL_SEL_INTR32		BIT(2)

#define IP101G_DEFAULT_PAGE			16

#define IP101G_P1_CNT_CTRL		17
#define CNT_CTRL_RX_EN			BIT(13)
#define IP101G_P8_CNT_CTRL		17
#define CNT_CTRL_RDCLR_EN		BIT(15)
#define IP101G_CNT_REG			18

#define IP175C_PHY_ID 0x02430d80
#define IP1001_PHY_ID 0x02430d90
#define IP101A_PHY_ID 0x02430c54

/* The 32-pin IP101GR package can re-configure the mode of the RXER/INTR_32 pin
 * (pin number 21). The hardware default is RXER (receive error) mode. But it
 * can be configured to interrupt mode manually.
 */
enum ip101gr_sel_intr32 {
	IP101GR_SEL_INTR32_KEEP,
	IP101GR_SEL_INTR32_INTR,
	IP101GR_SEL_INTR32_RXER,
};

struct ip101g_hw_stat {
	const char *name;
	int page;
};

static struct ip101g_hw_stat ip101g_hw_stats[] = {
	{ "phy_crc_errors", 1 },
	{ "phy_symbol_errors", 11, },
};

struct ip101a_g_phy_priv {
	enum ip101gr_sel_intr32 sel_intr32;
	u64 stats[ARRAY_SIZE(ip101g_hw_stats)];
};

static int ip175c_config_init(struct phy_device *phydev)
{
	int err, i;
	static int full_reset_performed;

	if (full_reset_performed == 0) {

		/* master reset */
		err = mdiobus_write(phydev->mdio.bus, 30, 0, 0x175c);
		if (err < 0)
			return err;

		/* ensure no bus delays overlap reset period */
		err = mdiobus_read(phydev->mdio.bus, 30, 0);

		/* data sheet specifies reset period is 2 msec */
		mdelay(2);

		/* enable IP175C mode */
		err = mdiobus_write(phydev->mdio.bus, 29, 31, 0x175c);
		if (err < 0)
			return err;

		/* Set MII0 speed and duplex (in PHY mode) */
		err = mdiobus_write(phydev->mdio.bus, 29, 22, 0x420);
		if (err < 0)
			return err;

		/* reset switch ports */
		for (i = 0; i < 5; i++) {
			err = mdiobus_write(phydev->mdio.bus, i,
					    MII_BMCR, BMCR_RESET);
			if (err < 0)
				return err;
		}

		for (i = 0; i < 5; i++)
			err = mdiobus_read(phydev->mdio.bus, i, MII_BMCR);

		mdelay(2);

		full_reset_performed = 1;
	}

	if (phydev->mdio.addr != 4) {
		phydev->state = PHY_RUNNING;
		phydev->speed = SPEED_100;
		phydev->duplex = DUPLEX_FULL;
		phydev->link = 1;
		netif_carrier_on(phydev->attached_dev);
	}

	return 0;
}

static int ip1001_config_init(struct phy_device *phydev)
{
	int c;

	/* Enable Auto Power Saving mode */
	c = phy_read(phydev, IP1001_SPEC_CTRL_STATUS_2);
	if (c < 0)
		return c;
	c |= IP1001_APS_ON;
	c = phy_write(phydev, IP1001_SPEC_CTRL_STATUS_2, c);
	if (c < 0)
		return c;

	if (phy_interface_is_rgmii(phydev)) {

		c = phy_read(phydev, IP10XX_SPEC_CTRL_STATUS);
		if (c < 0)
			return c;

		c &= ~(IP1001_RXPHASE_SEL | IP1001_TXPHASE_SEL);

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
			c |= (IP1001_RXPHASE_SEL | IP1001_TXPHASE_SEL);
		else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID)
			c |= IP1001_RXPHASE_SEL;
		else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
			c |= IP1001_TXPHASE_SEL;

		c = phy_write(phydev, IP10XX_SPEC_CTRL_STATUS, c);
		if (c < 0)
			return c;
	}

	return 0;
}

static int ip175c_read_status(struct phy_device *phydev)
{
	if (phydev->mdio.addr == 4) /* WAN port */
		genphy_read_status(phydev);
	else
		/* Don't need to read status for switch ports */
		phydev->irq = PHY_MAC_INTERRUPT;

	return 0;
}

static int ip175c_config_aneg(struct phy_device *phydev)
{
	if (phydev->mdio.addr == 4) /* WAN port */
		genphy_config_aneg(phydev);

	return 0;
}

static int ip101a_g_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct ip101a_g_phy_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Both functions (RX error and interrupt status) are sharing the same
	 * pin on the 32-pin IP101GR, so this is an exclusive choice.
	 */
	if (device_property_read_bool(dev, "icplus,select-rx-error") &&
	    device_property_read_bool(dev, "icplus,select-interrupt")) {
		dev_err(dev,
			"RXER and INTR mode cannot be selected together\n");
		return -EINVAL;
	}

	if (device_property_read_bool(dev, "icplus,select-rx-error"))
		priv->sel_intr32 = IP101GR_SEL_INTR32_RXER;
	else if (device_property_read_bool(dev, "icplus,select-interrupt"))
		priv->sel_intr32 = IP101GR_SEL_INTR32_INTR;
	else
		priv->sel_intr32 = IP101GR_SEL_INTR32_KEEP;

	phydev->priv = priv;

	return 0;
}

static int ip101a_g_config_intr_pin(struct phy_device *phydev)
{
	struct ip101a_g_phy_priv *priv = phydev->priv;
	int oldpage, err = 0;

	oldpage = phy_select_page(phydev, IP101G_DEFAULT_PAGE);
	if (oldpage < 0)
		goto out;

	/* configure the RXER/INTR_32 pin of the 32-pin IP101GR if needed: */
	switch (priv->sel_intr32) {
	case IP101GR_SEL_INTR32_RXER:
		err = __phy_modify(phydev, IP101G_DIGITAL_IO_SPEC_CTRL,
				   IP101G_DIGITAL_IO_SPEC_CTRL_SEL_INTR32, 0);
		if (err < 0)
			goto out;
		break;

	case IP101GR_SEL_INTR32_INTR:
		err = __phy_modify(phydev, IP101G_DIGITAL_IO_SPEC_CTRL,
				   IP101G_DIGITAL_IO_SPEC_CTRL_SEL_INTR32,
				   IP101G_DIGITAL_IO_SPEC_CTRL_SEL_INTR32);
		if (err < 0)
			goto out;
		break;

	default:
		/* Don't touch IP101G_DIGITAL_IO_SPEC_CTRL because it's not
		 * documented on IP101A and it's not clear whether this would
		 * cause problems.
		 * For the 32-pin IP101GR we simply keep the SEL_INTR32
		 * configuration as set by the bootloader when not configured
		 * to one of the special functions.
		 */
		break;
	}

out:
	return phy_restore_page(phydev, oldpage, err);
}

static int ip101a_config_init(struct phy_device *phydev)
{
	int ret;

	/* Enable Auto Power Saving mode */
	ret = phy_set_bits(phydev, IP10XX_SPEC_CTRL_STATUS, IP101A_G_APS_ON);
	if (ret)
		return ret;

	return ip101a_g_config_intr_pin(phydev);
}

static int ip101g_config_init(struct phy_device *phydev)
{
	int ret;

	/* Enable the PHY counters */
	ret = phy_modify_paged(phydev, 1, IP101G_P1_CNT_CTRL,
			       CNT_CTRL_RX_EN, CNT_CTRL_RX_EN);
	if (ret)
		return ret;

	/* Clear error counters on read */
	ret = phy_modify_paged(phydev, 8, IP101G_P8_CNT_CTRL,
			       CNT_CTRL_RDCLR_EN, CNT_CTRL_RDCLR_EN);
	if (ret)
		return ret;

	return ip101a_g_config_intr_pin(phydev);
}

static int ip101a_g_read_status(struct phy_device *phydev)
{
	int oldpage, ret, stat1, stat2;

	ret = genphy_read_status(phydev);
	if (ret)
		return ret;

	oldpage = phy_select_page(phydev, IP101G_DEFAULT_PAGE);
	if (oldpage < 0)
		goto out;

	ret = __phy_read(phydev, IP10XX_SPEC_CTRL_STATUS);
	if (ret < 0)
		goto out;
	stat1 = ret;

	ret = __phy_read(phydev, IP101A_G_PHY_SPEC_CTRL);
	if (ret < 0)
		goto out;
	stat2 = ret;

	if (stat1 & IP101A_G_AUTO_MDIX_DIS) {
		if (stat2 & IP101A_G_FORCE_MDIX)
			phydev->mdix_ctrl = ETH_TP_MDI_X;
		else
			phydev->mdix_ctrl = ETH_TP_MDI;
	} else {
		phydev->mdix_ctrl = ETH_TP_MDI_AUTO;
	}

	if (stat2 & IP101A_G_MDIX)
		phydev->mdix = ETH_TP_MDI_X;
	else
		phydev->mdix = ETH_TP_MDI;

	ret = 0;

out:
	return phy_restore_page(phydev, oldpage, ret);
}

static int ip101a_g_config_mdix(struct phy_device *phydev)
{
	u16 ctrl = 0, ctrl2 = 0;
	int oldpage;
	int ret = 0;

	switch (phydev->mdix_ctrl) {
	case ETH_TP_MDI:
		ctrl = IP101A_G_AUTO_MDIX_DIS;
		break;
	case ETH_TP_MDI_X:
		ctrl = IP101A_G_AUTO_MDIX_DIS;
		ctrl2 = IP101A_G_FORCE_MDIX;
		break;
	case ETH_TP_MDI_AUTO:
		break;
	default:
		return 0;
	}

	oldpage = phy_select_page(phydev, IP101G_DEFAULT_PAGE);
	if (oldpage < 0)
		goto out;

	ret = __phy_modify(phydev, IP10XX_SPEC_CTRL_STATUS,
			   IP101A_G_AUTO_MDIX_DIS, ctrl);
	if (ret)
		goto out;

	ret = __phy_modify(phydev, IP101A_G_PHY_SPEC_CTRL,
			   IP101A_G_FORCE_MDIX, ctrl2);

out:
	return phy_restore_page(phydev, oldpage, ret);
}

static int ip101a_g_config_aneg(struct phy_device *phydev)
{
	int ret;

	ret = ip101a_g_config_mdix(phydev);
	if (ret)
		return ret;

	return genphy_config_aneg(phydev);
}

static int ip101a_g_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read_paged(phydev, IP101G_DEFAULT_PAGE,
			     IP101A_G_IRQ_CONF_STATUS);
	if (err < 0)
		return err;

	return 0;
}

static int ip101a_g_config_intr(struct phy_device *phydev)
{
	u16 val;
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = ip101a_g_ack_interrupt(phydev);
		if (err)
			return err;

		/* INTR pin used: Speed/link/duplex will cause an interrupt */
		val = IP101A_G_IRQ_PIN_USED;
		err = phy_write_paged(phydev, IP101G_DEFAULT_PAGE,
				      IP101A_G_IRQ_CONF_STATUS, val);
	} else {
		val = IP101A_G_IRQ_ALL_MASK;
		err = phy_write_paged(phydev, IP101G_DEFAULT_PAGE,
				      IP101A_G_IRQ_CONF_STATUS, val);
		if (err)
			return err;

		err = ip101a_g_ack_interrupt(phydev);
	}

	return err;
}

static irqreturn_t ip101a_g_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read_paged(phydev, IP101G_DEFAULT_PAGE,
				    IP101A_G_IRQ_CONF_STATUS);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & (IP101A_G_IRQ_SPEED_CHANGE |
			    IP101A_G_IRQ_DUPLEX_CHANGE |
			    IP101A_G_IRQ_LINK_CHANGE)))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

/* The IP101A doesn't really have a page register. We just pretend to have one
 * so we can use the paged versions of the callbacks of the IP101G.
 */
static int ip101a_read_page(struct phy_device *phydev)
{
	return IP101G_DEFAULT_PAGE;
}

static int ip101a_write_page(struct phy_device *phydev, int page)
{
	WARN_ONCE(page != IP101G_DEFAULT_PAGE, "wrong page selected\n");

	return 0;
}

static int ip101g_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, IP101G_PAGE_CONTROL);
}

static int ip101g_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, IP101G_PAGE_CONTROL, page);
}

static int ip101a_g_has_page_register(struct phy_device *phydev)
{
	int oldval, val, ret;

	oldval = phy_read(phydev, IP101G_PAGE_CONTROL);
	if (oldval < 0)
		return oldval;

	ret = phy_write(phydev, IP101G_PAGE_CONTROL, 0xffff);
	if (ret)
		return ret;

	val = phy_read(phydev, IP101G_PAGE_CONTROL);
	if (val < 0)
		return val;

	ret = phy_write(phydev, IP101G_PAGE_CONTROL, oldval);
	if (ret)
		return ret;

	return val == IP101G_PAGE_CONTROL_MASK;
}

static int ip101a_g_match_phy_device(struct phy_device *phydev, bool ip101a)
{
	int ret;

	if (phydev->phy_id != IP101A_PHY_ID)
		return 0;

	/* The IP101A and the IP101G share the same PHY identifier.The IP101G
	 * seems to be a successor of the IP101A and implements more functions.
	 * Amongst other things there is a page select register, which is not
	 * available on the IP101A. Use this to distinguish these two.
	 */
	ret = ip101a_g_has_page_register(phydev);
	if (ret < 0)
		return ret;

	return ip101a == !ret;
}

static int ip101a_match_phy_device(struct phy_device *phydev)
{
	return ip101a_g_match_phy_device(phydev, true);
}

static int ip101g_match_phy_device(struct phy_device *phydev)
{
	return ip101a_g_match_phy_device(phydev, false);
}

static int ip101g_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(ip101g_hw_stats);
}

static void ip101g_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ip101g_hw_stats); i++)
		ethtool_puts(&data, ip101g_hw_stats[i].name);
}

static u64 ip101g_get_stat(struct phy_device *phydev, int i)
{
	struct ip101g_hw_stat stat = ip101g_hw_stats[i];
	struct ip101a_g_phy_priv *priv = phydev->priv;
	int val;
	u64 ret;

	val = phy_read_paged(phydev, stat.page, IP101G_CNT_REG);
	if (val < 0) {
		ret = U64_MAX;
	} else {
		priv->stats[i] += val;
		ret = priv->stats[i];
	}

	return ret;
}

static void ip101g_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ip101g_hw_stats); i++)
		data[i] = ip101g_get_stat(phydev, i);
}

static struct phy_driver icplus_driver[] = {
{
	PHY_ID_MATCH_MODEL(IP175C_PHY_ID),
	.name		= "ICPlus IP175C",
	/* PHY_BASIC_FEATURES */
	.config_init	= ip175c_config_init,
	.config_aneg	= ip175c_config_aneg,
	.read_status	= ip175c_read_status,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	PHY_ID_MATCH_MODEL(IP1001_PHY_ID),
	.name		= "ICPlus IP1001",
	/* PHY_GBIT_FEATURES */
	.config_init	= ip1001_config_init,
	.soft_reset	= genphy_soft_reset,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.name		= "ICPlus IP101A",
	.match_phy_device = ip101a_match_phy_device,
	.probe		= ip101a_g_probe,
	.read_page	= ip101a_read_page,
	.write_page	= ip101a_write_page,
	.config_intr	= ip101a_g_config_intr,
	.handle_interrupt = ip101a_g_handle_interrupt,
	.config_init	= ip101a_config_init,
	.config_aneg	= ip101a_g_config_aneg,
	.read_status	= ip101a_g_read_status,
	.soft_reset	= genphy_soft_reset,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.name		= "ICPlus IP101G",
	.match_phy_device = ip101g_match_phy_device,
	.probe		= ip101a_g_probe,
	.read_page	= ip101g_read_page,
	.write_page	= ip101g_write_page,
	.config_intr	= ip101a_g_config_intr,
	.handle_interrupt = ip101a_g_handle_interrupt,
	.config_init	= ip101g_config_init,
	.config_aneg	= ip101a_g_config_aneg,
	.read_status	= ip101a_g_read_status,
	.soft_reset	= genphy_soft_reset,
	.get_sset_count = ip101g_get_sset_count,
	.get_strings	= ip101g_get_strings,
	.get_stats	= ip101g_get_stats,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
} };

module_phy_driver(icplus_driver);

static const struct mdio_device_id __maybe_unused icplus_tbl[] = {
	{ PHY_ID_MATCH_MODEL(IP175C_PHY_ID) },
	{ PHY_ID_MATCH_MODEL(IP1001_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(IP101A_PHY_ID) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, icplus_tbl);
