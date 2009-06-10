/*
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2007 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/i2c.h>
#include <linux/fb.h>
#include "drmP.h"
#include "intel_drv.h"
#include "i915_drv.h"

/**
 * intel_ddc_probe
 *
 */
bool intel_ddc_probe(struct intel_output *intel_output)
{
	u8 out_buf[] = { 0x0, 0x0};
	u8 buf[2];
	int ret;
	struct i2c_msg msgs[] = {
		{
			.addr = 0x50,
			.flags = 0,
			.len = 1,
			.buf = out_buf,
		},
		{
			.addr = 0x50,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = buf,
		}
	};

	intel_i2c_quirk_set(intel_output->ddc_bus->drm_dev, true);
	ret = i2c_transfer(&intel_output->ddc_bus->adapter, msgs, 2);
	intel_i2c_quirk_set(intel_output->ddc_bus->drm_dev, false);

	if (ret == 2)
		return true;

	return false;
}

/**
 * intel_ddc_get_modes - get modelist from monitor
 * @connector: DRM connector device to use
 *
 * Fetch the EDID information from @connector using the DDC bus.
 */
int intel_ddc_get_modes(struct intel_output *intel_output)
{
	struct edid *edid;
	int ret = 0;

	intel_i2c_quirk_set(intel_output->ddc_bus->drm_dev, true);
	edid = drm_get_edid(&intel_output->base,
			    &intel_output->ddc_bus->adapter);
	intel_i2c_quirk_set(intel_output->ddc_bus->drm_dev, false);
	if (edid) {
		drm_mode_connector_update_edid_property(&intel_output->base,
							edid);
		ret = drm_add_edid_modes(&intel_output->base, edid);
		intel_output->base.display_info.raw_edid = NULL;
		kfree(edid);
	}

	return ret;
}
