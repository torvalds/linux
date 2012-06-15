/*
 * Copyright (c)  2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicensen
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
 * Authors:
 * Thomas Eaton <thomas.g.eaton@intel.com>
 * Scott Rowe <scott.m.rowe@intel.com>
*/

#include "mdfld_output.h"
#include "mdfld_dsi_dpi.h"
#include "mdfld_dsi_output.h"

#include "tc35876x-dsi-lvds.h"

int mdfld_get_panel_type(struct drm_device *dev, int pipe)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	return dev_priv->mdfld_panel_id;
}

static void mdfld_init_panel(struct drm_device *dev, int mipi_pipe,
								int p_type)
{
	switch (p_type) {
	case TPO_VID:
		mdfld_dsi_output_init(dev, mipi_pipe, &mdfld_tpo_vid_funcs);
		break;
	case TC35876X:
		tc35876x_init(dev);
		mdfld_dsi_output_init(dev, mipi_pipe, &mdfld_tc35876x_funcs);
		break;
	case TMD_VID:
		mdfld_dsi_output_init(dev, mipi_pipe, &mdfld_tmd_vid_funcs);
		break;
	case HDMI:
/*		if (dev_priv->mdfld_hdmi_present)
			mdfld_hdmi_init(dev, &dev_priv->mode_dev); */
		break;
	}
}


int mdfld_output_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	/* FIXME: hardcoded for now */
	dev_priv->mdfld_panel_id = TC35876X;
	/* MIPI panel 1 */
	mdfld_init_panel(dev, 0, dev_priv->mdfld_panel_id);
	/* HDMI panel */
	mdfld_init_panel(dev, 1, HDMI);
	return 0;
}

