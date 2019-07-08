// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      uvc_entity.c  --  USB Video Class driver
 *
 *      Copyright (C) 2005-2011
 *          Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/videodev2.h>

#include <media/v4l2-common.h>

#include "uvcvideo.h"

static int uvc_mc_create_links(struct uvc_video_chain *chain,
				    struct uvc_entity *entity)
{
	const u32 flags = MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE;
	struct media_entity *sink;
	unsigned int i;
	int ret;

	sink = (UVC_ENTITY_TYPE(entity) == UVC_TT_STREAMING)
	     ? (entity->vdev ? &entity->vdev->entity : NULL)
	     : &entity->subdev.entity;
	if (sink == NULL)
		return 0;

	for (i = 0; i < entity->num_pads; ++i) {
		struct media_entity *source;
		struct uvc_entity *remote;
		u8 remote_pad;

		if (!(entity->pads[i].flags & MEDIA_PAD_FL_SINK))
			continue;

		remote = uvc_entity_by_id(chain->dev, entity->baSourceID[i]);
		if (remote == NULL)
			return -EINVAL;

		source = (UVC_ENTITY_TYPE(remote) == UVC_TT_STREAMING)
		       ? (remote->vdev ? &remote->vdev->entity : NULL)
		       : &remote->subdev.entity;
		if (source == NULL)
			continue;

		remote_pad = remote->num_pads - 1;
		ret = media_create_pad_link(source, remote_pad,
					       sink, i, flags);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const struct v4l2_subdev_ops uvc_subdev_ops = {
};

void uvc_mc_cleanup_entity(struct uvc_entity *entity)
{
	if (UVC_ENTITY_TYPE(entity) != UVC_TT_STREAMING)
		media_entity_cleanup(&entity->subdev.entity);
	else if (entity->vdev != NULL)
		media_entity_cleanup(&entity->vdev->entity);
}

static int uvc_mc_init_entity(struct uvc_video_chain *chain,
			      struct uvc_entity *entity)
{
	int ret;

	if (UVC_ENTITY_TYPE(entity) != UVC_TT_STREAMING) {
		v4l2_subdev_init(&entity->subdev, &uvc_subdev_ops);
		strscpy(entity->subdev.name, entity->name,
			sizeof(entity->subdev.name));

		ret = media_entity_pads_init(&entity->subdev.entity,
					entity->num_pads, entity->pads);

		if (ret < 0)
			return ret;

		ret = v4l2_device_register_subdev(&chain->dev->vdev,
						  &entity->subdev);
	} else if (entity->vdev != NULL) {
		ret = media_entity_pads_init(&entity->vdev->entity,
					entity->num_pads, entity->pads);
		if (entity->flags & UVC_ENTITY_FLAG_DEFAULT)
			entity->vdev->entity.flags |= MEDIA_ENT_FL_DEFAULT;
	} else
		ret = 0;

	return ret;
}

int uvc_mc_register_entities(struct uvc_video_chain *chain)
{
	struct uvc_entity *entity;
	int ret;

	list_for_each_entry(entity, &chain->entities, chain) {
		ret = uvc_mc_init_entity(chain, entity);
		if (ret < 0) {
			uvc_printk(KERN_INFO, "Failed to initialize entity for "
				   "entity %u\n", entity->id);
			return ret;
		}
	}

	list_for_each_entry(entity, &chain->entities, chain) {
		ret = uvc_mc_create_links(chain, entity);
		if (ret < 0) {
			uvc_printk(KERN_INFO, "Failed to create links for "
				   "entity %u\n", entity->id);
			return ret;
		}
	}

	return 0;
}
