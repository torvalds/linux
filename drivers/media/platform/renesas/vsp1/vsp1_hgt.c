// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_hgt.c  --  R-Car VSP1 Histogram Generator 2D
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 * Contact: Niklas Söderlund (niklas.soderlund@ragnatech.se)
 */

#include <linux/device.h>
#include <linux/gfp.h>

#include <media/v4l2-subdev.h>
#include <media/videobuf2-vmalloc.h>

#include "vsp1.h"
#include "vsp1_dl.h"
#include "vsp1_hgt.h"

#define HGT_DATA_SIZE				((2 +  6 * 32) * 4)

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline u32 vsp1_hgt_read(struct vsp1_hgt *hgt, u32 reg)
{
	return vsp1_read(hgt->histo.entity.vsp1, reg);
}

static inline void vsp1_hgt_write(struct vsp1_hgt *hgt,
				  struct vsp1_dl_body *dlb, u32 reg, u32 data)
{
	vsp1_dl_body_write(dlb, reg, data);
}

/* -----------------------------------------------------------------------------
 * Frame End Handler
 */

void vsp1_hgt_frame_end(struct vsp1_entity *entity)
{
	struct vsp1_hgt *hgt = to_hgt(&entity->subdev);
	struct vsp1_histogram_buffer *buf;
	unsigned int m;
	unsigned int n;
	u32 *data;

	buf = vsp1_histogram_buffer_get(&hgt->histo);
	if (!buf)
		return;

	data = buf->addr;

	*data++ = vsp1_hgt_read(hgt, VI6_HGT_MAXMIN);
	*data++ = vsp1_hgt_read(hgt, VI6_HGT_SUM);

	for (m = 0; m < 6; ++m)
		for (n = 0; n < 32; ++n)
			*data++ = vsp1_hgt_read(hgt, VI6_HGT_HISTO(m, n));

	vsp1_histogram_buffer_complete(&hgt->histo, buf, HGT_DATA_SIZE);
}

/* -----------------------------------------------------------------------------
 * Controls
 */

#define V4L2_CID_VSP1_HGT_HUE_AREAS	(V4L2_CID_USER_BASE | 0x1001)

static int hgt_hue_areas_try_ctrl(struct v4l2_ctrl *ctrl)
{
	const u8 *values = ctrl->p_new.p_u8;
	unsigned int i;

	/*
	 * The hardware has constraints on the hue area boundaries beyond the
	 * control min, max and step. The values must match one of the following
	 * expressions.
	 *
	 * 0L <= 0U <= 1L <= 1U <= 2L <= 2U <= 3L <= 3U <= 4L <= 4U <= 5L <= 5U
	 * 0U <= 1L <= 1U <= 2L <= 2U <= 3L <= 3U <= 4L <= 4U <= 5L <= 5U <= 0L
	 *
	 * Start by verifying the common part...
	 */
	for (i = 1; i < (HGT_NUM_HUE_AREAS * 2) - 1; ++i) {
		if (values[i] > values[i+1])
			return -EINVAL;
	}

	/* ... and handle 0L separately. */
	if (values[0] > values[1] && values[11] > values[0])
		return -EINVAL;

	return 0;
}

static int hgt_hue_areas_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vsp1_hgt *hgt = container_of(ctrl->handler, struct vsp1_hgt,
					    ctrls);

	memcpy(hgt->hue_areas, ctrl->p_new.p_u8, sizeof(hgt->hue_areas));
	return 0;
}

static const struct v4l2_ctrl_ops hgt_hue_areas_ctrl_ops = {
	.try_ctrl = hgt_hue_areas_try_ctrl,
	.s_ctrl = hgt_hue_areas_s_ctrl,
};

static const struct v4l2_ctrl_config hgt_hue_areas = {
	.ops = &hgt_hue_areas_ctrl_ops,
	.id = V4L2_CID_VSP1_HGT_HUE_AREAS,
	.name = "Boundary Values for Hue Area",
	.type = V4L2_CTRL_TYPE_U8,
	.min = 0,
	.max = 255,
	.def = 0,
	.step = 1,
	.dims = { 12 },
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void hgt_configure_stream(struct vsp1_entity *entity,
				 struct vsp1_pipeline *pipe,
				 struct vsp1_dl_list *dl,
				 struct vsp1_dl_body *dlb)
{
	struct vsp1_hgt *hgt = to_hgt(&entity->subdev);
	struct v4l2_rect *compose;
	struct v4l2_rect *crop;
	unsigned int hratio;
	unsigned int vratio;
	u8 lower;
	u8 upper;
	unsigned int i;

	crop = vsp1_entity_get_pad_selection(entity, entity->state,
					     HISTO_PAD_SINK, V4L2_SEL_TGT_CROP);
	compose = vsp1_entity_get_pad_selection(entity, entity->state,
						HISTO_PAD_SINK,
						V4L2_SEL_TGT_COMPOSE);

	vsp1_hgt_write(hgt, dlb, VI6_HGT_REGRST, VI6_HGT_REGRST_RCLEA);

	vsp1_hgt_write(hgt, dlb, VI6_HGT_OFFSET,
		       (crop->left << VI6_HGT_OFFSET_HOFFSET_SHIFT) |
		       (crop->top << VI6_HGT_OFFSET_VOFFSET_SHIFT));
	vsp1_hgt_write(hgt, dlb, VI6_HGT_SIZE,
		       (crop->width << VI6_HGT_SIZE_HSIZE_SHIFT) |
		       (crop->height << VI6_HGT_SIZE_VSIZE_SHIFT));

	mutex_lock(hgt->ctrls.lock);
	for (i = 0; i < HGT_NUM_HUE_AREAS; ++i) {
		lower = hgt->hue_areas[i*2 + 0];
		upper = hgt->hue_areas[i*2 + 1];
		vsp1_hgt_write(hgt, dlb, VI6_HGT_HUE_AREA(i),
			       (lower << VI6_HGT_HUE_AREA_LOWER_SHIFT) |
			       (upper << VI6_HGT_HUE_AREA_UPPER_SHIFT));
	}
	mutex_unlock(hgt->ctrls.lock);

	hratio = crop->width * 2 / compose->width / 3;
	vratio = crop->height * 2 / compose->height / 3;
	vsp1_hgt_write(hgt, dlb, VI6_HGT_MODE,
		       (hratio << VI6_HGT_MODE_HRATIO_SHIFT) |
		       (vratio << VI6_HGT_MODE_VRATIO_SHIFT));
}

static const struct vsp1_entity_operations hgt_entity_ops = {
	.configure_stream = hgt_configure_stream,
	.destroy = vsp1_histogram_destroy,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

static const unsigned int hgt_mbus_formats[] = {
	MEDIA_BUS_FMT_AHSV8888_1X32,
};

struct vsp1_hgt *vsp1_hgt_create(struct vsp1_device *vsp1)
{
	struct vsp1_hgt *hgt;
	int ret;

	hgt = devm_kzalloc(vsp1->dev, sizeof(*hgt), GFP_KERNEL);
	if (hgt == NULL)
		return ERR_PTR(-ENOMEM);

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(&hgt->ctrls, 1);
	v4l2_ctrl_new_custom(&hgt->ctrls, &hgt_hue_areas, NULL);

	hgt->histo.entity.subdev.ctrl_handler = &hgt->ctrls;

	/* Initialize the video device and queue for statistics data. */
	ret = vsp1_histogram_init(vsp1, &hgt->histo, VSP1_ENTITY_HGT, "hgt",
				  &hgt_entity_ops, hgt_mbus_formats,
				  ARRAY_SIZE(hgt_mbus_formats),
				  HGT_DATA_SIZE, V4L2_META_FMT_VSP1_HGT);
	if (ret < 0) {
		vsp1_entity_destroy(&hgt->histo.entity);
		return ERR_PTR(ret);
	}

	v4l2_ctrl_handler_setup(&hgt->ctrls);

	return hgt;
}
