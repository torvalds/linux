/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2009 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

/*
 * Driver for PHY related operations via MCDI.
 */

#include "efx.h"
#include "phy.h"
#include "mcdi.h"
#include "mcdi_pcol.h"
#include "mdio_10g.h"

struct efx_mcdi_phy_cfg {
	u32 flags;
	u32 type;
	u32 supported_cap;
	u32 channel;
	u32 port;
	u32 stats_mask;
	u8 name[20];
	u32 media;
	u32 mmd_mask;
	u8 revision[20];
	u32 forced_cap;
};

static int
efx_mcdi_get_phy_cfg(struct efx_nic *efx, struct efx_mcdi_phy_cfg *cfg)
{
	u8 outbuf[MC_CMD_GET_PHY_CFG_OUT_LEN];
	size_t outlen;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_PHY_CFG_IN_LEN != 0);
	BUILD_BUG_ON(MC_CMD_GET_PHY_CFG_OUT_NAME_LEN != sizeof(cfg->name));

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_PHY_CFG, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	if (outlen < MC_CMD_GET_PHY_CFG_OUT_LEN) {
		rc = -EMSGSIZE;
		goto fail;
	}

	cfg->flags = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_FLAGS);
	cfg->type = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_TYPE);
	cfg->supported_cap =
		MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_SUPPORTED_CAP);
	cfg->channel = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_CHANNEL);
	cfg->port = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_PRT);
	cfg->stats_mask = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_STATS_MASK);
	memcpy(cfg->name, MCDI_PTR(outbuf, GET_PHY_CFG_OUT_NAME),
	       sizeof(cfg->name));
	cfg->media = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_MEDIA_TYPE);
	cfg->mmd_mask = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_MMD_MASK);
	memcpy(cfg->revision, MCDI_PTR(outbuf, GET_PHY_CFG_OUT_REVISION),
	       sizeof(cfg->revision));

	return 0;

fail:
	EFX_ERR(efx, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

static int efx_mcdi_set_link(struct efx_nic *efx, u32 capabilities,
			     u32 flags, u32 loopback_mode,
			     u32 loopback_speed)
{
	u8 inbuf[MC_CMD_SET_LINK_IN_LEN];
	int rc;

	BUILD_BUG_ON(MC_CMD_SET_LINK_OUT_LEN != 0);

	MCDI_SET_DWORD(inbuf, SET_LINK_IN_CAP, capabilities);
	MCDI_SET_DWORD(inbuf, SET_LINK_IN_FLAGS, flags);
	MCDI_SET_DWORD(inbuf, SET_LINK_IN_LOOPBACK_MODE, loopback_mode);
	MCDI_SET_DWORD(inbuf, SET_LINK_IN_LOOPBACK_SPEED, loopback_speed);

	rc = efx_mcdi_rpc(efx, MC_CMD_SET_LINK, inbuf, sizeof(inbuf),
			  NULL, 0, NULL);
	if (rc)
		goto fail;

	return 0;

fail:
	EFX_ERR(efx, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

static int efx_mcdi_loopback_modes(struct efx_nic *efx, u64 *loopback_modes)
{
	u8 outbuf[MC_CMD_GET_LOOPBACK_MODES_OUT_LEN];
	size_t outlen;
	int rc;

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LOOPBACK_MODES, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	if (outlen < MC_CMD_GET_LOOPBACK_MODES_OUT_LEN) {
		rc = -EMSGSIZE;
		goto fail;
	}

	*loopback_modes = MCDI_QWORD(outbuf, GET_LOOPBACK_MODES_SUGGESTED);

	return 0;

fail:
	EFX_ERR(efx, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

int efx_mcdi_mdio_read(struct efx_nic *efx, unsigned int bus,
			 unsigned int prtad, unsigned int devad, u16 addr,
			 u16 *value_out, u32 *status_out)
{
	u8 inbuf[MC_CMD_MDIO_READ_IN_LEN];
	u8 outbuf[MC_CMD_MDIO_READ_OUT_LEN];
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, MDIO_READ_IN_BUS, bus);
	MCDI_SET_DWORD(inbuf, MDIO_READ_IN_PRTAD, prtad);
	MCDI_SET_DWORD(inbuf, MDIO_READ_IN_DEVAD, devad);
	MCDI_SET_DWORD(inbuf, MDIO_READ_IN_ADDR, addr);

	rc = efx_mcdi_rpc(efx, MC_CMD_MDIO_READ, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	*value_out = (u16)MCDI_DWORD(outbuf, MDIO_READ_OUT_VALUE);
	*status_out = MCDI_DWORD(outbuf, MDIO_READ_OUT_STATUS);
	return 0;

fail:
	EFX_ERR(efx, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

int efx_mcdi_mdio_write(struct efx_nic *efx, unsigned int bus,
			  unsigned int prtad, unsigned int devad, u16 addr,
			  u16 value, u32 *status_out)
{
	u8 inbuf[MC_CMD_MDIO_WRITE_IN_LEN];
	u8 outbuf[MC_CMD_MDIO_WRITE_OUT_LEN];
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, MDIO_WRITE_IN_BUS, bus);
	MCDI_SET_DWORD(inbuf, MDIO_WRITE_IN_PRTAD, prtad);
	MCDI_SET_DWORD(inbuf, MDIO_WRITE_IN_DEVAD, devad);
	MCDI_SET_DWORD(inbuf, MDIO_WRITE_IN_ADDR, addr);
	MCDI_SET_DWORD(inbuf, MDIO_WRITE_IN_VALUE, value);

	rc = efx_mcdi_rpc(efx, MC_CMD_MDIO_WRITE, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	*status_out = MCDI_DWORD(outbuf, MDIO_WRITE_OUT_STATUS);
	return 0;

fail:
	EFX_ERR(efx, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

static u32 mcdi_to_ethtool_cap(u32 media, u32 cap)
{
	u32 result = 0;

	switch (media) {
	case MC_CMD_MEDIA_KX4:
		result |= SUPPORTED_Backplane;
		if (cap & (1 << MC_CMD_PHY_CAP_1000FDX_LBN))
			result |= SUPPORTED_1000baseKX_Full;
		if (cap & (1 << MC_CMD_PHY_CAP_10000FDX_LBN))
			result |= SUPPORTED_10000baseKX4_Full;
		break;

	case MC_CMD_MEDIA_XFP:
	case MC_CMD_MEDIA_SFP_PLUS:
		result |= SUPPORTED_FIBRE;
		break;

	case MC_CMD_MEDIA_BASE_T:
		result |= SUPPORTED_TP;
		if (cap & (1 << MC_CMD_PHY_CAP_10HDX_LBN))
			result |= SUPPORTED_10baseT_Half;
		if (cap & (1 << MC_CMD_PHY_CAP_10FDX_LBN))
			result |= SUPPORTED_10baseT_Full;
		if (cap & (1 << MC_CMD_PHY_CAP_100HDX_LBN))
			result |= SUPPORTED_100baseT_Half;
		if (cap & (1 << MC_CMD_PHY_CAP_100FDX_LBN))
			result |= SUPPORTED_100baseT_Full;
		if (cap & (1 << MC_CMD_PHY_CAP_1000HDX_LBN))
			result |= SUPPORTED_1000baseT_Half;
		if (cap & (1 << MC_CMD_PHY_CAP_1000FDX_LBN))
			result |= SUPPORTED_1000baseT_Full;
		if (cap & (1 << MC_CMD_PHY_CAP_10000FDX_LBN))
			result |= SUPPORTED_10000baseT_Full;
		break;
	}

	if (cap & (1 << MC_CMD_PHY_CAP_PAUSE_LBN))
		result |= SUPPORTED_Pause;
	if (cap & (1 << MC_CMD_PHY_CAP_ASYM_LBN))
		result |= SUPPORTED_Asym_Pause;
	if (cap & (1 << MC_CMD_PHY_CAP_AN_LBN))
		result |= SUPPORTED_Autoneg;

	return result;
}

static u32 ethtool_to_mcdi_cap(u32 cap)
{
	u32 result = 0;

	if (cap & SUPPORTED_10baseT_Half)
		result |= (1 << MC_CMD_PHY_CAP_10HDX_LBN);
	if (cap & SUPPORTED_10baseT_Full)
		result |= (1 << MC_CMD_PHY_CAP_10FDX_LBN);
	if (cap & SUPPORTED_100baseT_Half)
		result |= (1 << MC_CMD_PHY_CAP_100HDX_LBN);
	if (cap & SUPPORTED_100baseT_Full)
		result |= (1 << MC_CMD_PHY_CAP_100FDX_LBN);
	if (cap & SUPPORTED_1000baseT_Half)
		result |= (1 << MC_CMD_PHY_CAP_1000HDX_LBN);
	if (cap & (SUPPORTED_1000baseT_Full | SUPPORTED_1000baseKX_Full))
		result |= (1 << MC_CMD_PHY_CAP_1000FDX_LBN);
	if (cap & (SUPPORTED_10000baseT_Full | SUPPORTED_10000baseKX4_Full))
		result |= (1 << MC_CMD_PHY_CAP_10000FDX_LBN);
	if (cap & SUPPORTED_Pause)
		result |= (1 << MC_CMD_PHY_CAP_PAUSE_LBN);
	if (cap & SUPPORTED_Asym_Pause)
		result |= (1 << MC_CMD_PHY_CAP_ASYM_LBN);
	if (cap & SUPPORTED_Autoneg)
		result |= (1 << MC_CMD_PHY_CAP_AN_LBN);

	return result;
}

static u32 efx_get_mcdi_phy_flags(struct efx_nic *efx)
{
	struct efx_mcdi_phy_cfg *phy_cfg = efx->phy_data;
	enum efx_phy_mode mode, supported;
	u32 flags;

	/* TODO: Advertise the capabilities supported by this PHY */
	supported = 0;
	if (phy_cfg->flags & (1 << MC_CMD_GET_PHY_CFG_TXDIS_LBN))
		supported |= PHY_MODE_TX_DISABLED;
	if (phy_cfg->flags & (1 << MC_CMD_GET_PHY_CFG_LOWPOWER_LBN))
		supported |= PHY_MODE_LOW_POWER;
	if (phy_cfg->flags & (1 << MC_CMD_GET_PHY_CFG_POWEROFF_LBN))
		supported |= PHY_MODE_OFF;

	mode = efx->phy_mode & supported;

	flags = 0;
	if (mode & PHY_MODE_TX_DISABLED)
		flags |= (1 << MC_CMD_SET_LINK_TXDIS_LBN);
	if (mode & PHY_MODE_LOW_POWER)
		flags |= (1 << MC_CMD_SET_LINK_LOWPOWER_LBN);
	if (mode & PHY_MODE_OFF)
		flags |= (1 << MC_CMD_SET_LINK_POWEROFF_LBN);

	return flags;
}

static u32 mcdi_to_ethtool_media(u32 media)
{
	switch (media) {
	case MC_CMD_MEDIA_XAUI:
	case MC_CMD_MEDIA_CX4:
	case MC_CMD_MEDIA_KX4:
		return PORT_OTHER;

	case MC_CMD_MEDIA_XFP:
	case MC_CMD_MEDIA_SFP_PLUS:
		return PORT_FIBRE;

	case MC_CMD_MEDIA_BASE_T:
		return PORT_TP;

	default:
		return PORT_OTHER;
	}
}

static int efx_mcdi_phy_probe(struct efx_nic *efx)
{
	struct efx_mcdi_phy_cfg *phy_data;
	u8 outbuf[MC_CMD_GET_LINK_OUT_LEN];
	u32 caps;
	int rc;

	/* Initialise and populate phy_data */
	phy_data = kzalloc(sizeof(*phy_data), GFP_KERNEL);
	if (phy_data == NULL)
		return -ENOMEM;

	rc = efx_mcdi_get_phy_cfg(efx, phy_data);
	if (rc != 0)
		goto fail;

	/* Read initial link advertisement */
	BUILD_BUG_ON(MC_CMD_GET_LINK_IN_LEN != 0);
	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LINK, NULL, 0,
			  outbuf, sizeof(outbuf), NULL);
	if (rc)
		goto fail;

	/* Fill out nic state */
	efx->phy_data = phy_data;
	efx->phy_type = phy_data->type;

	efx->mdio_bus = phy_data->channel;
	efx->mdio.prtad = phy_data->port;
	efx->mdio.mmds = phy_data->mmd_mask & ~(1 << MC_CMD_MMD_CLAUSE22);
	efx->mdio.mode_support = 0;
	if (phy_data->mmd_mask & (1 << MC_CMD_MMD_CLAUSE22))
		efx->mdio.mode_support |= MDIO_SUPPORTS_C22;
	if (phy_data->mmd_mask & ~(1 << MC_CMD_MMD_CLAUSE22))
		efx->mdio.mode_support |= MDIO_SUPPORTS_C45 | MDIO_EMULATE_C22;

	caps = MCDI_DWORD(outbuf, GET_LINK_OUT_CAP);
	if (caps & (1 << MC_CMD_PHY_CAP_AN_LBN))
		efx->link_advertising =
			mcdi_to_ethtool_cap(phy_data->media, caps);
	else
		phy_data->forced_cap = caps;

	/* Assert that we can map efx -> mcdi loopback modes */
	BUILD_BUG_ON(LOOPBACK_NONE != MC_CMD_LOOPBACK_NONE);
	BUILD_BUG_ON(LOOPBACK_DATA != MC_CMD_LOOPBACK_DATA);
	BUILD_BUG_ON(LOOPBACK_GMAC != MC_CMD_LOOPBACK_GMAC);
	BUILD_BUG_ON(LOOPBACK_XGMII != MC_CMD_LOOPBACK_XGMII);
	BUILD_BUG_ON(LOOPBACK_XGXS != MC_CMD_LOOPBACK_XGXS);
	BUILD_BUG_ON(LOOPBACK_XAUI != MC_CMD_LOOPBACK_XAUI);
	BUILD_BUG_ON(LOOPBACK_GMII != MC_CMD_LOOPBACK_GMII);
	BUILD_BUG_ON(LOOPBACK_SGMII != MC_CMD_LOOPBACK_SGMII);
	BUILD_BUG_ON(LOOPBACK_XGBR != MC_CMD_LOOPBACK_XGBR);
	BUILD_BUG_ON(LOOPBACK_XFI != MC_CMD_LOOPBACK_XFI);
	BUILD_BUG_ON(LOOPBACK_XAUI_FAR != MC_CMD_LOOPBACK_XAUI_FAR);
	BUILD_BUG_ON(LOOPBACK_GMII_FAR != MC_CMD_LOOPBACK_GMII_FAR);
	BUILD_BUG_ON(LOOPBACK_SGMII_FAR != MC_CMD_LOOPBACK_SGMII_FAR);
	BUILD_BUG_ON(LOOPBACK_XFI_FAR != MC_CMD_LOOPBACK_XFI_FAR);
	BUILD_BUG_ON(LOOPBACK_GPHY != MC_CMD_LOOPBACK_GPHY);
	BUILD_BUG_ON(LOOPBACK_PHYXS != MC_CMD_LOOPBACK_PHYXS);
	BUILD_BUG_ON(LOOPBACK_PCS != MC_CMD_LOOPBACK_PCS);
	BUILD_BUG_ON(LOOPBACK_PMAPMD != MC_CMD_LOOPBACK_PMAPMD);
	BUILD_BUG_ON(LOOPBACK_XPORT != MC_CMD_LOOPBACK_XPORT);
	BUILD_BUG_ON(LOOPBACK_XGMII_WS != MC_CMD_LOOPBACK_XGMII_WS);
	BUILD_BUG_ON(LOOPBACK_XAUI_WS != MC_CMD_LOOPBACK_XAUI_WS);
	BUILD_BUG_ON(LOOPBACK_XAUI_WS_FAR != MC_CMD_LOOPBACK_XAUI_WS_FAR);
	BUILD_BUG_ON(LOOPBACK_XAUI_WS_NEAR != MC_CMD_LOOPBACK_XAUI_WS_NEAR);
	BUILD_BUG_ON(LOOPBACK_GMII_WS != MC_CMD_LOOPBACK_GMII_WS);
	BUILD_BUG_ON(LOOPBACK_XFI_WS != MC_CMD_LOOPBACK_XFI_WS);
	BUILD_BUG_ON(LOOPBACK_XFI_WS_FAR != MC_CMD_LOOPBACK_XFI_WS_FAR);
	BUILD_BUG_ON(LOOPBACK_PHYXS_WS != MC_CMD_LOOPBACK_PHYXS_WS);

	rc = efx_mcdi_loopback_modes(efx, &efx->loopback_modes);
	if (rc != 0)
		goto fail;
	/* The MC indicates that LOOPBACK_NONE is a valid loopback mode,
	 * but by convention we don't */
	efx->loopback_modes &= ~(1 << LOOPBACK_NONE);

	/* Set the initial link mode */
	efx_mcdi_phy_decode_link(
		efx, &efx->link_state,
		MCDI_DWORD(outbuf, GET_LINK_OUT_LINK_SPEED),
		MCDI_DWORD(outbuf, GET_LINK_OUT_FLAGS),
		MCDI_DWORD(outbuf, GET_LINK_OUT_FCNTL));

	/* Default to Autonegotiated flow control if the PHY supports it */
	efx->wanted_fc = EFX_FC_RX | EFX_FC_TX;
	if (phy_data->supported_cap & (1 << MC_CMD_PHY_CAP_AN_LBN))
		efx->wanted_fc |= EFX_FC_AUTO;

	return 0;

fail:
	kfree(phy_data);
	return rc;
}

int efx_mcdi_phy_reconfigure(struct efx_nic *efx)
{
	struct efx_mcdi_phy_cfg *phy_cfg = efx->phy_data;
	u32 caps = (efx->link_advertising ?
		    ethtool_to_mcdi_cap(efx->link_advertising) :
		    phy_cfg->forced_cap);

	return efx_mcdi_set_link(efx, caps, efx_get_mcdi_phy_flags(efx),
				 efx->loopback_mode, 0);
}

void efx_mcdi_phy_decode_link(struct efx_nic *efx,
			      struct efx_link_state *link_state,
			      u32 speed, u32 flags, u32 fcntl)
{
	switch (fcntl) {
	case MC_CMD_FCNTL_AUTO:
		WARN_ON(1);	/* This is not a link mode */
		link_state->fc = EFX_FC_AUTO | EFX_FC_TX | EFX_FC_RX;
		break;
	case MC_CMD_FCNTL_BIDIR:
		link_state->fc = EFX_FC_TX | EFX_FC_RX;
		break;
	case MC_CMD_FCNTL_RESPOND:
		link_state->fc = EFX_FC_RX;
		break;
	default:
		WARN_ON(1);
	case MC_CMD_FCNTL_OFF:
		link_state->fc = 0;
		break;
	}

	link_state->up = !!(flags & (1 << MC_CMD_GET_LINK_LINK_UP_LBN));
	link_state->fd = !!(flags & (1 << MC_CMD_GET_LINK_FULL_DUPLEX_LBN));
	link_state->speed = speed;
}

/* Verify that the forced flow control settings (!EFX_FC_AUTO) are
 * supported by the link partner. Warn the user if this isn't the case
 */
void efx_mcdi_phy_check_fcntl(struct efx_nic *efx, u32 lpa)
{
	struct efx_mcdi_phy_cfg *phy_cfg = efx->phy_data;
	u32 rmtadv;

	/* The link partner capabilities are only relevent if the
	 * link supports flow control autonegotiation */
	if (~phy_cfg->supported_cap & (1 << MC_CMD_PHY_CAP_AN_LBN))
		return;

	/* If flow control autoneg is supported and enabled, then fine */
	if (efx->wanted_fc & EFX_FC_AUTO)
		return;

	rmtadv = 0;
	if (lpa & (1 << MC_CMD_PHY_CAP_PAUSE_LBN))
		rmtadv |= ADVERTISED_Pause;
	if (lpa & (1 << MC_CMD_PHY_CAP_ASYM_LBN))
		rmtadv |=  ADVERTISED_Asym_Pause;

	if ((efx->wanted_fc & EFX_FC_TX) && rmtadv == ADVERTISED_Asym_Pause)
		EFX_ERR(efx, "warning: link partner doesn't support "
			"pause frames");
}

static bool efx_mcdi_phy_poll(struct efx_nic *efx)
{
	struct efx_link_state old_state = efx->link_state;
	u8 outbuf[MC_CMD_GET_LINK_OUT_LEN];
	int rc;

	WARN_ON(!mutex_is_locked(&efx->mac_lock));

	BUILD_BUG_ON(MC_CMD_GET_LINK_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LINK, NULL, 0,
			  outbuf, sizeof(outbuf), NULL);
	if (rc) {
		EFX_ERR(efx, "%s: failed rc=%d\n", __func__, rc);
		efx->link_state.up = false;
	} else {
		efx_mcdi_phy_decode_link(
			efx, &efx->link_state,
			MCDI_DWORD(outbuf, GET_LINK_OUT_LINK_SPEED),
			MCDI_DWORD(outbuf, GET_LINK_OUT_FLAGS),
			MCDI_DWORD(outbuf, GET_LINK_OUT_FCNTL));
	}

	return !efx_link_state_equal(&efx->link_state, &old_state);
}

static void efx_mcdi_phy_remove(struct efx_nic *efx)
{
	struct efx_mcdi_phy_data *phy_data = efx->phy_data;

	efx->phy_data = NULL;
	kfree(phy_data);
}

static void efx_mcdi_phy_get_settings(struct efx_nic *efx, struct ethtool_cmd *ecmd)
{
	struct efx_mcdi_phy_cfg *phy_cfg = efx->phy_data;
	u8 outbuf[MC_CMD_GET_LINK_OUT_LEN];
	int rc;

	ecmd->supported =
		mcdi_to_ethtool_cap(phy_cfg->media, phy_cfg->supported_cap);
	ecmd->advertising = efx->link_advertising;
	ecmd->speed = efx->link_state.speed;
	ecmd->duplex = efx->link_state.fd;
	ecmd->port = mcdi_to_ethtool_media(phy_cfg->media);
	ecmd->phy_address = phy_cfg->port;
	ecmd->transceiver = XCVR_INTERNAL;
	ecmd->autoneg = !!(efx->link_advertising & ADVERTISED_Autoneg);
	ecmd->mdio_support = (efx->mdio.mode_support &
			      (MDIO_SUPPORTS_C45 | MDIO_SUPPORTS_C22));

	BUILD_BUG_ON(MC_CMD_GET_LINK_IN_LEN != 0);
	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LINK, NULL, 0,
			  outbuf, sizeof(outbuf), NULL);
	if (rc) {
		EFX_ERR(efx, "%s: failed rc=%d\n", __func__, rc);
		return;
	}
	ecmd->lp_advertising =
		mcdi_to_ethtool_cap(phy_cfg->media,
				    MCDI_DWORD(outbuf, GET_LINK_OUT_LP_CAP));
}

static int efx_mcdi_phy_set_settings(struct efx_nic *efx, struct ethtool_cmd *ecmd)
{
	struct efx_mcdi_phy_cfg *phy_cfg = efx->phy_data;
	u32 caps;
	int rc;

	if (ecmd->autoneg) {
		caps = (ethtool_to_mcdi_cap(ecmd->advertising) |
			 1 << MC_CMD_PHY_CAP_AN_LBN);
	} else if (ecmd->duplex) {
		switch (ecmd->speed) {
		case 10:    caps = 1 << MC_CMD_PHY_CAP_10FDX_LBN;    break;
		case 100:   caps = 1 << MC_CMD_PHY_CAP_100FDX_LBN;   break;
		case 1000:  caps = 1 << MC_CMD_PHY_CAP_1000FDX_LBN;  break;
		case 10000: caps = 1 << MC_CMD_PHY_CAP_10000FDX_LBN; break;
		default:    return -EINVAL;
		}
	} else {
		switch (ecmd->speed) {
		case 10:    caps = 1 << MC_CMD_PHY_CAP_10HDX_LBN;    break;
		case 100:   caps = 1 << MC_CMD_PHY_CAP_100HDX_LBN;   break;
		case 1000:  caps = 1 << MC_CMD_PHY_CAP_1000HDX_LBN;  break;
		default:    return -EINVAL;
		}
	}

	rc = efx_mcdi_set_link(efx, caps, efx_get_mcdi_phy_flags(efx),
			       efx->loopback_mode, 0);
	if (rc)
		return rc;

	if (ecmd->autoneg) {
		efx_link_set_advertising(
			efx, ecmd->advertising | ADVERTISED_Autoneg);
		phy_cfg->forced_cap = 0;
	} else {
		efx_link_set_advertising(efx, 0);
		phy_cfg->forced_cap = caps;
	}
	return 0;
}

static int efx_mcdi_phy_test_alive(struct efx_nic *efx)
{
	u8 outbuf[MC_CMD_GET_PHY_STATE_OUT_LEN];
	size_t outlen;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_PHY_STATE_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_PHY_STATE, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;

	if (outlen < MC_CMD_GET_PHY_STATE_OUT_LEN)
		return -EMSGSIZE;
	if (MCDI_DWORD(outbuf, GET_PHY_STATE_STATE) != MC_CMD_PHY_STATE_OK)
		return -EINVAL;

	return 0;
}

struct efx_phy_operations efx_mcdi_phy_ops = {
	.probe		= efx_mcdi_phy_probe,
	.init 	 	= efx_port_dummy_op_int,
	.reconfigure	= efx_mcdi_phy_reconfigure,
	.poll		= efx_mcdi_phy_poll,
	.fini		= efx_port_dummy_op_void,
	.remove		= efx_mcdi_phy_remove,
	.get_settings	= efx_mcdi_phy_get_settings,
	.set_settings	= efx_mcdi_phy_set_settings,
	.test_alive	= efx_mcdi_phy_test_alive,
	.run_tests	= NULL,
	.test_name	= NULL,
};
