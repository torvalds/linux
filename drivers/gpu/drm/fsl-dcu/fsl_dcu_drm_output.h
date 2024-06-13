/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * Freescale DCU drm device driver
 */

#ifndef __FSL_DCU_DRM_CONNECTOR_H__
#define __FSL_DCU_DRM_CONNECTOR_H__

struct fsl_dcu_drm_connector {
	struct drm_connector base;
	struct drm_encoder *encoder;
	struct drm_panel *panel;
};

static inline struct fsl_dcu_drm_connector *
to_fsl_dcu_connector(struct drm_connector *con)
{
	return con ? container_of(con, struct fsl_dcu_drm_connector, base)
		     : NULL;
}

int fsl_dcu_drm_encoder_create(struct fsl_dcu_drm_device *fsl_dev,
			       struct drm_crtc *crtc);
int fsl_dcu_create_outputs(struct fsl_dcu_drm_device *fsl_dev);

#endif /* __FSL_DCU_DRM_CONNECTOR_H__ */
