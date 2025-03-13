// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Vitesse PHYs
 *
 * Author: Kriston Carson
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/bitfield.h>

/* Vitesse Extended Page Magic Register(s) */
#define MII_VSC73XX_EXT_PAGE_1E		0x01
#define MII_VSC82X4_EXT_PAGE_16E	0x10
#define MII_VSC82X4_EXT_PAGE_17E	0x11
#define MII_VSC82X4_EXT_PAGE_18E	0x12

/* Vitesse Extended Control Register 1 */
#define MII_VSC8244_EXT_CON1           0x17
#define MII_VSC8244_EXTCON1_INIT       0x0000
#define MII_VSC8244_EXTCON1_TX_SKEW_MASK	0x0c00
#define MII_VSC8244_EXTCON1_RX_SKEW_MASK	0x0300
#define MII_VSC8244_EXTCON1_TX_SKEW	0x0800
#define MII_VSC8244_EXTCON1_RX_SKEW	0x0200

/* Vitesse Interrupt Mask Register */
#define MII_VSC8244_IMASK		0x19
#define MII_VSC8244_IMASK_IEN		0x8000
#define MII_VSC8244_IMASK_SPEED		0x4000
#define MII_VSC8244_IMASK_LINK		0x2000
#define MII_VSC8244_IMASK_DUPLEX	0x1000
#define MII_VSC8244_IMASK_MASK		0xf000

#define MII_VSC8221_IMASK_MASK		0xa000

/* Vitesse Interrupt Status Register */
#define MII_VSC8244_ISTAT		0x1a
#define MII_VSC8244_ISTAT_STATUS	0x8000
#define MII_VSC8244_ISTAT_SPEED		0x4000
#define MII_VSC8244_ISTAT_LINK		0x2000
#define MII_VSC8244_ISTAT_DUPLEX	0x1000
#define MII_VSC8244_ISTAT_MASK		(MII_VSC8244_ISTAT_SPEED | \
					 MII_VSC8244_ISTAT_LINK | \
					 MII_VSC8244_ISTAT_DUPLEX)

#define MII_VSC8221_ISTAT_MASK		MII_VSC8244_ISTAT_LINK

/* Vitesse Auxiliary Control/Status Register */
#define MII_VSC8244_AUX_CONSTAT		0x1c
#define MII_VSC8244_AUXCONSTAT_INIT	0x0000
#define MII_VSC8244_AUXCONSTAT_DUPLEX	0x0020
#define MII_VSC8244_AUXCONSTAT_SPEED	0x0018
#define MII_VSC8244_AUXCONSTAT_GBIT	0x0010
#define MII_VSC8244_AUXCONSTAT_100	0x0008

#define MII_VSC8221_AUXCONSTAT_INIT	0x0004 /* need to set this bit? */
#define MII_VSC8221_AUXCONSTAT_RESERVED	0x0004

/* Vitesse Extended Page Access Register */
#define MII_VSC82X4_EXT_PAGE_ACCESS	0x1f

/* Vitesse VSC73XX Extended Control Register */
#define MII_VSC73XX_PHY_CTRL_EXT3		0x14

#define MII_VSC73XX_PHY_CTRL_EXT3_DOWNSHIFT_EN	BIT(4)
#define MII_VSC73XX_PHY_CTRL_EXT3_DOWNSHIFT_CNT	GENMASK(3, 2)
#define MII_VSC73XX_PHY_CTRL_EXT3_DOWNSHIFT_STA	BIT(1)
#define MII_VSC73XX_DOWNSHIFT_MAX		5
#define MII_VSC73XX_DOWNSHIFT_INVAL		1

/* VSC73XX PHY_BYPASS_CTRL register*/
#define MII_VSC73XX_PHY_BYPASS_CTRL		MII_DCOUNTER
#define MII_VSC73XX_PBC_TX_DIS			BIT(15)
#define MII_VSC73XX_PBC_FOR_SPD_AUTO_MDIX_DIS	BIT(7)
#define MII_VSC73XX_PBC_PAIR_SWAP_DIS		BIT(5)
#define MII_VSC73XX_PBC_POL_INV_DIS		BIT(4)
#define MII_VSC73XX_PBC_PARALLEL_DET_DIS	BIT(3)
#define MII_VSC73XX_PBC_AUTO_NP_EXCHANGE_DIS	BIT(1)

/* VSC73XX PHY_AUX_CTRL_STAT register */
#define MII_VSC73XX_PHY_AUX_CTRL_STAT	MII_NCONFIG
#define MII_VSC73XX_PACS_NO_MDI_X_IND	BIT(13)

/* Vitesse VSC8601 Extended PHY Control Register 1 */
#define MII_VSC8601_EPHY_CTL		0x17
#define MII_VSC8601_EPHY_CTL_RGMII_SKEW	(1 << 8)

#define PHY_ID_VSC8234			0x000fc620
#define PHY_ID_VSC8244			0x000fc6c0
#define PHY_ID_VSC8572			0x000704d0
#define PHY_ID_VSC8601			0x00070420
#define PHY_ID_VSC7385			0x00070450
#define PHY_ID_VSC7388			0x00070480
#define PHY_ID_VSC7395			0x00070550
#define PHY_ID_VSC7398			0x00070580
#define PHY_ID_VSC8662			0x00070660
#define PHY_ID_VSC8221			0x000fc550
#define PHY_ID_VSC8211			0x000fc4b0

MODULE_DESCRIPTION("Vitesse PHY driver");
MODULE_AUTHOR("Kriston Carson");
MODULE_LICENSE("GPL");

static int vsc824x_add_skew(struct phy_device *phydev)
{
	int err;
	int extcon;

	extcon = phy_read(phydev, MII_VSC8244_EXT_CON1);

	if (extcon < 0)
		return extcon;

	extcon &= ~(MII_VSC8244_EXTCON1_TX_SKEW_MASK |
			MII_VSC8244_EXTCON1_RX_SKEW_MASK);

	extcon |= (MII_VSC8244_EXTCON1_TX_SKEW |
			MII_VSC8244_EXTCON1_RX_SKEW);

	err = phy_write(phydev, MII_VSC8244_EXT_CON1, extcon);

	return err;
}

static int vsc824x_config_init(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, MII_VSC8244_AUX_CONSTAT,
			MII_VSC8244_AUXCONSTAT_INIT);
	if (err < 0)
		return err;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
		err = vsc824x_add_skew(phydev);

	return err;
}

#define VSC73XX_EXT_PAGE_ACCESS 0x1f

static int vsc73xx_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, VSC73XX_EXT_PAGE_ACCESS);
}

static int vsc73xx_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, VSC73XX_EXT_PAGE_ACCESS, page);
}

static int vsc73xx_get_downshift(struct phy_device *phydev, u8 *data)
{
	int val, enable, cnt;

	val = phy_read_paged(phydev, MII_VSC73XX_EXT_PAGE_1E,
			     MII_VSC73XX_PHY_CTRL_EXT3);
	if (val < 0)
		return val;

	enable = FIELD_GET(MII_VSC73XX_PHY_CTRL_EXT3_DOWNSHIFT_EN, val);
	cnt = FIELD_GET(MII_VSC73XX_PHY_CTRL_EXT3_DOWNSHIFT_CNT, val) + 2;

	*data = enable ? cnt : DOWNSHIFT_DEV_DISABLE;

	return 0;
}

static int vsc73xx_set_downshift(struct phy_device *phydev, u8 cnt)
{
	u16 mask, val;
	int ret;

	if (cnt > MII_VSC73XX_DOWNSHIFT_MAX)
		return -E2BIG;
	else if (cnt == MII_VSC73XX_DOWNSHIFT_INVAL)
		return -EINVAL;

	mask = MII_VSC73XX_PHY_CTRL_EXT3_DOWNSHIFT_EN;

	if (!cnt) {
		val = 0;
	} else {
		mask |= MII_VSC73XX_PHY_CTRL_EXT3_DOWNSHIFT_CNT;
		val = MII_VSC73XX_PHY_CTRL_EXT3_DOWNSHIFT_EN |
		      FIELD_PREP(MII_VSC73XX_PHY_CTRL_EXT3_DOWNSHIFT_CNT,
				 cnt - 2);
	}

	ret = phy_modify_paged(phydev, MII_VSC73XX_EXT_PAGE_1E,
			       MII_VSC73XX_PHY_CTRL_EXT3, mask, val);
	if (ret < 0)
		return ret;

	return genphy_soft_reset(phydev);
}

static int vsc73xx_get_tunable(struct phy_device *phydev,
			       struct ethtool_tunable *tuna, void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return vsc73xx_get_downshift(phydev, data);
	default:
		return -EOPNOTSUPP;
	}
}

static int vsc73xx_set_tunable(struct phy_device *phydev,
			       struct ethtool_tunable *tuna, const void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return vsc73xx_set_downshift(phydev, *(const u8 *)data);
	default:
		return -EOPNOTSUPP;
	}
}

static void vsc73xx_config_init(struct phy_device *phydev)
{
	/* Receiver init */
	phy_write(phydev, 0x1f, 0x2a30);
	phy_modify(phydev, 0x0c, 0x0300, 0x0200);
	phy_write(phydev, 0x1f, 0x0000);

	/* Config LEDs 0x61 */
	phy_modify(phydev, MII_TPISTATUS, 0xff00, 0x0061);

	/* Enable downshift by default */
	vsc73xx_set_downshift(phydev, MII_VSC73XX_DOWNSHIFT_MAX);

	/* Set Auto MDI-X by default */
	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;
}

static int vsc738x_config_init(struct phy_device *phydev)
{
	u16 rev;
	/* This magic sequence appear in the application note
	 * "VSC7385/7388 PHY Configuration".
	 *
	 * Maybe one day we will get to know what it all means.
	 */
	phy_write(phydev, 0x1f, 0x2a30);
	phy_modify(phydev, 0x08, 0x0200, 0x0200);
	phy_write(phydev, 0x1f, 0x52b5);
	phy_write(phydev, 0x10, 0xb68a);
	phy_modify(phydev, 0x12, 0xff07, 0x0003);
	phy_modify(phydev, 0x11, 0x00ff, 0x00a2);
	phy_write(phydev, 0x10, 0x968a);
	phy_write(phydev, 0x1f, 0x2a30);
	phy_modify(phydev, 0x08, 0x0200, 0x0000);
	phy_write(phydev, 0x1f, 0x0000);

	/* Read revision */
	rev = phy_read(phydev, MII_PHYSID2);
	rev &= 0x0f;

	/* Special quirk for revision 0 */
	if (rev == 0) {
		phy_write(phydev, 0x1f, 0x2a30);
		phy_modify(phydev, 0x08, 0x0200, 0x0200);
		phy_write(phydev, 0x1f, 0x52b5);
		phy_write(phydev, 0x12, 0x0000);
		phy_write(phydev, 0x11, 0x0689);
		phy_write(phydev, 0x10, 0x8f92);
		phy_write(phydev, 0x1f, 0x52b5);
		phy_write(phydev, 0x12, 0x0000);
		phy_write(phydev, 0x11, 0x0e35);
		phy_write(phydev, 0x10, 0x9786);
		phy_write(phydev, 0x1f, 0x2a30);
		phy_modify(phydev, 0x08, 0x0200, 0x0000);
		phy_write(phydev, 0x17, 0xff80);
		phy_write(phydev, 0x17, 0x0000);
	}

	phy_write(phydev, 0x1f, 0x0000);
	phy_write(phydev, 0x12, 0x0048);

	if (rev == 0) {
		phy_write(phydev, 0x1f, 0x2a30);
		phy_write(phydev, 0x14, 0x6600);
		phy_write(phydev, 0x1f, 0x0000);
		phy_write(phydev, 0x18, 0xa24e);
	} else {
		phy_write(phydev, 0x1f, 0x2a30);
		phy_modify(phydev, 0x16, 0x0fc0, 0x0240);
		phy_modify(phydev, 0x14, 0x6000, 0x4000);
		/* bits 14-15 in extended register 0x14 controls DACG amplitude
		 * 6 = -8%, 2 is hardware default
		 */
		phy_write(phydev, 0x1f, 0x0001);
		phy_modify(phydev, 0x14, 0xe000, 0x6000);
		phy_write(phydev, 0x1f, 0x0000);
	}

	vsc73xx_config_init(phydev);

	return 0;
}

static int vsc739x_config_init(struct phy_device *phydev)
{
	/* This magic sequence appears in the VSC7395 SparX-G5e application
	 * note "VSC7395/VSC7398 PHY Configuration"
	 *
	 * Maybe one day we will get to know what it all means.
	 */
	phy_write(phydev, 0x1f, 0x2a30);
	phy_modify(phydev, 0x08, 0x0200, 0x0200);
	phy_write(phydev, 0x1f, 0x52b5);
	phy_write(phydev, 0x10, 0xb68a);
	phy_modify(phydev, 0x12, 0xff07, 0x0003);
	phy_modify(phydev, 0x11, 0x00ff, 0x00a2);
	phy_write(phydev, 0x10, 0x968a);
	phy_write(phydev, 0x1f, 0x2a30);
	phy_modify(phydev, 0x08, 0x0200, 0x0000);
	phy_write(phydev, 0x1f, 0x0000);

	phy_write(phydev, 0x1f, 0x0000);
	phy_write(phydev, 0x12, 0x0048);
	phy_write(phydev, 0x1f, 0x2a30);
	phy_modify(phydev, 0x16, 0x0fc0, 0x0240);
	phy_modify(phydev, 0x14, 0x6000, 0x4000);
	phy_write(phydev, 0x1f, 0x0001);
	phy_modify(phydev, 0x14, 0xe000, 0x6000);
	phy_write(phydev, 0x1f, 0x0000);

	vsc73xx_config_init(phydev);

	return 0;
}

static int vsc73xx_mdix_set(struct phy_device *phydev, u8 mdix)
{
	int ret;
	u16 val;

	val = phy_read(phydev, MII_VSC73XX_PHY_BYPASS_CTRL);

	switch (mdix) {
	case ETH_TP_MDI:
		val |= MII_VSC73XX_PBC_FOR_SPD_AUTO_MDIX_DIS |
		       MII_VSC73XX_PBC_PAIR_SWAP_DIS |
		       MII_VSC73XX_PBC_POL_INV_DIS;
		break;
	case ETH_TP_MDI_X:
		/* When MDI-X auto configuration is disabled, is possible
		 * to force only MDI mode. Let's use autoconfig for forced
		 * MDIX mode.
		 */
	case ETH_TP_MDI_AUTO:
		val &= ~(MII_VSC73XX_PBC_FOR_SPD_AUTO_MDIX_DIS |
			 MII_VSC73XX_PBC_PAIR_SWAP_DIS |
			 MII_VSC73XX_PBC_POL_INV_DIS);
		break;
	default:
		return -EINVAL;
	}

	ret = phy_write(phydev, MII_VSC73XX_PHY_BYPASS_CTRL, val);
	if (ret)
		return ret;

	return genphy_restart_aneg(phydev);
}

static int vsc73xx_config_aneg(struct phy_device *phydev)
{
	int ret;

	ret = vsc73xx_mdix_set(phydev, phydev->mdix_ctrl);
	if (ret)
		return ret;

	return genphy_config_aneg(phydev);
}

static int vsc73xx_mdix_get(struct phy_device *phydev, u8 *mdix)
{
	u16 reg_val;

	reg_val = phy_read(phydev, MII_VSC73XX_PHY_AUX_CTRL_STAT);
	if (reg_val & MII_VSC73XX_PACS_NO_MDI_X_IND)
		*mdix = ETH_TP_MDI;
	else
		*mdix = ETH_TP_MDI_X;

	return 0;
}

static int vsc73xx_read_status(struct phy_device *phydev)
{
	int ret;

	ret = vsc73xx_mdix_get(phydev, &phydev->mdix);
	if (ret < 0)
		return ret;

	return genphy_read_status(phydev);
}

/* This adds a skew for both TX and RX clocks, so the skew should only be
 * applied to "rgmii-id" interfaces. It may not work as expected
 * on "rgmii-txid", "rgmii-rxid" or "rgmii" interfaces.
 */
static int vsc8601_add_skew(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, MII_VSC8601_EPHY_CTL);
	if (ret < 0)
		return ret;

	ret |= MII_VSC8601_EPHY_CTL_RGMII_SKEW;
	return phy_write(phydev, MII_VSC8601_EPHY_CTL, ret);
}

static int vsc8601_config_init(struct phy_device *phydev)
{
	int ret = 0;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
		ret = vsc8601_add_skew(phydev);

	if (ret < 0)
		return ret;

	return 0;
}

static int vsc82xx_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		/* Don't bother to ACK the interrupts since the 824x cannot
		 * clear the interrupts if they are disabled.
		 */
		err = phy_write(phydev, MII_VSC8244_IMASK,
			(phydev->drv->phy_id == PHY_ID_VSC8234 ||
			 phydev->drv->phy_id == PHY_ID_VSC8244 ||
			 phydev->drv->phy_id == PHY_ID_VSC8572 ||
			 phydev->drv->phy_id == PHY_ID_VSC8601) ?
				MII_VSC8244_IMASK_MASK :
				MII_VSC8221_IMASK_MASK);
	else {
		/* The Vitesse PHY cannot clear the interrupt
		 * once it has disabled them, so we clear them first
		 */
		err = phy_read(phydev, MII_VSC8244_ISTAT);

		if (err < 0)
			return err;

		err = phy_write(phydev, MII_VSC8244_IMASK, 0);
	}

	return err;
}

static irqreturn_t vsc82xx_handle_interrupt(struct phy_device *phydev)
{
	int irq_status, irq_mask;

	if (phydev->drv->phy_id == PHY_ID_VSC8244 ||
	    phydev->drv->phy_id == PHY_ID_VSC8572 ||
	    phydev->drv->phy_id == PHY_ID_VSC8601)
		irq_mask = MII_VSC8244_ISTAT_MASK;
	else
		irq_mask = MII_VSC8221_ISTAT_MASK;

	irq_status = phy_read(phydev, MII_VSC8244_ISTAT);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & irq_mask))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int vsc8221_config_init(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, MII_VSC8244_AUX_CONSTAT,
			MII_VSC8221_AUXCONSTAT_INIT);
	return err;

	/* Perhaps we should set EXT_CON1 based on the interface?
	 * Options are 802.3Z SerDes or SGMII
	 */
}

/* vsc82x4_config_autocross_enable - Enable auto MDI/MDI-X for forced links
 * @phydev: target phy_device struct
 *
 * Enable auto MDI/MDI-X when in 10/100 forced link speeds by writing
 * special values in the VSC8234/VSC8244 extended reserved registers
 */
static int vsc82x4_config_autocross_enable(struct phy_device *phydev)
{
	int ret;

	if (phydev->autoneg == AUTONEG_ENABLE || phydev->speed > SPEED_100)
		return 0;

	/* map extended registers set 0x10 - 0x1e */
	ret = phy_write(phydev, MII_VSC82X4_EXT_PAGE_ACCESS, 0x52b5);
	if (ret >= 0)
		ret = phy_write(phydev, MII_VSC82X4_EXT_PAGE_18E, 0x0012);
	if (ret >= 0)
		ret = phy_write(phydev, MII_VSC82X4_EXT_PAGE_17E, 0x2803);
	if (ret >= 0)
		ret = phy_write(phydev, MII_VSC82X4_EXT_PAGE_16E, 0x87fa);
	/* map standard registers set 0x10 - 0x1e */
	if (ret >= 0)
		ret = phy_write(phydev, MII_VSC82X4_EXT_PAGE_ACCESS, 0x0000);
	else
		phy_write(phydev, MII_VSC82X4_EXT_PAGE_ACCESS, 0x0000);

	return ret;
}

/* vsc82x4_config_aneg - restart auto-negotiation or write BMCR
 * @phydev: target phy_device struct
 *
 * Description: If auto-negotiation is enabled, we configure the
 *   advertising, and then restart auto-negotiation.  If it is not
 *   enabled, then we write the BMCR and also start the auto
 *   MDI/MDI-X feature
 */
static int vsc82x4_config_aneg(struct phy_device *phydev)
{
	int ret;

	/* Enable auto MDI/MDI-X when in 10/100 forced link speeds by
	 * writing special values in the VSC8234 extended reserved registers
	 */
	if (phydev->autoneg != AUTONEG_ENABLE && phydev->speed <= SPEED_100) {
		ret = genphy_setup_forced(phydev);

		if (ret < 0) /* error */
			return ret;

		return vsc82x4_config_autocross_enable(phydev);
	}

	return genphy_config_aneg(phydev);
}

/* Vitesse 82xx */
static struct phy_driver vsc82xx_driver[] = {
{
	.phy_id         = PHY_ID_VSC8234,
	.name           = "Vitesse VSC8234",
	.phy_id_mask    = 0x000ffff0,
	/* PHY_GBIT_FEATURES */
	.config_init    = &vsc824x_config_init,
	.config_aneg    = &vsc82x4_config_aneg,
	.config_intr    = &vsc82xx_config_intr,
	.handle_interrupt = &vsc82xx_handle_interrupt,
}, {
	.phy_id		= PHY_ID_VSC8244,
	.name		= "Vitesse VSC8244",
	.phy_id_mask	= 0x000fffc0,
	/* PHY_GBIT_FEATURES */
	.config_init	= &vsc824x_config_init,
	.config_aneg	= &vsc82x4_config_aneg,
	.config_intr	= &vsc82xx_config_intr,
	.handle_interrupt = &vsc82xx_handle_interrupt,
}, {
	.phy_id         = PHY_ID_VSC8572,
	.name           = "Vitesse VSC8572",
	.phy_id_mask    = 0x000ffff0,
	/* PHY_GBIT_FEATURES */
	.config_init    = &vsc824x_config_init,
	.config_aneg    = &vsc82x4_config_aneg,
	.config_intr    = &vsc82xx_config_intr,
	.handle_interrupt = &vsc82xx_handle_interrupt,
}, {
	.phy_id         = PHY_ID_VSC8601,
	.name           = "Vitesse VSC8601",
	.phy_id_mask    = 0x000ffff0,
	/* PHY_GBIT_FEATURES */
	.config_init    = &vsc8601_config_init,
	.config_intr    = &vsc82xx_config_intr,
	.handle_interrupt = &vsc82xx_handle_interrupt,
}, {
	.phy_id         = PHY_ID_VSC7385,
	.name           = "Vitesse VSC7385",
	.phy_id_mask    = 0x000ffff0,
	/* PHY_GBIT_FEATURES */
	.config_init    = vsc738x_config_init,
	.config_aneg    = vsc73xx_config_aneg,
	.read_status	= vsc73xx_read_status,
	.read_page      = vsc73xx_read_page,
	.write_page     = vsc73xx_write_page,
	.get_tunable    = vsc73xx_get_tunable,
	.set_tunable    = vsc73xx_set_tunable,
}, {
	.phy_id         = PHY_ID_VSC7388,
	.name           = "Vitesse VSC7388",
	.phy_id_mask    = 0x000ffff0,
	/* PHY_GBIT_FEATURES */
	.config_init    = vsc738x_config_init,
	.config_aneg    = vsc73xx_config_aneg,
	.read_status	= vsc73xx_read_status,
	.read_page      = vsc73xx_read_page,
	.write_page     = vsc73xx_write_page,
	.get_tunable    = vsc73xx_get_tunable,
	.set_tunable    = vsc73xx_set_tunable,
}, {
	.phy_id         = PHY_ID_VSC7395,
	.name           = "Vitesse VSC7395",
	.phy_id_mask    = 0x000ffff0,
	/* PHY_GBIT_FEATURES */
	.config_init    = vsc739x_config_init,
	.config_aneg    = vsc73xx_config_aneg,
	.read_status	= vsc73xx_read_status,
	.read_page      = vsc73xx_read_page,
	.write_page     = vsc73xx_write_page,
	.get_tunable    = vsc73xx_get_tunable,
	.set_tunable    = vsc73xx_set_tunable,
}, {
	.phy_id         = PHY_ID_VSC7398,
	.name           = "Vitesse VSC7398",
	.phy_id_mask    = 0x000ffff0,
	/* PHY_GBIT_FEATURES */
	.config_init    = vsc739x_config_init,
	.config_aneg    = vsc73xx_config_aneg,
	.read_status	= vsc73xx_read_status,
	.read_page      = vsc73xx_read_page,
	.write_page     = vsc73xx_write_page,
	.get_tunable    = vsc73xx_get_tunable,
	.set_tunable    = vsc73xx_set_tunable,
}, {
	.phy_id         = PHY_ID_VSC8662,
	.name           = "Vitesse VSC8662",
	.phy_id_mask    = 0x000ffff0,
	/* PHY_GBIT_FEATURES */
	.config_init    = &vsc824x_config_init,
	.config_aneg    = &vsc82x4_config_aneg,
	.config_intr    = &vsc82xx_config_intr,
	.handle_interrupt = &vsc82xx_handle_interrupt,
}, {
	/* Vitesse 8221 */
	.phy_id		= PHY_ID_VSC8221,
	.phy_id_mask	= 0x000ffff0,
	.name		= "Vitesse VSC8221",
	/* PHY_GBIT_FEATURES */
	.config_init	= &vsc8221_config_init,
	.config_intr	= &vsc82xx_config_intr,
	.handle_interrupt = &vsc82xx_handle_interrupt,
}, {
	/* Vitesse 8211 */
	.phy_id		= PHY_ID_VSC8211,
	.phy_id_mask	= 0x000ffff0,
	.name		= "Vitesse VSC8211",
	/* PHY_GBIT_FEATURES */
	.config_init	= &vsc8221_config_init,
	.config_intr	= &vsc82xx_config_intr,
	.handle_interrupt = &vsc82xx_handle_interrupt,
} };

module_phy_driver(vsc82xx_driver);

static const struct mdio_device_id __maybe_unused vitesse_tbl[] = {
	{ PHY_ID_VSC8234, 0x000ffff0 },
	{ PHY_ID_VSC8244, 0x000fffc0 },
	{ PHY_ID_VSC8572, 0x000ffff0 },
	{ PHY_ID_VSC7385, 0x000ffff0 },
	{ PHY_ID_VSC7388, 0x000ffff0 },
	{ PHY_ID_VSC7395, 0x000ffff0 },
	{ PHY_ID_VSC7398, 0x000ffff0 },
	{ PHY_ID_VSC8662, 0x000ffff0 },
	{ PHY_ID_VSC8221, 0x000ffff0 },
	{ PHY_ID_VSC8211, 0x000ffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, vitesse_tbl);
