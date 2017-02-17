/*
 * Copyright (C) 2015 Texas Instruments
 * Author: Jyri Sarha <jsarha@ti.com>
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

#ifndef __TILCDC_EXTERNAL_H__
#define __TILCDC_EXTERNAL_H__

int tilcdc_add_component_encoder(struct drm_device *dev);
void tilcdc_remove_external_device(struct drm_device *dev);
int tilcdc_get_external_components(struct device *dev,
				   struct component_match **match);
int tilcdc_attach_external_device(struct drm_device *ddev);
#endif /* __TILCDC_SLAVE_H__ */
