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
#include "mac.h"
#include "mcdi.h"
#include "mcdi_pcol.h"

static int efx_mcdi_set_mac(struct efx_nic *efx)
{
	u32 reject, fcntl;
	u8 cmdbytes[MC_CMD_SET_MAC_IN_LEN];

	memcpy(cmdbytes + MC_CMD_SET_MAC_IN_ADDR_OFST,
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

	MCDI_SET_DWORD(cmdbytes, SET_MAC_IN_FCNTL, fcntl);

	return efx_mcdi_rpc(efx, MC_CMD_SET_MAC, cmdbytes, sizeof(cmdbytes),
			    NULL, 0, NULL);
}

static int efx_mcdi_get_mac_faults(struct efx_nic *efx, u32 *faults)
{
	u8 outbuf[MC_CMD_GET_LINK_OUT_LEN];
	size_t outlength;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_LINK_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LINK, NULL, 0,
			  outbuf, sizeof(outbuf), &outlength);
	if (rc)
		goto fail;

	*faults = MCDI_DWORD(outbuf, GET_LINK_OUT_MAC_FAULT);
	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n",
		  __func__, rc);
	return rc;
}

int efx_mcdi_mac_stats(struct efx_nic *efx, dma_addr_t dma_addr,
		       u32 dma_len, int enable, int clear)
{
	u8 inbuf[MC_CMD_MAC_STATS_IN_LEN];
	int rc;
	efx_dword_t *cmd_ptr;
	int period = enable ? 1000 : 0;
	u32 addr_hi;
	u32 addr_lo;

	BUILD_BUG_ON(MC_CMD_MAC_STATS_OUT_LEN != 0);

	addr_lo = ((u64)dma_addr) >> 0;
	addr_hi = ((u64)dma_addr) >> 32;

	MCDI_SET_DWORD(inbuf, MAC_STATS_IN_DMA_ADDR_LO, addr_lo);
	MCDI_SET_DWORD(inbuf, MAC_STATS_IN_DMA_ADDR_HI, addr_hi);
	cmd_ptr = (efx_dword_t *)MCDI_PTR(inbuf, MAC_STATS_IN_CMD);
	EFX_POPULATE_DWORD_7(*cmd_ptr,
			     MC_CMD_MAC_STATS_CMD_DMA, !!enable,
			     MC_CMD_MAC_STATS_CMD_CLEAR, clear,
			     MC_CMD_MAC_STATS_CMD_PERIODIC_CHANGE, 1,
			     MC_CMD_MAC_STATS_CMD_PERIODIC_ENABLE, !!enable,
			     MC_CMD_MAC_STATS_CMD_PERIODIC_CLEAR, 0,
			     MC_CMD_MAC_STATS_CMD_PERIODIC_NOEVENT, 1,
			     MC_CMD_MAC_STATS_CMD_PERIOD_MS, period);
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

static int efx_mcdi_mac_reconfigure(struct efx_nic *efx)
{
	int rc;

	rc = efx_mcdi_set_mac(efx);
	if (rc != 0)
		return rc;

	/* Restore the multicast hash registers. */
	efx->type->push_multicast_hash(efx);

	return 0;
}


static bool efx_mcdi_mac_check_fault(struct efx_nic *efx)
{
	u32 faults;
	int rc = efx_mcdi_get_mac_faults(efx, &faults);
	return (rc != 0) || (faults != 0);
}


const struct efx_mac_operations efx_mcdi_mac_operations = {
	.reconfigure	= efx_mcdi_mac_reconfigure,
	.update_stats	= efx_port_dummy_op_void,
	.check_fault 	= efx_mcdi_mac_check_fault,
};
