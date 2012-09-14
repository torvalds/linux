/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2009-2010 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include "efx.h"
#include "mcdi.h"
#include "mcdi_pcol.h"

int efx_mcdi_set_mac(struct efx_nic *efx)
{
	u32 reject, fcntl;
	MCDI_DECLARE_BUF(cmdbytes, MC_CMD_SET_MAC_IN_LEN);

	memcpy(MCDI_PTR(cmdbytes, SET_MAC_IN_ADDR),
	       efx->net_dev->dev_addr, ETH_ALEN);

	MCDI_SET_DWORD(cmdbytes, SET_MAC_IN_MTU,
			EFX_MAX_FRAME_LEN(efx->net_dev->mtu));
	MCDI_SET_DWORD(cmdbytes, SET_MAC_IN_DRAIN, 0);

	/* The MCDI command provides for controlling accept/reject
	 * of broadcast packets too, but the driver doesn't currently
	 * expose this. */
	reject = (efx->promiscuous) ? 0 :
		(1 << MC_CMD_SET_MAC_IN_REJECT_UNCST_LBN);
	MCDI_SET_DWORD(cmdbytes, SET_MAC_IN_REJECT, reject);

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

bool efx_mcdi_mac_check_fault(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_LINK_OUT_LEN);
	size_t outlength;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_LINK_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LINK, NULL, 0,
			  outbuf, sizeof(outbuf), &outlength);
	if (rc) {
		netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n",
			  __func__, rc);
		return true;
	}

	return MCDI_DWORD(outbuf, GET_LINK_OUT_MAC_FAULT) != 0;
}

int efx_mcdi_mac_stats(struct efx_nic *efx, dma_addr_t dma_addr,
		       u32 dma_len, int enable, int clear)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAC_STATS_IN_LEN);
	int rc;
	efx_dword_t *cmd_ptr;
	int period = enable ? 1000 : 0;
	u32 addr_hi;
	u32 addr_lo;

	BUILD_BUG_ON(MC_CMD_MAC_STATS_OUT_DMA_LEN != 0);

	addr_lo = ((u64)dma_addr) >> 0;
	addr_hi = ((u64)dma_addr) >> 32;

	MCDI_SET_DWORD(inbuf, MAC_STATS_IN_DMA_ADDR_LO, addr_lo);
	MCDI_SET_DWORD(inbuf, MAC_STATS_IN_DMA_ADDR_HI, addr_hi);
	cmd_ptr = (efx_dword_t *)MCDI_PTR(inbuf, MAC_STATS_IN_CMD);
	EFX_POPULATE_DWORD_7(*cmd_ptr,
			     MC_CMD_MAC_STATS_IN_DMA, !!enable,
			     MC_CMD_MAC_STATS_IN_CLEAR, clear,
			     MC_CMD_MAC_STATS_IN_PERIODIC_CHANGE, 1,
			     MC_CMD_MAC_STATS_IN_PERIODIC_ENABLE, !!enable,
			     MC_CMD_MAC_STATS_IN_PERIODIC_CLEAR, 0,
			     MC_CMD_MAC_STATS_IN_PERIODIC_NOEVENT, 1,
			     MC_CMD_MAC_STATS_IN_PERIOD_MS, period);
	MCDI_SET_DWORD(inbuf, MAC_STATS_IN_DMA_LEN, dma_len);

	rc = efx_mcdi_rpc(efx, MC_CMD_MAC_STATS, inbuf, sizeof(inbuf),
			  NULL, 0, NULL);
	if (rc)
		goto fail;

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: %s failed rc=%d\n",
		  __func__, enable ? "enable" : "disable", rc);
	return rc;
}

int efx_mcdi_mac_reconfigure(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_SET_MCAST_HASH_IN_LEN);
	int rc;

	BUILD_BUG_ON(MC_CMD_SET_MCAST_HASH_IN_LEN !=
		     MC_CMD_SET_MCAST_HASH_IN_HASH0_OFST +
		     sizeof(efx->multicast_hash));

	WARN_ON(!mutex_is_locked(&efx->mac_lock));

	rc = efx_mcdi_set_mac(efx);
	if (rc != 0)
		return rc;

	memcpy(MCDI_PTR(inbuf, SET_MCAST_HASH_IN_HASH0),
	       efx->multicast_hash.byte, sizeof(efx->multicast_hash));
	return efx_mcdi_rpc(efx, MC_CMD_SET_MCAST_HASH,
			    inbuf, sizeof(inbuf), NULL, 0, NULL);
}
