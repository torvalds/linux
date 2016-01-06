/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2011 - 2012 Cavium, Inc.
 */

#include <linux/module.h>
#include <linux/phy.h>
#include <linux/of.h>

#define PHY_ID_BCM8706	0x0143bdc1
#define PHY_ID_BCM8727	0x0143bff0

#define BCM87XX_PMD_RX_SIGNAL_DETECT	(MII_ADDR_C45 | 0x1000a)
#define BCM87XX_10GBASER_PCS_STATUS	(MII_ADDR_C45 | 0x30020)
#define BCM87XX_XGXS_LANE_STATUS	(MII_ADDR_C45 | 0x40018)

#define BCM87XX_LASI_CONTROL (MII_ADDR_C45 | 0x39002)
#define BCM87XX_LASI_STATUS (MII_ADDR_C45 | 0x39005)

#if IS_ENABLED(CONFIG_OF_MDIO)
/* Set and/or override some configuration registers based on the
 * broadcom,c45-reg-init property stored in the of_node for the phydev.
 *
 * broadcom,c45-reg-init = <devid reg mask value>,...;
 *
 * There may be one or more sets of <devid reg mask value>:
 *
 * devid: which sub-device to use.
 * reg: the register.
 * mask: if non-zero, ANDed with existing register value.
 * value: ORed with the masked value and written to the regiser.
 *
 */
static int bcm87xx_of_reg_init(struct phy_device *phydev)
{
	const __be32 *paddr;
	const __be32 *paddr_end;
	int len, ret;

	if (!phydev->dev.of_node)
		return 0;

	paddr = of_get_property(phydev->dev.of_node,
				"broadcom,c45-reg-init", &len);
	if (!paddr)
		return 0;

	paddr_end = paddr + (len /= sizeof(*paddr));

	ret = 0;

	while (paddr + 3 < paddr_end) {
		u16 devid	= be32_to_cpup(paddr++);
		u16 reg		= be32_to_cpup(paddr++);
		u16 mask	= be32_to_cpup(paddr++);
		u16 val_bits	= be32_to_cpup(paddr++);
		int val;
		u32 regnum = MII_ADDR_C45 | (devid << 16) | reg;
		val = 0;
		if (mask) {
			val = phy_read(phydev, regnum);
			if (val < 0) {
				ret = val;
				goto err;
			}
			val &= mask;
		}
		val |= val_bits;

		ret = phy_write(phydev, regnum, val);
		if (ret < 0)
			goto err;
	}
err:
	return ret;
}
#else
static int bcm87xx_of_reg_init(struct phy_device *phydev)
{
	return 0;
}
#endif /* CONFIG_OF_MDIO */

static int bcm87xx_config_init(struct phy_device *phydev)
{
	phydev->supported = SUPPORTED_10000baseR_FEC;
	phydev->advertising = ADVERTISED_10000baseR_FEC;
	phydev->state = PHY_NOLINK;
	phydev->autoneg = AUTONEG_DISABLE;

	bcm87xx_of_reg_init(phydev);

	return 0;
}

static int bcm87xx_config_aneg(struct phy_device *phydev)
{
	return -EINVAL;
}

static int bcm87xx_read_status(struct phy_device *phydev)
{
	int rx_signal_detect;
	int pcs_status;
	int xgxs_lane_status;

	rx_signal_detect = phy_read(phydev, BCM87XX_PMD_RX_SIGNAL_DETECT);
	if (rx_signal_detect < 0)
		return rx_signal_detect;

	if ((rx_signal_detect & 1) == 0)
		goto no_link;

	pcs_status = phy_read(phydev, BCM87XX_10GBASER_PCS_STATUS);
	if (pcs_status < 0)
		return pcs_status;

	if ((pcs_status & 1) == 0)
		goto no_link;

	xgxs_lane_status = phy_read(phydev, BCM87XX_XGXS_LANE_STATUS);
	if (xgxs_lane_status < 0)
		return xgxs_lane_status;

	if ((xgxs_lane_status & 0x1000) == 0)
		goto no_link;

	phydev->speed = 10000;
	phydev->link = 1;
	phydev->duplex = 1;
	return 0;

no_link:
	phydev->link = 0;
	return 0;
}

static int bcm87xx_config_intr(struct phy_device *phydev)
{
	int reg, err;

	reg = phy_read(phydev, BCM87XX_LASI_CONTROL);

	if (reg < 0)
		return reg;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		reg |= 1;
	else
		reg &= ~1;

	err = phy_write(phydev, BCM87XX_LASI_CONTROL, reg);
	return err;
}

static int bcm87xx_did_interrupt(struct phy_device *phydev)
{
	int reg;

	reg = phy_read(phydev, BCM87XX_LASI_STATUS);

	if (reg < 0) {
		phydev_err(phydev,
			   "Error: Read of BCM87XX_LASI_STATUS failed: %d\n",
			   reg);
		return 0;
	}
	return (reg & 1) != 0;
}

static int bcm87xx_ack_interrupt(struct phy_device *phydev)
{
	/* Reading the LASI status clears it. */
	bcm87xx_did_interrupt(phydev);
	return 0;
}

static int bcm8706_match_phy_device(struct phy_device *phydev)
{
	return phydev->c45_ids.device_ids[4] == PHY_ID_BCM8706;
}

static int bcm8727_match_phy_device(struct phy_device *phydev)
{
	return phydev->c45_ids.device_ids[4] == PHY_ID_BCM8727;
}

static struct phy_driver bcm87xx_driver[] = {
{
	.phy_id		= PHY_ID_BCM8706,
	.phy_id_mask	= 0xffffffff,
	.name		= "Broadcom BCM8706",
	.flags		= PHY_HAS_INTERRUPT,
	.config_init	= bcm87xx_config_init,
	.config_aneg	= bcm87xx_config_aneg,
	.read_status	= bcm87xx_read_status,
	.ack_interrupt	= bcm87xx_ack_interrupt,
	.config_intr	= bcm87xx_config_intr,
	.did_interrupt	= bcm87xx_did_interrupt,
	.match_phy_device = bcm8706_match_phy_device,
	.driver		= { .owner = THIS_MODULE },
}, {
	.phy_id		= PHY_ID_BCM8727,
	.phy_id_mask	= 0xffffffff,
	.name		= "Broadcom BCM8727",
	.flags		= PHY_HAS_INTERRUPT,
	.config_init	= bcm87xx_config_init,
	.config_aneg	= bcm87xx_config_aneg,
	.read_status	= bcm87xx_read_status,
	.ack_interrupt	= bcm87xx_ack_interrupt,
	.config_intr	= bcm87xx_config_intr,
	.did_interrupt	= bcm87xx_did_interrupt,
	.match_phy_device = bcm8727_match_phy_device,
	.driver		= { .owner = THIS_MODULE },
} };

module_phy_driver(bcm87xx_driver);

MODULE_LICENSE("GPL");
