// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "mcdi_port_common.h"

int efx_mcdi_get_phy_cfg(struct efx_nic *efx, struct efx_mcdi_phy_data *cfg)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_PHY_CFG_OUT_LEN);
	size_t outlen;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_PHY_CFG_IN_LEN != 0);
	BUILD_BUG_ON(MC_CMD_GET_PHY_CFG_OUT_NAME_LEN != sizeof(cfg->name));

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_PHY_CFG, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	if (outlen < MC_CMD_GET_PHY_CFG_OUT_LEN) {
		rc = -EIO;
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
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

void efx_link_set_advertising(struct efx_nic *efx,
			      const unsigned long *advertising)
{
	memcpy(efx->link_advertising, advertising,
	       sizeof(__ETHTOOL_DECLARE_LINK_MODE_MASK()));

	efx->link_advertising[0] |= ADVERTISED_Autoneg;
	if (advertising[0] & ADVERTISED_Pause)
		efx->wanted_fc |= (EFX_FC_TX | EFX_FC_RX);
	else
		efx->wanted_fc &= ~(EFX_FC_TX | EFX_FC_RX);
	if (advertising[0] & ADVERTISED_Asym_Pause)
		efx->wanted_fc ^= EFX_FC_TX;
}

int efx_mcdi_set_link(struct efx_nic *efx, u32 capabilities,
		      u32 flags, u32 loopback_mode, u32 loopback_speed)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_SET_LINK_IN_LEN);
	int rc;

	BUILD_BUG_ON(MC_CMD_SET_LINK_OUT_LEN != 0);

	MCDI_SET_DWORD(inbuf, SET_LINK_IN_CAP, capabilities);
	MCDI_SET_DWORD(inbuf, SET_LINK_IN_FLAGS, flags);
	MCDI_SET_DWORD(inbuf, SET_LINK_IN_LOOPBACK_MODE, loopback_mode);
	MCDI_SET_DWORD(inbuf, SET_LINK_IN_LOOPBACK_SPEED, loopback_speed);

	rc = efx_mcdi_rpc(efx, MC_CMD_SET_LINK, inbuf, sizeof(inbuf),
			  NULL, 0, NULL);
	return rc;
}

int efx_mcdi_loopback_modes(struct efx_nic *efx, u64 *loopback_modes)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_LOOPBACK_MODES_OUT_LEN);
	size_t outlen;
	int rc;

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LOOPBACK_MODES, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	if (outlen < (MC_CMD_GET_LOOPBACK_MODES_OUT_SUGGESTED_OFST +
		      MC_CMD_GET_LOOPBACK_MODES_OUT_SUGGESTED_LEN)) {
		rc = -EIO;
		goto fail;
	}

	*loopback_modes = MCDI_QWORD(outbuf, GET_LOOPBACK_MODES_OUT_SUGGESTED);

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

void mcdi_to_ethtool_linkset(u32 media, u32 cap, unsigned long *linkset)
{
	#define SET_BIT(name)	__set_bit(ETHTOOL_LINK_MODE_ ## name ## _BIT, \
					  linkset)

	bitmap_zero(linkset, __ETHTOOL_LINK_MODE_MASK_NBITS);
	switch (media) {
	case MC_CMD_MEDIA_KX4:
		SET_BIT(Backplane);
		if (cap & (1 << MC_CMD_PHY_CAP_1000FDX_LBN))
			SET_BIT(1000baseKX_Full);
		if (cap & (1 << MC_CMD_PHY_CAP_10000FDX_LBN))
			SET_BIT(10000baseKX4_Full);
		if (cap & (1 << MC_CMD_PHY_CAP_40000FDX_LBN))
			SET_BIT(40000baseKR4_Full);
		break;

	case MC_CMD_MEDIA_XFP:
	case MC_CMD_MEDIA_SFP_PLUS:
	case MC_CMD_MEDIA_QSFP_PLUS:
		SET_BIT(FIBRE);
		if (cap & (1 << MC_CMD_PHY_CAP_1000FDX_LBN))
			SET_BIT(1000baseT_Full);
		if (cap & (1 << MC_CMD_PHY_CAP_10000FDX_LBN))
			SET_BIT(10000baseT_Full);
		if (cap & (1 << MC_CMD_PHY_CAP_40000FDX_LBN))
			SET_BIT(40000baseCR4_Full);
		if (cap & (1 << MC_CMD_PHY_CAP_100000FDX_LBN))
			SET_BIT(100000baseCR4_Full);
		if (cap & (1 << MC_CMD_PHY_CAP_25000FDX_LBN))
			SET_BIT(25000baseCR_Full);
		if (cap & (1 << MC_CMD_PHY_CAP_50000FDX_LBN))
			SET_BIT(50000baseCR2_Full);
		break;

	case MC_CMD_MEDIA_BASE_T:
		SET_BIT(TP);
		if (cap & (1 << MC_CMD_PHY_CAP_10HDX_LBN))
			SET_BIT(10baseT_Half);
		if (cap & (1 << MC_CMD_PHY_CAP_10FDX_LBN))
			SET_BIT(10baseT_Full);
		if (cap & (1 << MC_CMD_PHY_CAP_100HDX_LBN))
			SET_BIT(100baseT_Half);
		if (cap & (1 << MC_CMD_PHY_CAP_100FDX_LBN))
			SET_BIT(100baseT_Full);
		if (cap & (1 << MC_CMD_PHY_CAP_1000HDX_LBN))
			SET_BIT(1000baseT_Half);
		if (cap & (1 << MC_CMD_PHY_CAP_1000FDX_LBN))
			SET_BIT(1000baseT_Full);
		if (cap & (1 << MC_CMD_PHY_CAP_10000FDX_LBN))
			SET_BIT(10000baseT_Full);
		break;
	}

	if (cap & (1 << MC_CMD_PHY_CAP_PAUSE_LBN))
		SET_BIT(Pause);
	if (cap & (1 << MC_CMD_PHY_CAP_ASYM_LBN))
		SET_BIT(Asym_Pause);
	if (cap & (1 << MC_CMD_PHY_CAP_AN_LBN))
		SET_BIT(Autoneg);

	#undef SET_BIT
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
		/* Fall through */
	case MC_CMD_FCNTL_OFF:
		link_state->fc = 0;
		break;
	}

	link_state->up = !!(flags & (1 << MC_CMD_GET_LINK_OUT_LINK_UP_LBN));
	link_state->fd = !!(flags & (1 << MC_CMD_GET_LINK_OUT_FULL_DUPLEX_LBN));
	link_state->speed = speed;
}
