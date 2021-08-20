// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2021, Intel Corporation. */

#include "ice.h"
#include "ice_eswitch.h"
#include "ice_devlink.h"

/**
 * ice_eswitch_mode_set - set new eswitch mode
 * @devlink: pointer to devlink structure
 * @mode: eswitch mode to switch to
 * @extack: pointer to extack structure
 */
int
ice_eswitch_mode_set(struct devlink *devlink, u16 mode,
		     struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_priv(devlink);

	if (pf->eswitch_mode == mode)
		return 0;

	if (pf->num_alloc_vfs) {
		dev_info(ice_pf_to_dev(pf), "Changing eswitch mode is allowed only if there is no VFs created");
		NL_SET_ERR_MSG_MOD(extack, "Changing eswitch mode is allowed only if there is no VFs created");
		return -EOPNOTSUPP;
	}

	switch (mode) {
	case DEVLINK_ESWITCH_MODE_LEGACY:
		dev_info(ice_pf_to_dev(pf), "PF %d changed eswitch mode to legacy",
			 pf->hw.pf_id);
		NL_SET_ERR_MSG_MOD(extack, "Changed eswitch mode to legacy");
		break;
	case DEVLINK_ESWITCH_MODE_SWITCHDEV:
	{
		dev_info(ice_pf_to_dev(pf), "PF %d changed eswitch mode to switchdev",
			 pf->hw.pf_id);
		NL_SET_ERR_MSG_MOD(extack, "Changed eswitch mode to switchdev");
		break;
	}
	default:
		NL_SET_ERR_MSG_MOD(extack, "Unknown eswitch mode");
		return -EINVAL;
	}

	pf->eswitch_mode = mode;
	return 0;
}

/**
 * ice_eswitch_mode_get - get current eswitch mode
 * @devlink: pointer to devlink structure
 * @mode: output parameter for current eswitch mode
 */
int ice_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	struct ice_pf *pf = devlink_priv(devlink);

	*mode = pf->eswitch_mode;
	return 0;
}
