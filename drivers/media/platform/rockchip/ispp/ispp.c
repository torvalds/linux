// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>
#include <media/media-entity.h>

#include "dev.h"
#include "regs.h"

u32 cal_fec_mesh(u32 width, u32 height, u32 mode)
{
	u32 mesh_size, mesh_left_height;
	u32 w = ALIGN(width, 32);
	u32 h = ALIGN(height, 32);
	u32 spb_num = (h + 127) >> 7;
	u32 left_height = h & 127;
	u32 mesh_width = mode ? (w / 32 + 1) : (w / 16 + 1);
	u32 mesh_height = mode ? 9 : 17;

	if (!left_height)
		left_height = 128;
	mesh_left_height = mode ? (left_height / 16 + 1) :
				(left_height / 8 + 1);
	mesh_size = (spb_num - 1) * mesh_width * mesh_height +
		mesh_width * mesh_left_height;

	return mesh_size;
}

static const struct isppsd_fmt rkispp_formats[] = {
	{
		.mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
		.fourcc = V4L2_PIX_FMT_NV16,
		.wr_fmt = FMT_YUV422,
	},
};

static const struct isppsd_fmt *find_fmt(u32 mbus_code)
{
	const struct isppsd_fmt *fmt;
	int i, array_size = ARRAY_SIZE(rkispp_formats);

	for (i = 0; i < array_size; i++) {
		fmt = &rkispp_formats[i];
		if (fmt->mbus_code == mbus_code)
			return fmt;
	}

	return NULL;
}

static int rkispp_subdev_link_setup(struct media_entity *entity,
				    const struct media_pad *local,
				    const struct media_pad *remote,
				    u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct rkispp_subdev *ispp_sdev;
	struct rkispp_device *dev;
	struct rkispp_stream_vdev *vdev;
	struct rkispp_stream *stream = NULL;

	if (!sd)
		return -ENODEV;
	ispp_sdev = v4l2_get_subdevdata(sd);
	dev = ispp_sdev->dev;
	vdev = &dev->stream_vdev;

	if (!strcmp(remote->entity->name, II_VDEV_NAME)) {
		stream = &vdev->stream[STREAM_II];
		if (flags & MEDIA_LNK_FL_ENABLED)
			dev->inp = INP_DDR;
		else if (ispp_sdev->remote_sd)
			dev->inp = INP_ISP;
		else
			dev->inp = INP_INVAL;
	} else if (!strcmp(remote->entity->name, MB_VDEV_NAME)) {
		stream = &vdev->stream[STREAM_MB];
	} else if (!strcmp(remote->entity->name, S0_VDEV_NAME)) {
		stream = &vdev->stream[STREAM_S0];
	} else if (!strcmp(remote->entity->name, S1_VDEV_NAME)) {
		stream = &vdev->stream[STREAM_S1];
	} else if (!strcmp(remote->entity->name, S2_VDEV_NAME)) {
		stream = &vdev->stream[STREAM_S2];
	}
	if (stream)
		stream->linked = flags & MEDIA_LNK_FL_ENABLED;
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "input:%d\n", dev->inp);
	return 0;
}

static int rkispp_sd_get_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct rkispp_subdev *ispp_sdev = v4l2_get_subdevdata(sd);
	struct rkispp_device *dev = ispp_sdev->dev;
	const struct isppsd_fmt *ispp_fmt;
	int ret = 0;

	if (dev->inp != INP_ISP) {
		*fmt = ispp_sdev->in_fmt;
		return 0;
	}

	ret = v4l2_subdev_call(ispp_sdev->remote_sd,
			       pad, get_fmt, cfg, fmt);
	if (ret == 0) {
		ispp_fmt = find_fmt(fmt->format.code);
		if (!ispp_fmt)
			return -EINVAL;
		ispp_sdev->in_fmt = *fmt;
		ispp_sdev->out_fmt = *ispp_fmt;
		ispp_sdev->out_fmt.width = fmt->format.width;
		ispp_sdev->out_fmt.height = fmt->format.height;
	}

	return ret;
}

static int rkispp_sd_set_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct rkispp_subdev *ispp_sdev = v4l2_get_subdevdata(sd);
	struct rkispp_device *dev = ispp_sdev->dev;
	const struct isppsd_fmt *ispp_fmt;

	if (dev->inp != INP_ISP) {
		ispp_sdev->in_fmt = *fmt;
		return 0;
	}

	ispp_fmt = find_fmt(fmt->format.code);
	if (!ispp_fmt)
		return -EINVAL;

	ispp_sdev->out_fmt = *ispp_fmt;
	ispp_sdev->in_fmt = *fmt;
	return v4l2_subdev_call(ispp_sdev->remote_sd, pad,
				set_fmt, NULL, fmt);
}

static int rkispp_sd_s_stream(struct v4l2_subdev *sd, int on)
{
	struct rkispp_subdev *ispp_sdev = v4l2_get_subdevdata(sd);
	struct rkispp_device *dev = ispp_sdev->dev;
	struct rkispp_stream_vdev *vdev;

	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "s_stream on:%d\n", on);

	vdev = &dev->stream_vdev;
	if (on) {
		void *buf, *size;

		if (vdev->module_ens & ISPP_MODULE_TNR) {
			buf = &vdev->tnr_buf.pic_cur.dma_addr;
			size = &vdev->tnr_buf.pic_cur.size;
			v4l2_subdev_call(ispp_sdev->remote_sd,
				video, s_rx_buffer, buf, size);
			buf = &vdev->tnr_buf.gain_cur.dma_addr;
			size = &vdev->tnr_buf.gain_cur.size;
			if (vdev->tnr_mode) {
				v4l2_subdev_call(ispp_sdev->remote_sd,
					video, s_rx_buffer, buf, size);
				buf = &vdev->tnr_buf.pic_next.dma_addr;
				size = &vdev->tnr_buf.pic_next.size;
				v4l2_subdev_call(ispp_sdev->remote_sd,
					video, s_rx_buffer, buf, size);
				buf = &vdev->tnr_buf.gain_next.dma_addr;
				size = &vdev->tnr_buf.gain_next.size;
			}
		} else {
			buf = &vdev->nr_buf.pic_cur.dma_addr;
			size = &vdev->nr_buf.pic_cur.size;
			v4l2_subdev_call(ispp_sdev->remote_sd,
				video, s_rx_buffer, buf, size);
			buf = &vdev->nr_buf.gain_cur.dma_addr;
			size = &vdev->nr_buf.gain_cur.size;
		}
		v4l2_subdev_call(ispp_sdev->remote_sd,
			video, s_rx_buffer, buf, size);
	}
	return  v4l2_subdev_call(ispp_sdev->remote_sd,
				video, s_stream, on);
}

static int rkispp_sd_s_power(struct v4l2_subdev *sd, int on)
{
	struct rkispp_subdev *ispp_sdev = v4l2_get_subdevdata(sd);
	struct rkispp_device *ispp_dev = ispp_sdev->dev;
	int ret;

	v4l2_dbg(1, rkispp_debug, &ispp_dev->v4l2_dev,
		 "s_power on:%d\n", on);

	if (on) {
		ret = pm_runtime_get_sync(ispp_dev->dev);
		if (ret < 0) {
			v4l2_err(&ispp_dev->v4l2_dev,
				 "%s runtime get failed:%d\n",
				 __func__, ret);
			return ret;
		}
		atomic_set(&ispp_sdev->frm_sync_seq, 0);
		rkispp_soft_reset(ispp_dev->base_addr);
		writel(0xfffffff, ispp_dev->base_addr + RKISPP_CTRL_INT_MSK);
		if (ispp_dev->inp == INP_ISP) {
			struct v4l2_subdev_format *fmt = &ispp_sdev->in_fmt;

			fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
			ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, fmt);
			if (ret < 0) {
				v4l2_err(&ispp_dev->v4l2_dev,
					 "%s get format fail:%d\n",
					 __func__, ret);
				goto err;
			}
			ret = v4l2_subdev_call(ispp_sdev->remote_sd,
					       core, s_power, 1);
			if (ret < 0) {
				v4l2_err(&ispp_dev->v4l2_dev,
					 "%s set isp power on fail:%d\n",
					 __func__, ret);
				goto err;
			}
		}
	} else {
		writel(0, ispp_dev->base_addr + RKISPP_CTRL_INT_MSK);
		if (ispp_dev->inp == INP_ISP)
			v4l2_subdev_call(ispp_sdev->remote_sd, core, s_power, 0);
		ret = pm_runtime_put(ispp_dev->dev);
		if (ret < 0)
			v4l2_err(&ispp_dev->v4l2_dev,
				 "%s runtime put failed:%d\n",
				 __func__, ret);
	}

	return ret;
err:
	pm_runtime_put(ispp_dev->dev);
	return ret;
}

static const struct media_entity_operations rkispp_sd_media_ops = {
	.link_setup = rkispp_subdev_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_pad_ops rkispp_sd_pad_ops = {
	.get_fmt = rkispp_sd_get_fmt,
	.set_fmt = rkispp_sd_set_fmt,
};

static const struct v4l2_subdev_video_ops rkispp_sd_video_ops = {
	.s_stream = rkispp_sd_s_stream,
};

static const struct v4l2_subdev_core_ops rkispp_sd_core_ops = {
	.s_power = rkispp_sd_s_power,
};

static struct v4l2_subdev_ops rkispp_sd_ops = {
	.core = &rkispp_sd_core_ops,
	.video = &rkispp_sd_video_ops,
	.pad = &rkispp_sd_pad_ops,
};

int rkispp_register_subdev(struct rkispp_device *dev,
			   struct v4l2_device *v4l2_dev)
{
	struct rkispp_subdev *ispp_sdev = &dev->ispp_sdev;
	struct v4l2_subdev *sd;
	int ret;

	memset(ispp_sdev, 0, sizeof(*ispp_sdev));
	ispp_sdev->dev = dev;
	sd = &ispp_sdev->sd;

	v4l2_subdev_init(sd, &rkispp_sd_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->entity.ops = &rkispp_sd_media_ops;
	snprintf(sd->name, sizeof(sd->name), "rkispp-subdev");

	ispp_sdev->pads[RKISPP_PAD_SINK].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	ispp_sdev->pads[RKISPP_PAD_SINK_PARAMS].flags = MEDIA_PAD_FL_SINK;
	ispp_sdev->pads[RKISPP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	ispp_sdev->pads[RKISPP_PAD_SOURCE_STATS].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&sd->entity, RKISPP_PAD_MAX,
				     ispp_sdev->pads);
	if (ret < 0)
		return ret;
	sd->owner = THIS_MODULE;
	v4l2_set_subdevdata(sd, ispp_sdev);
	sd->grp_id = GRP_ID_ISPP;
	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0)
		goto free_media;

	ret = v4l2_device_register_subdev_nodes(v4l2_dev);
	if (ret < 0)
		goto free_subdev;
	return ret;
free_subdev:
	v4l2_device_unregister_subdev(sd);
free_media:
	media_entity_cleanup(&sd->entity);
	v4l2_err(sd, "Failed to register subdev, ret:%d\n", ret);
	return ret;
}

void rkispp_unregister_subdev(struct rkispp_device *dev)
{
	struct v4l2_subdev *sd = &dev->ispp_sdev.sd;

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
}
