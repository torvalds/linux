// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mc.h>
#include <uapi/linux/rk-video-format.h>
#include "dev.h"
#include "regs.h"

static void fec_enable(struct rkispp_params_vdev *params_vdev, bool en)
{
	struct rkispp_device *dev = params_vdev->dev;
	u32 buf_idx;

	if (en) {
		buf_idx = params_vdev->buf_fec_idx;
		if (!params_vdev->buf_fec[buf_idx].vaddr) {
			dev_err(dev->dev, "no fec buffer allocated\n");
			return;
		}
	}
	rkispp_set_bits(params_vdev->dev, RKISPP_FEC_CORE_CTRL, SW_FEC_EN, en);
}

static void fec_config(struct rkispp_params_vdev *params_vdev,
		       struct fec_config *arg)
{
	struct rkispp_device *dev = params_vdev->dev;
	struct rkispp_fec_head *fec_data;
	u32 width, height, mesh_size;
	dma_addr_t dma_addr;
	u32 val, i, buf_idx;

	width = dev->ispp_sdev.out_fmt.width;
	height = dev->ispp_sdev.out_fmt.height;
	mesh_size = cal_fec_mesh(width, height, 1);
	if (arg->mesh_size > mesh_size) {
		v4l2_err(&dev->v4l2_dev,
			 "Input mesh size too large. mesh size 0x%x, 0x%x\n",
			 arg->mesh_size, mesh_size);
		return;
	}

	for (i = 0; i < FEC_MESH_BUF_NUM; i++) {
		if (arg->buf_fd == params_vdev->buf_fec[i].dma_fd)
			break;
	}
	if (i == FEC_MESH_BUF_NUM) {
		dev_err(dev->dev, "cannot find fec buf fd(%d)\n", arg->buf_fd);
		return;
	}

	if (!params_vdev->buf_fec[i].vaddr) {
		dev_err(dev->dev, "no fec buffer allocated\n");
		return;
	}

	buf_idx = params_vdev->buf_fec_idx;
	fec_data = (struct rkispp_fec_head *)params_vdev->buf_fec[buf_idx].vaddr;
	fec_data->stat = FEC_BUF_INIT;

	buf_idx = i;
	fec_data = (struct rkispp_fec_head *)params_vdev->buf_fec[buf_idx].vaddr;
	fec_data->stat = FEC_BUF_CHIPINUSE;
	params_vdev->buf_fec_idx = buf_idx;

	rkispp_prepare_buffer(dev, &params_vdev->buf_fec[buf_idx]);

	dma_addr = params_vdev->buf_fec[buf_idx].dma_addr;
	val = dma_addr + fec_data->meshxf_oft;
	rkispp_write(params_vdev->dev, RKISPP_FEC_MESH_XFRA_BASE, val);
	val = dma_addr + fec_data->meshyf_oft;
	rkispp_write(params_vdev->dev, RKISPP_FEC_MESH_YFRA_BASE, val);
	val = dma_addr + fec_data->meshxi_oft;
	rkispp_write(params_vdev->dev, RKISPP_FEC_MESH_XINT_BASE, val);
	val = dma_addr + fec_data->meshyi_oft;
	rkispp_write(params_vdev->dev, RKISPP_FEC_MESH_YINT_BASE, val);

	val = 0;
	if (arg->mesh_density)
		val = SW_MESH_DENSITY;
	rkispp_set_bits(params_vdev->dev, RKISPP_FEC_CORE_CTRL, SW_MESH_DENSITY, val);

	rkispp_write(params_vdev->dev, RKISPP_FEC_MESH_SIZE, arg->mesh_size);

	val = arg->dst_width << 16 | arg->dst_height;
	rkispp_write(params_vdev->dev, RKISPP_FEC_DST_SIZE, val);

	val = arg->src_width << 16 | arg->src_height;
	rkispp_write(params_vdev->dev, RKISPP_FEC_SRC_SIZE, val);

	val = arg->fec_bic_mode;
	rkispp_set_bits(params_vdev->dev, RKISPP_FEC_CORE_CTRL, SW_BIC_MODE, val);
}

static void fec_data_abandon(struct rkispp_params_vdev *vdev,
			     struct fec_params_cfg *params)
{
	struct rkispp_fec_head *data;
	int i;

	for (i = 0; i < FEC_MESH_BUF_NUM; i++) {
		if (params->fec_cfg.buf_fd == vdev->buf_fec[i].dma_fd) {
			data = (struct rkispp_fec_head *)vdev->buf_fec[i].vaddr;
			if (data)
				data->stat = FEC_BUF_INIT;
			break;
		}
	}
}

static void rkispp_params_cfg(struct rkispp_params_vdev *params_vdev, u32 frame_id)
{
	struct fec_params_cfg *new_params = NULL;
	u32 module_en_update, module_cfg_update, module_ens;

	spin_lock(&params_vdev->config_lock);
	if (!params_vdev->streamon) {
		spin_unlock(&params_vdev->config_lock);
		return;
	}

	/* get buffer by frame_id */
	while (!list_empty(&params_vdev->params) && !params_vdev->cur_buf) {
		params_vdev->cur_buf = list_first_entry(&params_vdev->params,
				struct rkispp_buffer, queue);

		new_params = (struct fec_params_cfg *)(params_vdev->cur_buf->vaddr[0]);
		if (new_params->frame_id < frame_id) {
			if (new_params->module_cfg_update & ISPP_MODULE_FEC)
				fec_data_abandon(params_vdev, new_params);
			list_del(&params_vdev->cur_buf->queue);
			vb2_buffer_done(&params_vdev->cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
			params_vdev->cur_buf = NULL;
			continue;
		} else if (new_params->frame_id == frame_id) {
			list_del(&params_vdev->cur_buf->queue);
		} else {
			params_vdev->cur_buf = NULL;
		}
		break;
	}

	if (!params_vdev->cur_buf) {
		spin_unlock(&params_vdev->config_lock);
		return;
	}

	new_params = (struct fec_params_cfg *)(params_vdev->cur_buf->vaddr[0]);

	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens;
	if (params_vdev->dev->hw_dev->is_fec_ext) {
		module_en_update &= ~ISPP_MODULE_FEC;
		module_cfg_update &= ~ISPP_MODULE_FEC;
		module_ens &= ~ISPP_MODULE_FEC;
	}


	if (module_cfg_update & ISPP_MODULE_FEC)
		fec_config(params_vdev,
			   &new_params->fec_cfg);
	if (module_en_update & ISPP_MODULE_FEC)
		fec_enable(params_vdev,
			   !!(module_ens & ISPP_MODULE_FEC));

	vb2_buffer_done(&params_vdev->cur_buf->vb.vb2_buf,
				VB2_BUF_STATE_DONE);
	params_vdev->cur_buf = NULL;

	spin_unlock(&params_vdev->config_lock);
}

static void params_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkispp_buffer *params_buf = to_rkispp_buffer(vbuf);
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkispp_params_vdev *params_vdev = vq->drv_priv;
	struct fec_params_cfg *new_params;
	unsigned long flags;

	new_params = (struct fec_params_cfg *)vb2_plane_vaddr(vb, 0);
	spin_lock_irqsave(&params_vdev->config_lock, flags);
	if (params_vdev->first_params) {
		params_vdev->first_params = false;
		wake_up(&params_vdev->dev->sync_onoff);
	}
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);
	params_buf->vaddr[0] = new_params;
	spin_lock_irqsave(&params_vdev->config_lock, flags);
	list_add_tail(&params_buf->queue, &params_vdev->params);
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);
}

static struct rkispp_params_ops rkispp_params_ops = {
	.rkispp_params_cfg = rkispp_params_cfg,
	.rkispp_params_vb2_buf_queue = params_vb2_buf_queue,
};

void rkispp_params_init_ops_v20(struct rkispp_params_vdev *params_vdev)
{
	params_vdev->params_ops = &rkispp_params_ops;
}
