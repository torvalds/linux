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

u32 ethtool_linkset_to_mcdi_cap(const unsigned long *linkset)
{
	u32 result = 0;

	#define TEST_BIT(name)	test_bit(ETHTOOL_LINK_MODE_ ## name ## _BIT, \
					 linkset)

	if (TEST_BIT(10baseT_Half))
		result |= (1 << MC_CMD_PHY_CAP_10HDX_LBN);
	if (TEST_BIT(10baseT_Full))
		result |= (1 << MC_CMD_PHY_CAP_10FDX_LBN);
	if (TEST_BIT(100baseT_Half))
		result |= (1 << MC_CMD_PHY_CAP_100HDX_LBN);
	if (TEST_BIT(100baseT_Full))
		result |= (1 << MC_CMD_PHY_CAP_100FDX_LBN);
	if (TEST_BIT(1000baseT_Half))
		result |= (1 << MC_CMD_PHY_CAP_1000HDX_LBN);
	if (TEST_BIT(1000baseT_Full) || TEST_BIT(1000baseKX_Full))
		result |= (1 << MC_CMD_PHY_CAP_1000FDX_LBN);
	if (TEST_BIT(10000baseT_Full) || TEST_BIT(10000baseKX4_Full))
		result |= (1 << MC_CMD_PHY_CAP_10000FDX_LBN);
	if (TEST_BIT(40000baseCR4_Full) || TEST_BIT(40000baseKR4_Full))
		result |= (1 << MC_CMD_PHY_CAP_40000FDX_LBN);
	if (TEST_BIT(100000baseCR4_Full))
		result |= (1 << MC_CMD_PHY_CAP_100000FDX_LBN);
	if (TEST_BIT(25000baseCR_Full))
		result |= (1 << MC_CMD_PHY_CAP_25000FDX_LBN);
	if (TEST_BIT(50000baseCR2_Full))
		result |= (1 << MC_CMD_PHY_CAP_50000FDX_LBN);
	if (TEST_BIT(Pause))
		result |= (1 << MC_CMD_PHY_CAP_PAUSE_LBN);
	if (TEST_BIT(Asym_Pause))
		result |= (1 << MC_CMD_PHY_CAP_ASYM_LBN);
	if (TEST_BIT(Autoneg))
		result |= (1 << MC_CMD_PHY_CAP_AN_LBN);

	#undef TEST_BIT

	return result;
}

u32 efx_get_mcdi_phy_flags(struct efx_nic *efx)
{
	struct efx_mcdi_phy_data *phy_cfg = efx->phy_data;
	enum efx_phy_mode mode, supported;
	u32 flags;

	/* TODO: Advertise the capabilities supported by this PHY */
	supported = 0;
	if (phy_cfg->flags & (1 << MC_CMD_GET_PHY_CFG_OUT_TXDIS_LBN))
		supported |= PHY_MODE_TX_DISABLED;
	if (phy_cfg->flags & (1 << MC_CMD_GET_PHY_CFG_OUT_LOWPOWER_LBN))
		supported |= PHY_MODE_LOW_POWER;
	if (phy_cfg->flags & (1 << MC_CMD_GET_PHY_CFG_OUT_POWEROFF_LBN))
		supported |= PHY_MODE_OFF;

	mode = efx->phy_mode & supported;

	flags = 0;
	if (mode & PHY_MODE_TX_DISABLED)
		flags |= (1 << MC_CMD_SET_LINK_IN_TXDIS_LBN);
	if (mode & PHY_MODE_LOW_POWER)
		flags |= (1 << MC_CMD_SET_LINK_IN_LOWPOWER_LBN);
	if (mode & PHY_MODE_OFF)
		flags |= (1 << MC_CMD_SET_LINK_IN_POWEROFF_LBN);

	return flags;
}

u8 mcdi_to_ethtool_media(u32 media)
{
	switch (media) {
	case MC_CMD_MEDIA_XAUI:
	case MC_CMD_MEDIA_CX4:
	case MC_CMD_MEDIA_KX4:
		return PORT_OTHER;

	case MC_CMD_MEDIA_XFP:
	case MC_CMD_MEDIA_SFP_PLUS:
	case MC_CMD_MEDIA_QSFP_PLUS:
		return PORT_FIBRE;

	case MC_CMD_MEDIA_BASE_T:
		return PORT_TP;

	default:
		return PORT_OTHER;
	}
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

/* The semantics of the ethtool FEC mode bitmask are not well defined,
 * particularly the meaning of combinations of bits.  Which means we get to
 * define our own semantics, as follows:
 * OFF overrides any other bits, and means "disable all FEC" (with the
 * exception of 25G KR4/CR4, where it is not possible to reject it if AN
 * partner requests it).
 * AUTO on its own means use cable requirements and link partner autoneg with
 * fw-default preferences for the cable type.
 * AUTO and either RS or BASER means use the specified FEC type if cable and
 * link partner support it, otherwise autoneg/fw-default.
 * RS or BASER alone means use the specified FEC type if cable and link partner
 * support it and either requests it, otherwise no FEC.
 * Both RS and BASER (whether AUTO or not) means use FEC if cable and link
 * partner support it, preferring RS to BASER.
 */
u32 ethtool_fec_caps_to_mcdi(u32 ethtool_cap)
{
	u32 ret = 0;

	if (ethtool_cap & ETHTOOL_FEC_OFF)
		return 0;

	if (ethtool_cap & ETHTOOL_FEC_AUTO)
		ret |= (1 << MC_CMD_PHY_CAP_BASER_FEC_LBN) |
		       (1 << MC_CMD_PHY_CAP_25G_BASER_FEC_LBN) |
		       (1 << MC_CMD_PHY_CAP_RS_FEC_LBN);
	if (ethtool_cap & ETHTOOL_FEC_RS)
		ret |= (1 << MC_CMD_PHY_CAP_RS_FEC_LBN) |
		       (1 << MC_CMD_PHY_CAP_RS_FEC_REQUESTED_LBN);
	if (ethtool_cap & ETHTOOL_FEC_BASER)
		ret |= (1 << MC_CMD_PHY_CAP_BASER_FEC_LBN) |
		       (1 << MC_CMD_PHY_CAP_25G_BASER_FEC_LBN) |
		       (1 << MC_CMD_PHY_CAP_BASER_FEC_REQUESTED_LBN) |
		       (1 << MC_CMD_PHY_CAP_25G_BASER_FEC_REQUESTED_LBN);
	return ret;
}

/* Invert ethtool_fec_caps_to_mcdi.  There are two combinations that function
 * can never produce, (baser xor rs) and neither req; the implementation below
 * maps both of those to AUTO.  This should never matter, and it's not clear
 * what a better mapping would be anyway.
 */
u32 mcdi_fec_caps_to_ethtool(u32 caps, bool is_25g)
{
	bool rs = caps & (1 << MC_CMD_PHY_CAP_RS_FEC_LBN),
	     rs_req = caps & (1 << MC_CMD_PHY_CAP_RS_FEC_REQUESTED_LBN),
	     baser = is_25g ? caps & (1 << MC_CMD_PHY_CAP_25G_BASER_FEC_LBN)
			    : caps & (1 << MC_CMD_PHY_CAP_BASER_FEC_LBN),
	     baser_req = is_25g ? caps & (1 << MC_CMD_PHY_CAP_25G_BASER_FEC_REQUESTED_LBN)
				: caps & (1 << MC_CMD_PHY_CAP_BASER_FEC_REQUESTED_LBN);

	if (!baser && !rs)
		return ETHTOOL_FEC_OFF;
	return (rs_req ? ETHTOOL_FEC_RS : 0) |
	       (baser_req ? ETHTOOL_FEC_BASER : 0) |
	       (baser == baser_req && rs == rs_req ? 0 : ETHTOOL_FEC_AUTO);
}

/* Verify that the forced flow control settings (!EFX_FC_AUTO) are
 * supported by the link partner. Warn the user if this isn't the case
 */
void efx_mcdi_phy_check_fcntl(struct efx_nic *efx, u32 lpa)
{
	struct efx_mcdi_phy_data *phy_cfg = efx->phy_data;
	u32 rmtadv;

	/* The link partner capabilities are only relevant if the
	 * link supports flow control autonegotiation
	 */
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
		netif_err(efx, link, efx->net_dev,
			  "warning: link partner doesn't support pause frames");
}

bool efx_mcdi_phy_poll(struct efx_nic *efx)
{
	struct efx_link_state old_state = efx->link_state;
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_LINK_OUT_LEN);
	int rc;

	WARN_ON(!mutex_is_locked(&efx->mac_lock));

	BUILD_BUG_ON(MC_CMD_GET_LINK_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LINK, NULL, 0,
			  outbuf, sizeof(outbuf), NULL);
	if (rc)
		efx->link_state.up = false;
	else
		efx_mcdi_phy_decode_link(
			efx, &efx->link_state,
			MCDI_DWORD(outbuf, GET_LINK_OUT_LINK_SPEED),
			MCDI_DWORD(outbuf, GET_LINK_OUT_FLAGS),
			MCDI_DWORD(outbuf, GET_LINK_OUT_FCNTL));

	return !efx_link_state_equal(&efx->link_state, &old_state);
}

int efx_mcdi_phy_get_fecparam(struct efx_nic *efx, struct ethtool_fecparam *fec)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_LINK_OUT_V2_LEN);
	u32 caps, active, speed; /* MCDI format */
	bool is_25g = false;
	size_t outlen;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_LINK_IN_LEN != 0);
	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LINK, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < MC_CMD_GET_LINK_OUT_V2_LEN)
		return -EOPNOTSUPP;

	/* behaviour for 25G/50G links depends on 25G BASER bit */
	speed = MCDI_DWORD(outbuf, GET_LINK_OUT_V2_LINK_SPEED);
	is_25g = speed == 25000 || speed == 50000;

	caps = MCDI_DWORD(outbuf, GET_LINK_OUT_V2_CAP);
	fec->fec = mcdi_fec_caps_to_ethtool(caps, is_25g);
	/* BASER is never supported on 100G */
	if (speed == 100000)
		fec->fec &= ~ETHTOOL_FEC_BASER;

	active = MCDI_DWORD(outbuf, GET_LINK_OUT_V2_FEC_TYPE);
	switch (active) {
	case MC_CMD_FEC_NONE:
		fec->active_fec = ETHTOOL_FEC_OFF;
		break;
	case MC_CMD_FEC_BASER:
		fec->active_fec = ETHTOOL_FEC_BASER;
		break;
	case MC_CMD_FEC_RS:
		fec->active_fec = ETHTOOL_FEC_RS;
		break;
	default:
		netif_warn(efx, hw, efx->net_dev,
			   "Firmware reports unrecognised FEC_TYPE %u\n",
			   active);
		/* We don't know what firmware has picked.  AUTO is as good a
		 * "can't happen" value as any other.
		 */
		fec->active_fec = ETHTOOL_FEC_AUTO;
		break;
	}

	return 0;
}

int efx_mcdi_phy_test_alive(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_PHY_STATE_OUT_LEN);
	size_t outlen;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_PHY_STATE_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_PHY_STATE, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;

	if (outlen < MC_CMD_GET_PHY_STATE_OUT_LEN)
		return -EIO;
	if (MCDI_DWORD(outbuf, GET_PHY_STATE_OUT_STATE) != MC_CMD_PHY_STATE_OK)
		return -EINVAL;

	return 0;
}

int efx_mcdi_set_mac(struct efx_nic *efx)
{
	u32 fcntl;
	MCDI_DECLARE_BUF(cmdbytes, MC_CMD_SET_MAC_IN_LEN);

	BUILD_BUG_ON(MC_CMD_SET_MAC_OUT_LEN != 0);

	/* This has no effect on EF10 */
	ether_addr_copy(MCDI_PTR(cmdbytes, SET_MAC_IN_ADDR),
			efx->net_dev->dev_addr);

	MCDI_SET_DWORD(cmdbytes, SET_MAC_IN_MTU,
		       EFX_MAX_FRAME_LEN(efx->net_dev->mtu));
	MCDI_SET_DWORD(cmdbytes, SET_MAC_IN_DRAIN, 0);

	/* Set simple MAC filter for Siena */
	MCDI_POPULATE_DWORD_1(cmdbytes, SET_MAC_IN_REJECT,
			      SET_MAC_IN_REJECT_UNCST, efx->unicast_filter);

	MCDI_POPULATE_DWORD_1(cmdbytes, SET_MAC_IN_FLAGS,
			      SET_MAC_IN_FLAG_INCLUDE_FCS,
			      !!(efx->net_dev->features & NETIF_F_RXFCS));

	switch (efx->wanted_fc) {
	case EFX_FC_RX | EFX_FC_TX:
		fcntl = MC_CMD_FCNTL_BIDIR;
		break;
	case EFX_FC_RX:
		fcntl = MC_CMD_FCNTL_RESPOND;
		break;
	default:
		fcntl = MC_CMD_FCNTL_OFF;
		break;
	}
	if (efx->wanted_fc & EFX_FC_AUTO)
		fcntl = MC_CMD_FCNTL_AUTO;
	if (efx->fc_disable)
		fcntl = MC_CMD_FCNTL_OFF;

	MCDI_SET_DWORD(cmdbytes, SET_MAC_IN_FCNTL, fcntl);

	return efx_mcdi_rpc(efx, MC_CMD_SET_MAC, cmdbytes, sizeof(cmdbytes),
			    NULL, 0, NULL);
}

/* Get physical port number (EF10 only; on Siena it is same as PF number) */
int efx_mcdi_port_get_number(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_PORT_ASSIGNMENT_OUT_LEN);
	int rc;

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_PORT_ASSIGNMENT, NULL, 0,
			  outbuf, sizeof(outbuf), NULL);
	if (rc)
		return rc;

	return MCDI_DWORD(outbuf, GET_PORT_ASSIGNMENT_OUT_PORT);
}
