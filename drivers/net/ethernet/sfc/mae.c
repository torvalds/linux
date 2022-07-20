// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 * Copyright 2020-2022 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "mae.h"
#include "mcdi.h"
#include "mcdi_pcol.h"

void efx_mae_mport_vf(struct efx_nic *efx __always_unused, u32 vf_id, u32 *out)
{
	efx_dword_t mport;

	EFX_POPULATE_DWORD_3(mport,
			     MAE_MPORT_SELECTOR_TYPE, MAE_MPORT_SELECTOR_TYPE_FUNC,
			     MAE_MPORT_SELECTOR_FUNC_PF_ID, MAE_MPORT_SELECTOR_FUNC_PF_ID_CALLER,
			     MAE_MPORT_SELECTOR_FUNC_VF_ID, vf_id);
	*out = EFX_DWORD_VAL(mport);
}

/* id is really only 24 bits wide */
int efx_mae_lookup_mport(struct efx_nic *efx, u32 selector, u32 *id)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_MAE_MPORT_LOOKUP_OUT_LEN);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAE_MPORT_LOOKUP_IN_LEN);
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, MAE_MPORT_LOOKUP_IN_MPORT_SELECTOR, selector);
	rc = efx_mcdi_rpc(efx, MC_CMD_MAE_MPORT_LOOKUP, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < sizeof(outbuf))
		return -EIO;
	*id = MCDI_DWORD(outbuf, MAE_MPORT_LOOKUP_OUT_MPORT_ID);
	return 0;
}
