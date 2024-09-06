/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef UDL_EDID_H
#define UDL_EDID_H

#include <linux/types.h>

struct drm_connector;
struct drm_edid;
struct udl_device;

bool udl_probe_edid(struct udl_device *udl);
const struct drm_edid *udl_edid_read(struct drm_connector *connector);

#endif
