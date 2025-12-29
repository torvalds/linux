// SPDX-License-Identifier: GPL-2.0-or-later
/* DSA Driver for MaxLinear GSW1xx switch devices
 *
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 * Copyright (C) 2023 - 2024 MaxLinear Inc.
 * Copyright (C) 2022 Snap One, LLC.  All rights reserved.
 * Copyright (C) 2017 - 2019 Hauke Mehrtens <hauke@hauke-m.de>
 * Copyright (C) 2012 John Crispin <john@phrozen.org>
 * Copyright (C) 2010 Lantiq Deutschland
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <net/dsa.h>

#include "lantiq_gswip.h"
#include "mxl-gsw1xx.h"
#include "mxl-gsw1xx_pce.h"

struct gsw1xx_priv {
	struct mdio_device	*mdio_dev;
	int			smdio_badr;
	struct			regmap *sgmii;
	struct			regmap *gpio;
	struct			regmap *clk;
	struct			regmap *shell;
	struct			phylink_pcs pcs;
	struct delayed_work	clear_raneg;
	phy_interface_t		tbi_interface;
	struct gswip_priv	gswip;
};

static int gsw1xx_config_smdio_badr(struct gsw1xx_priv *priv,
				    unsigned int reg)
{
	struct mii_bus *bus = priv->mdio_dev->bus;
	int sw_addr = priv->mdio_dev->addr;
	int smdio_badr = priv->smdio_badr;
	int res;

	if (smdio_badr == GSW1XX_SMDIO_BADR_UNKNOWN ||
	    reg - smdio_badr >= GSW1XX_SMDIO_BADR ||
	    smdio_badr > reg) {
		/* Configure the Switch Base Address */
		smdio_badr = reg & ~GENMASK(3, 0);
		res = __mdiobus_write(bus, sw_addr, GSW1XX_SMDIO_BADR, smdio_badr);
		if (res < 0) {
			dev_err(&priv->mdio_dev->dev,
				"%s: Error %d, configuring switch base\n",
				__func__, res);
			return res;
		}
		priv->smdio_badr = smdio_badr;
	}

	return smdio_badr;
}

static int gsw1xx_regmap_read(void *context, unsigned int reg,
			      unsigned int *val)
{
	struct gsw1xx_priv *priv = context;
	struct mii_bus *bus = priv->mdio_dev->bus;
	int sw_addr = priv->mdio_dev->addr;
	int smdio_badr;
	int res;

	smdio_badr = gsw1xx_config_smdio_badr(priv, reg);
	if (smdio_badr < 0)
		return smdio_badr;

	res = __mdiobus_read(bus, sw_addr, reg - smdio_badr);
	if (res < 0) {
		dev_err(&priv->mdio_dev->dev, "%s: Error %d reading 0x%x\n",
			__func__, res, reg);
		return res;
	}

	*val = res;

	return 0;
}

static int gsw1xx_regmap_write(void *context, unsigned int reg,
			       unsigned int val)
{
	struct gsw1xx_priv *priv = context;
	struct mii_bus *bus = priv->mdio_dev->bus;
	int sw_addr = priv->mdio_dev->addr;
	int smdio_badr;
	int res;

	smdio_badr = gsw1xx_config_smdio_badr(priv, reg);
	if (smdio_badr < 0)
		return smdio_badr;

	res = __mdiobus_write(bus, sw_addr, reg - smdio_badr, val);
	if (res < 0)
		dev_err(&priv->mdio_dev->dev,
			"%s: Error %d, writing 0x%x:0x%x\n", __func__, res, reg,
			val);

	return res;
}

static const struct regmap_bus gsw1xx_regmap_bus = {
	.reg_write = gsw1xx_regmap_write,
	.reg_read = gsw1xx_regmap_read,
};

static void gsw1xx_mdio_regmap_lock(void *mdio_lock)
{
	mutex_lock_nested(mdio_lock, MDIO_MUTEX_NESTED);
}

static void gsw1xx_mdio_regmap_unlock(void *mdio_lock)
{
	mutex_unlock(mdio_lock);
}

static unsigned int gsw1xx_pcs_inband_caps(struct phylink_pcs *pcs,
					   phy_interface_t interface)
{
	return LINK_INBAND_DISABLE | LINK_INBAND_ENABLE;
}

static struct gsw1xx_priv *pcs_to_gsw1xx(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct gsw1xx_priv, pcs);
}

static int gsw1xx_pcs_enable(struct phylink_pcs *pcs)
{
	struct gsw1xx_priv *priv = pcs_to_gsw1xx(pcs);

	/* Deassert SGMII shell reset */
	return regmap_clear_bits(priv->shell, GSW1XX_SHELL_RST_REQ,
				 GSW1XX_RST_REQ_SGMII_SHELL);
}

static void gsw1xx_pcs_disable(struct phylink_pcs *pcs)
{
	struct gsw1xx_priv *priv = pcs_to_gsw1xx(pcs);

	cancel_delayed_work_sync(&priv->clear_raneg);

	/* Assert SGMII shell reset (will also clear RANEG bit) */
	regmap_set_bits(priv->shell, GSW1XX_SHELL_RST_REQ,
			GSW1XX_RST_REQ_SGMII_SHELL);

	priv->tbi_interface = PHY_INTERFACE_MODE_NA;
}

static void gsw1xx_pcs_get_state(struct phylink_pcs *pcs,
				 unsigned int neg_mode,
				 struct phylink_link_state *state)
{
	struct gsw1xx_priv *priv = pcs_to_gsw1xx(pcs);
	int ret;
	u32 val;

	ret = regmap_read(priv->sgmii, GSW1XX_SGMII_TBI_TBISTAT, &val);
	if (ret < 0)
		return;

	state->link = !!(val & GSW1XX_SGMII_TBI_TBISTAT_LINK);
	state->an_complete = !!(val & GSW1XX_SGMII_TBI_TBISTAT_AN_COMPLETE);

	ret = regmap_read(priv->sgmii, GSW1XX_SGMII_TBI_LPSTAT, &val);
	if (ret < 0)
		return;

	state->duplex = (val & GSW1XX_SGMII_TBI_LPSTAT_DUPLEX) ?
			 DUPLEX_FULL : DUPLEX_HALF;
	if (val & GSW1XX_SGMII_TBI_LPSTAT_PAUSE_RX)
		state->pause |= MLO_PAUSE_RX;

	if (val & GSW1XX_SGMII_TBI_LPSTAT_PAUSE_TX)
		state->pause |= MLO_PAUSE_TX;

	switch (FIELD_GET(GSW1XX_SGMII_TBI_LPSTAT_SPEED, val)) {
	case GSW1XX_SGMII_TBI_LPSTAT_SPEED_10:
		state->speed = SPEED_10;
		break;
	case GSW1XX_SGMII_TBI_LPSTAT_SPEED_100:
		state->speed = SPEED_100;
		break;
	case GSW1XX_SGMII_TBI_LPSTAT_SPEED_1000:
		state->speed = SPEED_1000;
		break;
	case GSW1XX_SGMII_TBI_LPSTAT_SPEED_NOSGMII:
		if (state->interface == PHY_INTERFACE_MODE_1000BASEX)
			state->speed = SPEED_1000;
		else if (state->interface == PHY_INTERFACE_MODE_2500BASEX)
			state->speed = SPEED_2500;
		else
			state->speed = SPEED_UNKNOWN;
		break;
	}
}

static int gsw1xx_pcs_phy_xaui_write(struct gsw1xx_priv *priv, u16 addr,
				     u16 data)
{
	int ret, val;

	ret = regmap_write(priv->sgmii, GSW1XX_SGMII_PHY_D, data);
	if (ret < 0)
		return ret;

	ret = regmap_write(priv->sgmii, GSW1XX_SGMII_PHY_A, addr);
	if (ret < 0)
		return ret;

	ret = regmap_write(priv->sgmii, GSW1XX_SGMII_PHY_C,
			   GSW1XX_SGMII_PHY_WRITE |
			   GSW1XX_SGMII_PHY_RESET_N);
	if (ret < 0)
		return ret;

	return regmap_read_poll_timeout(priv->sgmii, GSW1XX_SGMII_PHY_C,
					val, val & GSW1XX_SGMII_PHY_STATUS,
					1000, 100000);
}

static int gsw1xx_pcs_reset(struct gsw1xx_priv *priv)
{
	int ret;
	u16 val;

	/* Assert and deassert SGMII shell reset */
	ret = regmap_set_bits(priv->shell, GSW1XX_SHELL_RST_REQ,
			      GSW1XX_RST_REQ_SGMII_SHELL);
	if (ret < 0)
		return ret;

	ret = regmap_clear_bits(priv->shell, GSW1XX_SHELL_RST_REQ,
				GSW1XX_RST_REQ_SGMII_SHELL);
	if (ret < 0)
		return ret;

	/* Hardware Bringup FSM Enable  */
	ret = regmap_write(priv->sgmii, GSW1XX_SGMII_PHY_HWBU_CTRL,
			   GSW1XX_SGMII_PHY_HWBU_CTRL_EN_HWBU_FSM |
			   GSW1XX_SGMII_PHY_HWBU_CTRL_HW_FSM_EN);
	if (ret < 0)
		return ret;

	/* Configure SGMII PHY Receiver */
	val = FIELD_PREP(GSW1XX_SGMII_PHY_RX0_CFG2_EQ,
			 GSW1XX_SGMII_PHY_RX0_CFG2_EQ_DEF) |
	      GSW1XX_SGMII_PHY_RX0_CFG2_LOS_EN |
	      GSW1XX_SGMII_PHY_RX0_CFG2_TERM_EN |
	      FIELD_PREP(GSW1XX_SGMII_PHY_RX0_CFG2_FILT_CNT,
			 GSW1XX_SGMII_PHY_RX0_CFG2_FILT_CNT_DEF);

	/* RX lane seems to be inverted internally, so bit
	 * GSW1XX_SGMII_PHY_RX0_CFG2_INVERT needs to be set for normal
	 * (ie. non-inverted) operation.
	 *
	 * TODO: Take care of inverted RX pair once generic property is
	 *       available
	 */

	val |= GSW1XX_SGMII_PHY_RX0_CFG2_INVERT;

	ret = regmap_write(priv->sgmii, GSW1XX_SGMII_PHY_RX0_CFG2, val);
	if (ret < 0)
		return ret;

	val = FIELD_PREP(GSW1XX_SGMII_PHY_TX0_CFG3_VBOOST_LEVEL,
			 GSW1XX_SGMII_PHY_TX0_CFG3_VBOOST_LEVEL_DEF);

	/* TODO: Take care of inverted TX pair once generic property is
	 *       available
	 */

	ret = regmap_write(priv->sgmii, GSW1XX_SGMII_PHY_TX0_CFG3, val);
	if (ret < 0)
		return ret;

	/* Reset and Release TBI */
	val = GSW1XX_SGMII_TBI_TBICTL_INITTBI | GSW1XX_SGMII_TBI_TBICTL_ENTBI |
	      GSW1XX_SGMII_TBI_TBICTL_CRSTRR | GSW1XX_SGMII_TBI_TBICTL_CRSOFF;
	ret = regmap_write(priv->sgmii, GSW1XX_SGMII_TBI_TBICTL, val);
	if (ret < 0)
		return ret;
	val &= ~GSW1XX_SGMII_TBI_TBICTL_INITTBI;
	ret = regmap_write(priv->sgmii, GSW1XX_SGMII_TBI_TBICTL, val);
	if (ret < 0)
		return ret;

	/* Release Tx Data Buffers */
	ret = regmap_set_bits(priv->sgmii, GSW1XX_SGMII_PCS_TXB_CTL,
			      GSW1XX_SGMII_PCS_TXB_CTL_INIT_TX_TXB);
	if (ret < 0)
		return ret;
	ret = regmap_clear_bits(priv->sgmii, GSW1XX_SGMII_PCS_TXB_CTL,
				GSW1XX_SGMII_PCS_TXB_CTL_INIT_TX_TXB);
	if (ret < 0)
		return ret;

	/* Release Rx Data Buffers */
	ret = regmap_set_bits(priv->sgmii, GSW1XX_SGMII_PCS_RXB_CTL,
			      GSW1XX_SGMII_PCS_RXB_CTL_INIT_RX_RXB);
	if (ret < 0)
		return ret;
	return regmap_clear_bits(priv->sgmii, GSW1XX_SGMII_PCS_RXB_CTL,
				 GSW1XX_SGMII_PCS_RXB_CTL_INIT_RX_RXB);
}

static int gsw1xx_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
			     phy_interface_t interface,
			     const unsigned long *advertising,
			     bool permit_pause_to_mac)
{
	struct gsw1xx_priv *priv = pcs_to_gsw1xx(pcs);
	u16 txaneg, anegctl, nco_ctrl;
	bool reconf = false;
	int ret = 0;

	/* do not unnecessarily disrupt link and skip resetting the hardware in
	 * case the PCS has previously been successfully configured for this
	 * interface mode
	 */
	if (priv->tbi_interface == interface)
		reconf = true;

	/* mark PCS configuration as incomplete */
	priv->tbi_interface = PHY_INTERFACE_MODE_NA;

	if (!reconf)
		ret = gsw1xx_pcs_reset(priv);

	if (ret)
		return ret;

	/* override bootstrap pin settings
	 * OVRANEG sets ANEG Mode, Enable ANEG and restart ANEG to be
	 * taken from bits ANMODE, ANEGEN, RANEG of the ANEGCTL register.
	 * OVERABL sets ability bits in tx_config_reg to be taken from
	 * the TXANEGH and TXANEGL registers.
	 */
	anegctl = GSW1XX_SGMII_TBI_ANEGCTL_OVRANEG |
		  GSW1XX_SGMII_TBI_ANEGCTL_OVRABL;

	switch (phylink_get_link_timer_ns(interface)) {
	case 10000:
		anegctl |= FIELD_PREP(GSW1XX_SGMII_TBI_ANEGCTL_LT,
				      GSW1XX_SGMII_TBI_ANEGCTL_LT_10US);
		break;
	case 1600000:
		anegctl |= FIELD_PREP(GSW1XX_SGMII_TBI_ANEGCTL_LT,
				      GSW1XX_SGMII_TBI_ANEGCTL_LT_1_6MS);
		break;
	case 5000000:
		anegctl |= FIELD_PREP(GSW1XX_SGMII_TBI_ANEGCTL_LT,
				      GSW1XX_SGMII_TBI_ANEGCTL_LT_5MS);
		break;
	case 10000000:
		anegctl |= FIELD_PREP(GSW1XX_SGMII_TBI_ANEGCTL_LT,
				      GSW1XX_SGMII_TBI_ANEGCTL_LT_10MS);
		break;
	default:
		return -EINVAL;
	}

	if (neg_mode & PHYLINK_PCS_NEG_INBAND)
		anegctl |= GSW1XX_SGMII_TBI_ANEGCTL_ANEGEN;

	txaneg = phylink_mii_c22_pcs_encode_advertisement(interface, advertising);

	if (interface == PHY_INTERFACE_MODE_SGMII) {
		/* lacking a defined reverse-SGMII interface mode this
		 * driver only supports SGMII (MAC side) for now
		 */
		anegctl |= FIELD_PREP(GSW1XX_SGMII_TBI_ANEGCTL_ANMODE,
				      GSW1XX_SGMII_TBI_ANEGCTL_ANMODE_SGMII_MAC);
		txaneg |= ADVERTISE_LPACK;
	} else if (interface == PHY_INTERFACE_MODE_1000BASEX ||
		   interface == PHY_INTERFACE_MODE_2500BASEX) {
		anegctl |= FIELD_PREP(GSW1XX_SGMII_TBI_ANEGCTL_ANMODE,
				      GSW1XX_SGMII_TBI_ANEGCTL_ANMODE_1000BASEX);
	} else {
		dev_err(priv->gswip.dev, "%s: wrong interface mode %s\n",
			__func__, phy_modes(interface));
		return -EINVAL;
	}

	ret = regmap_write(priv->sgmii, GSW1XX_SGMII_TBI_TXANEGH,
			   FIELD_GET(GENMASK(15, 8), txaneg));
	if (ret < 0)
		return ret;
	ret = regmap_write(priv->sgmii, GSW1XX_SGMII_TBI_TXANEGL,
			   FIELD_GET(GENMASK(7, 0), txaneg));
	if (ret < 0)
		return ret;
	ret = regmap_write(priv->sgmii, GSW1XX_SGMII_TBI_ANEGCTL, anegctl);
	if (ret < 0)
		return ret;

	if (!reconf) {
		/* setup SerDes clock speed */
		if (interface == PHY_INTERFACE_MODE_2500BASEX)
			nco_ctrl = GSW1XX_SGMII_2G5 | GSW1XX_SGMII_2G5_NCO2;
		else
			nco_ctrl = GSW1XX_SGMII_1G | GSW1XX_SGMII_1G_NCO1;

		ret = regmap_update_bits(priv->clk, GSW1XX_CLK_NCO_CTRL,
					 GSW1XX_SGMII_HSP_MASK |
					 GSW1XX_SGMII_SEL,
					 nco_ctrl);
		if (ret)
			return ret;

		ret = gsw1xx_pcs_phy_xaui_write(priv, 0x30, 0x80);
		if (ret)
			return ret;
	}

	/* PCS configuration has now been completed, store mode to prevent
	 * disrupting the link in case of future calls of this function for the
	 * same interface mode.
	 */
	priv->tbi_interface = interface;

	return 0;
}

static void gsw1xx_pcs_clear_raneg(struct work_struct *work)
{
	struct gsw1xx_priv *priv =
		container_of(work, struct gsw1xx_priv, clear_raneg.work);

	regmap_clear_bits(priv->sgmii, GSW1XX_SGMII_TBI_ANEGCTL,
			  GSW1XX_SGMII_TBI_ANEGCTL_RANEG);
}

static void gsw1xx_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct gsw1xx_priv *priv = pcs_to_gsw1xx(pcs);

	cancel_delayed_work_sync(&priv->clear_raneg);

	regmap_set_bits(priv->sgmii, GSW1XX_SGMII_TBI_ANEGCTL,
			GSW1XX_SGMII_TBI_ANEGCTL_RANEG);

	/* despite being documented as self-clearing, the RANEG bit
	 * sometimes remains set, preventing auto-negotiation from happening.
	 * MaxLinear advises to manually clear the bit after 10ms.
	 */
	schedule_delayed_work(&priv->clear_raneg, msecs_to_jiffies(10));
}

static void gsw1xx_pcs_link_up(struct phylink_pcs *pcs,
			       unsigned int neg_mode,
			       phy_interface_t interface, int speed,
			       int duplex)
{
	struct gsw1xx_priv *priv = pcs_to_gsw1xx(pcs);
	u16 lpstat;

	/* When in-band AN is enabled hardware will set lpstat */
	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
		return;

	/* Force speed and duplex settings */
	if (interface == PHY_INTERFACE_MODE_SGMII) {
		if (speed == SPEED_10)
			lpstat = FIELD_PREP(GSW1XX_SGMII_TBI_LPSTAT_SPEED,
					    GSW1XX_SGMII_TBI_LPSTAT_SPEED_10);
		else if (speed == SPEED_100)
			lpstat = FIELD_PREP(GSW1XX_SGMII_TBI_LPSTAT_SPEED,
					    GSW1XX_SGMII_TBI_LPSTAT_SPEED_100);
		else
			lpstat = FIELD_PREP(GSW1XX_SGMII_TBI_LPSTAT_SPEED,
					    GSW1XX_SGMII_TBI_LPSTAT_SPEED_1000);
	} else {
		lpstat = FIELD_PREP(GSW1XX_SGMII_TBI_LPSTAT_SPEED,
				    GSW1XX_SGMII_TBI_LPSTAT_SPEED_NOSGMII);
	}

	if (duplex == DUPLEX_FULL)
		lpstat |= GSW1XX_SGMII_TBI_LPSTAT_DUPLEX;

	regmap_write(priv->sgmii, GSW1XX_SGMII_TBI_LPSTAT, lpstat);
}

static const struct phylink_pcs_ops gsw1xx_pcs_ops = {
	.pcs_inband_caps = gsw1xx_pcs_inband_caps,
	.pcs_enable = gsw1xx_pcs_enable,
	.pcs_disable = gsw1xx_pcs_disable,
	.pcs_get_state = gsw1xx_pcs_get_state,
	.pcs_config = gsw1xx_pcs_config,
	.pcs_an_restart = gsw1xx_pcs_an_restart,
	.pcs_link_up = gsw1xx_pcs_link_up,
};

static void gsw1xx_phylink_get_caps(struct dsa_switch *ds, int port,
				    struct phylink_config *config)
{
	struct gswip_priv *priv = ds->priv;

	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
				   MAC_10 | MAC_100 | MAC_1000;

	switch (port) {
	case 0:
	case 1:
	case 2:
	case 3:
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		break;
	case 4: /* port 4: SGMII */
		__set_bit(PHY_INTERFACE_MODE_SGMII,
			  config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_1000BASEX,
			  config->supported_interfaces);
		if (priv->hw_info->supports_2500m) {
			__set_bit(PHY_INTERFACE_MODE_2500BASEX,
				  config->supported_interfaces);
			config->mac_capabilities |= MAC_2500FD;
		}
		return; /* no support for EEE on SGMII port */
	case 5: /* port 5: RGMII or RMII */
		__set_bit(PHY_INTERFACE_MODE_RMII,
			  config->supported_interfaces);
		phy_interface_set_rgmii(config->supported_interfaces);
		break;
	}

	config->lpi_capabilities = MAC_100FD | MAC_1000FD;
	config->lpi_timer_default = 20;
	memcpy(config->lpi_interfaces, config->supported_interfaces,
	       sizeof(config->lpi_interfaces));
}

static struct phylink_pcs *gsw1xx_phylink_mac_select_pcs(struct phylink_config *config,
							 phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct gswip_priv *gswip_priv = dp->ds->priv;
	struct gsw1xx_priv *gsw1xx_priv = container_of(gswip_priv,
						       struct gsw1xx_priv,
						       gswip);

	switch (dp->index) {
	case GSW1XX_SGMII_PORT:
		return &gsw1xx_priv->pcs;
	default:
		return NULL;
	}
}

static struct regmap *gsw1xx_regmap_init(struct gsw1xx_priv *priv,
					 const char *name,
					 unsigned int reg_base,
					 unsigned int max_register)
{
	const struct regmap_config config = {
		.name = name,
		.reg_bits = 16,
		.val_bits = 16,
		.reg_base = reg_base,
		.max_register = max_register,
		.lock = gsw1xx_mdio_regmap_lock,
		.unlock = gsw1xx_mdio_regmap_unlock,
		.lock_arg = &priv->mdio_dev->bus->mdio_lock,
	};

	return devm_regmap_init(&priv->mdio_dev->dev, &gsw1xx_regmap_bus,
				priv, &config);
}

static int gsw1xx_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct gsw1xx_priv *priv;
	u32 version;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mdio_dev = mdiodev;
	priv->smdio_badr = GSW1XX_SMDIO_BADR_UNKNOWN;

	priv->gswip.dev = dev;
	priv->gswip.hw_info = of_device_get_match_data(dev);
	if (!priv->gswip.hw_info)
		return -EINVAL;

	priv->gswip.gswip = gsw1xx_regmap_init(priv, "switch",
					       GSW1XX_SWITCH_BASE, 0xfff);
	if (IS_ERR(priv->gswip.gswip))
		return PTR_ERR(priv->gswip.gswip);

	priv->gswip.mdio = gsw1xx_regmap_init(priv, "mdio", GSW1XX_MMDIO_BASE,
					      0xff);
	if (IS_ERR(priv->gswip.mdio))
		return PTR_ERR(priv->gswip.mdio);

	priv->gswip.mii = gsw1xx_regmap_init(priv, "mii", GSW1XX_RGMII_BASE,
					     0xff);
	if (IS_ERR(priv->gswip.mii))
		return PTR_ERR(priv->gswip.mii);

	priv->sgmii = gsw1xx_regmap_init(priv, "sgmii", GSW1XX_SGMII_BASE,
					 0xfff);
	if (IS_ERR(priv->sgmii))
		return PTR_ERR(priv->sgmii);

	priv->gpio = gsw1xx_regmap_init(priv, "gpio", GSW1XX_GPIO_BASE, 0xff);
	if (IS_ERR(priv->gpio))
		return PTR_ERR(priv->gpio);

	priv->clk = gsw1xx_regmap_init(priv, "clk", GSW1XX_CLK_BASE, 0xff);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	priv->shell = gsw1xx_regmap_init(priv, "shell", GSW1XX_SHELL_BASE,
					 0xff);
	if (IS_ERR(priv->shell))
		return PTR_ERR(priv->shell);

	priv->pcs.ops = &gsw1xx_pcs_ops;
	priv->pcs.poll = true;
	__set_bit(PHY_INTERFACE_MODE_SGMII,
		  priv->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_1000BASEX,
		  priv->pcs.supported_interfaces);
	if (priv->gswip.hw_info->supports_2500m)
		__set_bit(PHY_INTERFACE_MODE_2500BASEX,
			  priv->pcs.supported_interfaces);
	priv->tbi_interface = PHY_INTERFACE_MODE_NA;

	/* assert SGMII reset to power down SGMII unit */
	ret = regmap_set_bits(priv->shell, GSW1XX_SHELL_RST_REQ,
			      GSW1XX_RST_REQ_SGMII_SHELL);
	if (ret < 0)
		return ret;

	/* configure GPIO pin-mux for MMDIO in case of external PHY connected to
	 * SGMII or RGMII as slave interface
	 */
	regmap_set_bits(priv->gpio, GPIO_ALTSEL0, 3);
	regmap_set_bits(priv->gpio, GPIO_ALTSEL1, 3);

	ret = regmap_read(priv->gswip.gswip, GSWIP_VERSION, &version);
	if (ret)
		return ret;

	INIT_DELAYED_WORK(&priv->clear_raneg, gsw1xx_pcs_clear_raneg);

	ret = gswip_probe_common(&priv->gswip, version);
	if (ret)
		return ret;

	dev_set_drvdata(dev, &priv->gswip);

	return 0;
}

static void gsw1xx_remove(struct mdio_device *mdiodev)
{
	struct gswip_priv *priv = dev_get_drvdata(&mdiodev->dev);
	struct gsw1xx_priv *gsw1xx_priv;

	if (!priv)
		return;

	dsa_unregister_switch(priv->ds);

	gsw1xx_priv = container_of(priv, struct gsw1xx_priv, gswip);
	cancel_delayed_work_sync(&gsw1xx_priv->clear_raneg);
}

static void gsw1xx_shutdown(struct mdio_device *mdiodev)
{
	struct gswip_priv *priv = dev_get_drvdata(&mdiodev->dev);
	struct gsw1xx_priv *gsw1xx_priv;

	if (!priv)
		return;

	dsa_switch_shutdown(priv->ds);

	dev_set_drvdata(&mdiodev->dev, NULL);

	gsw1xx_priv = container_of(priv, struct gsw1xx_priv, gswip);
	cancel_delayed_work_sync(&gsw1xx_priv->clear_raneg);
}

static const struct gswip_hw_info gsw12x_data = {
	.max_ports		= GSW1XX_PORTS,
	.allowed_cpu_ports	= BIT(GSW1XX_MII_PORT) | BIT(GSW1XX_SGMII_PORT),
	.mii_ports		= BIT(GSW1XX_MII_PORT),
	.mii_port_reg_offset	= -GSW1XX_MII_PORT,
	.mac_select_pcs		= gsw1xx_phylink_mac_select_pcs,
	.phylink_get_caps	= &gsw1xx_phylink_get_caps,
	.supports_2500m		= true,
	.pce_microcode		= &gsw1xx_pce_microcode,
	.pce_microcode_size	= ARRAY_SIZE(gsw1xx_pce_microcode),
	.tag_protocol		= DSA_TAG_PROTO_MXL_GSW1XX,
};

static const struct gswip_hw_info gsw140_data = {
	.max_ports		= GSW1XX_PORTS,
	.allowed_cpu_ports	= BIT(GSW1XX_MII_PORT) | BIT(GSW1XX_SGMII_PORT),
	.mii_ports		= BIT(GSW1XX_MII_PORT),
	.mii_port_reg_offset	= -GSW1XX_MII_PORT,
	.mac_select_pcs		= gsw1xx_phylink_mac_select_pcs,
	.phylink_get_caps	= &gsw1xx_phylink_get_caps,
	.supports_2500m		= true,
	.pce_microcode		= &gsw1xx_pce_microcode,
	.pce_microcode_size	= ARRAY_SIZE(gsw1xx_pce_microcode),
	.tag_protocol		= DSA_TAG_PROTO_MXL_GSW1XX,
};

static const struct gswip_hw_info gsw141_data = {
	.max_ports		= GSW1XX_PORTS,
	.allowed_cpu_ports	= BIT(GSW1XX_MII_PORT) | BIT(GSW1XX_SGMII_PORT),
	.mii_ports		= BIT(GSW1XX_MII_PORT),
	.mii_port_reg_offset	= -GSW1XX_MII_PORT,
	.mac_select_pcs		= gsw1xx_phylink_mac_select_pcs,
	.phylink_get_caps	= gsw1xx_phylink_get_caps,
	.pce_microcode		= &gsw1xx_pce_microcode,
	.pce_microcode_size	= ARRAY_SIZE(gsw1xx_pce_microcode),
	.tag_protocol		= DSA_TAG_PROTO_MXL_GSW1XX,
};

/*
 * GSW125 is the industrial temperature version of GSW120.
 * GSW145 is the industrial temperature version of GSW140.
 */
static const struct of_device_id gsw1xx_of_match[] = {
	{ .compatible = "maxlinear,gsw120", .data = &gsw12x_data },
	{ .compatible = "maxlinear,gsw125", .data = &gsw12x_data },
	{ .compatible = "maxlinear,gsw140", .data = &gsw140_data },
	{ .compatible = "maxlinear,gsw141", .data = &gsw141_data },
	{ .compatible = "maxlinear,gsw145", .data = &gsw140_data },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, gsw1xx_of_match);

static struct mdio_driver gsw1xx_driver = {
	.probe		= gsw1xx_probe,
	.remove		= gsw1xx_remove,
	.shutdown	= gsw1xx_shutdown,
	.mdiodrv.driver	= {
		.name = "mxl-gsw1xx",
		.of_match_table = gsw1xx_of_match,
	},
};

mdio_module_driver(gsw1xx_driver);

MODULE_AUTHOR("Daniel Golle <daniel@makrotopia.org>");
MODULE_DESCRIPTION("Driver for MaxLinear GSW1xx ethernet switch");
MODULE_LICENSE("GPL");
