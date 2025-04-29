// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2007 Intel Corporation
 *
 * Authers: Jesse Barnes <jesse.barnes@intel.com>
 */

#include <linux/i2c.h>

#include <drm/drm_edid.h>

#include "psb_intel_drv.h"

/**
 * psb_intel_ddc_get_modes - get modelist from monitor
 * @connector: DRM connector device to use
 * @adapter:   Associated I2C adaptor
 *
 * Fetch the EDID information from @connector using the DDC bus.
 */
int psb_intel_ddc_get_modes(struct drm_connector *connector,
			    struct i2c_adapter *adapter)
{
	struct edid *edid;
	int ret = 0;

	edid = drm_get_edid(connector, adapter);
	if (edid) {
		drm_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}
	return ret;
}
