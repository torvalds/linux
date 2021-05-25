// SPDX-License-Identifier: GPL-2.0+
/*
 *	drivers/net/phy/broadcom.c
 *
 *	Broadcom BCM5411, BCM5421 and BCM5461 Gigabit Ethernet
 *	transceivers.
 *
 *	Copyright (c) 2006  Maciej W. Rozycki
 *
 *	Inspired by code written by Amy Fong.
 */

#include "bcm-phy-lib.h"
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/brcmphy.h>
#include <linux/of.h>

#define BRCM_PHY_MODEL(phydev) \
	((phydev)->drv->phy_id & (phydev)->drv->phy_id_mask)

#define BRCM_PHY_REV(phydev) \
	((phydev)->drv->phy_id & ~((phydev)->drv->phy_id_mask))

MODULE_DESCRIPTION("Broadcom PHY driver");
MODULE_AUTHOR("Maciej W. Rozycki");
MODULE_LICENSE("GPL");

static int bcm54xx_config_clock_delay(struct phy_device *phydev)
{
	int rc, val;

	/* handling PHY's internal RX clock delay */
	val = bcm54xx_auxctl_read(phydev, MII_BCM54XX_AUXCTL_SHDWSEL_MISC);
	val |= MII_BCM54XX_AUXCTL_MISC_WREN;
	if (phydev->interface == PHY_INTERFACE_MODE_RGMII ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		/* Disable RGMII RXC-RXD skew */
		val &= ~MII_BCM54XX_AUXCTL_SHDWSEL_MISC_RGMII_SKEW_EN;
	}
	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) {
		/* Enable RGMII RXC-RXD skew */
		val |= MII_BCM54XX_AUXCTL_SHDWSEL_MISC_RGMII_SKEW_EN;
	}
	rc = bcm54xx_auxctl_write(phydev, MII_BCM54XX_AUXCTL_SHDWSEL_MISC,
				  val);
	if (rc < 0)
		return rc;

	/* handling PHY's internal TX clock delay */
	val = bcm_phy_read_shadow(phydev, BCM54810_SHD_CLK_CTL);
	if (phydev->interface == PHY_INTERFACE_MODE_RGMII ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) {
		/* Disable internal TX clock delay */
		val &= ~BCM54810_SHD_CLK_CTL_GTXCLK_EN;
	}
	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		/* Enable internal TX clock delay */
		val |= BCM54810_SHD_CLK_CTL_GTXCLK_EN;
	}
	rc = bcm_phy_write_shadow(phydev, BCM54810_SHD_CLK_CTL, val);
	if (rc < 0)
		return rc;

	return 0;
}

static int bcm54210e_config_init(struct phy_device *phydev)
{
	int val;

	bcm54xx_config_clock_delay(phydev);

	if (phydev->dev_flags & PHY_BRCM_EN_MASTER_MODE) {
		val = phy_read(phydev, MII_CTRL1000);
		val |= CTL1000_AS_MASTER | CTL1000_ENABLE_MASTER;
		phy_write(phydev, MII_CTRL1000, val);
	}

	return 0;
}

static int bcm54612e_config_init(struct phy_device *phydev)
{
	int reg;

	bcm54xx_config_clock_delay(phydev);

	/* Enable CLK125 MUX on LED4 if ref clock is enabled. */
	if (!(phydev->dev_flags & PHY_BRCM_RX_REFCLK_UNUSED)) {
		int err;

		reg = bcm_phy_read_exp(phydev, BCM54612E_EXP_SPARE0);
		err = bcm_phy_write_exp(phydev, BCM54612E_EXP_SPARE0,
					BCM54612E_LED4_CLK125OUT_EN | reg);

		if (err < 0)
			return err;
	}

	return 0;
}

static int bcm54616s_config_init(struct phy_device *phydev)
{
	int rc, val;

	if (phydev->interface != PHY_INTERFACE_MODE_SGMII &&
	    phydev->interface != PHY_INTERFACE_MODE_1000BASEX)
		return 0;

	/* Ensure proper interface mode is selected. */
	/* Disable RGMII mode */
	val = bcm54xx_auxctl_read(phydev, MII_BCM54XX_AUXCTL_SHDWSEL_MISC);
	if (val < 0)
		return val;
	val &= ~MII_BCM54XX_AUXCTL_SHDWSEL_MISC_RGMII_EN;
	val |= MII_BCM54XX_AUXCTL_MISC_WREN;
	rc = bcm54xx_auxctl_write(phydev, MII_BCM54XX_AUXCTL_SHDWSEL_MISC,
				  val);
	if (rc < 0)
		return rc;

	/* Select 1000BASE-X register set (primary SerDes) */
	val = bcm_phy_read_shadow(phydev, BCM54XX_SHD_MODE);
	if (val < 0)
		return val;
	val |= BCM54XX_SHD_MODE_1000BX;
	rc = bcm_phy_write_shadow(phydev, BCM54XX_SHD_MODE, val);
	if (rc < 0)
		return rc;

	/* Power down SerDes interface */
	rc = phy_set_bits(phydev, MII_BMCR, BMCR_PDOWN);
	if (rc < 0)
		return rc;

	/* Select proper interface mode */
	val &= ~BCM54XX_SHD_INTF_SEL_MASK;
	val |= phydev->interface == PHY_INTERFACE_MODE_SGMII ?
		BCM54XX_SHD_INTF_SEL_SGMII :
		BCM54XX_SHD_INTF_SEL_GBIC;
	rc = bcm_phy_write_shadow(phydev, BCM54XX_SHD_MODE, val);
	if (rc < 0)
		return rc;

	/* Power up SerDes interface */
	rc = phy_clear_bits(phydev, MII_BMCR, BMCR_PDOWN);
	if (rc < 0)
		return rc;

	/* Select copper register set */
	val &= ~BCM54XX_SHD_MODE_1000BX;
	rc = bcm_phy_write_shadow(phydev, BCM54XX_SHD_MODE, val);
	if (rc < 0)
		return rc;

	/* Power up copper interface */
	return phy_clear_bits(phydev, MII_BMCR, BMCR_PDOWN);
}

/* Needs SMDSP clock enabled via bcm54xx_phydsp_config() */
static int bcm50610_a0_workaround(struct phy_device *phydev)
{
	int err;

	err = bcm_phy_write_exp(phydev, MII_BCM54XX_EXP_AADJ1CH0,
				MII_BCM54XX_EXP_AADJ1CH0_SWP_ABCD_OEN |
				MII_BCM54XX_EXP_AADJ1CH0_SWSEL_THPF);
	if (err < 0)
		return err;

	err = bcm_phy_write_exp(phydev, MII_BCM54XX_EXP_AADJ1CH3,
				MII_BCM54XX_EXP_AADJ1CH3_ADCCKADJ);
	if (err < 0)
		return err;

	err = bcm_phy_write_exp(phydev, MII_BCM54XX_EXP_EXP75,
				MII_BCM54XX_EXP_EXP75_VDACCTRL);
	if (err < 0)
		return err;

	err = bcm_phy_write_exp(phydev, MII_BCM54XX_EXP_EXP96,
				MII_BCM54XX_EXP_EXP96_MYST);
	if (err < 0)
		return err;

	err = bcm_phy_write_exp(phydev, MII_BCM54XX_EXP_EXP97,
				MII_BCM54XX_EXP_EXP97_MYST);

	return err;
}

static int bcm54xx_phydsp_config(struct phy_device *phydev)
{
	int err, err2;

	/* Enable the SMDSP clock */
	err = bcm54xx_auxctl_write(phydev,
				   MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL,
				   MII_BCM54XX_AUXCTL_ACTL_SMDSP_ENA |
				   MII_BCM54XX_AUXCTL_ACTL_TX_6DB);
	if (err < 0)
		return err;

	if (BRCM_PHY_MODEL(phydev) == PHY_ID_BCM50610 ||
	    BRCM_PHY_MODEL(phydev) == PHY_ID_BCM50610M) {
		/* Clear bit 9 to fix a phy interop issue. */
		err = bcm_phy_write_exp(phydev, MII_BCM54XX_EXP_EXP08,
					MII_BCM54XX_EXP_EXP08_RJCT_2MHZ);
		if (err < 0)
			goto error;

		if (phydev->drv->phy_id == PHY_ID_BCM50610) {
			err = bcm50610_a0_workaround(phydev);
			if (err < 0)
				goto error;
		}
	}

	if (BRCM_PHY_MODEL(phydev) == PHY_ID_BCM57780) {
		int val;

		val = bcm_phy_read_exp(phydev, MII_BCM54XX_EXP_EXP75);
		if (val < 0)
			goto error;

		val |= MII_BCM54XX_EXP_EXP75_CM_OSC;
		err = bcm_phy_write_exp(phydev, MII_BCM54XX_EXP_EXP75, val);
	}

error:
	/* Disable the SMDSP clock */
	err2 = bcm54xx_auxctl_write(phydev,
				    MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL,
				    MII_BCM54XX_AUXCTL_ACTL_TX_6DB);

	/* Return the first error reported. */
	return err ? err : err2;
}

static void bcm54xx_adjust_rxrefclk(struct phy_device *phydev)
{
	u32 orig;
	int val;
	bool clk125en = true;

	/* Abort if we are using an untested phy. */
	if (BRCM_PHY_MODEL(phydev) != PHY_ID_BCM57780 &&
	    BRCM_PHY_MODEL(phydev) != PHY_ID_BCM50610 &&
	    BRCM_PHY_MODEL(phydev) != PHY_ID_BCM50610M &&
	    BRCM_PHY_MODEL(phydev) != PHY_ID_BCM54210E &&
	    BRCM_PHY_MODEL(phydev) != PHY_ID_BCM54810 &&
	    BRCM_PHY_MODEL(phydev) != PHY_ID_BCM54811)
		return;

	val = bcm_phy_read_shadow(phydev, BCM54XX_SHD_SCR3);
	if (val < 0)
		return;

	orig = val;

	if ((BRCM_PHY_MODEL(phydev) == PHY_ID_BCM50610 ||
	     BRCM_PHY_MODEL(phydev) == PHY_ID_BCM50610M) &&
	    BRCM_PHY_REV(phydev) >= 0x3) {
		/*
		 * Here, bit 0 _disables_ CLK125 when set.
		 * This bit is set by default.
		 */
		clk125en = false;
	} else {
		if (phydev->dev_flags & PHY_BRCM_RX_REFCLK_UNUSED) {
			if (BRCM_PHY_MODEL(phydev) != PHY_ID_BCM54811) {
				/* Here, bit 0 _enables_ CLK125 when set */
				val &= ~BCM54XX_SHD_SCR3_DEF_CLK125;
			}
			clk125en = false;
		}
	}

	if (!clk125en || (phydev->dev_flags & PHY_BRCM_AUTO_PWRDWN_ENABLE))
		val &= ~BCM54XX_SHD_SCR3_DLLAPD_DIS;
	else
		val |= BCM54XX_SHD_SCR3_DLLAPD_DIS;

	if (phydev->dev_flags & PHY_BRCM_DIS_TXCRXC_NOENRGY) {
		if (BRCM_PHY_MODEL(phydev) == PHY_ID_BCM54210E ||
		    BRCM_PHY_MODEL(phydev) == PHY_ID_BCM54810 ||
		    BRCM_PHY_MODEL(phydev) == PHY_ID_BCM54210E)
			val |= BCM54XX_SHD_SCR3_RXCTXC_DIS;
		else
			val |= BCM54XX_SHD_SCR3_TRDDAPD;
	}

	if (orig != val)
		bcm_phy_write_shadow(phydev, BCM54XX_SHD_SCR3, val);

	val = bcm_phy_read_shadow(phydev, BCM54XX_SHD_APD);
	if (val < 0)
		return;

	orig = val;

	if (!clk125en || (phydev->dev_flags & PHY_BRCM_AUTO_PWRDWN_ENABLE))
		val |= BCM54XX_SHD_APD_EN;
	else
		val &= ~BCM54XX_SHD_APD_EN;

	if (orig != val)
		bcm_phy_write_shadow(phydev, BCM54XX_SHD_APD, val);
}

static int bcm54xx_config_init(struct phy_device *phydev)
{
	int reg, err, val;

	reg = phy_read(phydev, MII_BCM54XX_ECR);
	if (reg < 0)
		return reg;

	/* Mask interrupts globally.  */
	reg |= MII_BCM54XX_ECR_IM;
	err = phy_write(phydev, MII_BCM54XX_ECR, reg);
	if (err < 0)
		return err;

	/* Unmask events we are interested in.  */
	reg = ~(MII_BCM54XX_INT_DUPLEX |
		MII_BCM54XX_INT_SPEED |
		MII_BCM54XX_INT_LINK);
	err = phy_write(phydev, MII_BCM54XX_IMR, reg);
	if (err < 0)
		return err;

	if ((BRCM_PHY_MODEL(phydev) == PHY_ID_BCM50610 ||
	     BRCM_PHY_MODEL(phydev) == PHY_ID_BCM50610M) &&
	    (phydev->dev_flags & PHY_BRCM_CLEAR_RGMII_MODE))
		bcm_phy_write_shadow(phydev, BCM54XX_SHD_RGMII_MODE, 0);

	bcm54xx_adjust_rxrefclk(phydev);

	switch (BRCM_PHY_MODEL(phydev)) {
	case PHY_ID_BCM50610:
	case PHY_ID_BCM50610M:
		err = bcm54xx_config_clock_delay(phydev);
		break;
	case PHY_ID_BCM54210E:
		err = bcm54210e_config_init(phydev);
		break;
	case PHY_ID_BCM54612E:
		err = bcm54612e_config_init(phydev);
		break;
	case PHY_ID_BCM54616S:
		err = bcm54616s_config_init(phydev);
		break;
	case PHY_ID_BCM54810:
		/* For BCM54810, we need to disable BroadR-Reach function */
		val = bcm_phy_read_exp(phydev,
				       BCM54810_EXP_BROADREACH_LRE_MISC_CTL);
		val &= ~BCM54810_EXP_BROADREACH_LRE_MISC_CTL_EN;
		err = bcm_phy_write_exp(phydev,
					BCM54810_EXP_BROADREACH_LRE_MISC_CTL,
					val);
		break;
	}
	if (err)
		return err;

	bcm54xx_phydsp_config(phydev);

	/* For non-SFP setups, encode link speed into LED1 and LED3 pair
	 * (green/amber).
	 * Also flash these two LEDs on activity. This means configuring
	 * them for MULTICOLOR and encoding link/activity into them.
	 * Don't do this for devices on an SFP module, since some of these
	 * use the LED outputs to control the SFP LOS signal, and changing
	 * these settings will cause LOS to malfunction.
	 */
	if (!phy_on_sfp(phydev)) {
		val = BCM5482_SHD_LEDS1_LED1(BCM_LED_SRC_MULTICOLOR1) |
			BCM5482_SHD_LEDS1_LED3(BCM_LED_SRC_MULTICOLOR1);
		bcm_phy_write_shadow(phydev, BCM5482_SHD_LEDS1, val);

		val = BCM_LED_MULTICOLOR_IN_PHASE |
			BCM5482_SHD_LEDS1_LED1(BCM_LED_MULTICOLOR_LINK_ACT) |
			BCM5482_SHD_LEDS1_LED3(BCM_LED_MULTICOLOR_LINK_ACT);
		bcm_phy_write_exp(phydev, BCM_EXP_MULTICOLOR, val);
	}

	return 0;
}

static int bcm54xx_resume(struct phy_device *phydev)
{
	int ret;

	/* Writes to register other than BMCR would be ignored
	 * unless we clear the PDOWN bit first
	 */
	ret = genphy_resume(phydev);
	if (ret < 0)
		return ret;

	/* Upon exiting power down, the PHY remains in an internal reset state
	 * for 40us
	 */
	fsleep(40);

	return bcm54xx_config_init(phydev);
}

static int bcm54811_config_init(struct phy_device *phydev)
{
	int err, reg;

	/* Disable BroadR-Reach function. */
	reg = bcm_phy_read_exp(phydev, BCM54810_EXP_BROADREACH_LRE_MISC_CTL);
	reg &= ~BCM54810_EXP_BROADREACH_LRE_MISC_CTL_EN;
	err = bcm_phy_write_exp(phydev, BCM54810_EXP_BROADREACH_LRE_MISC_CTL,
				reg);
	if (err < 0)
		return err;

	err = bcm54xx_config_init(phydev);

	/* Enable CLK125 MUX on LED4 if ref clock is enabled. */
	if (!(phydev->dev_flags & PHY_BRCM_RX_REFCLK_UNUSED)) {
		reg = bcm_phy_read_exp(phydev, BCM54612E_EXP_SPARE0);
		err = bcm_phy_write_exp(phydev, BCM54612E_EXP_SPARE0,
					BCM54612E_LED4_CLK125OUT_EN | reg);
		if (err < 0)
			return err;
	}

	return err;
}

static int bcm5481_config_aneg(struct phy_device *phydev)
{
	struct device_node *np = phydev->mdio.dev.of_node;
	int ret;

	/* Aneg firstly. */
	ret = genphy_config_aneg(phydev);

	/* Then we can set up the delay. */
	bcm54xx_config_clock_delay(phydev);

	if (of_property_read_bool(np, "enet-phy-lane-swap")) {
		/* Lane Swap - Undocumented register...magic! */
		ret = bcm_phy_write_exp(phydev, MII_BCM54XX_EXP_SEL_ER + 0x9,
					0x11B);
		if (ret < 0)
			return ret;
	}

	return ret;
}

struct bcm54616s_phy_priv {
	bool mode_1000bx_en;
};

static int bcm54616s_probe(struct phy_device *phydev)
{
	struct bcm54616s_phy_priv *priv;
	int val;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	val = bcm_phy_read_shadow(phydev, BCM54XX_SHD_MODE);
	if (val < 0)
		return val;

	/* The PHY is strapped in RGMII-fiber mode when INTERF_SEL[1:0]
	 * is 01b, and the link between PHY and its link partner can be
	 * either 1000Base-X or 100Base-FX.
	 * RGMII-1000Base-X is properly supported, but RGMII-100Base-FX
	 * support is still missing as of now.
	 */
	if ((val & BCM54XX_SHD_INTF_SEL_MASK) == BCM54XX_SHD_INTF_SEL_RGMII) {
		val = bcm_phy_read_shadow(phydev, BCM54616S_SHD_100FX_CTRL);
		if (val < 0)
			return val;

		/* Bit 0 of the SerDes 100-FX Control register, when set
		 * to 1, sets the MII/RGMII -> 100BASE-FX configuration.
		 * When this bit is set to 0, it sets the GMII/RGMII ->
		 * 1000BASE-X configuration.
		 */
		if (!(val & BCM54616S_100FX_MODE))
			priv->mode_1000bx_en = true;

		phydev->port = PORT_FIBRE;
	}

	return 0;
}

static int bcm54616s_config_aneg(struct phy_device *phydev)
{
	struct bcm54616s_phy_priv *priv = phydev->priv;
	int ret;

	/* Aneg firstly. */
	if (priv->mode_1000bx_en)
		ret = genphy_c37_config_aneg(phydev);
	else
		ret = genphy_config_aneg(phydev);

	/* Then we can set up the delay. */
	bcm54xx_config_clock_delay(phydev);

	return ret;
}

static int bcm54616s_read_status(struct phy_device *phydev)
{
	struct bcm54616s_phy_priv *priv = phydev->priv;
	int err;

	if (priv->mode_1000bx_en)
		err = genphy_c37_read_status(phydev);
	else
		err = genphy_read_status(phydev);

	return err;
}

static int brcm_phy_setbits(struct phy_device *phydev, int reg, int set)
{
	int val;

	val = phy_read(phydev, reg);
	if (val < 0)
		return val;

	return phy_write(phydev, reg, val | set);
}

static int brcm_fet_config_init(struct phy_device *phydev)
{
	int reg, err, err2, brcmtest;

	/* Reset the PHY to bring it to a known state. */
	err = phy_write(phydev, MII_BMCR, BMCR_RESET);
	if (err < 0)
		return err;

	reg = phy_read(phydev, MII_BRCM_FET_INTREG);
	if (reg < 0)
		return reg;

	/* Unmask events we are interested in and mask interrupts globally. */
	reg = MII_BRCM_FET_IR_DUPLEX_EN |
	      MII_BRCM_FET_IR_SPEED_EN |
	      MII_BRCM_FET_IR_LINK_EN |
	      MII_BRCM_FET_IR_ENABLE |
	      MII_BRCM_FET_IR_MASK;

	err = phy_write(phydev, MII_BRCM_FET_INTREG, reg);
	if (err < 0)
		return err;

	/* Enable shadow register access */
	brcmtest = phy_read(phydev, MII_BRCM_FET_BRCMTEST);
	if (brcmtest < 0)
		return brcmtest;

	reg = brcmtest | MII_BRCM_FET_BT_SRE;

	err = phy_write(phydev, MII_BRCM_FET_BRCMTEST, reg);
	if (err < 0)
		return err;

	/* Set the LED mode */
	reg = phy_read(phydev, MII_BRCM_FET_SHDW_AUXMODE4);
	if (reg < 0) {
		err = reg;
		goto done;
	}

	reg &= ~MII_BRCM_FET_SHDW_AM4_LED_MASK;
	reg |= MII_BRCM_FET_SHDW_AM4_LED_MODE1;

	err = phy_write(phydev, MII_BRCM_FET_SHDW_AUXMODE4, reg);
	if (err < 0)
		goto done;

	/* Enable auto MDIX */
	err = brcm_phy_setbits(phydev, MII_BRCM_FET_SHDW_MISCCTRL,
				       MII_BRCM_FET_SHDW_MC_FAME);
	if (err < 0)
		goto done;

	if (phydev->dev_flags & PHY_BRCM_AUTO_PWRDWN_ENABLE) {
		/* Enable auto power down */
		err = brcm_phy_setbits(phydev, MII_BRCM_FET_SHDW_AUXSTAT2,
					       MII_BRCM_FET_SHDW_AS2_APDE);
	}

done:
	/* Disable shadow register access */
	err2 = phy_write(phydev, MII_BRCM_FET_BRCMTEST, brcmtest);
	if (!err)
		err = err2;

	return err;
}

static int brcm_fet_ack_interrupt(struct phy_device *phydev)
{
	int reg;

	/* Clear pending interrupts.  */
	reg = phy_read(phydev, MII_BRCM_FET_INTREG);
	if (reg < 0)
		return reg;

	return 0;
}

static int brcm_fet_config_intr(struct phy_device *phydev)
{
	int reg, err;

	reg = phy_read(phydev, MII_BRCM_FET_INTREG);
	if (reg < 0)
		return reg;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = brcm_fet_ack_interrupt(phydev);
		if (err)
			return err;

		reg &= ~MII_BRCM_FET_IR_MASK;
		err = phy_write(phydev, MII_BRCM_FET_INTREG, reg);
	} else {
		reg |= MII_BRCM_FET_IR_MASK;
		err = phy_write(phydev, MII_BRCM_FET_INTREG, reg);
		if (err)
			return err;

		err = brcm_fet_ack_interrupt(phydev);
	}

	return err;
}

static irqreturn_t brcm_fet_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read(phydev, MII_BRCM_FET_INTREG);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (irq_status == 0)
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

struct bcm53xx_phy_priv {
	u64	*stats;
};

static int bcm53xx_phy_probe(struct phy_device *phydev)
{
	struct bcm53xx_phy_priv *priv;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	priv->stats = devm_kcalloc(&phydev->mdio.dev,
				   bcm_phy_get_sset_count(phydev), sizeof(u64),
				   GFP_KERNEL);
	if (!priv->stats)
		return -ENOMEM;

	return 0;
}

static void bcm53xx_phy_get_stats(struct phy_device *phydev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct bcm53xx_phy_priv *priv = phydev->priv;

	bcm_phy_get_stats(phydev, priv->stats, stats, data);
}

static struct phy_driver broadcom_drivers[] = {
{
	.phy_id		= PHY_ID_BCM5411,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5411",
	/* PHY_GBIT_FEATURES */
	.config_init	= bcm54xx_config_init,
	.config_intr	= bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
}, {
	.phy_id		= PHY_ID_BCM5421,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5421",
	/* PHY_GBIT_FEATURES */
	.config_init	= bcm54xx_config_init,
	.config_intr	= bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
}, {
	.phy_id		= PHY_ID_BCM54210E,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM54210E",
	/* PHY_GBIT_FEATURES */
	.config_init	= bcm54xx_config_init,
	.config_intr	= bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
}, {
	.phy_id		= PHY_ID_BCM5461,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5461",
	/* PHY_GBIT_FEATURES */
	.config_init	= bcm54xx_config_init,
	.config_intr	= bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
}, {
	.phy_id		= PHY_ID_BCM54612E,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM54612E",
	/* PHY_GBIT_FEATURES */
	.config_init	= bcm54xx_config_init,
	.config_intr	= bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
}, {
	.phy_id		= PHY_ID_BCM54616S,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM54616S",
	/* PHY_GBIT_FEATURES */
	.config_init	= bcm54xx_config_init,
	.config_aneg	= bcm54616s_config_aneg,
	.config_intr	= bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
	.read_status	= bcm54616s_read_status,
	.probe		= bcm54616s_probe,
}, {
	.phy_id		= PHY_ID_BCM5464,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5464",
	/* PHY_GBIT_FEATURES */
	.config_init	= bcm54xx_config_init,
	.config_intr	= bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= PHY_ID_BCM5481,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5481",
	/* PHY_GBIT_FEATURES */
	.config_init	= bcm54xx_config_init,
	.config_aneg	= bcm5481_config_aneg,
	.config_intr	= bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
}, {
	.phy_id         = PHY_ID_BCM54810,
	.phy_id_mask    = 0xfffffff0,
	.name           = "Broadcom BCM54810",
	/* PHY_GBIT_FEATURES */
	.config_init    = bcm54xx_config_init,
	.config_aneg    = bcm5481_config_aneg,
	.config_intr    = bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
	.suspend	= genphy_suspend,
	.resume		= bcm54xx_resume,
}, {
	.phy_id         = PHY_ID_BCM54811,
	.phy_id_mask    = 0xfffffff0,
	.name           = "Broadcom BCM54811",
	/* PHY_GBIT_FEATURES */
	.config_init    = bcm54811_config_init,
	.config_aneg    = bcm5481_config_aneg,
	.config_intr    = bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
	.suspend	= genphy_suspend,
	.resume		= bcm54xx_resume,
}, {
	.phy_id		= PHY_ID_BCM5482,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5482",
	/* PHY_GBIT_FEATURES */
	.config_init	= bcm54xx_config_init,
	.config_intr	= bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
}, {
	.phy_id		= PHY_ID_BCM50610,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM50610",
	/* PHY_GBIT_FEATURES */
	.config_init	= bcm54xx_config_init,
	.config_intr	= bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
}, {
	.phy_id		= PHY_ID_BCM50610M,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM50610M",
	/* PHY_GBIT_FEATURES */
	.config_init	= bcm54xx_config_init,
	.config_intr	= bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
}, {
	.phy_id		= PHY_ID_BCM57780,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM57780",
	/* PHY_GBIT_FEATURES */
	.config_init	= bcm54xx_config_init,
	.config_intr	= bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
}, {
	.phy_id		= PHY_ID_BCMAC131,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCMAC131",
	/* PHY_BASIC_FEATURES */
	.config_init	= brcm_fet_config_init,
	.config_intr	= brcm_fet_config_intr,
	.handle_interrupt = brcm_fet_handle_interrupt,
}, {
	.phy_id		= PHY_ID_BCM5241,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5241",
	/* PHY_BASIC_FEATURES */
	.config_init	= brcm_fet_config_init,
	.config_intr	= brcm_fet_config_intr,
	.handle_interrupt = brcm_fet_handle_interrupt,
}, {
	.phy_id		= PHY_ID_BCM5395,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5395",
	.flags		= PHY_IS_INTERNAL,
	/* PHY_GBIT_FEATURES */
	.get_sset_count	= bcm_phy_get_sset_count,
	.get_strings	= bcm_phy_get_strings,
	.get_stats	= bcm53xx_phy_get_stats,
	.probe		= bcm53xx_phy_probe,
}, {
	.phy_id		= PHY_ID_BCM53125,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM53125",
	.flags		= PHY_IS_INTERNAL,
	/* PHY_GBIT_FEATURES */
	.get_sset_count	= bcm_phy_get_sset_count,
	.get_strings	= bcm_phy_get_strings,
	.get_stats	= bcm53xx_phy_get_stats,
	.probe		= bcm53xx_phy_probe,
	.config_init	= bcm54xx_config_init,
	.config_intr	= bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
}, {
	.phy_id         = PHY_ID_BCM89610,
	.phy_id_mask    = 0xfffffff0,
	.name           = "Broadcom BCM89610",
	/* PHY_GBIT_FEATURES */
	.config_init    = bcm54xx_config_init,
	.config_intr    = bcm_phy_config_intr,
	.handle_interrupt = bcm_phy_handle_interrupt,
} };

module_phy_driver(broadcom_drivers);

static struct mdio_device_id __maybe_unused broadcom_tbl[] = {
	{ PHY_ID_BCM5411, 0xfffffff0 },
	{ PHY_ID_BCM5421, 0xfffffff0 },
	{ PHY_ID_BCM54210E, 0xfffffff0 },
	{ PHY_ID_BCM5461, 0xfffffff0 },
	{ PHY_ID_BCM54612E, 0xfffffff0 },
	{ PHY_ID_BCM54616S, 0xfffffff0 },
	{ PHY_ID_BCM5464, 0xfffffff0 },
	{ PHY_ID_BCM5481, 0xfffffff0 },
	{ PHY_ID_BCM54810, 0xfffffff0 },
	{ PHY_ID_BCM54811, 0xfffffff0 },
	{ PHY_ID_BCM5482, 0xfffffff0 },
	{ PHY_ID_BCM50610, 0xfffffff0 },
	{ PHY_ID_BCM50610M, 0xfffffff0 },
	{ PHY_ID_BCM57780, 0xfffffff0 },
	{ PHY_ID_BCMAC131, 0xfffffff0 },
	{ PHY_ID_BCM5241, 0xfffffff0 },
	{ PHY_ID_BCM5395, 0xfffffff0 },
	{ PHY_ID_BCM53125, 0xfffffff0 },
	{ PHY_ID_BCM89610, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, broadcom_tbl);
