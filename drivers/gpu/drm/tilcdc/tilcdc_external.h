/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Texas Instruments
 * Author: Jyri Sarha <jsarha@ti.com>
 */

#ifndef __TILCDC_EXTERNAL_H__
#define __TILCDC_EXTERNAL_H__

int tilcdc_add_component_encoder(struct drm_device *dev);
int tilcdc_get_external_components(struct device *dev,
				   struct component_match **match);
int tilcdc_attach_external_device(struct drm_device *ddev);
#endif /* __TILCDC_SLAVE_H__ */
