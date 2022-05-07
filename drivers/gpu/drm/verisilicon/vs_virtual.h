/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */


#ifndef __VS_VIRTUAL_H_
#define __VS_VIRTUAL_H_

#include <linux/debugfs.h>

struct vs_virtual_display {
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct device *dc;
	u32 bus_format;

	struct dentry *dump_debugfs;
	struct debugfs_blob_wrapper dump_blob;
	struct vs_gem_object *dump_obj;
	unsigned int pitch;
};

static inline struct vs_virtual_display *
to_virtual_display_with_connector(struct drm_connector *connector)
{
	return container_of(connector, struct vs_virtual_display, connector);
}

static inline struct vs_virtual_display *
to_virtual_display_with_encoder(struct drm_encoder *encoder)
{
	return container_of(encoder, struct vs_virtual_display, encoder);
}

extern struct platform_driver virtual_display_platform_driver;
#endif /* __VS_VIRTUAL_H_ */
