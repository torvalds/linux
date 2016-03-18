/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __VIDEO_ADF_ADF_H
#define __VIDEO_ADF_ADF_H

#include <linux/idr.h>
#include <linux/list.h>
#include <video/adf.h>
#include "sync.h"

struct adf_event_refcount {
	struct rb_node node;
	enum adf_event_type type;
	int refcount;
};

void adf_buffer_cleanup(struct adf_buffer *buf);
void adf_buffer_mapping_cleanup(struct adf_buffer_mapping *mapping,
		struct adf_buffer *buf);
void adf_post_cleanup(struct adf_device *dev, struct adf_pending_post *post);

struct adf_attachment_list *adf_attachment_find(struct list_head *list,
		struct adf_overlay_engine *eng, struct adf_interface *intf);
int adf_attachment_validate(struct adf_device *dev,
		struct adf_overlay_engine *eng, struct adf_interface *intf);
void adf_attachment_free(struct adf_attachment_list *attachment);

struct adf_event_refcount *adf_obj_find_event_refcount(struct adf_obj *obj,
		enum adf_event_type type);

static inline int adf_obj_check_supports_event(struct adf_obj *obj,
		enum adf_event_type type)
{
	if (!obj->ops || !obj->ops->supports_event)
		return -EOPNOTSUPP;
	if (!obj->ops->supports_event(obj, type))
		return -EINVAL;
	return 0;
}

static inline int adf_device_attach_op(struct adf_device *dev,
		struct adf_overlay_engine *eng, struct adf_interface *intf)
{
	if (!dev->ops->attach)
		return 0;

	return dev->ops->attach(dev, eng, intf);
}

static inline int adf_device_detach_op(struct adf_device *dev,
		struct adf_overlay_engine *eng, struct adf_interface *intf)
{
	if (!dev->ops->detach)
		return 0;

	return dev->ops->detach(dev, eng, intf);
}

#endif /* __VIDEO_ADF_ADF_H */
