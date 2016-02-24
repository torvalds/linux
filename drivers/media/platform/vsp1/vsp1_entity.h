/*
 * vsp1_entity.h  --  R-Car VSP1 Base Entity
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VSP1_ENTITY_H__
#define __VSP1_ENTITY_H__

#include <linux/list.h>
#include <linux/spinlock.h>

#include <media/v4l2-subdev.h>

struct vsp1_device;
struct vsp1_dl_list;
struct vsp1_pipeline;

enum vsp1_entity_type {
	VSP1_ENTITY_BRU,
	VSP1_ENTITY_HSI,
	VSP1_ENTITY_HST,
	VSP1_ENTITY_LIF,
	VSP1_ENTITY_LUT,
	VSP1_ENTITY_RPF,
	VSP1_ENTITY_SRU,
	VSP1_ENTITY_UDS,
	VSP1_ENTITY_WPF,
};

#define VSP1_ENTITY_MAX_INPUTS		5	/* For the BRU */

/*
 * struct vsp1_route - Entity routing configuration
 * @type: Entity type this routing entry is associated with
 * @index: Entity index this routing entry is associated with
 * @reg: Output routing configuration register
 * @inputs: Target node value for each input
 *
 * Each $vsp1_route entry describes routing configuration for the entity
 * specified by the entry's @type and @index. @reg indicates the register that
 * holds output routing configuration for the entity, and the @inputs array
 * store the target node value for each input of the entity.
 */
struct vsp1_route {
	enum vsp1_entity_type type;
	unsigned int index;
	unsigned int reg;
	unsigned int inputs[VSP1_ENTITY_MAX_INPUTS];
};

/**
 * struct vsp1_entity_operations - Entity operations
 * @destroy:	Destroy the entity.
 * @set_memory:	Setup memory buffer access. This operation applies the settings
 *		stored in the rwpf mem field to the display list. Valid for RPF
 *		and WPF only.
 * @configure:	Setup the hardware based on the entity state (pipeline, formats,
 *		selection rectangles, ...)
 */
struct vsp1_entity_operations {
	void (*destroy)(struct vsp1_entity *);
	void (*set_memory)(struct vsp1_entity *, struct vsp1_dl_list *dl);
	void (*configure)(struct vsp1_entity *, struct vsp1_pipeline *,
			  struct vsp1_dl_list *);
};

struct vsp1_entity {
	struct vsp1_device *vsp1;

	const struct vsp1_entity_operations *ops;

	enum vsp1_entity_type type;
	unsigned int index;
	const struct vsp1_route *route;

	struct list_head list_dev;
	struct list_head list_pipe;

	struct media_pad *pads;
	unsigned int source_pad;

	struct media_entity *sink;
	unsigned int sink_pad;

	struct v4l2_subdev subdev;
	struct v4l2_subdev_pad_config *config;
};

static inline struct vsp1_entity *to_vsp1_entity(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_entity, subdev);
}

int vsp1_entity_init(struct vsp1_device *vsp1, struct vsp1_entity *entity,
		     const char *name, unsigned int num_pads,
		     const struct v4l2_subdev_ops *ops);
void vsp1_entity_destroy(struct vsp1_entity *entity);

extern const struct v4l2_subdev_internal_ops vsp1_subdev_internal_ops;

int vsp1_entity_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags);

struct v4l2_subdev_pad_config *
vsp1_entity_get_pad_config(struct vsp1_entity *entity,
			   struct v4l2_subdev_pad_config *cfg,
			   enum v4l2_subdev_format_whence which);
struct v4l2_mbus_framefmt *
vsp1_entity_get_pad_format(struct vsp1_entity *entity,
			   struct v4l2_subdev_pad_config *cfg,
			   unsigned int pad);
struct v4l2_rect *
vsp1_entity_get_pad_compose(struct vsp1_entity *entity,
			    struct v4l2_subdev_pad_config *cfg,
			    unsigned int pad);
int vsp1_entity_init_cfg(struct v4l2_subdev *subdev,
			 struct v4l2_subdev_pad_config *cfg);

void vsp1_entity_route_setup(struct vsp1_entity *source,
			     struct vsp1_dl_list *dl);

int vsp1_subdev_get_pad_format(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_format *fmt);
int vsp1_subdev_enum_mbus_code(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_mbus_code_enum *code,
			       const unsigned int *codes, unsigned int ncodes);
int vsp1_subdev_enum_frame_size(struct v4l2_subdev *subdev,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_size_enum *fse,
				unsigned int min_w, unsigned int min_h,
				unsigned int max_w, unsigned int max_h);

#endif /* __VSP1_ENTITY_H__ */
