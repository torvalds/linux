// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - Stats subdevice
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP statistics */

#include "rkisp1-common.h"

#define RKISP1_STATS_DEV_NAME	RKISP1_DRIVER_NAME "_stats"

#define RKISP1_ISP_STATS_REQ_BUFS_MIN 2
#define RKISP1_ISP_STATS_REQ_BUFS_MAX 8

enum rkisp1_isp_readout_cmd {
	RKISP1_ISP_READOUT_MEAS,
	RKISP1_ISP_READOUT_META,
};

struct rkisp1_isp_readout_work {
	struct work_struct work;
	struct rkisp1_stats *stats;

	unsigned int frame_id;
	unsigned int isp_ris;
	enum rkisp1_isp_readout_cmd readout;
	struct vb2_buffer *vb;
};

static int rkisp1_stats_enum_fmt_meta_cap(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp1_stats *stats = video_get_drvdata(video);

	if (f->index > 0 || f->type != video->queue->type)
		return -EINVAL;

	f->pixelformat = stats->vdev_fmt.fmt.meta.dataformat;
	return 0;
}

static int rkisp1_stats_g_fmt_meta_cap(struct file *file, void *priv,
				       struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp1_stats *stats = video_get_drvdata(video);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != video->queue->type)
		return -EINVAL;

	memset(meta, 0, sizeof(*meta));
	meta->dataformat = stats->vdev_fmt.fmt.meta.dataformat;
	meta->buffersize = stats->vdev_fmt.fmt.meta.buffersize;

	return 0;
}

static int rkisp1_stats_querycap(struct file *file,
				 void *priv, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);

	strscpy(cap->driver, RKISP1_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, vdev->name, sizeof(cap->card));
	strscpy(cap->bus_info, RKISP1_BUS_INFO, sizeof(cap->bus_info));

	return 0;
}

/* ISP video device IOCTLs */
static const struct v4l2_ioctl_ops rkisp1_stats_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_meta_cap = rkisp1_stats_enum_fmt_meta_cap,
	.vidioc_g_fmt_meta_cap = rkisp1_stats_g_fmt_meta_cap,
	.vidioc_s_fmt_meta_cap = rkisp1_stats_g_fmt_meta_cap,
	.vidioc_try_fmt_meta_cap = rkisp1_stats_g_fmt_meta_cap,
	.vidioc_querycap = rkisp1_stats_querycap,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations rkisp1_stats_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = v4l2_fh_open,
	.release = vb2_fop_release
};

static int rkisp1_stats_vb2_queue_setup(struct vb2_queue *vq,
					unsigned int *num_buffers,
					unsigned int *num_planes,
					unsigned int sizes[],
					struct device *alloc_devs[])
{
	struct rkisp1_stats *stats = vq->drv_priv;

	*num_planes = 1;

	*num_buffers = clamp_t(u32, *num_buffers, RKISP1_ISP_STATS_REQ_BUFS_MIN,
			       RKISP1_ISP_STATS_REQ_BUFS_MAX);

	sizes[0] = sizeof(struct rkisp1_stat_buffer);

	INIT_LIST_HEAD(&stats->stat);

	return 0;
}

static void rkisp1_stats_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_buffer *stats_buf =
		container_of(vbuf, struct rkisp1_buffer, vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkisp1_stats *stats_dev = vq->drv_priv;

	stats_buf->vaddr[0] = vb2_plane_vaddr(vb, 0);

	mutex_lock(&stats_dev->wq_lock);
	list_add_tail(&stats_buf->queue, &stats_dev->stat);
	mutex_unlock(&stats_dev->wq_lock);
}

static int rkisp1_stats_vb2_buf_prepare(struct vb2_buffer *vb)
{
	if (vb2_plane_size(vb, 0) < sizeof(struct rkisp1_stat_buffer))
		return -EINVAL;

	vb2_set_plane_payload(vb, 0, sizeof(struct rkisp1_stat_buffer));

	return 0;
}

static void rkisp1_stats_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rkisp1_stats *stats = vq->drv_priv;
	struct rkisp1_buffer *buf;
	unsigned long flags;
	unsigned int i;

	/* Make sure no new work queued in isr before draining wq */
	spin_lock_irqsave(&stats->irq_lock, flags);
	stats->is_streaming = false;
	spin_unlock_irqrestore(&stats->irq_lock, flags);

	drain_workqueue(stats->readout_wq);

	mutex_lock(&stats->wq_lock);
	for (i = 0; i < RKISP1_ISP_STATS_REQ_BUFS_MAX; i++) {
		if (list_empty(&stats->stat))
			break;
		buf = list_first_entry(&stats->stat,
				       struct rkisp1_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	mutex_unlock(&stats->wq_lock);
}

static int
rkisp1_stats_vb2_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct rkisp1_stats *stats = queue->drv_priv;

	stats->is_streaming = true;

	return 0;
}

static const struct vb2_ops rkisp1_stats_vb2_ops = {
	.queue_setup = rkisp1_stats_vb2_queue_setup,
	.buf_queue = rkisp1_stats_vb2_buf_queue,
	.buf_prepare = rkisp1_stats_vb2_buf_prepare,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = rkisp1_stats_vb2_stop_streaming,
	.start_streaming = rkisp1_stats_vb2_start_streaming,
};

static int
rkisp1_stats_init_vb2_queue(struct vb2_queue *q, struct rkisp1_stats *stats)
{
	struct rkisp1_vdev_node *node;

	node = container_of(q, struct rkisp1_vdev_node, buf_queue);

	q->type = V4L2_BUF_TYPE_META_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->drv_priv = stats;
	q->ops = &rkisp1_stats_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->buf_struct_size = sizeof(struct rkisp1_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &node->vlock;

	return vb2_queue_init(q);
}

static void rkisp1_stats_get_awb_meas(struct rkisp1_stats *stats,
				      struct rkisp1_stat_buffer *pbuf)
{
	/* Protect against concurrent access from ISR? */
	struct rkisp1_device *rkisp1 = stats->rkisp1;
	u32 reg_val;

	pbuf->meas_type |= RKISP1_CIF_ISP_STAT_AWB;
	reg_val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_AWB_WHITE_CNT);
	pbuf->params.awb.awb_mean[0].cnt =
				RKISP1_CIF_ISP_AWB_GET_PIXEL_CNT(reg_val);
	reg_val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_AWB_MEAN);

	pbuf->params.awb.awb_mean[0].mean_cr_or_r =
				RKISP1_CIF_ISP_AWB_GET_MEAN_CR_R(reg_val);
	pbuf->params.awb.awb_mean[0].mean_cb_or_b =
				RKISP1_CIF_ISP_AWB_GET_MEAN_CB_B(reg_val);
	pbuf->params.awb.awb_mean[0].mean_y_or_g =
				RKISP1_CIF_ISP_AWB_GET_MEAN_Y_G(reg_val);
}

static void rkisp1_stats_get_aec_meas(struct rkisp1_stats *stats,
				      struct rkisp1_stat_buffer *pbuf)
{
	struct rkisp1_device *rkisp1 = stats->rkisp1;
	unsigned int i;

	pbuf->meas_type |= RKISP1_CIF_ISP_STAT_AUTOEXP;
	for (i = 0; i < RKISP1_CIF_ISP_AE_MEAN_MAX; i++)
		pbuf->params.ae.exp_mean[i] =
			(u8)rkisp1_read(rkisp1,
					RKISP1_CIF_ISP_EXP_MEAN_00 + i * 4);
}

static void rkisp1_stats_get_afc_meas(struct rkisp1_stats *stats,
				      struct rkisp1_stat_buffer *pbuf)
{
	struct rkisp1_device *rkisp1 = stats->rkisp1;
	struct rkisp1_cif_isp_af_stat *af;

	pbuf->meas_type = RKISP1_CIF_ISP_STAT_AFM_FIN;

	af = &pbuf->params.af;
	af->window[0].sum = rkisp1_read(rkisp1, RKISP1_CIF_ISP_AFM_SUM_A);
	af->window[0].lum = rkisp1_read(rkisp1, RKISP1_CIF_ISP_AFM_LUM_A);
	af->window[1].sum = rkisp1_read(rkisp1, RKISP1_CIF_ISP_AFM_SUM_B);
	af->window[1].lum = rkisp1_read(rkisp1, RKISP1_CIF_ISP_AFM_LUM_B);
	af->window[2].sum = rkisp1_read(rkisp1, RKISP1_CIF_ISP_AFM_SUM_C);
	af->window[2].lum = rkisp1_read(rkisp1, RKISP1_CIF_ISP_AFM_LUM_C);
}

static void rkisp1_stats_get_hst_meas(struct rkisp1_stats *stats,
				      struct rkisp1_stat_buffer *pbuf)
{
	struct rkisp1_device *rkisp1 = stats->rkisp1;
	unsigned int i;

	pbuf->meas_type |= RKISP1_CIF_ISP_STAT_HIST;
	for (i = 0; i < RKISP1_CIF_ISP_HIST_BIN_N_MAX; i++)
		pbuf->params.hist.hist_bins[i] =
			(u8)rkisp1_read(rkisp1,
					RKISP1_CIF_ISP_HIST_BIN_0 + i * 4);
}

static void rkisp1_stats_get_bls_meas(struct rkisp1_stats *stats,
				      struct rkisp1_stat_buffer *pbuf)
{
	struct rkisp1_device *rkisp1 = stats->rkisp1;
	const struct rkisp1_isp_mbus_info *in_fmt = rkisp1->isp.sink_fmt;
	struct rkisp1_cif_isp_bls_meas_val *bls_val;

	bls_val = &pbuf->params.ae.bls_val;
	if (in_fmt->bayer_pat == RKISP1_RAW_BGGR) {
		bls_val->meas_b =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_A_MEASURED);
		bls_val->meas_gb =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_B_MEASURED);
		bls_val->meas_gr =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_C_MEASURED);
		bls_val->meas_r =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_D_MEASURED);
	} else if (in_fmt->bayer_pat == RKISP1_RAW_GBRG) {
		bls_val->meas_gb =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_A_MEASURED);
		bls_val->meas_b =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_B_MEASURED);
		bls_val->meas_r =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_C_MEASURED);
		bls_val->meas_gr =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_D_MEASURED);
	} else if (in_fmt->bayer_pat == RKISP1_RAW_GRBG) {
		bls_val->meas_gr =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_A_MEASURED);
		bls_val->meas_r =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_B_MEASURED);
		bls_val->meas_b =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_C_MEASURED);
		bls_val->meas_gb =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_D_MEASURED);
	} else if (in_fmt->bayer_pat == RKISP1_RAW_RGGB) {
		bls_val->meas_r =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_A_MEASURED);
		bls_val->meas_gr =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_B_MEASURED);
		bls_val->meas_gb =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_C_MEASURED);
		bls_val->meas_b =
			rkisp1_read(rkisp1, RKISP1_CIF_ISP_BLS_D_MEASURED);
	}
}

static void
rkisp1_stats_send_measurement(struct rkisp1_stats *stats,
			      struct rkisp1_isp_readout_work *meas_work)
{
	struct rkisp1_stat_buffer *cur_stat_buf;
	struct rkisp1_buffer *cur_buf = NULL;
	unsigned int frame_sequence =
		atomic_read(&stats->rkisp1->isp.frame_sequence);
	u64 timestamp = ktime_get_ns();

	if (frame_sequence != meas_work->frame_id) {
		dev_warn(stats->rkisp1->dev,
			 "Measurement late(%d, %d)\n",
			 frame_sequence, meas_work->frame_id);
		frame_sequence = meas_work->frame_id;
	}

	mutex_lock(&stats->wq_lock);
	/* get one empty buffer */
	if (!list_empty(&stats->stat)) {
		cur_buf = list_first_entry(&stats->stat,
					   struct rkisp1_buffer, queue);
		list_del(&cur_buf->queue);
	}
	mutex_unlock(&stats->wq_lock);

	if (!cur_buf)
		return;

	cur_stat_buf =
		(struct rkisp1_stat_buffer *)(cur_buf->vaddr[0]);

	if (meas_work->isp_ris & RKISP1_CIF_ISP_AWB_DONE) {
		rkisp1_stats_get_awb_meas(stats, cur_stat_buf);
		cur_stat_buf->meas_type |= RKISP1_CIF_ISP_STAT_AWB;
	}

	if (meas_work->isp_ris & RKISP1_CIF_ISP_AFM_FIN) {
		rkisp1_stats_get_afc_meas(stats, cur_stat_buf);
		cur_stat_buf->meas_type |= RKISP1_CIF_ISP_STAT_AFM_FIN;
	}

	if (meas_work->isp_ris & RKISP1_CIF_ISP_EXP_END) {
		rkisp1_stats_get_aec_meas(stats, cur_stat_buf);
		rkisp1_stats_get_bls_meas(stats, cur_stat_buf);
		cur_stat_buf->meas_type |= RKISP1_CIF_ISP_STAT_AUTOEXP;
	}

	if (meas_work->isp_ris & RKISP1_CIF_ISP_HIST_MEASURE_RDY) {
		rkisp1_stats_get_hst_meas(stats, cur_stat_buf);
		cur_stat_buf->meas_type |= RKISP1_CIF_ISP_STAT_HIST;
	}

	vb2_set_plane_payload(&cur_buf->vb.vb2_buf, 0,
			      sizeof(struct rkisp1_stat_buffer));
	cur_buf->vb.sequence = frame_sequence;
	cur_buf->vb.vb2_buf.timestamp = timestamp;
	vb2_buffer_done(&cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

static void rkisp1_stats_readout_work(struct work_struct *work)
{
	struct rkisp1_isp_readout_work *readout_work =
		container_of(work, struct rkisp1_isp_readout_work, work);
	struct rkisp1_stats *stats = readout_work->stats;

	if (readout_work->readout == RKISP1_ISP_READOUT_MEAS)
		rkisp1_stats_send_measurement(stats, readout_work);

	kfree(readout_work);
}

void rkisp1_stats_isr(struct rkisp1_stats *stats, u32 isp_ris)
{
	unsigned int frame_sequence =
		atomic_read(&stats->rkisp1->isp.frame_sequence);
	struct rkisp1_device *rkisp1 = stats->rkisp1;
	struct rkisp1_isp_readout_work *work;
	unsigned int isp_mis_tmp = 0;
	u32 val;

	spin_lock(&stats->irq_lock);

	val = RKISP1_CIF_ISP_AWB_DONE | RKISP1_CIF_ISP_AFM_FIN |
	      RKISP1_CIF_ISP_EXP_END | RKISP1_CIF_ISP_HIST_MEASURE_RDY;
	rkisp1_write(rkisp1, val, RKISP1_CIF_ISP_ICR);

	isp_mis_tmp = rkisp1_read(rkisp1, RKISP1_CIF_ISP_MIS);
	if (isp_mis_tmp &
	    (RKISP1_CIF_ISP_AWB_DONE | RKISP1_CIF_ISP_AFM_FIN |
	     RKISP1_CIF_ISP_EXP_END | RKISP1_CIF_ISP_HIST_MEASURE_RDY))
		rkisp1->debug.stats_error++;

	if (!stats->is_streaming)
		goto unlock;
	if (isp_ris & (RKISP1_CIF_ISP_AWB_DONE |
		       RKISP1_CIF_ISP_AFM_FIN |
		       RKISP1_CIF_ISP_EXP_END |
		       RKISP1_CIF_ISP_HIST_MEASURE_RDY)) {
		work = kzalloc(sizeof(*work), GFP_ATOMIC);
		if (work) {
			INIT_WORK(&work->work,
				  rkisp1_stats_readout_work);
			work->readout = RKISP1_ISP_READOUT_MEAS;
			work->stats = stats;
			work->frame_id = frame_sequence;
			work->isp_ris = isp_ris;
			if (!queue_work(stats->readout_wq,
					&work->work))
				kfree(work);
		} else {
			dev_err(stats->rkisp1->dev,
				"Could not allocate work\n");
		}
	}

unlock:
	spin_unlock(&stats->irq_lock);
}

static void rkisp1_init_stats(struct rkisp1_stats *stats)
{
	stats->vdev_fmt.fmt.meta.dataformat =
		V4L2_META_FMT_RK_ISP1_STAT_3A;
	stats->vdev_fmt.fmt.meta.buffersize =
		sizeof(struct rkisp1_stat_buffer);
}

int rkisp1_stats_register(struct rkisp1_stats *stats,
			  struct v4l2_device *v4l2_dev,
			  struct rkisp1_device *rkisp1)
{
	struct rkisp1_vdev_node *node = &stats->vnode;
	struct video_device *vdev = &node->vdev;
	int ret;

	stats->rkisp1 = rkisp1;
	mutex_init(&stats->wq_lock);
	mutex_init(&node->vlock);
	INIT_LIST_HEAD(&stats->stat);
	spin_lock_init(&stats->irq_lock);

	strscpy(vdev->name, RKISP1_STATS_DEV_NAME, sizeof(vdev->name));

	video_set_drvdata(vdev, stats);
	vdev->ioctl_ops = &rkisp1_stats_ioctl;
	vdev->fops = &rkisp1_stats_fops;
	vdev->release = video_device_release_empty;
	vdev->lock = &node->vlock;
	vdev->v4l2_dev = v4l2_dev;
	vdev->queue = &node->buf_queue;
	vdev->device_caps = V4L2_CAP_META_CAPTURE | V4L2_CAP_STREAMING;
	vdev->vfl_dir =  VFL_DIR_RX;
	rkisp1_stats_init_vb2_queue(vdev->queue, stats);
	rkisp1_init_stats(stats);
	video_set_drvdata(vdev, stats);

	node->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret)
		goto err_release_queue;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(&vdev->dev,
			"failed to register %s, ret=%d\n", vdev->name, ret);
		goto err_cleanup_media_entity;
	}

	stats->readout_wq = alloc_workqueue("measurement_queue",
					    WQ_UNBOUND | WQ_MEM_RECLAIM,
					    1);

	if (!stats->readout_wq) {
		ret = -ENOMEM;
		goto err_unreg_vdev;
	}

	return 0;

err_unreg_vdev:
	video_unregister_device(vdev);
err_cleanup_media_entity:
	media_entity_cleanup(&vdev->entity);
err_release_queue:
	vb2_queue_release(vdev->queue);
	mutex_destroy(&node->vlock);
	mutex_destroy(&stats->wq_lock);
	return ret;
}

void rkisp1_stats_unregister(struct rkisp1_stats *stats)
{
	struct rkisp1_vdev_node *node = &stats->vnode;
	struct video_device *vdev = &node->vdev;

	destroy_workqueue(stats->readout_wq);
	video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	vb2_queue_release(vdev->queue);
	mutex_destroy(&node->vlock);
	mutex_destroy(&stats->wq_lock);
}
