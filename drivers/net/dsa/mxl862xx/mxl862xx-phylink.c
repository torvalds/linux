// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Phylink and PCS support for MaxLinear MxL862xx switch family
 *
 * Copyright (C) 2024 MaxLinear Inc.
 * Copyright (C) 2025 John Crispin <john@phrozen.org>
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/bitfield.h>
#include <linux/mutex.h>
#include <linux/phylink.h>
#include <net/dsa.h>

#include "mxl862xx.h"
#include "mxl862xx-api.h"
#include "mxl862xx-cmd.h"
#include "mxl862xx-host.h"
#include "mxl862xx-phylink.h"

void mxl862xx_phylink_get_caps(struct dsa_switch *ds, int port,
			       struct phylink_config *config)
{
	struct mxl862xx_priv *priv = ds->priv;

	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE | MAC_10 |
				   MAC_100 | MAC_1000 | MAC_2500FD;

	switch (port) {
	case 1 ... 8:
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		break;
	case 9:
	case 13:
		/* Advertised also on old firmware lacking the XPCS API:
		 * there the SerDes runs in its flash-configured mode
		 * without host control (mac_select_pcs returns NULL),
		 * keeping the CPU port working.
		 */
		__set_bit(PHY_INTERFACE_MODE_SGMII, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_1000BASEX, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_2500BASEX, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_10GBASER, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_10GKR, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_USXGMII, config->supported_interfaces);
		fallthrough;
	case 10 ... 12:
	case 14 ... 16:
		if (!MXL862XX_FW_VER_MIN(priv, 1, 0, 84))
			break;
		__set_bit(PHY_INTERFACE_MODE_QSGMII, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_10G_QXGMII, config->supported_interfaces);

		break;
	default:
		break;
	}

	if (port == 9 || port == 13)
		config->mac_capabilities |= MAC_10000FD | MAC_5000FD;
}

static struct mxl862xx_pcs *pcs_to_mxl862xx_pcs(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct mxl862xx_pcs, pcs);
}

static int mxl862xx_xpcs_if_mode(phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
		return MXL862XX_XPCS_IF_SGMII;
	case PHY_INTERFACE_MODE_QSGMII:
		return MXL862XX_XPCS_IF_QSGMII;
	case PHY_INTERFACE_MODE_1000BASEX:
		return MXL862XX_XPCS_IF_1000BASEX;
	case PHY_INTERFACE_MODE_2500BASEX:
		return MXL862XX_XPCS_IF_2500BASEX;
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_10G_QXGMII:
		return MXL862XX_XPCS_IF_USXGMII;
	case PHY_INTERFACE_MODE_10GBASER:
		return MXL862XX_XPCS_IF_10GBASER;
	case PHY_INTERFACE_MODE_10GKR:
		return MXL862XX_XPCS_IF_10GKR;
	default:
		return -EINVAL;
	}
}

static int mxl862xx_xpcs_neg_mode(unsigned int neg_mode)
{
	if (!(neg_mode & PHYLINK_PCS_NEG_INBAND))
		return MXL862XX_XPCS_NEG_NONE;
	if (neg_mode & PHYLINK_PCS_NEG_ENABLED)
		return MXL862XX_XPCS_NEG_INBAND_AN_ON;
	return MXL862XX_XPCS_NEG_INBAND_AN_OFF;
}

static int mxl862xx_pcs_enable(struct phylink_pcs *pcs)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);

	/* Bringup is done idempotently by pcs_config; just account this
	 * sub-port so pcs_disable powers the shared XPCS down only after
	 * the last sub-port has been released.
	 */
	mutex_lock(&mpcs->priv->serdes_lock);
	mpcs->priv->serdes_refcount[mpcs->serdes_id]++;
	mutex_unlock(&mpcs->priv->serdes_lock);

	return 0;
}

static void mxl862xx_pcs_disable(struct phylink_pcs *pcs)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);
	struct mxl862xx_xpcs_pcs_disable dis = {};
	struct mxl862xx_priv *priv = mpcs->priv;

	dis.port_id = mpcs->serdes_id;

	/* The SerDes is shared across QSGMII/QUSXGMII sub-ports; only
	 * power it down once the last active sub-port goes away. Hold
	 * serdes_lock across the count and the power-down so a sibling
	 * sub-port enable cannot race the transition to zero.
	 */
	mutex_lock(&priv->serdes_lock);
	if (--priv->serdes_refcount[mpcs->serdes_id] == 0)
		MXL862XX_API_WRITE(priv, MXL862XX_XPCS_PCS_DISABLE, dis);
	mutex_unlock(&priv->serdes_lock);
}

/* The XPCS firmware reports failures in the result field using its own
 * libc errno values; ENOTSUP (134) in particular has no kernel errno.
 * Translate the codes the firmware can actually return.
 */
static int mxl862xx_xpcs_errno(int result)
{
	switch (result) {
	case -5:	/* firmware -EIO */
		return -EIO;
	case -134:	/* firmware -ENOTSUP */
		return -EOPNOTSUPP;
	default:	/* firmware -EINVAL and anything unexpected */
		return -EINVAL;
	}
}

static int mxl862xx_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
			       phy_interface_t interface,
			       const unsigned long *advertising,
			       bool permit_pause_to_mac)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);
	struct mxl862xx_priv *priv = mpcs->priv;
	struct mxl862xx_xpcs_pcs_cfg cfg = {};
	int if_mode, lane, ret, adv;

	if_mode = mxl862xx_xpcs_if_mode(interface);
	if (if_mode < 0) {
		dev_err(priv->ds->dev, "unsupported interface: %s\n",
			phy_modes(interface));
		return if_mode;
	}

	/* The XPCS bringup is per-instance and idempotent in the
	 * firmware: every QSGMII/QUSXGMII sub-port may call pcs_config
	 * and the firmware will skip the bringup if the requested mode
	 * matches the cached one, then update MAC pause for the
	 * sub-port indicated by @usx_subport. No serdes_lock is needed
	 * here: the refcount held since pcs_enable keeps a sibling
	 * pcs_disable from powering the XPCS down, and pcs_disable
	 * invalidates the firmware's cached mode so the next pcs_config
	 * redoes the bringup.
	 */
	lane = (interface == PHY_INTERFACE_MODE_10G_QXGMII) ?
	       MXL862XX_XPCS_USX_QUAD : MXL862XX_XPCS_USX_SINGLE;

	cfg.mode = cpu_to_le16(FIELD_PREP(MXL862XX_XPCS_CFG_PORT_ID,
					  mpcs->serdes_id) |
			       FIELD_PREP(MXL862XX_XPCS_CFG_USX_SUBPORT,
					  mpcs->slot) |
			       FIELD_PREP(MXL862XX_XPCS_CFG_USX_LANE_MODE, lane) |
			       FIELD_PREP(MXL862XX_XPCS_CFG_INTERFACE, if_mode) |
			       FIELD_PREP(MXL862XX_XPCS_CFG_NEG_MODE,
					  mxl862xx_xpcs_neg_mode(neg_mode)) |
			       FIELD_PREP(MXL862XX_XPCS_CFG_ROLE,
					  MXL862XX_XPCS_ROLE_MAC) |
			       FIELD_PREP(MXL862XX_XPCS_CFG_PERMIT_PAUSE,
					  permit_pause_to_mac));

	if (neg_mode & PHYLINK_PCS_NEG_INBAND) {
		adv = phylink_mii_c22_pcs_encode_advertisement(interface,
							       advertising);
		if (adv >= 0)
			cfg.advertising.cl37 = cpu_to_le16(adv);
	}

	ret = MXL862XX_API_READ(priv, MXL862XX_XPCS_PCS_CONFIG, cfg);
	if (ret)
		return ret;

	ret = (s16)le16_to_cpu(cfg.result);
	if (ret < 0)
		return mxl862xx_xpcs_errno(ret);

	mpcs->interface = interface;
	return ret > 0 ? 1 : 0;
}

static void mxl862xx_pcs_get_state(struct phylink_pcs *pcs,
				   unsigned int neg_mode,
				   struct phylink_link_state *state)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);
	struct mxl862xx_priv *priv = mpcs->priv;
	struct mxl862xx_xpcs_pcs_state st = {};
	int if_mode, lane, ret;
	u32 mode;
	u16 bmsr;

	if_mode = mxl862xx_xpcs_if_mode(state->interface);
	if (if_mode < 0) {
		state->link = false;
		return;
	}

	lane = (state->interface == PHY_INTERFACE_MODE_10G_QXGMII) ?
	       MXL862XX_XPCS_USX_QUAD : MXL862XX_XPCS_USX_SINGLE;

	st.mode = cpu_to_le32(FIELD_PREP(MXL862XX_XPCS_ST_PORT_ID,
					 mpcs->serdes_id) |
			      FIELD_PREP(MXL862XX_XPCS_ST_INTERFACE, if_mode) |
			      FIELD_PREP(MXL862XX_XPCS_ST_USX_SUBPORT,
					 mpcs->slot) |
			      FIELD_PREP(MXL862XX_XPCS_ST_USX_LANE_MODE, lane));

	ret = MXL862XX_API_READ(priv, MXL862XX_XPCS_PCS_GET_STATE, st);
	if (ret) {
		state->link = false;
		return;
	}

	mode = le32_to_cpu(st.mode);
	state->link = FIELD_GET(MXL862XX_XPCS_ST_LINK, mode) &&
		      !FIELD_GET(MXL862XX_XPCS_ST_PCS_FAULT, mode);
	state->an_complete = FIELD_GET(MXL862XX_XPCS_ST_AN_COMPLETE, mode);

	switch (state->interface) {
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		bmsr = (state->link ? BMSR_LSTATUS : 0) |
		       (state->an_complete ? BMSR_ANEGCOMPLETE : 0);
		phylink_mii_c22_pcs_decode_state(state, neg_mode, bmsr,
						 le16_to_cpu(st.lpa.cl37));
		break;

	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_10G_QXGMII:
		if (state->link)
			phylink_decode_usxgmii_word(state,
						    le16_to_cpu(st.lpa.usx));
		break;

	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_10GKR:
		if (state->link) {
			state->speed = SPEED_10000;
			state->duplex = DUPLEX_FULL;
		}
		break;

	default:
		state->link = false;
		break;
	}
}

static void mxl862xx_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);
	struct mxl862xx_priv *priv = mpcs->priv;
	struct mxl862xx_xpcs_an_restart an = {};
	int if_mode, lane;

	if_mode = mxl862xx_xpcs_if_mode(mpcs->interface);
	if (if_mode < 0)
		return;

	lane = (mpcs->interface == PHY_INTERFACE_MODE_10G_QXGMII) ?
	       MXL862XX_XPCS_USX_QUAD : MXL862XX_XPCS_USX_SINGLE;

	an.mode = cpu_to_le16(FIELD_PREP(MXL862XX_XPCS_ANR_PORT_ID,
					 mpcs->serdes_id) |
			      FIELD_PREP(MXL862XX_XPCS_ANR_INTERFACE, if_mode) |
			      FIELD_PREP(MXL862XX_XPCS_ANR_USX_SUBPORT,
					 mpcs->slot) |
			      FIELD_PREP(MXL862XX_XPCS_ANR_USX_LANE_MODE, lane));

	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_AN_RESTART, an);
}

static void mxl862xx_pcs_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
				 phy_interface_t interface, int speed,
				 int duplex)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);
	struct mxl862xx_xpcs_pcs_link_up lu = {};
	struct mxl862xx_priv *priv = mpcs->priv;
	int if_mode, lane, dup;

	/* With inband-AN enabled (role=MAC), the XPCS auto-resolves
	 * speed/duplex from the partner's AN word and the firmware
	 * short-circuits link_up. Skip the firmware round-trip, same
	 * as pcs-mtk-lynxi.
	 */
	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
		return;

	if_mode = mxl862xx_xpcs_if_mode(interface);
	if (if_mode < 0)
		return;

	lane = (interface == PHY_INTERFACE_MODE_10G_QXGMII) ?
	       MXL862XX_XPCS_USX_QUAD : MXL862XX_XPCS_USX_SINGLE;
	dup = (duplex == DUPLEX_FULL) ? MXL862XX_XPCS_DUPLEX_FULL :
					MXL862XX_XPCS_DUPLEX_HALF;

	lu.mode = cpu_to_le16(FIELD_PREP(MXL862XX_XPCS_LU_PORT_ID,
					 mpcs->serdes_id) |
			      FIELD_PREP(MXL862XX_XPCS_LU_INTERFACE, if_mode) |
			      FIELD_PREP(MXL862XX_XPCS_LU_USX_SUBPORT,
					 mpcs->slot) |
			      FIELD_PREP(MXL862XX_XPCS_LU_USX_LANE_MODE, lane) |
			      FIELD_PREP(MXL862XX_XPCS_LU_DUPLEX, dup));
	lu.speed = cpu_to_le16(speed);

	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_PCS_LINK_UP, lu);
}

static unsigned int mxl862xx_pcs_inband_caps(struct phylink_pcs *pcs,
					     phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		return LINK_INBAND_DISABLE | LINK_INBAND_ENABLE;
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_10G_QXGMII:
	case PHY_INTERFACE_MODE_10GKR:
		return LINK_INBAND_ENABLE;
	case PHY_INTERFACE_MODE_10GBASER:
		return LINK_INBAND_DISABLE;
	default:
		return 0;
	}
}

static const struct phylink_pcs_ops mxl862xx_pcs_ops = {
	.pcs_enable = mxl862xx_pcs_enable,
	.pcs_disable = mxl862xx_pcs_disable,
	.pcs_config = mxl862xx_pcs_config,
	.pcs_get_state = mxl862xx_pcs_get_state,
	.pcs_an_restart = mxl862xx_pcs_an_restart,
	.pcs_link_up = mxl862xx_pcs_link_up,
	.pcs_inband_caps = mxl862xx_pcs_inband_caps,
};

void mxl862xx_setup_pcs(struct mxl862xx_priv *priv, struct mxl862xx_pcs *pcs,
			int port)
{
	pcs->priv = priv;
	pcs->serdes_id = MXL862XX_SERDES_PORT_ID(port);
	pcs->slot = MXL862XX_SERDES_SLOT(port);
	pcs->interface = PHY_INTERFACE_MODE_NA;

	pcs->pcs.ops = &mxl862xx_pcs_ops;
	pcs->pcs.poll = true;

	__set_bit(PHY_INTERFACE_MODE_QSGMII, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_10G_QXGMII, pcs->pcs.supported_interfaces);
	if (pcs->slot != 0)
		return;

	__set_bit(PHY_INTERFACE_MODE_SGMII, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_1000BASEX, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_2500BASEX, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_10GBASER, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_10GKR, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_USXGMII, pcs->pcs.supported_interfaces);
}

static struct phylink_pcs *
mxl862xx_phylink_mac_select_pcs(struct phylink_config *config,
				phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct mxl862xx_priv *priv = dp->ds->priv;
	int port = dp->index;

	switch (port) {
	case 9 ... 16:
		if (!MXL862XX_FW_VER_MIN(priv, 1, 0, 84)) {
			dev_warn_once(dp->ds->dev,
				      "SerDes PCS unsupported on old firmware.\n");
			return NULL;
		}
		return &priv->serdes_ports[port - 9].pcs;
	default:
		return NULL;
	}
}

static void mxl862xx_phylink_mac_config(struct phylink_config *config,
					unsigned int mode,
					const struct phylink_link_state *state)
{
}

static void mxl862xx_phylink_mac_link_down(struct phylink_config *config,
					   unsigned int mode,
					   phy_interface_t interface)
{
}

static void mxl862xx_phylink_mac_link_up(struct phylink_config *config,
					 struct phy_device *phydev,
					 unsigned int mode,
					 phy_interface_t interface,
					 int speed, int duplex,
					 bool tx_pause, bool rx_pause)
{
}

const struct phylink_mac_ops mxl862xx_phylink_mac_ops = {
	.mac_config = mxl862xx_phylink_mac_config,
	.mac_link_down = mxl862xx_phylink_mac_link_down,
	.mac_link_up = mxl862xx_phylink_mac_link_up,
	.mac_select_pcs = mxl862xx_phylink_mac_select_pcs,
};
