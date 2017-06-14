/*
 * vsp1_hgo.c  --  R-Car VSP1 Histogram Generator 1D
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/gfp.h>

#include <media/v4l2-subdev.h>
#include <media/videobuf2-vmalloc.h>

#include "vsp1.h"
#include "vsp1_dl.h"
#include "vsp1_hgo.h"

#define HGO_DATA_SIZE				((2 + 256) * 4)

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline u32 vsp1_hgo_read(struct vsp1_hgo *hgo, u32 reg)
{
	return vsp1_read(hgo->histo.entity.vsp1, reg);
}

static inline void vsp1_hgo_write(struct vsp1_hgo *hgo, struct vsp1_dl_list *dl,
				  u32 reg, u32 data)
{
	vsp1_dl_list_write(dl, reg, data);
}

/* -----------------------------------------------------------------------------
 * Frame End Handler
 */

void vsp1_hgo_frame_end(struct vsp1_entity *entity)
{
	struct vsp1_hgo *hgo = to_hgo(&entity->subdev);
	struct vsp1_histogram_buffer *buf;
	unsigned int i;
	size_t size;
	u32 *data;

	buf = vsp1_histogram_buffer_get(&hgo->histo);
	if (!buf)
		return;

	data = buf->addr;

	if (hgo->num_bins == 256) {
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_MAXMIN);
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_SUM);

		for (i = 0; i < 256; ++i) {
			vsp1_write(hgo->histo.entity.vsp1,
				   VI6_HGO_EXT_HIST_ADDR, i);
			*data++ = vsp1_hgo_read(hgo, VI6_HGO_EXT_HIST_DATA);
		}

		size = (2 + 256) * sizeof(u32);
	} else if (hgo->max_rgb) {
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_MAXMIN);
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_SUM);

		for (i = 0; i < 64; ++i)
			*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_HISTO(i));

		size = (2 + 64) * sizeof(u32);
	} else {
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_R_MAXMIN);
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_MAXMIN);
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_B_MAXMIN);

		*data++ = vsp1_hgo_read(hgo, VI6_HGO_R_SUM);
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_SUM);
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_B_SUM);

		for (i = 0; i < 64; ++i) {
			data[i] = vsp1_hgo_read(hgo, VI6_HGO_R_HISTO(i));
			data[i+64] = vsp1_hgo_read(hgo, VI6_HGO_G_HISTO(i));
			data[i+128] = vsp1_hgo_read(hgo, VI6_HGO_B_HISTO(i));
		}

		size = (6 + 64 * 3) * sizeof(u32);
	}

	vsp1_histogram_buffer_complete(&hgo->histo, buf, size);
}

/* -----------------------------------------------------------------------------
 * Controls
 */

#define V4L2_CID_VSP1_HGO_MAX_RGB		(V4L2_CID_USER_BASE | 0x1001)
#define V4L2_CID_VSP1_HGO_NUM_BINS		(V4L2_CID_USER_BASE | 0x1002)

static const struct v4l2_ctrl_config hgo_max_rgb_control = {
	.id = V4L2_CID_VSP1_HGO_MAX_RGB,
	.name = "Maximum RGB Mode",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.def = 0,
	.step = 1,
	.flags = V4L2_CTRL_FLAG_MODIFY_LAYOUT,
};

static const s64 hgo_num_bins[] = {
	64, 256,
};

static const struct v4l2_ctrl_config hgo_num_bins_control = {
	.id = V4L2_CID_VSP1_HGO_NUM_BINS,
	.name = "Number of Bins",
	.type = V4L2_CTRL_TYPE_INTEGER_MENU,
	.min = 0,
	.max = 1,
	.def = 0,
	.qmenu_int = hgo_num_bins,
	.flags = V4L2_CTRL_FLAG_MODIFY_LAYOUT,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void hgo_configure(struct vsp1_entity *entity,
			  struct vsp1_pipeline *pipe,
			  struct vsp1_dl_list *dl,
			  enum vsp1_entity_params params)
{
	struct vsp1_hgo *hgo = to_hgo(&entity->subdev);
	struct v4l2_rect *compose;
	struct v4l2_rect *crop;
	unsigned int hratio;
	unsigned int vratio;

	if (params != VSP1_ENTITY_PARAMS_INIT)
		return;

	crop = vsp1_entity_get_pad_selection(entity, entity->config,
					     HISTO_PAD_SINK, V4L2_SEL_TGT_CROP);
	compose = vsp1_entity_get_pad_selection(entity, entity->config,
						HISTO_PAD_SINK,
						V4L2_SEL_TGT_COMPOSE);

	vsp1_hgo_write(hgo, dl, VI6_HGO_REGRST, VI6_HGO_REGRST_RCLEA);

	vsp1_hgo_write(hgo, dl, VI6_HGO_OFFSET,
		       (crop->left << VI6_HGO_OFFSET_HOFFSET_SHIFT) |
		       (crop->top << VI6_HGO_OFFSET_VOFFSET_SHIFT));
	vsp1_hgo_write(hgo, dl, VI6_HGO_SIZE,
		       (crop->width << VI6_HGO_SIZE_HSIZE_SHIFT) |
		       (crop->height << VI6_HGO_SIZE_VSIZE_SHIFT));

	mutex_lock(hgo->ctrls.handler.lock);
	hgo->max_rgb = hgo->ctrls.max_rgb->cur.val;
	if (hgo->ctrls.num_bins)
		hgo->num_bins = hgo_num_bins[hgo->ctrls.num_bins->cur.val];
	mutex_unlock(hgo->ctrls.handler.lock);

	hratio = crop->width * 2 / compose->width / 3;
	vratio = crop->height * 2 / compose->height / 3;
	vsp1_hgo_write(hgo, dl, VI6_HGO_MODE,
		       (hgo->num_bins == 256 ? VI6_HGO_MODE_STEP : 0) |
		       (hgo->max_rgb ? VI6_HGO_MODE_MAXRGB : 0) |
		       (hratio << VI6_HGO_MODE_HRATIO_SHIFT) |
		       (vratio << VI6_HGO_MODE_VRATIO_SHIFT));
}

static const struct vsp1_entity_operations hgo_entity_ops = {
	.configure = hgo_configure,
	.destroy = vsp1_histogram_destroy,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

static const unsigned int hgo_mbus_formats[] = {
	MEDIA_BUS_FMT_AYUV8_1X32,
	MEDIA_BUS_FMT_ARGB8888_1X32,
	MEDIA_BUS_FMT_AHSV8888_1X32,
};

struct vsp1_hgo *vsp1_hgo_create(struct vsp1_device *vsp1)
{
	struct vsp1_hgo *hgo;
	int ret;

	hgo = devm_kzalloc(vsp1->dev, sizeof(*hgo), GFP_KERNEL);
	if (hgo == NULL)
		return ERR_PTR(-ENOMEM);

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(&hgo->ctrls.handler,
			       vsp1->info->gen == 3 ? 2 : 1);
	hgo->ctrls.max_rgb = v4l2_ctrl_new_custom(&hgo->ctrls.handler,
						  &hgo_max_rgb_control, NULL);
	if (vsp1->info->gen == 3)
		hgo->ctrls.num_bins =
			v4l2_ctrl_new_custom(&hgo->ctrls.handler,
					     &hgo_num_bins_control, NULL);

	hgo->max_rgb = false;
	hgo->num_bins = 64;

	hgo->histo.entity.subdev.ctrl_handler = &hgo->ctrls.handler;

	/* Initialize the video device and queue for statistics data. */
	ret = vsp1_histogram_init(vsp1, &hgo->histo, VSP1_ENTITY_HGO, "hgo",
				  &hgo_entity_ops, hgo_mbus_formats,
				  ARRAY_SIZE(hgo_mbus_formats),
				  HGO_DATA_SIZE, V4L2_META_FMT_VSP1_HGO);
	if (ret < 0) {
		vsp1_entity_destroy(&hgo->histo.entity);
		return ERR_PTR(ret);
	}

	return hgo;
}
