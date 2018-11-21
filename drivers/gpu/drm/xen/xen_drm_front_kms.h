/* SPDX-License-Identifier: GPL-2.0 OR MIT */

/*
 *  Xen para-virtual DRM device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#ifndef __XEN_DRM_FRONT_KMS_H_
#define __XEN_DRM_FRONT_KMS_H_

#include <linux/types.h>

struct xen_drm_front_drm_info;
struct xen_drm_front_drm_pipeline;

int xen_drm_front_kms_init(struct xen_drm_front_drm_info *drm_info);

void xen_drm_front_kms_fini(struct xen_drm_front_drm_info *drm_info);

void xen_drm_front_kms_on_frame_done(struct xen_drm_front_drm_pipeline *pipeline,
				     u64 fb_cookie);

#endif /* __XEN_DRM_FRONT_KMS_H_ */
