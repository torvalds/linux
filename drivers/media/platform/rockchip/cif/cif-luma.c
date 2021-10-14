// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#include <linux/kfifo.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include "dev.h"
#include "regs.h"
#include "cif-luma.h"
#include "mipi-csi2.h"

#define RKCIF_LUMA_REQ_BUFS_MIN		2
#define RKCIF_LUMA_REQ_BUFS_MAX		8
#define SW_Y_STAT_RD_ID_MASK		GENMASK(5, 4)
#define SW_Y_STAT_RD_BLOCK_MASK		GENMASK(7, 6)
#define SW_Y_STAT_EN			BIT(0)
#define SW_Y_STAT_RD_EN			BIT(3)
#define SW_Y_STAT_BAYER_TYPE(a)		(((a) & 0x3) << 1)
#define SW_Y_STAT_RD_ID(a)		(((a) & 0x3) << 4)
#define SW_Y_STAT_RD_BLOCK(a)		(((a) & 0x3) << 6)

static int rkcif_luma_enum_fmt_meta_cap(struct file *file, void *priv,
					struct v4l2_fmtdesc *f)
{
	struct video_device *video = video_devdata(file);
	struct rkcif_luma_vdev *luma_vdev = video_get_drvdata(video);

	if (f->index > 0 || f->type != video->queue->type)
		return -EINVAL;

	f->pixelformat = luma_vdev->vdev_fmt.fmt.meta.dataformat;
	return 0;
}

static int rkcif_luma_g_fmt_meta_cap(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct rkcif_luma_vdev *luma_vdev = video_get_drvdata(video);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != video->queue->type)
		return -EINVAL;

	memset(meta, 0, sizeof(*meta));
	meta->dataformat = luma_vdev->vdev_fmt.fmt.meta.dataformat;
	meta->buffersize = luma_vdev->vdev_fmt.fmt.meta.buffersize;

	return 0;
}

static int rkcif_luma_querycap(struct file *file,
			       void *priv, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct rkcif_luma_vdev *luma_vdev = video_get_drvdata(vdev);
	struct device *dev = luma_vdev->cifdev->dev;

	strlcpy(cap->driver, dev->driver->name, sizeof(cap->driver));
	strlcpy(cap->card, dev->driver->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", dev_name(dev));

	return 0;
}

/* ISP video device IOCTLs */
static const struct v4l2_ioctl_ops rkcif_luma_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_meta_cap = rkcif_luma_enum_fmt_meta_cap,
	.vidioc_g_fmt_meta_cap = rkcif_luma_g_fmt_meta_cap,
	.vidioc_s_fmt_meta_cap = rkcif_luma_g_fmt_meta_cap,
	.vidioc_try_fmt_meta_cap = rkcif_luma_g_fmt_meta_cap,
	.vidioc_querycap = rkcif_luma_querycap
};

static int rkcif_luma_fh_open(struct file *filp)
{
	struct rkcif_luma_vdev *params = video_drvdata(filp);
	int ret;

	ret = v4l2_fh_open(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_get(&params->vnode.vdev.entity);
		if (ret < 0)
			vb2_fop_release(filp);
	}

	return ret;
}

static int rkcif_luma_fop_release(struct file *file)
{
	struct rkcif_luma_vdev *luma = video_drvdata(file);
	int ret;

	ret = vb2_fop_release(file);
	if (!ret)
		v4l2_pipeline_pm_put(&luma->vnode.vdev.entity);
	return ret;
}

struct v4l2_file_operations rkcif_luma_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = rkcif_luma_fh_open,
	.release = rkcif_luma_fop_release
};

static int rkcif_luma_vb2_queue_setup(struct vb2_queue *vq,
				      unsigned int *num_buffers,
				      unsigned int *num_planes,
				      unsigned int sizes[],
				      struct device *alloc_ctxs[])
{
	struct rkcif_luma_vdev *luma_vdev = vq->drv_priv;

	*num_planes = 1;

	*num_buffers = clamp_t(u32, *num_buffers, RKCIF_LUMA_REQ_BUFS_MIN,
			       RKCIF_LUMA_REQ_BUFS_MAX);

	sizes[0] = sizeof(struct rkisp_isp2x_luma_buffer);

	INIT_LIST_HEAD(&luma_vdev->stat);

	return 0;
}

static void rkcif_luma_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkcif_buffer *luma_buf = to_rkcif_buffer(vbuf);
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkcif_luma_vdev *luma_dev = vq->drv_priv;

	luma_buf->vaddr[0] = vb2_plane_vaddr(vb, 0);

	spin_lock_bh(&luma_dev->rd_lock);
	list_add_tail(&luma_buf->queue, &luma_dev->stat);
	spin_unlock_bh(&luma_dev->rd_lock);
}

static void rkcif_luma_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rkcif_luma_vdev *luma_vdev = vq->drv_priv;
	struct rkcif_buffer *buf;
	unsigned long flags;
	int i;

	/* Make sure no new work queued in isr before draining wq */
	spin_lock_irqsave(&luma_vdev->irq_lock, flags);
	luma_vdev->streamon = false;
	spin_unlock_irqrestore(&luma_vdev->irq_lock, flags);

	tasklet_disable(&luma_vdev->rd_tasklet);

	spin_lock_bh(&luma_vdev->rd_lock);
	for (i = 0; i < RKCIF_LUMA_REQ_BUFS_MAX; i++) {
		if (list_empty(&luma_vdev->stat))
			break;
		buf = list_first_entry(&luma_vdev->stat,
				       struct rkcif_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_bh(&luma_vdev->rd_lock);
}

static int
rkcif_luma_vb2_start_streaming(struct vb2_queue *queue,
			       unsigned int count)
{
	struct rkcif_luma_vdev *luma_vdev = queue->drv_priv;
	u32 i;

	for (i = 0; i < RKCIF_RAW_MAX; i++)
		luma_vdev->ystat_rdflg[i] = false;

	luma_vdev->streamon = true;
	kfifo_reset(&luma_vdev->rd_kfifo);
	tasklet_enable(&luma_vdev->rd_tasklet);

	return 0;
}

static struct vb2_ops rkcif_luma_vb2_ops = {
	.queue_setup = rkcif_luma_vb2_queue_setup,
	.buf_queue = rkcif_luma_vb2_buf_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = rkcif_luma_vb2_stop_streaming,
	.start_streaming = rkcif_luma_vb2_start_streaming,
};

static int rkcif_luma_init_vb2_queue(struct vb2_queue *q,
				     struct rkcif_luma_vdev *luma_vdev)
{
	q->type = V4L2_BUF_TYPE_META_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = luma_vdev;
	q->ops = &rkcif_luma_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->buf_struct_size = sizeof(struct rkcif_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &luma_vdev->vnode.vlock;
	q->dev = luma_vdev->cifdev->dev;

	return vb2_queue_init(q);
}

static void
rkcif_stats_send_luma(struct rkcif_luma_vdev *vdev,
		      struct rkcif_luma_readout_work *work)
{
	unsigned int cur_frame_id;
	struct rkisp_isp2x_luma_buffer *cur_stat_buf;
	struct rkcif_buffer *cur_buf = NULL;
	u32 i, j;

	spin_lock(&vdev->rd_lock);
	/* get one empty buffer */
	if (!list_empty(&vdev->stat)) {
		cur_buf = list_first_entry(&vdev->stat,
					   struct rkcif_buffer, queue);
		list_del(&cur_buf->queue);
	}
	spin_unlock(&vdev->rd_lock);

	if (!cur_buf) {
		v4l2_warn(vdev->vnode.vdev.v4l2_dev,
			  "no luma buffer available\n");
		return;
	}

	cur_stat_buf =
		(struct rkisp_isp2x_luma_buffer *)(cur_buf->vaddr[0]);
	if (!cur_stat_buf) {
		v4l2_err(vdev->vnode.vdev.v4l2_dev,
			 "cur_stat_buf is NULL\n");
		return;
	}

	cur_stat_buf->frame_id = work->frame_id;
	cur_stat_buf->meas_type = work->meas_type;
	for (i = 0; i < RKCIF_RAW_MAX; i++) {
		for (j = 0; j < ISP2X_MIPI_LUMA_MEAN_MAX; j++) {
			cur_stat_buf->luma[i].exp_mean[j] =
				work->luma[i].exp_mean[j];
		}
	}

	cur_frame_id = cur_stat_buf->frame_id;
	vb2_set_plane_payload(&cur_buf->vb.vb2_buf, 0,
			      sizeof(struct rkisp_isp2x_luma_buffer));
	cur_buf->vb.sequence = cur_frame_id;
	cur_buf->vb.vb2_buf.timestamp = work->timestamp;
	vb2_buffer_done(&cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

static void rkcif_luma_readout_task(unsigned long data)
{
	unsigned int out = 0;
	struct rkcif_luma_readout_work work;
	struct rkcif_luma_vdev *vdev =
		(struct rkcif_luma_vdev *)data;

	while (!kfifo_is_empty(&vdev->rd_kfifo)) {
		out = kfifo_out(&vdev->rd_kfifo,
				&work, sizeof(work));
		if (!out)
			break;

		if (work.readout == RKCIF_READOUT_LUMA)
			rkcif_stats_send_luma(vdev, &work);
	}
}

void rkcif_luma_isr(struct rkcif_luma_vdev *luma_vdev, int mipi_id, u32 frame_id)
{
	u8 hdr_mode = luma_vdev->cifdev->hdr.hdr_mode;
	enum rkcif_luma_frm_mode frm_mode;
	bool send_task;
	u32 i, value;

	spin_lock(&luma_vdev->irq_lock);
	if (!luma_vdev->streamon)
		goto unlock;

	switch (hdr_mode) {
	case NO_HDR:
		frm_mode = RKCIF_LUMA_ONEFRM;
		break;
	case HDR_X2:
		frm_mode = RKCIF_LUMA_TWOFRM;
		break;
	case HDR_X3:
		frm_mode = RKCIF_LUMA_THREEFRM;
		break;
	default:
		goto unlock;
	}

	if (mipi_id == RKCIF_STREAM_MIPI_ID0 && !luma_vdev->ystat_rdflg[0]) {
		value = rkcif_read_register(luma_vdev->cifdev, CIF_REG_Y_STAT_CONTROL);
		value &= ~(SW_Y_STAT_RD_ID_MASK | SW_Y_STAT_RD_BLOCK_MASK);
		value |= SW_Y_STAT_RD_ID(0x0) | SW_Y_STAT_RD_EN;
		rkcif_write_register(luma_vdev->cifdev, CIF_REG_Y_STAT_CONTROL, value);
		for (i = 0; i < ISP2X_MIPI_LUMA_MEAN_MAX; i++)
			luma_vdev->work.luma[0].exp_mean[i] =
				rkcif_read_register(luma_vdev->cifdev, CIF_REG_Y_STAT_VALUE);

		luma_vdev->ystat_rdflg[0] = true;
	}
	if (mipi_id == RKCIF_STREAM_MIPI_ID1 && !luma_vdev->ystat_rdflg[1]) {
		value = rkcif_read_register(luma_vdev->cifdev, CIF_REG_Y_STAT_CONTROL);
		value &= ~(SW_Y_STAT_RD_ID_MASK | SW_Y_STAT_RD_BLOCK_MASK);
		value |= SW_Y_STAT_RD_ID(0x1) | SW_Y_STAT_RD_EN;
		rkcif_write_register(luma_vdev->cifdev, CIF_REG_Y_STAT_CONTROL, value);
		for (i = 0; i < ISP2X_MIPI_LUMA_MEAN_MAX; i++)
			luma_vdev->work.luma[1].exp_mean[i] =
				rkcif_read_register(luma_vdev->cifdev, CIF_REG_Y_STAT_VALUE);

		luma_vdev->ystat_rdflg[1] = true;
	}
	if (mipi_id == RKCIF_STREAM_MIPI_ID2 && !luma_vdev->ystat_rdflg[2]) {
		value = rkcif_read_register(luma_vdev->cifdev, CIF_REG_Y_STAT_CONTROL);
		value &= ~(SW_Y_STAT_RD_ID_MASK | SW_Y_STAT_RD_BLOCK_MASK);
		value |= SW_Y_STAT_RD_ID(0x2) | SW_Y_STAT_RD_EN;
		rkcif_write_register(luma_vdev->cifdev, CIF_REG_Y_STAT_CONTROL, value);
		for (i = 0; i < ISP2X_MIPI_LUMA_MEAN_MAX; i++)
			luma_vdev->work.luma[2].exp_mean[i] =
				rkcif_read_register(luma_vdev->cifdev, CIF_REG_Y_STAT_VALUE);

		luma_vdev->ystat_rdflg[2] = true;
	}

	send_task = false;
	if (frm_mode == RKCIF_LUMA_THREEFRM) {
		if (luma_vdev->ystat_rdflg[0] && luma_vdev->ystat_rdflg[1] &&
		    luma_vdev->ystat_rdflg[2])
			send_task = true;
	} else if (frm_mode == RKCIF_LUMA_TWOFRM) {
		if (luma_vdev->ystat_rdflg[0] && luma_vdev->ystat_rdflg[1])
			send_task = true;
	} else {
		if (luma_vdev->ystat_rdflg[0])
			send_task = true;
	}

	if (send_task) {
		luma_vdev->work.readout = RKCIF_READOUT_LUMA;
		luma_vdev->work.timestamp = ktime_get_ns();
		luma_vdev->work.frame_id = frame_id;

		if (frm_mode == RKCIF_LUMA_THREEFRM)
			luma_vdev->work.meas_type = ISP2X_RAW0_Y_STATE | ISP2X_RAW1_Y_STATE |
						    ISP2X_RAW2_Y_STATE;
		else if (frm_mode == RKCIF_LUMA_TWOFRM)
			luma_vdev->work.meas_type = ISP2X_RAW0_Y_STATE | ISP2X_RAW1_Y_STATE;
		else
			luma_vdev->work.meas_type = ISP2X_RAW0_Y_STATE;

		if (!kfifo_is_full(&luma_vdev->rd_kfifo))
			kfifo_in(&luma_vdev->rd_kfifo,
				 &luma_vdev->work, sizeof(luma_vdev->work));
		else
			v4l2_err(luma_vdev->vnode.vdev.v4l2_dev,
				 "stats kfifo is full\n");

		tasklet_schedule(&luma_vdev->rd_tasklet);

		for (i = 0; i < RKCIF_RAW_MAX; i++)
			luma_vdev->ystat_rdflg[i] = false;

		memset(&luma_vdev->work, 0, sizeof(luma_vdev->work));
	}

unlock:
	spin_unlock(&luma_vdev->irq_lock);
}

void rkcif_start_luma(struct rkcif_luma_vdev *luma_vdev, const struct cif_input_fmt *cif_fmt_in)
{
	u32 bayer = 0;

	if (cif_fmt_in->fmt_type != CIF_FMT_TYPE_RAW)
		return;

	switch (cif_fmt_in->mbus_code) {
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SBGGR12_1X12:
		bayer = 3;
		break;
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
		bayer = 2;
		break;
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
		bayer = 1;
		break;
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		bayer = 0;
		break;
	}

	rkcif_write_register(luma_vdev->cifdev, CIF_REG_Y_STAT_CONTROL,
			     SW_Y_STAT_BAYER_TYPE(bayer) | SW_Y_STAT_EN);
	luma_vdev->enable = true;
}

void rkcif_stop_luma(struct rkcif_luma_vdev *luma_vdev)
{
	rkcif_write_register(luma_vdev->cifdev, CIF_REG_Y_STAT_CONTROL, 0x0);
	luma_vdev->enable = false;
}

static void rkcif_init_luma_vdev(struct rkcif_luma_vdev *luma_vdev)
{
	luma_vdev->vdev_fmt.fmt.meta.dataformat =
		V4L2_META_FMT_RK_ISP1_STAT_LUMA;
	luma_vdev->vdev_fmt.fmt.meta.buffersize =
		sizeof(struct rkisp_isp2x_luma_buffer);
}

int rkcif_register_luma_vdev(struct rkcif_luma_vdev *luma_vdev,
			     struct v4l2_device *v4l2_dev,
			     struct rkcif_device *dev)
{
	int ret;
	struct rkcif_luma_node *node = &luma_vdev->vnode;
	struct video_device *vdev = &node->vdev;

	luma_vdev->cifdev = dev;

	INIT_LIST_HEAD(&luma_vdev->stat);
	spin_lock_init(&luma_vdev->irq_lock);
	spin_lock_init(&luma_vdev->rd_lock);

	strlcpy(vdev->name, "rkcif-mipi-luma", sizeof(vdev->name));
	mutex_init(&node->vlock);

	vdev->ioctl_ops = &rkcif_luma_ioctl;
	vdev->fops = &rkcif_luma_fops;
	vdev->release = video_device_release_empty;
	vdev->lock = &node->vlock;
	vdev->v4l2_dev = v4l2_dev;
	vdev->queue = &node->buf_queue;
	vdev->device_caps = V4L2_CAP_META_CAPTURE | V4L2_CAP_STREAMING;
	vdev->vfl_dir =  VFL_DIR_RX;
	rkcif_luma_init_vb2_queue(vdev->queue, luma_vdev);
	rkcif_init_luma_vdev(luma_vdev);
	video_set_drvdata(vdev, luma_vdev);

	node->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 0, &node->pad);
	if (ret < 0)
		goto err_release_queue;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		dev_err(&vdev->dev,
			"could not register Video for Linux device\n");
		goto err_cleanup_media_entity;
	}

	ret = kfifo_alloc(&luma_vdev->rd_kfifo,
			  RKCIF_LUMA_READOUT_WORK_SIZE,
			  GFP_KERNEL);
	if (ret) {
		dev_err(&vdev->dev,
			"kfifo_alloc failed with error %d\n",
			ret);
		goto err_unregister_video;
	}

	tasklet_init(&luma_vdev->rd_tasklet,
		     rkcif_luma_readout_task,
		     (unsigned long)luma_vdev);
	tasklet_disable(&luma_vdev->rd_tasklet);

	return 0;

err_unregister_video:
	video_unregister_device(vdev);
err_cleanup_media_entity:
	media_entity_cleanup(&vdev->entity);
err_release_queue:
	vb2_queue_release(vdev->queue);
	return ret;
}

void rkcif_unregister_luma_vdev(struct rkcif_luma_vdev *luma_vdev)
{
	struct rkcif_luma_node *node = &luma_vdev->vnode;
	struct video_device *vdev = &node->vdev;

	kfifo_free(&luma_vdev->rd_kfifo);
	tasklet_kill(&luma_vdev->rd_tasklet);
	video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	vb2_queue_release(vdev->queue);
}
