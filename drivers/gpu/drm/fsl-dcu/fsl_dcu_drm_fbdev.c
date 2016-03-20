/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * Freescale DCU drm device driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_fb_cma_helper.h>

#include "fsl_dcu_drm_drv.h"

/* initialize fbdev helper */
void fsl_dcu_fbdev_init(struct drm_device *dev)
{
	struct fsl_dcu_drm_device *fsl_dev = dev_get_drvdata(dev->dev);

	fsl_dev->fbdev = drm_fbdev_cma_init(dev, 24, 1, 1);
}
