/*
 * DRM/KMS device registration for TI OMAP platforms
 *
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_data/omap_drm.h>

#include "soc.h"
#include "omap_device.h"
#include "omap_hwmod.h"

#if defined(CONFIG_DRM_OMAP) || (CONFIG_DRM_OMAP_MODULE)

static struct omap_drm_platform_data platform_data;

static struct platform_device omap_drm_device = {
	.dev = {
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &platform_data,
	},
	.name = "omapdrm",
	.id = 0,
};

static int __init omap_init_drm(void)
{
	struct omap_hwmod *oh = NULL;
	struct platform_device *pdev;

	/* lookup and populate the DMM information, if present - OMAP4+ */
	oh = omap_hwmod_lookup("dmm");

	if (oh) {
		pdev = omap_device_build(oh->name, -1, oh, NULL, 0);
		WARN(IS_ERR(pdev), "Could not build omap_device for %s\n",
			oh->name);
	}

	platform_data.omaprev = GET_OMAP_TYPE;

	return platform_device_register(&omap_drm_device);

}

arch_initcall(omap_init_drm);

#endif
