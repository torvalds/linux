/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
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

#include "msm_drv.h"
#include "msm_connector.h"

void msm_connector_init(struct msm_connector *connector,
		const struct msm_connector_funcs *funcs,
		struct drm_encoder *encoder)
{
	connector->funcs = funcs;
	connector->encoder = encoder;
}

struct drm_encoder *msm_connector_attached_encoder(
		struct drm_connector *connector)
{
	struct msm_connector *msm_connector = to_msm_connector(connector);
	return msm_connector->encoder;
}
