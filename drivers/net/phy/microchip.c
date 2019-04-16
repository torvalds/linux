// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2015 Microchip Technology
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/microchipphy.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <dt-bindings/net/microchip-lan78xx.h>

#define DRIVER_AUTHOR	"WOOJUNG HUH <woojung.huh@microchip.com>"
#define DRIVER_DESC	"Microchip LAN88XX PHY driver"

struct lan88xx_priv {
	int	chip_id;
	int	chip_rev;
	__u32	wolopts;
};

static int lan88xx_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, LAN88XX_EXT_PAGE_ACCESS);
}

static int lan88xx_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, LAN88XX_EXT_PAGE_ACCESS, page);
}

static int lan88xx_phy_config_intr(struct phy_device *phydev)
{
	int rc;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		/* unmask all source and clear them before enable */
		rc = phy_write(phydev, LAN88XX_INT_MASK, 0x7FFF);
		rc = phy_read(phydev, LAN88XX_INT_STS);
		rc = phy_write(phydev, LAN88XX_INT_MASK,
			       LAN88XX_INT_MASK_MDINTPIN_EN_ |
			       LAN88XX_INT_MASK_LINK_CHANGE_);
	} else {
		rc = phy_write(phydev, LAN88XX_INT_MASK, 0);
	}

	return rc < 0 ? rc : 0;
}

static int lan88xx_phy_ack_interrupt(struct phy_device *phydev)
{
	int rc = phy_read(phydev, LAN88XX_INT_STS);

	return rc < 0 ? rc : 0;
}

static int lan88xx_suspend(struct phy_device *phydev)
{
	struct lan88xx_priv *priv = phydev->priv;

	/* do not power down PHY when WOL is enabled */
	if (!priv->wolopts)
		genphy_suspend(phydev);

	return 0;
}

static int lan88xx_TR_reg_set(struct phy_device *phydev, u16 regaddr,
			      u32 data)
{
	int val, save_page, ret = 0;
	u16 buf;

	/* Save current page */
	save_page = phy_save_page(phydev);
	if (save_page < 0) {
		phydev_warn(phydev, "Failed to get current page\n");
		goto err;
	}

	/* Switch to TR page */
	lan88xx_write_page(phydev, LAN88XX_EXT_PAGE_ACCESS_TR);

	ret = __phy_write(phydev, LAN88XX_EXT_PAGE_TR_LOW_DATA,
			  (data & 0xFFFF));
	if (ret < 0) {
		phydev_warn(phydev, "Failed to write TR low data\n");
		goto err;
	}

	ret = __phy_write(phydev, LAN88XX_EXT_PAGE_TR_HIGH_DATA,
			  (data & 0x00FF0000) >> 16);
	if (ret < 0) {
		phydev_warn(phydev, "Failed to write TR high data\n");
		goto err;
	}

	/* Config control bits [15:13] of register */
	buf = (regaddr & ~(0x3 << 13));/* Clr [14:13] to write data in reg */
	buf |= 0x8000; /* Set [15] to Packet transmit */

	ret = __phy_write(phydev, LAN88XX_EXT_PAGE_TR_CR, buf);
	if (ret < 0) {
		phydev_warn(phydev, "Failed to write data in reg\n");
		goto err;
	}

	usleep_range(1000, 2000);/* Wait for Data to be written */
	val = __phy_read(phydev, LAN88XX_EXT_PAGE_TR_CR);
	if (!(val & 0x8000))
		phydev_warn(phydev, "TR Register[0x%X] configuration failed\n",
			    regaddr);
err:
	return phy_restore_page(phydev, save_page, ret);
}

static void lan88xx_config_TR_regs(struct phy_device *phydev)
{
	int err;

	/* Get access to Channel 0x1, Node 0xF , Register 0x01.
	 * Write 24-bit value 0x12B00A to register. Setting MrvlTrFix1000Kf,
	 * MrvlTrFix1000Kp, MasterEnableTR bits.
	 */
	err = lan88xx_TR_reg_set(phydev, 0x0F82, 0x12B00A);
	if (err < 0)
		phydev_warn(phydev, "Failed to Set Register[0x0F82]\n");

	/* Get access to Channel b'10, Node b'1101, Register 0x06.
	 * Write 24-bit value 0xD2C46F to register. Setting SSTrKf1000Slv,
	 * SSTrKp1000Mas bits.
	 */
	err = lan88xx_TR_reg_set(phydev, 0x168C, 0xD2C46F);
	if (err < 0)
		phydev_warn(phydev, "Failed to Set Register[0x168C]\n");

	/* Get access to Channel b'10, Node b'1111, Register 0x11.
	 * Write 24-bit value 0x620 to register. Setting rem_upd_done_thresh
	 * bits
	 */
	err = lan88xx_TR_reg_set(phydev, 0x17A2, 0x620);
	if (err < 0)
		phydev_warn(phydev, "Failed to Set Register[0x17A2]\n");

	/* Get access to Channel b'10, Node b'1101, Register 0x10.
	 * Write 24-bit value 0xEEFFDD to register. Setting
	 * eee_TrKp1Long_1000, eee_TrKp2Long_1000, eee_TrKp3Long_1000,
	 * eee_TrKp1Short_1000,eee_TrKp2Short_1000, eee_TrKp3Short_1000 bits.
	 */
	err = lan88xx_TR_reg_set(phydev, 0x16A0, 0xEEFFDD);
	if (err < 0)
		phydev_warn(phydev, "Failed to Set Register[0x16A0]\n");

	/* Get access to Channel b'10, Node b'1101, Register 0x13.
	 * Write 24-bit value 0x071448 to register. Setting
	 * slv_lpi_tr_tmr_val1, slv_lpi_tr_tmr_val2 bits.
	 */
	err = lan88xx_TR_reg_set(phydev, 0x16A6, 0x071448);
	if (err < 0)
		phydev_warn(phydev, "Failed to Set Register[0x16A6]\n");

	/* Get access to Channel b'10, Node b'1101, Register 0x12.
	 * Write 24-bit value 0x13132F to register. Setting
	 * slv_sigdet_timer_val1, slv_sigdet_timer_val2 bits.
	 */
	err = lan88xx_TR_reg_set(phydev, 0x16A4, 0x13132F);
	if (err < 0)
		phydev_warn(phydev, "Failed to Set Register[0x16A4]\n");

	/* Get access to Channel b'10, Node b'1101, Register 0x14.
	 * Write 24-bit value 0x0 to register. Setting eee_3level_delay,
	 * eee_TrKf_freeze_delay bits.
	 */
	err = lan88xx_TR_reg_set(phydev, 0x16A8, 0x0);
	if (err < 0)
		phydev_warn(phydev, "Failed to Set Register[0x16A8]\n");

	/* Get access to Channel b'01, Node b'1111, Register 0x34.
	 * Write 24-bit value 0x91B06C to register. Setting
	 * FastMseSearchThreshLong1000, FastMseSearchThreshShort1000,
	 * FastMseSearchUpdGain1000 bits.
	 */
	err = lan88xx_TR_reg_set(phydev, 0x0FE8, 0x91B06C);
	if (err < 0)
		phydev_warn(phydev, "Failed to Set Register[0x0FE8]\n");

	/* Get access to Channel b'01, Node b'1111, Register 0x3E.
	 * Write 24-bit value 0xC0A028 to register. Setting
	 * FastMseKp2ThreshLong1000, FastMseKp2ThreshShort1000,
	 * FastMseKp2UpdGain1000, FastMseKp2ExitEn1000 bits.
	 */
	err = lan88xx_TR_reg_set(phydev, 0x0FFC, 0xC0A028);
	if (err < 0)
		phydev_warn(phydev, "Failed to Set Register[0x0FFC]\n");

	/* Get access to Channel b'01, Node b'1111, Register 0x35.
	 * Write 24-bit value 0x041600 to register. Setting
	 * FastMseSearchPhShNum1000, FastMseSearchClksPerPh1000,
	 * FastMsePhChangeDelay1000 bits.
	 */
	err = lan88xx_TR_reg_set(phydev, 0x0FEA, 0x041600);
	if (err < 0)
		phydev_warn(phydev, "Failed to Set Register[0x0FEA]\n");

	/* Get access to Channel b'10, Node b'1101, Register 0x03.
	 * Write 24-bit value 0x000004 to register. Setting TrFreeze bits.
	 */
	err = lan88xx_TR_reg_set(phydev, 0x1686, 0x000004);
	if (err < 0)
		phydev_warn(phydev, "Failed to Set Register[0x1686]\n");
}

static int lan88xx_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct lan88xx_priv *priv;
	u32 led_modes[4];
	int len;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->wolopts = 0;

	len = of_property_read_variable_u32_array(dev->of_node,
						  "microchip,led-modes",
						  led_modes,
						  0,
						  ARRAY_SIZE(led_modes));
	if (len >= 0) {
		u32 reg = 0;
		int i;

		for (i = 0; i < len; i++) {
			if (led_modes[i] > 15)
				return -EINVAL;
			reg |= led_modes[i] << (i * 4);
		}
		for (; i < ARRAY_SIZE(led_modes); i++)
			reg |= LAN78XX_FORCE_LED_OFF << (i * 4);
		(void)phy_write(phydev, LAN78XX_PHY_LED_MODE_SELECT, reg);
	} else if (len == -EOVERFLOW) {
		return -EINVAL;
	}

	/* these values can be used to identify internal PHY */
	priv->chip_id = phy_read_mmd(phydev, 3, LAN88XX_MMD3_CHIP_ID);
	priv->chip_rev = phy_read_mmd(phydev, 3, LAN88XX_MMD3_CHIP_REV);

	phydev->priv = priv;

	return 0;
}

static void lan88xx_remove(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct lan88xx_priv *priv = phydev->priv;

	if (priv)
		devm_kfree(dev, priv);
}

static int lan88xx_set_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	struct lan88xx_priv *priv = phydev->priv;

	priv->wolopts = wol->wolopts;

	return 0;
}

static void lan88xx_set_mdix(struct phy_device *phydev)
{
	int buf;
	int val;

	switch (phydev->mdix_ctrl) {
	case ETH_TP_MDI:
		val = LAN88XX_EXT_MODE_CTRL_MDI_;
		break;
	case ETH_TP_MDI_X:
		val = LAN88XX_EXT_MODE_CTRL_MDI_X_;
		break;
	case ETH_TP_MDI_AUTO:
		val = LAN88XX_EXT_MODE_CTRL_AUTO_MDIX_;
		break;
	default:
		return;
	}

	phy_write(phydev, LAN88XX_EXT_PAGE_ACCESS, LAN88XX_EXT_PAGE_SPACE_1);
	buf = phy_read(phydev, LAN88XX_EXT_MODE_CTRL);
	buf &= ~LAN88XX_EXT_MODE_CTRL_MDIX_MASK_;
	buf |= val;
	phy_write(phydev, LAN88XX_EXT_MODE_CTRL, buf);
	phy_write(phydev, LAN88XX_EXT_PAGE_ACCESS, LAN88XX_EXT_PAGE_SPACE_0);
}

static int lan88xx_config_init(struct phy_device *phydev)
{
	int val;

	genphy_config_init(phydev);
	/*Zerodetect delay enable */
	val = phy_read_mmd(phydev, MDIO_MMD_PCS,
			   PHY_ARDENNES_MMD_DEV_3_PHY_CFG);
	val |= PHY_ARDENNES_MMD_DEV_3_PHY_CFG_ZD_DLY_EN_;

	phy_write_mmd(phydev, MDIO_MMD_PCS, PHY_ARDENNES_MMD_DEV_3_PHY_CFG,
		      val);

	/* Config DSP registers */
	lan88xx_config_TR_regs(phydev);

	return 0;
}

static int lan88xx_config_aneg(struct phy_device *phydev)
{
	lan88xx_set_mdix(phydev);

	return genphy_config_aneg(phydev);
}

static struct phy_driver microchip_phy_driver[] = {
{
	.phy_id		= 0x0007c130,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Microchip LAN88xx",

	.features	= PHY_GBIT_FEATURES,

	.probe		= lan88xx_probe,
	.remove		= lan88xx_remove,

	.config_init	= lan88xx_config_init,
	.config_aneg	= lan88xx_config_aneg,

	.ack_interrupt	= lan88xx_phy_ack_interrupt,
	.config_intr	= lan88xx_phy_config_intr,

	.suspend	= lan88xx_suspend,
	.resume		= genphy_resume,
	.set_wol	= lan88xx_set_wol,
	.read_page	= lan88xx_read_page,
	.write_page	= lan88xx_write_page,
} };

module_phy_driver(microchip_phy_driver);

static struct mdio_device_id __maybe_unused microchip_tbl[] = {
	{ 0x0007c130, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, microchip_tbl);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
