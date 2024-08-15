// SPDX-License-Identifier: GPL-2.0 OR MIT

/*
 *  Xen para-virtual DRM device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#include <linux/device.h>

#include <drm/drm_print.h>

#include <xen/interface/io/displif.h>
#include <xen/xenbus.h>

#include "xen_drm_front.h"
#include "xen_drm_front_cfg.h"

static int cfg_connector(struct xen_drm_front_info *front_info,
			 struct xen_drm_front_cfg_connector *connector,
			 const char *path, int index)
{
	char *connector_path;

	connector_path = devm_kasprintf(&front_info->xb_dev->dev,
					GFP_KERNEL, "%s/%d", path, index);
	if (!connector_path)
		return -ENOMEM;

	if (xenbus_scanf(XBT_NIL, connector_path, XENDISPL_FIELD_RESOLUTION,
			 "%d" XENDISPL_RESOLUTION_SEPARATOR "%d",
			 &connector->width, &connector->height) < 0) {
		/* either no entry configured or wrong resolution set */
		connector->width = 0;
		connector->height = 0;
		return -EINVAL;
	}

	connector->xenstore_path = connector_path;

	DRM_INFO("Connector %s: resolution %dx%d\n",
		 connector_path, connector->width, connector->height);
	return 0;
}

int xen_drm_front_cfg_card(struct xen_drm_front_info *front_info,
			   struct xen_drm_front_cfg *cfg)
{
	struct xenbus_device *xb_dev = front_info->xb_dev;
	int ret, i;

	if (xenbus_read_unsigned(front_info->xb_dev->nodename,
				 XENDISPL_FIELD_BE_ALLOC, 0)) {
		DRM_INFO("Backend can provide display buffers\n");
		cfg->be_alloc = true;
	}

	cfg->num_connectors = 0;
	for (i = 0; i < ARRAY_SIZE(cfg->connectors); i++) {
		ret = cfg_connector(front_info, &cfg->connectors[i],
				    xb_dev->nodename, i);
		if (ret < 0)
			break;
		cfg->num_connectors++;
	}

	if (!cfg->num_connectors) {
		DRM_ERROR("No connector(s) configured at %s\n",
			  xb_dev->nodename);
		return -ENODEV;
	}

	return 0;
}

