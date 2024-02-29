// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_lut.c  --  R-Car VSP1 Look-Up Table
 *
 * Copyright (C) 2013 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/device.h>
#include <linux/gfp.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_dl.h"
#include "vsp1_lut.h"

#define LUT_MIN_SIZE				4U
#define LUT_MAX_SIZE				8190U

#define LUT_SIZE				256

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_lut_write(struct vsp1_lut *lut,
				  struct vsp1_dl_body *dlb, u32 reg, u32 data)
{
	vsp1_dl_body_write(dlb, reg, data);
}

/* -----------------------------------------------------------------------------
 * Controls
 */

#define V4L2_CID_VSP1_LUT_TABLE			(V4L2_CID_USER_BASE | 0x1001)

static int lut_set_table(struct vsp1_lut *lut, struct v4l2_ctrl *ctrl)
{
	struct vsp1_dl_body *dlb;
	unsigned int i;

	dlb = vsp1_dl_body_get(lut->pool);
	if (!dlb)
		return -ENOMEM;

	for (i = 0; i < LUT_SIZE; ++i)
		vsp1_dl_body_write(dlb, VI6_LUT_TABLE + 4 * i,
				       ctrl->p_new.p_u32[i]);

	spin_lock_irq(&lut->lock);
	swap(lut->lut, dlb);
	spin_unlock_irq(&lut->lock);

	vsp1_dl_body_put(dlb);
	return 0;
}

static int lut_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vsp1_lut *lut =
		container_of(ctrl->handler, struct vsp1_lut, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_VSP1_LUT_TABLE:
		lut_set_table(lut, ctrl);
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops lut_ctrl_ops = {
	.s_ctrl = lut_s_ctrl,
};

static const struct v4l2_ctrl_config lut_table_control = {
	.ops = &lut_ctrl_ops,
	.id = V4L2_CID_VSP1_LUT_TABLE,
	.name = "Look-Up Table",
	.type = V4L2_CTRL_TYPE_U32,
	.min = 0x00000000,
	.max = 0x00ffffff,
	.step = 1,
	.def = 0,
	.dims = { LUT_SIZE },
};

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static const unsigned int lut_codes[] = {
	MEDIA_BUS_FMT_ARGB8888_1X32,
	MEDIA_BUS_FMT_AHSV8888_1X32,
	MEDIA_BUS_FMT_AYUV8_1X32,
};

static int lut_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	return vsp1_subdev_enum_mbus_code(subdev, sd_state, code, lut_codes,
					  ARRAY_SIZE(lut_codes));
}

static int lut_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	return vsp1_subdev_enum_frame_size(subdev, sd_state, fse,
					   LUT_MIN_SIZE,
					   LUT_MIN_SIZE, LUT_MAX_SIZE,
					   LUT_MAX_SIZE);
}

static int lut_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	return vsp1_subdev_set_pad_format(subdev, sd_state, fmt, lut_codes,
					  ARRAY_SIZE(lut_codes),
					  LUT_MIN_SIZE, LUT_MIN_SIZE,
					  LUT_MAX_SIZE, LUT_MAX_SIZE);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static const struct v4l2_subdev_pad_ops lut_pad_ops = {
	.enum_mbus_code = lut_enum_mbus_code,
	.enum_frame_size = lut_enum_frame_size,
	.get_fmt = vsp1_subdev_get_pad_format,
	.set_fmt = lut_set_format,
};

static const struct v4l2_subdev_ops lut_ops = {
	.pad    = &lut_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void lut_configure_stream(struct vsp1_entity *entity,
				 struct vsp1_pipeline *pipe,
				 struct vsp1_dl_list *dl,
				 struct vsp1_dl_body *dlb)
{
	struct vsp1_lut *lut = to_lut(&entity->subdev);

	vsp1_lut_write(lut, dlb, VI6_LUT_CTRL, VI6_LUT_CTRL_EN);
}

static void lut_configure_frame(struct vsp1_entity *entity,
				struct vsp1_pipeline *pipe,
				struct vsp1_dl_list *dl,
				struct vsp1_dl_body *dlb)
{
	struct vsp1_lut *lut = to_lut(&entity->subdev);
	struct vsp1_dl_body *lut_dlb;
	unsigned long flags;

	spin_lock_irqsave(&lut->lock, flags);
	lut_dlb = lut->lut;
	lut->lut = NULL;
	spin_unlock_irqrestore(&lut->lock, flags);

	if (lut_dlb) {
		vsp1_dl_list_add_body(dl, lut_dlb);

		/* Release our local reference. */
		vsp1_dl_body_put(lut_dlb);
	}
}

static void lut_destroy(struct vsp1_entity *entity)
{
	struct vsp1_lut *lut = to_lut(&entity->subdev);

	vsp1_dl_body_pool_destroy(lut->pool);
}

static const struct vsp1_entity_operations lut_entity_ops = {
	.configure_stream = lut_configure_stream,
	.configure_frame = lut_configure_frame,
	.destroy = lut_destroy,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_lut *vsp1_lut_create(struct vsp1_device *vsp1)
{
	struct vsp1_lut *lut;
	int ret;

	lut = devm_kzalloc(vsp1->dev, sizeof(*lut), GFP_KERNEL);
	if (lut == NULL)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&lut->lock);

	lut->entity.ops = &lut_entity_ops;
	lut->entity.type = VSP1_ENTITY_LUT;

	ret = vsp1_entity_init(vsp1, &lut->entity, "lut", 2, &lut_ops,
			       MEDIA_ENT_F_PROC_VIDEO_LUT);
	if (ret < 0)
		return ERR_PTR(ret);

	/*
	 * Pre-allocate a body pool, with 3 bodies allowing a userspace update
	 * before the hardware has committed a previous set of tables, handling
	 * both the queued and pending dl entries.
	 */
	lut->pool = vsp1_dl_body_pool_create(vsp1, 3, LUT_SIZE, 0);
	if (!lut->pool)
		return ERR_PTR(-ENOMEM);

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(&lut->ctrls, 1);
	v4l2_ctrl_new_custom(&lut->ctrls, &lut_table_control, NULL);

	lut->entity.subdev.ctrl_handler = &lut->ctrls;

	if (lut->ctrls.error) {
		dev_err(vsp1->dev, "lut: failed to initialize controls\n");
		ret = lut->ctrls.error;
		vsp1_entity_destroy(&lut->entity);
		return ERR_PTR(ret);
	}

	v4l2_ctrl_handler_setup(&lut->ctrls);

	return lut;
}
