// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Intel Corporation. */

#include "ice_dcb_lib.h"

/**
 * ice_init_pf_dcb - initialize DCB for a PF
 * @pf: pf to initiialize DCB for
 */
int ice_init_pf_dcb(struct ice_pf *pf)
{
	struct device *dev = &pf->pdev->dev;
	struct ice_port_info *port_info;
	struct ice_hw *hw = &pf->hw;

	port_info = hw->port_info;

	/* check if device is DCB capable */
	if (!hw->func_caps.common_cap.dcb) {
		dev_dbg(dev, "DCB not supported\n");
		return -EOPNOTSUPP;
	}

	/* Best effort to put DCBx and LLDP into a good state */
	port_info->dcbx_status = ice_get_dcbx_status(hw);
	if (port_info->dcbx_status != ICE_DCBX_STATUS_DONE &&
	    port_info->dcbx_status != ICE_DCBX_STATUS_IN_PROGRESS) {
		bool dcbx_status;

		/* Attempt to start LLDP engine. Ignore errors
		 * as this will error if it is already started
		 */
		ice_aq_start_lldp(hw, NULL);

		/* Attempt to start DCBX. Ignore errors as this
		 * will error if it is already started
		 */
		ice_aq_start_stop_dcbx(hw, true, &dcbx_status, NULL);
	}

	return ice_init_dcb(hw);
}
