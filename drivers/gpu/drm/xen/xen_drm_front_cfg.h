/* SPDX-License-Identifier: GPL-2.0 OR MIT */

/*
 *  Xen para-virtual DRM device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#ifndef __XEN_DRM_FRONT_CFG_H_
#define __XEN_DRM_FRONT_CFG_H_

#include <linux/types.h>

#define XEN_DRM_FRONT_MAX_CRTCS	4

struct xen_drm_front_cfg_connector {
	int width;
	int height;
	char *xenstore_path;
};

struct xen_drm_front_cfg {
	struct xen_drm_front_info *front_info;
	/* number of connectors in this configuration */
	int num_connectors;
	/* connector configurations */
	struct xen_drm_front_cfg_connector connectors[XEN_DRM_FRONT_MAX_CRTCS];
	/* set if dumb buffers are allocated externally on backend side */
	bool be_alloc;
};

int xen_drm_front_cfg_card(struct xen_drm_front_info *front_info,
			   struct xen_drm_front_cfg *cfg);

#endif /* __XEN_DRM_FRONT_CFG_H_ */
