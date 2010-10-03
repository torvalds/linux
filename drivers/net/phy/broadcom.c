/*
 *	drivers/net/phy/broadcom.c
 *
 *	Broadcom BCM5411, BCM5421 and BCM5461 Gigabit Ethernet
 *	transceivers.
 *
 *	Copyright (c) 2006  Maciej W. Rozycki
 *
 *	Inspired by code written by Amy Fong.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/phy.h>
#include <linux/brcmphy.h>


#define BRCM_PHY_MODEL(phydev) \
	((phydev)->drv->phy_id & (phydev)->drv->phy_id_mask)

#define BRCM_PHY_REV(phydev) \
	((phydev)->drv->phy_id & ~((phydev)->drv->phy_id_mask))


#define MII_BCM54XX_ECR		0x10	/* BCM54xx extended control register */
#define MII_BCM54XX_ECR_IM	0x1000	/* Interrupt mask */
#define MII_BCM54XX_ECR_IF	0x0800	/* Interrupt force */

#define MII_BCM54XX_ESR		0x11	/* BCM54xx extended status register */
#define MII_BCM54XX_ESR_IS	0x1000	/* Interrupt status */

#define MII_BCM54XX_EXP_DATA	0x15	/* Expansion register data */
#define MII_BCM54XX_EXP_SEL	0x17	/* Expansion register select */
#define MII_BCM54XX_EXP_SEL_SSD	0x0e00	/* Secondary SerDes select */
#define MII_BCM54XX_EXP_SEL_ER	0x0f00	/* Expansion register select */

#define MII_BCM54XX_AUX_CTL	0x18	/* Auxiliary control register */
#define MII_BCM54XX_ISR		0x1a	/* BCM54xx interrupt status register */
#define MII_BCM54XX_IMR		0x1b	/* BCM54xx interrupt mask register */
#define MII_BCM54XX_INT_CRCERR	0x0001	/* CRC error */
#define MII_BCM54XX_INT_LINK	0x0002	/* Link status changed */
#define MII_BCM54XX_INT_SPEED	0x0004	/* Link speed change */
#define MII_BCM54XX_INT_DUPLEX	0x0008	/* Duplex mode changed */
#define MII_BCM54XX_INT_LRS	0x0010	/* Local receiver status changed */
#define MII_BCM54XX_INT_RRS	0x0020	/* Remote receiver status changed */
#define MII_BCM54XX_INT_SSERR	0x0040	/* Scrambler synchronization error */
#define MII_BCM54XX_INT_UHCD	0x0080	/* Unsupported HCD negotiated */
#define MII_BCM54XX_INT_NHCD	0x0100	/* No HCD */
#define MII_BCM54XX_INT_NHCDL	0x0200	/* No HCD link */
#define MII_BCM54XX_INT_ANPR	0x0400	/* Auto-negotiation page received */
#define MII_BCM54XX_INT_LC	0x0800	/* All counters below 128 */
#define MII_BCM54XX_INT_HC	0x1000	/* Counter above 32768 */
#define MII_BCM54XX_INT_MDIX	0x2000	/* MDIX status change */
#define MII_BCM54XX_INT_PSERR	0x4000	/* Pair swap error */

#define MII_BCM54XX_SHD		0x1c	/* 0x1c shadow registers */
#define MII_BCM54XX_SHD_WRITE	0x8000
#define MII_BCM54XX_SHD_VAL(x)	((x & 0x1f) << 10)
#define MII_BCM54XX_SHD_DATA(x)	((x & 0x3ff) << 0)

/*
 * AUXILIARY CONTROL SHADOW ACCESS REGISTERS.  (PHY REG 0x18)
 */
#define MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL	0x0000
#define MII_BCM54XX_AUXCTL_ACTL_TX_6DB		0x0400
#define MII_BCM54XX_AUXCTL_ACTL_SMDSP_ENA	0x0800

#define MII_BCM54XX_AUXCTL_MISC_WREN	0x8000
#define MII_BCM54XX_AUXCTL_MISC_FORCE_AMDIX	0x0200
#define MII_BCM54XX_AUXCTL_MISC_RDSEL_MISC	0x7000
#define MII_BCM54XX_AUXCTL_SHDWSEL_MISC	0x0007

#define MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL	0x0000


/*
 * Broadcom LED source encodings.  These are used in BCM5461, BCM5481,
 * BCM5482, and possibly some others.
 */
#define BCM_LED_SRC_LINKSPD1	0x0
#define BCM_LED_SRC_LINKSPD2	0x1
#define BCM_LED_SRC_XMITLED	0x2
#define BCM_LED_SRC_ACTIVITYLED	0x3
#define BCM_LED_SRC_FDXLED	0x4
#define BCM_LED_SRC_SLAVE	0x5
#define BCM_LED_SRC_INTR	0x6
#define BCM_LED_SRC_QUALITY	0x7
#define BCM_LED_SRC_RCVLED	0x8
#define BCM_LED_SRC_MULTICOLOR1	0xa
#define BCM_LED_SRC_OPENSHORT	0xb
#define BCM_LED_SRC_OFF		0xe	/* Tied high */
#define BCM_LED_SRC_ON		0xf	/* Tied low */


/*
 * BCM5482: Shadow registers
 * Shadow values go into bits [14:10] of register 0x1c to select a shadow
 * register to access.
 */
/* 00101: Spare Control Register 3 */
#define BCM54XX_SHD_SCR3		0x05
#define  BCM54XX_SHD_SCR3_DEF_CLK125	0x0001
#define  BCM54XX_SHD_SCR3_DLLAPD_DIS	0x0002
#define  BCM54XX_SHD_SCR3_TRDDAPD	0x0004

/* 01010: Auto Power-Down */
#define BCM54XX_SHD_APD			0x0a
#define  BCM54XX_SHD_APD_EN		0x0020

#define BCM5482_SHD_LEDS1	0x0d	/* 01101: LED Selector 1 */
					/* LED3 / ~LINKSPD[2] selector */
#define BCM5482_SHD_LEDS1_LED3(src)	((src & 0xf) << 4)
					/* LED1 / ~LINKSPD[1] selector */
#define BCM5482_SHD_LEDS1_LED1(src)	((src & 0xf) << 0)
#define BCM54XX_SHD_RGMII_MODE	0x0b	/* 01011: RGMII Mode Selector */
#define BCM5482_SHD_SSD		0x14	/* 10100: Secondary SerDes control */
#define BCM5482_SHD_SSD_LEDM	0x0008	/* SSD LED Mode enable */
#define BCM5482_SHD_SSD_EN	0x0001	/* SSD enable */
#define BCM5482_SHD_MODE	0x1f	/* 11111: Mode Control Register */
#define BCM5482_SHD_MODE_1000BX	0x0001	/* Enable 1000BASE-X registers */


/*
 * EXPANSION SHADOW ACCESS REGISTERS.  (PHY REG 0x15, 0x16, and 0x17)
 */
#define MII_BCM54XX_EXP_AADJ1CH0		0x001f
#define  MII_BCM54XX_EXP_AADJ1CH0_SWP_ABCD_OEN	0x0200
#define  MII_BCM54XX_EXP_AADJ1CH0_SWSEL_THPF	0x0100
#define MII_BCM54XX_EXP_AADJ1CH3		0x601f
#define  MII_BCM54XX_EXP_AADJ1CH3_ADCCKADJ	0x0002
#define MII_BCM54XX_EXP_EXP08			0x0F08
#define  MII_BCM54XX_EXP_EXP08_RJCT_2MHZ	0x0001
#define  MII_BCM54XX_EXP_EXP08_EARLY_DAC_WAKE	0x0200
#define MII_BCM54XX_EXP_EXP75			0x0f75
#define  MII_BCM54XX_EXP_EXP75_VDACCTRL		0x003c
#define  MII_BCM54XX_EXP_EXP75_CM_OSC		0x0001
#define MII_BCM54XX_EXP_EXP96			0x0f96
#define  MII_BCM54XX_EXP_EXP96_MYST		0x0010
#define MII_BCM54XX_EXP_EXP97			0x0f97
#define  MII_BCM54XX_EXP_EXP97_MYST		0x0c0c

/*
 * BCM5482: Secondary SerDes registers
 */
#define BCM5482_SSD_1000BX_CTL		0x00	/* 1000BASE-X Control */
#define BCM5482_SSD_1000BX_CTL_PWRDOWN	0x0800	/* Power-down SSD */
#define BCM5482_SSD_SGMII_SLAVE		0x15	/* SGMII Slave Register */
#define BCM5482_SSD_SGMII_SLAVE_EN	0x0002	/* Slave mode enable */
#define BCM5482_SSD_SGMII_SLAVE_AD	0x0001	/* Slave auto-detection */


/*****************************************************************************/
/* Fast Ethernet Transceiver definitions. */
/*****************************************************************************/

#define MII_BRCM_FET_INTREG		0x1a	/* Interrupt register */
#define MII_BRCM_FET_IR_MASK		0x0100	/* Mask all interrupts */
#define MII_BRCM_FET_IR_LINK_EN		0x0200	/* Link status change enable */
#define MII_BRCM_FET_IR_SPEED_EN	0x0400	/* Link speed change enable */
#define MII_BRCM_FET_IR_DUPLEX_EN	0x0800	/* Duplex mode change enable */
#define MII_BRCM_FET_IR_ENABLE		0x4000	/* Interrupt enable */

#define MII_BRCM_FET_BRCMTEST		0x1f	/* Brcm test register */
#define MII_BRCM_FET_BT_SRE		0x0080	/* Shadow register enable */


/*** Shadow register definitions ***/

#define MII_BRCM_FET_SHDW_MISCCTRL	0x10	/* Shadow misc ctrl */
#define MII_BRCM_FET_SHDW_MC_FAME	0x4000	/* Force Auto MDIX enable */

#define MII_BRCM_FET_SHDW_AUXMODE4	0x1a	/* Auxiliary mode 4 */
#define MII_BRCM_FET_SHDW_AM4_LED_MASK	0x0003
#define MII_BRCM_FET_SHDW_AM4_LED_MODE1 0x0001

#define MII_BRCM_FET_SHDW_AUXSTAT2	0x1b	/* Auxiliary status 2 */
#define MII_BRCM_FET_SHDW_AS2_APDE	0x0020	/* Auto power down enable */


MODULE_DESCRIPTION("Broadcom PHY driver");
MODULE_AUTHOR("Maciej W. Rozycki");
MODULE_LICENSE("GPL");

/*
 * Indirect register access functions for the 1000BASE-T/100BASE-TX/10BASE-T
 * 0x1c shadow registers.
 */
static int bcm54xx_shadow_read(struct phy_device *phydev, u16 shadow)
{
	phy_write(phydev, MII_BCM54XX_SHD, MII_BCM54XX_SHD_VAL(shadow));
	return MII_BCM54XX_SHD_DATA(phy_read(phydev, MII_BCM54XX_SHD));
}

static int bcm54xx_shadow_write(struct phy_device *phydev, u16 shadow, u16 val)
{
	return phy_write(phydev, MII_BCM54XX_SHD,
			 MII_BCM54XX_SHD_WRITE |
			 MII_BCM54XX_SHD_VAL(shadow) |
			 MII_BCM54XX_SHD_DATA(val));
}

/* Indirect register access functions for the Expansion Registers */
static int bcm54xx_exp_read(struct phy_device *phydev, u16 regnum)
{
	int val;

	val = phy_write(phydev, MII_BCM54XX_EXP_SEL, regnum);
	if (val < 0)
		return val;

	val = phy_read(phydev, MII_BCM54XX_EXP_DATA);

	/* Restore default value.  It's O.K. if this write fails. */
	phy_write(phydev, MII_BCM54XX_EXP_SEL, 0);

	return val;
}

static int bcm54xx_exp_write(struct phy_device *phydev, u16 regnum, u16 val)
{
	int ret;

	ret = phy_write(phydev, MII_BCM54XX_EXP_SEL, regnum);
	if (ret < 0)
		return ret;

	ret = phy_write(phydev, MII_BCM54XX_EXP_DATA, val);

	/* Restore default value.  It's O.K. if this write fails. */
	phy_write(phydev, MII_BCM54XX_EXP_SEL, 0);

	return ret;
}

static int bcm54xx_auxctl_write(struct phy_device *phydev, u16 regnum, u16 val)
{
	return phy_write(phydev, MII_BCM54XX_AUX_CTL, regnum | val);
}

/* Needs SMDSP clock enabled via bcm54xx_phydsp_config() */
static int bcm50610_a0_workaround(struct phy_device *phydev)
{
	int err;

	err = bcm54xx_exp_write(phydev, MII_BCM54XX_EXP_AADJ1CH0,
				MII_BCM54XX_EXP_AADJ1CH0_SWP_ABCD_OEN |
				MII_BCM54XX_EXP_AADJ1CH0_SWSEL_THPF);
	if (err < 0)
		return err;

	err = bcm54xx_exp_write(phydev, MII_BCM54XX_EXP_AADJ1CH3,
					MII_BCM54XX_EXP_AADJ1CH3_ADCCKADJ);
	if (err < 0)
		return err;

	err = bcm54xx_exp_write(phydev, MII_BCM54XX_EXP_EXP75,
				MII_BCM54XX_EXP_EXP75_VDACCTRL);
	if (err < 0)
		return err;

	err = bcm54xx_exp_write(phydev, MII_BCM54XX_EXP_EXP96,
				MII_BCM54XX_EXP_EXP96_MYST);
	if (err < 0)
		return err;

	err = bcm54xx_exp_write(phydev, MII_BCM54XX_EXP_EXP97,
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
		err = bcm54xx_exp_write(phydev, MII_BCM54XX_EXP_EXP08,
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

		val = bcm54xx_exp_read(phydev, MII_BCM54XX_EXP_EXP75);
		if (val < 0)
			goto error;

		val |= MII_BCM54XX_EXP_EXP75_CM_OSC;
		err = bcm54xx_exp_write(phydev, MII_BCM54XX_EXP_EXP75, val);
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
	    BRCM_PHY_MODEL(phydev) != PHY_ID_BCM50610M)
		return;

	val = bcm54xx_shadow_read(phydev, BCM54XX_SHD_SCR3);
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
			/* Here, bit 0 _enables_ CLK125 when set */
			val &= ~BCM54XX_SHD_SCR3_DEF_CLK125;
			clk125en = false;
		}
	}

	if (clk125en == false ||
	    (phydev->dev_flags & PHY_BRCM_AUTO_PWRDWN_ENABLE))
		val &= ~BCM54XX_SHD_SCR3_DLLAPD_DIS;
	else
		val |= BCM54XX_SHD_SCR3_DLLAPD_DIS;

	if (phydev->dev_flags & PHY_BRCM_DIS_TXCRXC_NOENRGY)
		val |= BCM54XX_SHD_SCR3_TRDDAPD;

	if (orig != val)
		bcm54xx_shadow_write(phydev, BCM54XX_SHD_SCR3, val);

	val = bcm54xx_shadow_read(phydev, BCM54XX_SHD_APD);
	if (val < 0)
		return;

	orig = val;

	if (clk125en == false ||
	    (phydev->dev_flags & PHY_BRCM_AUTO_PWRDWN_ENABLE))
		val |= BCM54XX_SHD_APD_EN;
	else
		val &= ~BCM54XX_SHD_APD_EN;

	if (orig != val)
		bcm54xx_shadow_write(phydev, BCM54XX_SHD_APD, val);
}

static int bcm54xx_config_init(struct phy_device *phydev)
{
	int reg, err;

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
		bcm54xx_shadow_write(phydev, BCM54XX_SHD_RGMII_MODE, 0);

	if ((phydev->dev_flags & PHY_BRCM_RX_REFCLK_UNUSED) ||
	    (phydev->dev_flags & PHY_BRCM_DIS_TXCRXC_NOENRGY) ||
	    (phydev->dev_flags & PHY_BRCM_AUTO_PWRDWN_ENABLE))
		bcm54xx_adjust_rxrefclk(phydev);

	bcm54xx_phydsp_config(phydev);

	return 0;
}

static int bcm5482_config_init(struct phy_device *phydev)
{
	int err, reg;

	err = bcm54xx_config_init(phydev);

	if (phydev->dev_flags & PHY_BCM_FLAGS_MODE_1000BX) {
		/*
		 * Enable secondary SerDes and its use as an LED source
		 */
		reg = bcm54xx_shadow_read(phydev, BCM5482_SHD_SSD);
		bcm54xx_shadow_write(phydev, BCM5482_SHD_SSD,
				     reg |
				     BCM5482_SHD_SSD_LEDM |
				     BCM5482_SHD_SSD_EN);

		/*
		 * Enable SGMII slave mode and auto-detection
		 */
		reg = BCM5482_SSD_SGMII_SLAVE | MII_BCM54XX_EXP_SEL_SSD;
		err = bcm54xx_exp_read(phydev, reg);
		if (err < 0)
			return err;
		err = bcm54xx_exp_write(phydev, reg, err |
					BCM5482_SSD_SGMII_SLAVE_EN |
					BCM5482_SSD_SGMII_SLAVE_AD);
		if (err < 0)
			return err;

		/*
		 * Disable secondary SerDes powerdown
		 */
		reg = BCM5482_SSD_1000BX_CTL | MII_BCM54XX_EXP_SEL_SSD;
		err = bcm54xx_exp_read(phydev, reg);
		if (err < 0)
			return err;
		err = bcm54xx_exp_write(phydev, reg,
					err & ~BCM5482_SSD_1000BX_CTL_PWRDOWN);
		if (err < 0)
			return err;

		/*
		 * Select 1000BASE-X register set (primary SerDes)
		 */
		reg = bcm54xx_shadow_read(phydev, BCM5482_SHD_MODE);
		bcm54xx_shadow_write(phydev, BCM5482_SHD_MODE,
				     reg | BCM5482_SHD_MODE_1000BX);

		/*
		 * LED1=ACTIVITYLED, LED3=LINKSPD[2]
		 * (Use LED1 as secondary SerDes ACTIVITY LED)
		 */
		bcm54xx_shadow_write(phydev, BCM5482_SHD_LEDS1,
			BCM5482_SHD_LEDS1_LED1(BCM_LED_SRC_ACTIVITYLED) |
			BCM5482_SHD_LEDS1_LED3(BCM_LED_SRC_LINKSPD2));

		/*
		 * Auto-negotiation doesn't seem to work quite right
		 * in this mode, so we disable it and force it to the
		 * right speed/duplex setting.  Only 'link status'
		 * is important.
		 */
		phydev->autoneg = AUTONEG_DISABLE;
		phydev->speed = SPEED_1000;
		phydev->duplex = DUPLEX_FULL;
	}

	return err;
}

static int bcm5482_read_status(struct phy_device *phydev)
{
	int err;

	err = genphy_read_status(phydev);

	if (phydev->dev_flags & PHY_BCM_FLAGS_MODE_1000BX) {
		/*
		 * Only link status matters for 1000Base-X mode, so force
		 * 1000 Mbit/s full-duplex status
		 */
		if (phydev->link) {
			phydev->speed = SPEED_1000;
			phydev->duplex = DUPLEX_FULL;
		}
	}

	return err;
}

static int bcm54xx_ack_interrupt(struct phy_device *phydev)
{
	int reg;

	/* Clear pending interrupts.  */
	reg = phy_read(phydev, MII_BCM54XX_ISR);
	if (reg < 0)
		return reg;

	return 0;
}

static int bcm54xx_config_intr(struct phy_device *phydev)
{
	int reg, err;

	reg = phy_read(phydev, MII_BCM54XX_ECR);
	if (reg < 0)
		return reg;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		reg &= ~MII_BCM54XX_ECR_IM;
	else
		reg |= MII_BCM54XX_ECR_IM;

	err = phy_write(phydev, MII_BCM54XX_ECR, reg);
	return err;
}

static int bcm5481_config_aneg(struct phy_device *phydev)
{
	int ret;

	/* Aneg firsly. */
	ret = genphy_config_aneg(phydev);

	/* Then we can set up the delay. */
	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) {
		u16 reg;

		/*
		 * There is no BCM5481 specification available, so down
		 * here is everything we know about "register 0x18". This
		 * at least helps BCM5481 to successfuly receive packets
		 * on MPC8360E-RDK board. Peter Barada <peterb@logicpd.com>
		 * says: "This sets delay between the RXD and RXC signals
		 * instead of using trace lengths to achieve timing".
		 */

		/* Set RDX clk delay. */
		reg = 0x7 | (0x7 << 12);
		phy_write(phydev, 0x18, reg);

		reg = phy_read(phydev, 0x18);
		/* Set RDX-RXC skew. */
		reg |= (1 << 8);
		/* Write bits 14:0. */
		reg |= (1 << 15);
		phy_write(phydev, 0x18, reg);
	}

	return ret;
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

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		reg &= ~MII_BRCM_FET_IR_MASK;
	else
		reg |= MII_BRCM_FET_IR_MASK;

	err = phy_write(phydev, MII_BRCM_FET_INTREG, reg);
	return err;
}

static struct phy_driver bcm5411_driver = {
	.phy_id		= PHY_ID_BCM5411,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5411",
	.features	= PHY_GBIT_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= bcm54xx_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= bcm54xx_ack_interrupt,
	.config_intr	= bcm54xx_config_intr,
	.driver		= { .owner = THIS_MODULE },
};

static struct phy_driver bcm5421_driver = {
	.phy_id		= PHY_ID_BCM5421,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5421",
	.features	= PHY_GBIT_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= bcm54xx_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= bcm54xx_ack_interrupt,
	.config_intr	= bcm54xx_config_intr,
	.driver		= { .owner = THIS_MODULE },
};

static struct phy_driver bcm5461_driver = {
	.phy_id		= PHY_ID_BCM5461,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5461",
	.features	= PHY_GBIT_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= bcm54xx_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= bcm54xx_ack_interrupt,
	.config_intr	= bcm54xx_config_intr,
	.driver		= { .owner = THIS_MODULE },
};

static struct phy_driver bcm5464_driver = {
	.phy_id		= PHY_ID_BCM5464,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5464",
	.features	= PHY_GBIT_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= bcm54xx_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= bcm54xx_ack_interrupt,
	.config_intr	= bcm54xx_config_intr,
	.driver		= { .owner = THIS_MODULE },
};

static struct phy_driver bcm5481_driver = {
	.phy_id		= PHY_ID_BCM5481,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5481",
	.features	= PHY_GBIT_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= bcm54xx_config_init,
	.config_aneg	= bcm5481_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= bcm54xx_ack_interrupt,
	.config_intr	= bcm54xx_config_intr,
	.driver		= { .owner = THIS_MODULE },
};

static struct phy_driver bcm5482_driver = {
	.phy_id		= PHY_ID_BCM5482,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5482",
	.features	= PHY_GBIT_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= bcm5482_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= bcm5482_read_status,
	.ack_interrupt	= bcm54xx_ack_interrupt,
	.config_intr	= bcm54xx_config_intr,
	.driver		= { .owner = THIS_MODULE },
};

static struct phy_driver bcm50610_driver = {
	.phy_id		= PHY_ID_BCM50610,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM50610",
	.features	= PHY_GBIT_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= bcm54xx_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= bcm54xx_ack_interrupt,
	.config_intr	= bcm54xx_config_intr,
	.driver		= { .owner = THIS_MODULE },
};

static struct phy_driver bcm50610m_driver = {
	.phy_id		= PHY_ID_BCM50610M,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM50610M",
	.features	= PHY_GBIT_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= bcm54xx_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= bcm54xx_ack_interrupt,
	.config_intr	= bcm54xx_config_intr,
	.driver		= { .owner = THIS_MODULE },
};

static struct phy_driver bcm57780_driver = {
	.phy_id		= PHY_ID_BCM57780,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM57780",
	.features	= PHY_GBIT_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= bcm54xx_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= bcm54xx_ack_interrupt,
	.config_intr	= bcm54xx_config_intr,
	.driver		= { .owner = THIS_MODULE },
};

static struct phy_driver bcmac131_driver = {
	.phy_id		= PHY_ID_BCMAC131,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCMAC131",
	.features	= PHY_BASIC_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= brcm_fet_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= brcm_fet_ack_interrupt,
	.config_intr	= brcm_fet_config_intr,
	.driver		= { .owner = THIS_MODULE },
};

static struct phy_driver bcm5241_driver = {
	.phy_id		= PHY_ID_BCM5241,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Broadcom BCM5241",
	.features	= PHY_BASIC_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= brcm_fet_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= brcm_fet_ack_interrupt,
	.config_intr	= brcm_fet_config_intr,
	.driver		= { .owner = THIS_MODULE },
};

static int __init broadcom_init(void)
{
	int ret;

	ret = phy_driver_register(&bcm5411_driver);
	if (ret)
		goto out_5411;
	ret = phy_driver_register(&bcm5421_driver);
	if (ret)
		goto out_5421;
	ret = phy_driver_register(&bcm5461_driver);
	if (ret)
		goto out_5461;
	ret = phy_driver_register(&bcm5464_driver);
	if (ret)
		goto out_5464;
	ret = phy_driver_register(&bcm5481_driver);
	if (ret)
		goto out_5481;
	ret = phy_driver_register(&bcm5482_driver);
	if (ret)
		goto out_5482;
	ret = phy_driver_register(&bcm50610_driver);
	if (ret)
		goto out_50610;
	ret = phy_driver_register(&bcm50610m_driver);
	if (ret)
		goto out_50610m;
	ret = phy_driver_register(&bcm57780_driver);
	if (ret)
		goto out_57780;
	ret = phy_driver_register(&bcmac131_driver);
	if (ret)
		goto out_ac131;
	ret = phy_driver_register(&bcm5241_driver);
	if (ret)
		goto out_5241;
	return ret;

out_5241:
	phy_driver_unregister(&bcmac131_driver);
out_ac131:
	phy_driver_unregister(&bcm57780_driver);
out_57780:
	phy_driver_unregister(&bcm50610m_driver);
out_50610m:
	phy_driver_unregister(&bcm50610_driver);
out_50610:
	phy_driver_unregister(&bcm5482_driver);
out_5482:
	phy_driver_unregister(&bcm5481_driver);
out_5481:
	phy_driver_unregister(&bcm5464_driver);
out_5464:
	phy_driver_unregister(&bcm5461_driver);
out_5461:
	phy_driver_unregister(&bcm5421_driver);
out_5421:
	phy_driver_unregister(&bcm5411_driver);
out_5411:
	return ret;
}

static void __exit broadcom_exit(void)
{
	phy_driver_unregister(&bcm5241_driver);
	phy_driver_unregister(&bcmac131_driver);
	phy_driver_unregister(&bcm57780_driver);
	phy_driver_unregister(&bcm50610m_driver);
	phy_driver_unregister(&bcm50610_driver);
	phy_driver_unregister(&bcm5482_driver);
	phy_driver_unregister(&bcm5481_driver);
	phy_driver_unregister(&bcm5464_driver);
	phy_driver_unregister(&bcm5461_driver);
	phy_driver_unregister(&bcm5421_driver);
	phy_driver_unregister(&bcm5411_driver);
}

module_init(broadcom_init);
module_exit(broadcom_exit);

static struct mdio_device_id __maybe_unused broadcom_tbl[] = {
	{ PHY_ID_BCM5411, 0xfffffff0 },
	{ PHY_ID_BCM5421, 0xfffffff0 },
	{ PHY_ID_BCM5461, 0xfffffff0 },
	{ PHY_ID_BCM5464, 0xfffffff0 },
	{ PHY_ID_BCM5482, 0xfffffff0 },
	{ PHY_ID_BCM5482, 0xfffffff0 },
	{ PHY_ID_BCM50610, 0xfffffff0 },
	{ PHY_ID_BCM50610M, 0xfffffff0 },
	{ PHY_ID_BCM57780, 0xfffffff0 },
	{ PHY_ID_BCMAC131, 0xfffffff0 },
	{ PHY_ID_BCM5241, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, broadcom_tbl);
