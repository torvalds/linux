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

#ifndef __MSM_CONNECTOR_H__
#define __MSM_CONNECTOR_H__

#include "msm_drv.h"

/*
 * Base class for MSM connectors.  Typically a connector is a bit more
 * passive.  But with the split between (for example) DTV within MDP4,
 * and HDMI encoder, we really need two parts to an encoder.  Instead
 * what we do is have the part external to the display controller block
 * in the connector, which is called from the encoder to delegate the
 * appropriate parts of modeset.
 */

struct msm_connector;

struct msm_connector_funcs {
	void (*dpms)(struct msm_connector *connector, int mode);
	void (*mode_set)(struct msm_connector *connector,
			struct drm_display_mode *mode);
};

struct msm_connector {
	struct drm_connector base;
	struct drm_encoder *encoder;
	const struct msm_connector_funcs *funcs;
};
#define to_msm_connector(x) container_of(x, struct msm_connector, base)

void msm_connector_init(struct msm_connector *connector,
		const struct msm_connector_funcs *funcs,
		struct drm_encoder *encoder);

struct drm_encoder *msm_connector_attached_encoder(
		struct drm_connector *connector);

static inline struct msm_connector *get_connector(struct drm_encoder *encoder)
{
	struct msm_drm_private *priv = encoder->dev->dev_private;
	int i;

	for (i = 0; i < priv->num_connectors; i++) {
		struct drm_connector *connector = priv->connectors[i];
		if (msm_connector_attached_encoder(connector) == encoder)
			return to_msm_connector(connector);
	}

	return NULL;
}

#endif /* __MSM_CONNECTOR_H__ */
