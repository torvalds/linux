/*
 * Copyright © 2013 Intel Corporation
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
 *
 * Author: Jani Nikula <jani.nikula@intel.com>
 */

#ifndef _INTEL_DSI_DSI_H
#define _INTEL_DSI_DSI_H

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <video/mipi_display.h>
#include "i915_drv.h"
#include "intel_drv.h"
#include "intel_dsi.h"

#define DPI_LP_MODE_EN	false
#define DPI_HS_MODE_EN	true

void dsi_hs_mode_enable(struct intel_dsi *intel_dsi, bool enable);

int dsi_vc_dcs_write(struct intel_dsi *intel_dsi, int channel,
		     const u8 *data, int len);

int dsi_vc_generic_write(struct intel_dsi *intel_dsi, int channel,
			 const u8 *data, int len);

int dsi_vc_dcs_read(struct intel_dsi *intel_dsi, int channel, u8 dcs_cmd,
		    u8 *buf, int buflen);

int dsi_vc_generic_read(struct intel_dsi *intel_dsi, int channel,
			u8 *reqdata, int reqlen, u8 *buf, int buflen);

int dpi_send_cmd(struct intel_dsi *intel_dsi, u32 cmd, bool hs);

/* XXX: questionable write helpers */
static inline int dsi_vc_dcs_write_0(struct intel_dsi *intel_dsi,
				     int channel, u8 dcs_cmd)
{
	return dsi_vc_dcs_write(intel_dsi, channel, &dcs_cmd, 1);
}

static inline int dsi_vc_dcs_write_1(struct intel_dsi *intel_dsi,
				     int channel, u8 dcs_cmd, u8 param)
{
	u8 buf[2] = { dcs_cmd, param };
	return dsi_vc_dcs_write(intel_dsi, channel, buf, 2);
}

static inline int dsi_vc_generic_write_0(struct intel_dsi *intel_dsi,
					 int channel)
{
	return dsi_vc_generic_write(intel_dsi, channel, NULL, 0);
}

static inline int dsi_vc_generic_write_1(struct intel_dsi *intel_dsi,
					 int channel, u8 param)
{
	return dsi_vc_generic_write(intel_dsi, channel, &param, 1);
}

static inline int dsi_vc_generic_write_2(struct intel_dsi *intel_dsi,
					 int channel, u8 param1, u8 param2)
{
	u8 buf[2] = { param1, param2 };
	return dsi_vc_generic_write(intel_dsi, channel, buf, 2);
}

/* XXX: questionable read helpers */
static inline int dsi_vc_generic_read_0(struct intel_dsi *intel_dsi,
					int channel, u8 *buf, int buflen)
{
	return dsi_vc_generic_read(intel_dsi, channel, NULL, 0, buf, buflen);
}

static inline int dsi_vc_generic_read_1(struct intel_dsi *intel_dsi,
					int channel, u8 param, u8 *buf,
					int buflen)
{
	return dsi_vc_generic_read(intel_dsi, channel, &param, 1, buf, buflen);
}

static inline int dsi_vc_generic_read_2(struct intel_dsi *intel_dsi,
					int channel, u8 param1, u8 param2,
					u8 *buf, int buflen)
{
	u8 req[2] = { param1, param2 };

	return dsi_vc_generic_read(intel_dsi, channel, req, 2, buf, buflen);
}


#endif /* _INTEL_DSI_DSI_H */
