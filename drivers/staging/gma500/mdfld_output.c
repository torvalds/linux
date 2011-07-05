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

#include <linux/init.h>
#include "mdfld_dsi_dbi.h"
#include "mdfld_dsi_dpi.h"
#include "mdfld_dsi_output.h"
#include "mdfld_output.h"

#include "displays/tpo_cmd.h"
#include "displays/tpo_vid.h"
#include "displays/tmd_cmd.h"
#include "displays/tmd_vid.h"
#include "displays/pyr_cmd.h"
#include "displays/pyr_vid.h"
/* #include "displays/hdmi.h" */

/* For now a single type per device is all we cope with */
int mdfld_get_panel_type(struct drm_device *dev, int pipe)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	return dev_priv->panel_id;
}

int mdfld_panel_dpi(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	switch (dev_priv->panel_id) {
	case TMD_VID:
	case TPO_VID:
	case PYR_VID:
		return true;
	case TMD_CMD:
	case TPO_CMD:
	case PYR_CMD:
	default:
		return false;
	}
}

static void init_panel(struct drm_device *dev, int mipi_pipe, int p_type)
{
	struct panel_funcs *p_cmd_funcs;
	struct panel_funcs *p_vid_funcs;

	/* Oh boy ... FIXME */
	p_cmd_funcs = kzalloc(sizeof(struct panel_funcs), GFP_KERNEL);
	p_vid_funcs = kzalloc(sizeof(struct panel_funcs), GFP_KERNEL);

	switch (p_type) {
	case TPO_CMD:
		tpo_cmd_init(dev, p_cmd_funcs);
		mdfld_dsi_output_init(dev, mipi_pipe, NULL, p_cmd_funcs, NULL);
		break;
	case TPO_VID:
		tpo_vid_init(dev, p_vid_funcs);
		mdfld_dsi_output_init(dev, mipi_pipe, NULL, NULL, p_vid_funcs);
		break;
	case TMD_CMD:
		/*tmd_cmd_init(dev, p_cmd_funcs); */
		mdfld_dsi_output_init(dev, mipi_pipe, NULL, p_cmd_funcs, NULL);
		break;
	case TMD_VID:
		tmd_vid_init(dev, p_vid_funcs);
		mdfld_dsi_output_init(dev, mipi_pipe, NULL, NULL, p_vid_funcs);
		break;
	case PYR_CMD:
		pyr_cmd_init(dev, p_cmd_funcs);
		mdfld_dsi_output_init(dev, mipi_pipe, NULL, p_cmd_funcs, NULL);
		break;
	case PYR_VID:
		/*pyr_vid_init(dev, p_vid_funcs); */
		mdfld_dsi_output_init(dev, mipi_pipe, NULL, NULL, p_vid_funcs);
		break;
	case TPO:	/* TPO panel supports both cmd & vid interfaces */
		tpo_cmd_init(dev, p_cmd_funcs);
		tpo_vid_init(dev, p_vid_funcs);
		mdfld_dsi_output_init(dev, mipi_pipe, NULL, p_cmd_funcs,
				      p_vid_funcs);
		break;
	case TMD:
		break;
	case PYR:
		break;
#if 0
	case HDMI:
		dev_dbg(dev->dev, "Initializing HDMI");
		mdfld_hdmi_init(dev, &dev_priv->mode_dev);
		break;
#endif
	default:
		dev_err(dev->dev, "Unsupported interface %d", p_type);
		break;
	}
}

void mdfld_output_init(struct drm_device *dev)
{
	int type;

	/* MIPI panel 1 */
	type = mdfld_get_panel_type(dev, 0);
	dev_info(dev->dev, "panel 1: type is %d\n", type);
	init_panel(dev, 0, type);

	/* MIPI panel 2 */
	type = mdfld_get_panel_type(dev, 2);
	dev_info(dev->dev, "panel 2: type is %d\n", type);
	init_panel(dev, 2, type);
}
