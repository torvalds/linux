// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>
#include <media/media-entity.h>
#include <media/videobuf2-dma-contig.h>

#include "dev.h"
#include "regs.h"

void rkispp_free_pool(struct rkispp_stream_vdev *vdev)
{
	const struct vb2_mem_ops *ops = &vb2_dma_contig_memops;
	struct rkispp_isp_buf_pool *buf;
	int i, j;

	for (i = 0; i < RKISPP_BUF_POOL_MAX; i++) {
		buf = &vdev->pool[i];
		if (!buf->dbufs)
			break;
		for (j = 0; j < GROUP_BUF_MAX; j++) {
			if (buf->mem_priv[j]) {
				ops->unmap_dmabuf(buf->mem_priv[j]);
				ops->detach_dmabuf(buf->mem_priv[j]);
				buf->mem_priv[j] = NULL;
			}
		}
		buf->dbufs = NULL;
	}
}

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

	if (local->index != RKISPP_PAD_SINK &&
	    local->index != RKISPP_PAD_SOURCE)
		return 0;

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
		stream->linked = flags & MEDIA_LNK_FL_ENABLED;
		v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
			 "input:%d\n", dev->inp);
	} else if (!strcmp(remote->entity->name, MB_VDEV_NAME)) {
		stream = &vdev->stream[STREAM_MB];
	} else if (!strcmp(remote->entity->name, S0_VDEV_NAME)) {
		stream = &vdev->stream[STREAM_S0];
	} else if (!strcmp(remote->entity->name, S1_VDEV_NAME)) {
		stream = &vdev->stream[STREAM_S1];
	} else if (!strcmp(remote->entity->name, S2_VDEV_NAME)) {
		stream = &vdev->stream[STREAM_S2];
	}
	if (stream && dev->stream_sync) {
		stream->linked = flags & MEDIA_LNK_FL_ENABLED;
		v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
			 "stream:%d linked:%d\n",
			 stream->id, stream->linked);
	}
	return 0;
}

static int rkispp_sd_get_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct rkispp_subdev *ispp_sdev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;
	const struct isppsd_fmt *ispp_fmt;
	int ret = 0;

	if (!fmt)
		goto err;

	if (fmt->pad != RKISPP_PAD_SINK &&
	    fmt->pad != RKISPP_PAD_SOURCE)
		goto err;

	mf = &fmt->format;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (!cfg)
			goto err;
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
	}

	if (ispp_sdev->dev->inp != INP_ISP) {
		*mf = ispp_sdev->in_fmt;
		return 0;
	}

	if (fmt->pad == RKISPP_PAD_SINK) {
		ret = v4l2_subdev_call(ispp_sdev->remote_sd,
				       pad, get_fmt, cfg, fmt);
		if (!ret) {
			ispp_fmt = find_fmt(fmt->format.code);
			if (!ispp_fmt)
				goto err;
			ispp_sdev->in_fmt = *mf;
			ispp_sdev->out_fmt = *ispp_fmt;
		}
	} else {
		*mf = ispp_sdev->in_fmt;
		mf->width = ispp_sdev->out_fmt.width;
		mf->height = ispp_sdev->out_fmt.height;
	}
	return ret;
err:
	return -EINVAL;
}

static int rkispp_sd_set_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	/* format from isp output or rkispp_m_bypass input */
	return 0;
}

static int rkispp_sd_get_selection(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_selection *sel)
{
	struct rkispp_subdev *ispp_sdev = v4l2_get_subdevdata(sd);
	struct v4l2_rect *crop;
	int ret = 0;

	if (!sel)
		goto err;
	if (sel->pad != RKISPP_PAD_SINK)
		goto err;

	crop = &sel->r;
	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (!cfg)
			goto err;
		crop = v4l2_subdev_get_try_crop(sd, cfg, sel->pad);
	}

	if (ispp_sdev->dev->inp != INP_ISP) {
		crop->left = 0;
		crop->top = 0;
		crop->width = ispp_sdev->in_fmt.width;
		crop->height = ispp_sdev->in_fmt.height;
		return 0;
	}

	ret = v4l2_subdev_call(ispp_sdev->remote_sd,
			pad, get_selection, cfg, sel);
	if (!ret && sel->target == V4L2_SEL_TGT_CROP) {
		ispp_sdev->out_fmt.width = crop->width;
		ispp_sdev->out_fmt.height = crop->height;
	}

	return ret;
err:
	return -EINVAL;
}

static int rkispp_sd_set_selection(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_selection *sel)
{
	struct rkispp_subdev *ispp_sdev = v4l2_get_subdevdata(sd);
	struct v4l2_rect *crop;
	int ret = 0;

	if (!sel)
		goto err;
	if (sel->pad != RKISPP_PAD_SINK ||
	    sel->target != V4L2_SEL_TGT_CROP)
		goto err;

	crop = &sel->r;
	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (!cfg)
			goto err;
		crop = v4l2_subdev_get_try_crop(sd, cfg, sel->pad);
	}

	if (ispp_sdev->dev->inp != INP_ISP) {
		crop->left = 0;
		crop->top = 0;
		crop->width = ispp_sdev->in_fmt.width;
		crop->height = ispp_sdev->in_fmt.height;
		return 0;
	}

	ret = v4l2_subdev_call(ispp_sdev->remote_sd,
			pad, set_selection, cfg, sel);
	if (!ret) {
		ispp_sdev->out_fmt.width = crop->width;
		ispp_sdev->out_fmt.height = crop->height;
	}

	return ret;
err:
	return -EINVAL;
}

static int rkispp_sd_s_stream(struct v4l2_subdev *sd, int on)
{
	struct rkispp_subdev *ispp_sdev = v4l2_get_subdevdata(sd);
	struct rkispp_device *dev = ispp_sdev->dev;
	int ret;

	v4l2_dbg(1, rkispp_debug, &ispp_sdev->dev->v4l2_dev,
		 "s_stream on:%d\n", on);

	if (on)
		ispp_sdev->state = ISPP_START;
	ret = v4l2_subdev_call(ispp_sdev->remote_sd,
			       video, s_stream, on);
	if ((on && ret) || (!on && !ret)) {
		ispp_sdev->state = ISPP_STOP;
		rkispp_free_pool(&dev->stream_vdev);
	}
	return ret;
}

static int rkispp_sd_s_rx_buffer(struct v4l2_subdev *sd,
				 void *buf, unsigned int *size)
{
	const struct vb2_mem_ops *ops = &vb2_dma_contig_memops;
	struct rkispp_subdev *ispp_sdev = v4l2_get_subdevdata(sd);
	struct rkispp_device *dev = ispp_sdev->dev;
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	struct rkisp_ispp_buf *dbufs = buf;
	struct rkispp_isp_buf_pool *pool;
	u32 i, val = (vdev->module_ens & ISPP_MODULE_TNR) ?
			ISPP_MODULE_TNR : ISPP_MODULE_NR;
	int ret = 0;
	void *mem;

	/* size isn't using now */
	if (!dbufs)
		return -EINVAL;

	if (ispp_sdev->state == ISPP_START) {
		rkispp_module_work_event(dev, dbufs, NULL, val, false);
		return ret;
	}

	/* init dma buf pool */
	for (i = 0; i < RKISPP_BUF_POOL_MAX; i++) {
		pool = &vdev->pool[i];
		if (!pool->dbufs)
			break;
	}
	pool->dbufs = dbufs;
	for (i = 0; i < GROUP_BUF_MAX; i++) {
		mem = ops->attach_dmabuf(dev->dev, dbufs->dbuf[i],
			dbufs->dbuf[i]->size, DMA_BIDIRECTIONAL);
		if (IS_ERR(mem)) {
			ret = PTR_ERR(mem);
			goto err;
		}
		pool->mem_priv[i] = mem;
		ret = ops->map_dmabuf(mem);
		if (ret)
			goto err;
		pool->dma[i] = *((dma_addr_t *)ops->cookie(mem));
		v4l2_dbg(1, rkispp_debug, sd,
			 "dma[%d]:0x%x\n", i, pool->dma[i]);
	}
	return 0;
err:
	rkispp_free_pool(vdev);
	return ret;
}

static int rkispp_sd_s_power(struct v4l2_subdev *sd, int on)
{
	struct rkispp_subdev *ispp_sdev = v4l2_get_subdevdata(sd);
	struct rkispp_device *ispp_dev = ispp_sdev->dev;
	void __iomem *base = ispp_dev->base_addr;
	struct iommu_domain *domain;
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
		writel(SW_SCL_BYPASS, base + RKISPP_SCL0_CTRL);
		writel(SW_SCL_BYPASS, base + RKISPP_SCL1_CTRL);
		writel(SW_SCL_BYPASS, base + RKISPP_SCL2_CTRL);
		writel(OTHER_FORCE_UPD, base + RKISPP_CTRL_UPDATE);
		writel(SW_SHP_DMA_DIS, base + RKISPP_SHARP_CORE_CTRL);
		writel(SW_FEC2DDR_DIS, base + RKISPP_FEC_CORE_CTRL);
		writel(0xfffffff, base + RKISPP_CTRL_INT_MSK);
		writel(GATE_DIS_ALL, base + RKISPP_CTRL_CLKGATE);
		usleep_range(100, 120);
		writel(GATE_DIS_NR, base + RKISPP_CTRL_CLKGATE);
		if (ispp_dev->inp == INP_ISP) {
			struct v4l2_subdev_format fmt;
			struct v4l2_subdev_selection sel;

			/* update format, if ispp input change */
			fmt.pad = RKISPP_PAD_SINK;
			fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
			ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &fmt);
			if (ret) {
				v4l2_err(&ispp_dev->v4l2_dev,
					 "%s get format fail:%d\n",
					 __func__, ret);
				goto err;
			}
			sel.pad = RKISPP_PAD_SINK;
			sel.target = V4L2_SEL_TGT_CROP;
			sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
			ret = v4l2_subdev_call(sd, pad,
				get_selection, NULL, &sel);
			if (ret) {
				v4l2_err(&ispp_dev->v4l2_dev,
					 "%s get crop fail:%d\n",
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
		rkispp_soft_reset(ispp_dev->base_addr);
		domain = iommu_get_domain_for_dev(ispp_dev->dev);
		if (domain) {
#ifdef CONFIG_IOMMU_API
			domain->ops->detach_dev(domain, ispp_dev->dev);
			domain->ops->attach_dev(domain, ispp_dev->dev);
#endif
		}
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
	.get_selection = rkispp_sd_get_selection,
	.set_selection = rkispp_sd_set_selection,
};

static const struct v4l2_subdev_video_ops rkispp_sd_video_ops = {
	.s_stream = rkispp_sd_s_stream,
	.s_rx_buffer = rkispp_sd_s_rx_buffer,
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
	ispp_sdev->state = ISPP_STOP;
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
