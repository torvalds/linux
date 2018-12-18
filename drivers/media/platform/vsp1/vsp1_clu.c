// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_clu.c  --  R-Car VSP1 Cubic Look-Up Table
 *
 * Copyright (C) 2015-2016 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/device.h>
#include <linux/slab.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_clu.h"
#include "vsp1_dl.h"

#define CLU_MIN_SIZE				4U
#define CLU_MAX_SIZE				8190U

#define CLU_SIZE				(17 * 17 * 17)

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_clu_write(struct vsp1_clu *clu,
				  struct vsp1_dl_body *dlb, u32 reg, u32 data)
{
	vsp1_dl_body_write(dlb, reg, data);
}

/* -----------------------------------------------------------------------------
 * Controls
 */

#define V4L2_CID_VSP1_CLU_TABLE			(V4L2_CID_USER_BASE | 0x1001)
#define V4L2_CID_VSP1_CLU_MODE			(V4L2_CID_USER_BASE | 0x1002)
#define V4L2_CID_VSP1_CLU_MODE_2D		0
#define V4L2_CID_VSP1_CLU_MODE_3D		1

static int clu_set_table(struct vsp1_clu *clu, struct v4l2_ctrl *ctrl)
{
	struct vsp1_dl_body *dlb;
	unsigned int i;

	dlb = vsp1_dl_body_get(clu->pool);
	if (!dlb)
		return -ENOMEM;

	vsp1_dl_body_write(dlb, VI6_CLU_ADDR, 0);
	for (i = 0; i < CLU_SIZE; ++i)
		vsp1_dl_body_write(dlb, VI6_CLU_DATA, ctrl->p_new.p_u32[i]);

	spin_lock_irq(&clu->lock);
	swap(clu->clu, dlb);
	spin_unlock_irq(&clu->lock);

	vsp1_dl_body_put(dlb);
	return 0;
}

static int clu_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vsp1_clu *clu =
		container_of(ctrl->handler, struct vsp1_clu, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_VSP1_CLU_TABLE:
		clu_set_table(clu, ctrl);
		break;

	case V4L2_CID_VSP1_CLU_MODE:
		clu->mode = ctrl->val;
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops clu_ctrl_ops = {
	.s_ctrl = clu_s_ctrl,
};

static const struct v4l2_ctrl_config clu_table_control = {
	.ops = &clu_ctrl_ops,
	.id = V4L2_CID_VSP1_CLU_TABLE,
	.name = "Look-Up Table",
	.type = V4L2_CTRL_TYPE_U32,
	.min = 0x00000000,
	.max = 0x00ffffff,
	.step = 1,
	.def = 0,
	.dims = { 17, 17, 17 },
};

static const char * const clu_mode_menu[] = {
	"2D",
	"3D",
	NULL,
};

static const struct v4l2_ctrl_config clu_mode_control = {
	.ops = &clu_ctrl_ops,
	.id = V4L2_CID_VSP1_CLU_MODE,
	.name = "Mode",
	.type = V4L2_CTRL_TYPE_MENU,
	.min = 0,
	.max = 1,
	.def = 1,
	.qmenu = clu_mode_menu,
};

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static const unsigned int clu_codes[] = {
	MEDIA_BUS_FMT_ARGB8888_1X32,
	MEDIA_BUS_FMT_AHSV8888_1X32,
	MEDIA_BUS_FMT_AYUV8_1X32,
};

static int clu_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	return vsp1_subdev_enum_mbus_code(subdev, cfg, code, clu_codes,
					  ARRAY_SIZE(clu_codes));
}

static int clu_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	return vsp1_subdev_enum_frame_size(subdev, cfg, fse, CLU_MIN_SIZE,
					   CLU_MIN_SIZE, CLU_MAX_SIZE,
					   CLU_MAX_SIZE);
}

static int clu_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	return vsp1_subdev_set_pad_format(subdev, cfg, fmt, clu_codes,
					  ARRAY_SIZE(clu_codes),
					  CLU_MIN_SIZE, CLU_MIN_SIZE,
					  CLU_MAX_SIZE, CLU_MAX_SIZE);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static const struct v4l2_subdev_pad_ops clu_pad_ops = {
	.init_cfg = vsp1_entity_init_cfg,
	.enum_mbus_code = clu_enum_mbus_code,
	.enum_frame_size = clu_enum_frame_size,
	.get_fmt = vsp1_subdev_get_pad_format,
	.set_fmt = clu_set_format,
};

static const struct v4l2_subdev_ops clu_ops = {
	.pad    = &clu_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void clu_configure_stream(struct vsp1_entity *entity,
				 struct vsp1_pipeline *pipe,
				 struct vsp1_dl_body *dlb)
{
	struct vsp1_clu *clu = to_clu(&entity->subdev);
	struct v4l2_mbus_framefmt *format;

	/*
	 * The yuv_mode can't be changed during streaming. Cache it internally
	 * for future runtime configuration calls.
	 */
	format = vsp1_entity_get_pad_format(&clu->entity,
					    clu->entity.config,
					    CLU_PAD_SINK);
	clu->yuv_mode = format->code == MEDIA_BUS_FMT_AYUV8_1X32;
}

static void clu_configure_frame(struct vsp1_entity *entity,
				struct vsp1_pipeline *pipe,
				struct vsp1_dl_list *dl,
				struct vsp1_dl_body *dlb)
{
	struct vsp1_clu *clu = to_clu(&entity->subdev);
	struct vsp1_dl_body *clu_dlb;
	unsigned long flags;
	u32 ctrl = VI6_CLU_CTRL_AAI | VI6_CLU_CTRL_MVS | VI6_CLU_CTRL_EN;

	/* 2D mode can only be used with the YCbCr pixel encoding. */
	if (clu->mode == V4L2_CID_VSP1_CLU_MODE_2D && clu->yuv_mode)
		ctrl |= VI6_CLU_CTRL_AX1I_2D | VI6_CLU_CTRL_AX2I_2D
		     |  VI6_CLU_CTRL_OS0_2D | VI6_CLU_CTRL_OS1_2D
		     |  VI6_CLU_CTRL_OS2_2D | VI6_CLU_CTRL_M2D;

	vsp1_clu_write(clu, dlb, VI6_CLU_CTRL, ctrl);

	spin_lock_irqsave(&clu->lock, flags);
	clu_dlb = clu->clu;
	clu->clu = NULL;
	spin_unlock_irqrestore(&clu->lock, flags);

	if (clu_dlb) {
		vsp1_dl_list_add_body(dl, clu_dlb);

		/* Release our local reference. */
		vsp1_dl_body_put(clu_dlb);
	}
}

static void clu_destroy(struct vsp1_entity *entity)
{
	struct vsp1_clu *clu = to_clu(&entity->subdev);

	vsp1_dl_body_pool_destroy(clu->pool);
}

static const struct vsp1_entity_operations clu_entity_ops = {
	.configure_stream = clu_configure_stream,
	.configure_frame = clu_configure_frame,
	.destroy = clu_destroy,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_clu *vsp1_clu_create(struct vsp1_device *vsp1)
{
	struct vsp1_clu *clu;
	int ret;

	clu = devm_kzalloc(vsp1->dev, sizeof(*clu), GFP_KERNEL);
	if (clu == NULL)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&clu->lock);

	clu->entity.ops = &clu_entity_ops;
	clu->entity.type = VSP1_ENTITY_CLU;

	ret = vsp1_entity_init(vsp1, &clu->entity, "clu", 2, &clu_ops,
			       MEDIA_ENT_F_PROC_VIDEO_LUT);
	if (ret < 0)
		return ERR_PTR(ret);

	/*
	 * Pre-allocate a body pool, with 3 bodies allowing a userspace update
	 * before the hardware has committed a previous set of tables, handling
	 * both the queued and pending dl entries. One extra entry is added to
	 * the CLU_SIZE to allow for the VI6_CLU_ADDR header.
	 */
	clu->pool = vsp1_dl_body_pool_create(clu->entity.vsp1, 3, CLU_SIZE + 1,
					     0);
	if (!clu->pool)
		return ERR_PTR(-ENOMEM);

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(&clu->ctrls, 2);
	v4l2_ctrl_new_custom(&clu->ctrls, &clu_table_control, NULL);
	v4l2_ctrl_new_custom(&clu->ctrls, &clu_mode_control, NULL);

	clu->entity.subdev.ctrl_handler = &clu->ctrls;

	if (clu->ctrls.error) {
		dev_err(vsp1->dev, "clu: failed to initialize controls\n");
		ret = clu->ctrls.error;
		vsp1_entity_destroy(&clu->entity);
		return ERR_PTR(ret);
	}

	v4l2_ctrl_handler_setup(&clu->ctrls);

	return clu;
}
