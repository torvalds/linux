// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2009-2013 Solarflare Communications Inc.
 */

/*
 * Driver for PHY related operations via MCDI.
 */

#include <linux/slab.h>
#include "efx.h"
#include "mcdi_port.h"
#include "mcdi.h"
#include "mcdi_pcol.h"
#include "nic.h"
#include "selftest.h"
#include "mcdi_port_common.h"


u32 efx_mcdi_phy_get_caps(struct efx_nic *efx)
{
	struct efx_mcdi_phy_data *phy_data = efx->phy_data;

	return phy_data->supported_cap;
}

bool efx_mcdi_mac_check_fault(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_LINK_OUT_LEN);
	size_t outlength;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_LINK_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LINK, NULL, 0,
			  outbuf, sizeof(outbuf), &outlength);
	if (rc)
		return true;

	return MCDI_DWORD(outbuf, GET_LINK_OUT_MAC_FAULT) != 0;
}

int efx_mcdi_port_probe(struct efx_nic *efx)
{
	int rc;

	/* Fill out loopback modes and initial link state */
	rc = efx_mcdi_phy_probe(efx);
	if (rc != 0)
		return rc;

	return efx_mcdi_mac_init_stats(efx);
}

void efx_mcdi_port_remove(struct efx_nic *efx)
{
	efx_mcdi_phy_remove(efx);
	efx_mcdi_mac_fini_stats(efx);
}
