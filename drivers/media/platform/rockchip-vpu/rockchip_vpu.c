/*
 * Rockchip VPU codec driver
 *
 * Copyright (C) 2014 Google, Inc.
 *	Tomasz Figa <tfiga@chromium.org>
 *
 * Based on s5p-mfc driver by Samsung Electronics Co., Ltd.
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "rockchip_vpu_common.h"

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-event.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "rockchip_vpu_dec.h"
#include "rockchip_vpu_enc.h"
#include "rockchip_vpu_hw.h"

int debug;
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug,
		 "Debug level - higher value produces more verbose messages");

/*
 * DMA coherent helpers.
 */

int rockchip_vpu_aux_buf_alloc(struct rockchip_vpu_dev *vpu,
			    struct rockchip_vpu_aux_buf *buf, size_t size)
{
	buf->cpu = dma_alloc_coherent(vpu->dev, size, &buf->dma, GFP_KERNEL);
	if (!buf->cpu)
		return -ENOMEM;

	buf->size = size;
	return 0;
}

void rockchip_vpu_aux_buf_free(struct rockchip_vpu_dev *vpu,
			     struct rockchip_vpu_aux_buf *buf)
{
	dma_free_coherent(vpu->dev, buf->size, buf->cpu, buf->dma);

	buf->cpu = NULL;
	buf->dma = 0;
	buf->size = 0;
}

/*
 * Context scheduling.
 */

static void rockchip_vpu_prepare_run(struct rockchip_vpu_ctx *ctx)
{
	if (ctx->run_ops->prepare_run)
		ctx->run_ops->prepare_run(ctx);
}

static void __rockchip_vpu_dequeue_run_locked(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_buf *src, *dst;

	/*
	 * Since ctx was dequeued from ready_ctxs list, we know that it has
	 * at least one buffer in each queue.
	 */
	src = list_first_entry(&ctx->src_queue, struct rockchip_vpu_buf, list);
	dst = list_first_entry(&ctx->dst_queue, struct rockchip_vpu_buf, list);

	list_del(&src->list);
	list_del(&dst->list);

	ctx->run.src = src;
	ctx->run.dst = dst;
}

static struct rockchip_vpu_ctx *
rockchip_vpu_encode_after_decode_war(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_dev *dev = ctx->dev;

	if (dev->dummy_encode_ctx &&
			dev->was_decoding && rockchip_vpu_ctx_is_encoder(ctx))
		return dev->dummy_encode_ctx;

	return ctx;
}

static void rockchip_vpu_try_run(struct rockchip_vpu_dev *dev)
{
	struct rockchip_vpu_ctx *ctx = NULL;
	unsigned long flags;

	vpu_debug_enter();

	spin_lock_irqsave(&dev->irqlock, flags);

	if (list_empty(&dev->ready_ctxs) ||
	    test_bit(VPU_SUSPENDED, &dev->state))
		/* Nothing to do. */
		goto out;

	if (test_and_set_bit(VPU_RUNNING, &dev->state))
		/*
		* The hardware is already running. We will pick another
		* run after we get the notification in rockchip_vpu_run_done().
		*/
		goto out;

	ctx = list_entry(dev->ready_ctxs.next, struct rockchip_vpu_ctx, list);

	/*
	 * WAR for corrupted hardware state when encoding directly after
	 * certain decoding runs.
	 *
	 * If previous context was decoding and currently picked one is
	 * encoding then we need to execute a dummy encode with proper
	 * settings to reinitialize certain internal hardware state.
	 */
	ctx = rockchip_vpu_encode_after_decode_war(ctx);

	if (!rockchip_vpu_ctx_is_dummy_encode(ctx)) {
		list_del_init(&ctx->list);
		__rockchip_vpu_dequeue_run_locked(ctx);
	}

	dev->current_ctx = ctx;
	dev->was_decoding = !rockchip_vpu_ctx_is_encoder(ctx);

out:
	spin_unlock_irqrestore(&dev->irqlock, flags);

	if (ctx) {
		rockchip_vpu_prepare_run(ctx);
		rockchip_vpu_run(ctx);
	}

	vpu_debug_leave();
}

static void __rockchip_vpu_try_context_locked(struct rockchip_vpu_dev *dev,
					    struct rockchip_vpu_ctx *ctx)
{
	if (!list_empty(&ctx->list))
		/* Context already queued. */
		return;

	if (!list_empty(&ctx->dst_queue) && !list_empty(&ctx->src_queue))
		list_add_tail(&ctx->list, &dev->ready_ctxs);
}

void rockchip_vpu_run_done(struct rockchip_vpu_ctx *ctx,
			 enum vb2_buffer_state result)
{
	struct rockchip_vpu_dev *dev = ctx->dev;
	unsigned long flags;

	vpu_debug_enter();

	if (ctx->run_ops->run_done)
		ctx->run_ops->run_done(ctx, result);

	if (!rockchip_vpu_ctx_is_dummy_encode(ctx)) {
		struct vb2_v4l2_buffer *src =
			to_vb2_v4l2_buffer(&ctx->run.src->vb.vb2_buf);
		struct vb2_v4l2_buffer *dst =
			to_vb2_v4l2_buffer(&ctx->run.dst->vb.vb2_buf);

		dst->timestamp = src->timestamp;
		vb2_buffer_done(&ctx->run.src->vb.vb2_buf, result);
		vb2_buffer_done(&ctx->run.dst->vb.vb2_buf, result);
	}

	dev->current_ctx = NULL;
	wake_up_all(&dev->run_wq);

	spin_lock_irqsave(&dev->irqlock, flags);

	__rockchip_vpu_try_context_locked(dev, ctx);
	clear_bit(VPU_RUNNING, &dev->state);

	spin_unlock_irqrestore(&dev->irqlock, flags);

	/* Try scheduling another run to see if we have anything left to do. */
	rockchip_vpu_try_run(dev);

	vpu_debug_leave();
}

void rockchip_vpu_try_context(struct rockchip_vpu_dev *dev,
			    struct rockchip_vpu_ctx *ctx)
{
	unsigned long flags;

	vpu_debug_enter();

	spin_lock_irqsave(&dev->irqlock, flags);

	__rockchip_vpu_try_context_locked(dev, ctx);

	spin_unlock_irqrestore(&dev->irqlock, flags);

	rockchip_vpu_try_run(dev);

	vpu_debug_enter();
}

/*
 * bit stream assembler
 */

static int stream_buffer_status(struct stream_s *stream)
{
	if (stream->byte_cnt + 5 > stream->size) {
		stream->overflow = 1;
		return -1;
	}

	return 0;
}

void stream_put_bits(struct stream_s *buffer, s32 value, s32 number,
		     const char *name)
{
	s32 bits;
	u32 byte_buffer = buffer->byte_buffer;
	u8 *stream = buffer->stream;

	if (stream_buffer_status(buffer) != 0)
		return;

	vpu_debug(0, "assemble %s value %x, bits %d\n", name, value, number);

	BUG_ON(value >= (1 << number));
	BUG_ON(number >= 25);

	bits = number + buffer->buffered_bits;
	value <<= (32 - bits);
	byte_buffer = byte_buffer | value;

	while (bits > 7) {
		*stream = (u8)(byte_buffer >> 24);

		bits -= 8;
		byte_buffer <<= 8;
		stream++;
		buffer->byte_cnt++;
	}

	buffer->byte_buffer = byte_buffer;
	buffer->buffered_bits = (u8)bits;
	buffer->stream = stream;

	return;
}

void stream_buffer_reset(struct stream_s *buffer)
{
	buffer->stream = buffer->buffer;
	buffer->byte_cnt = 0;
	buffer->overflow = 0;
	buffer->byte_buffer = 0;
	buffer->buffered_bits = 0;
}

int stream_buffer_init(struct stream_s *buffer, u8 *stream, s32 size)
{
	if (stream == NULL) {
		buffer->stream = kzalloc(size, GFP_KERNEL);
	}

	if (buffer->stream == NULL) {
		vpu_err("allocate stream buffer failed\n");
		return -1;
	}

	buffer->buffer = buffer->stream;
	buffer->size = size;

	stream_buffer_reset(buffer);

	if (stream_buffer_status(buffer) != 0)
		return -1;

	return 0;
}

/*
 * Control registration.
 */

#define IS_VPU_PRIV(x) ((V4L2_CTRL_ID2WHICH(x) == V4L2_CTRL_CLASS_MPEG) && \
			  V4L2_CTRL_DRIVER_PRIV(x))

int rockchip_vpu_ctrls_setup(struct rockchip_vpu_ctx *ctx,
			   const struct v4l2_ctrl_ops *ctrl_ops,
			   struct rockchip_vpu_control *controls,
			   unsigned num_ctrls,
			   const char *const *(*get_menu)(u32))
{
	struct v4l2_ctrl_config cfg;
	int i;

	if (num_ctrls > ARRAY_SIZE(ctx->ctrls)) {
		vpu_err("context control array not large enough\n");
		return -ENOSPC;
	}

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, num_ctrls);
	if (ctx->ctrl_handler.error) {
		vpu_err("v4l2_ctrl_handler_init failed\n");
		return ctx->ctrl_handler.error;
	}

	for (i = 0; i < num_ctrls; i++) {
		if (IS_VPU_PRIV(controls[i].id)
		    || controls[i].id >= V4L2_CID_CUSTOM_BASE
		    || controls[i].type == V4L2_CTRL_TYPE_PRIVATE) {
			memset(&cfg, 0, sizeof(struct v4l2_ctrl_config));

			cfg.ops = ctrl_ops;
			cfg.id = controls[i].id;
			cfg.min = controls[i].minimum;
			cfg.max = controls[i].maximum;
			cfg.max_stores = controls[i].max_stores;
			cfg.def = controls[i].default_value;
			cfg.name = controls[i].name;
			cfg.type = controls[i].type;
			cfg.elem_size = controls[i].elem_size;
			memcpy(cfg.dims, controls[i].dims, sizeof(cfg.dims));

			if (cfg.type == V4L2_CTRL_TYPE_MENU) {
				cfg.menu_skip_mask = cfg.menu_skip_mask;
				cfg.qmenu = get_menu(cfg.id);
			} else {
				cfg.step = controls[i].step;
			}

			ctx->ctrls[i] = v4l2_ctrl_new_custom(
				&ctx->ctrl_handler, &cfg, NULL);
		} else {
			if (controls[i].type == V4L2_CTRL_TYPE_MENU) {
				ctx->ctrls[i] =
				    v4l2_ctrl_new_std_menu
					(&ctx->ctrl_handler,
					 ctrl_ops,
					 controls[i].id,
					 controls[i].maximum,
					 0,
					 controls[i].
					 default_value);
			} else {
				ctx->ctrls[i] =
				    v4l2_ctrl_new_std(&ctx->ctrl_handler,
						      ctrl_ops,
						      controls[i].id,
						      controls[i].minimum,
						      controls[i].maximum,
						      controls[i].step,
						      controls[i].
						      default_value);
			}
		}

		if (ctx->ctrl_handler.error) {
			vpu_err("Adding control (%d) failed\n", i);
			return ctx->ctrl_handler.error;
		}

		if (controls[i].is_volatile && ctx->ctrls[i])
			ctx->ctrls[i]->flags |= V4L2_CTRL_FLAG_VOLATILE;
		if (controls[i].is_read_only && ctx->ctrls[i])
			ctx->ctrls[i]->flags |= V4L2_CTRL_FLAG_READ_ONLY;
		if (controls[i].can_store && ctx->ctrls[i])
			ctx->ctrls[i]->flags |= V4L2_CTRL_FLAG_CAN_STORE;
	}

	v4l2_ctrl_handler_setup(&ctx->ctrl_handler);
	ctx->num_ctrls = num_ctrls;
	return 0;
}

void rockchip_vpu_ctrls_delete(struct rockchip_vpu_ctx *ctx)
{
	int i;

	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	for (i = 0; i < ctx->num_ctrls; i++)
		ctx->ctrls[i] = NULL;
}

/*
 * V4L2 file operations.
 */

static int rockchip_vpu_open(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct rockchip_vpu_dev *dev = video_drvdata(filp);
	struct rockchip_vpu_ctx *ctx = NULL;
	struct vb2_queue *q;
	int ret = 0;

	/*
	 * We do not need any extra locking here, because we operate only
	 * on local data here, except reading few fields from dev, which
	 * do not change through device's lifetime (which is guaranteed by
	 * reference on module from open()) and V4L2 internal objects (such
	 * as vdev and ctx->fh), which have proper locking done in respective
	 * helper functions used here.
	 */

	vpu_debug_enter();

	/* Allocate memory for context */
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto err_leave;
	}

	v4l2_fh_init(&ctx->fh, video_devdata(filp));
	filp->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);
	ctx->dev = dev;
	INIT_LIST_HEAD(&ctx->src_queue);
	INIT_LIST_HEAD(&ctx->dst_queue);
	INIT_LIST_HEAD(&ctx->list);

	if (vdev == dev->vfd_enc) {
		/* only for encoder */
		ret = rockchip_vpu_enc_init(ctx);
		if (ret) {
			vpu_err("Failed to initialize encoder context\n");
			goto err_fh_free;
		}
	} else if (vdev == dev->vfd_dec) {
		/* only for decoder */
		ret = rockchip_vpu_dec_init(ctx);
		if (ret) {
			vpu_err("Failed to initialize decoder context\n");
			goto err_fh_free;
		}
	} else {
		ret = -ENOENT;
		goto err_fh_free;
	}
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;

	/* Init videobuf2 queue for CAPTURE */
	q = &ctx->vq_dst;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->drv_priv = &ctx->fh;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->lock = &dev->vpu_mutex;
	q->buf_struct_size = sizeof(struct rockchip_vpu_buf);

	if (vdev == dev->vfd_enc) {
		q->ops = get_enc_queue_ops();
	} else if (vdev == dev->vfd_dec) {
		q->ops = get_dec_queue_ops();
		q->use_dma_bidirectional = 1;
	}

	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;

	ret = vb2_queue_init(q);
	if (ret) {
		vpu_err("Failed to initialize videobuf2 queue(capture)\n");
		goto err_enc_dec_exit;
	}

	/* Init videobuf2 queue for OUTPUT */
	q = &ctx->vq_src;
	q->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	q->drv_priv = &ctx->fh;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->lock = &dev->vpu_mutex;
	q->buf_struct_size = sizeof(struct rockchip_vpu_buf);

	if (vdev == dev->vfd_enc)
		q->ops = get_enc_queue_ops();
	else if (vdev == dev->vfd_dec)
		q->ops = get_dec_queue_ops();

	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;

	ret = vb2_queue_init(q);
	if (ret) {
		vpu_err("Failed to initialize videobuf2 queue(output)\n");
		goto err_vq_dst_release;
	}

	vpu_debug_leave();

	return 0;

err_vq_dst_release:
	vb2_queue_release(&ctx->vq_dst);
err_enc_dec_exit:
	if (vdev == dev->vfd_enc)
		rockchip_vpu_enc_exit(ctx);
	else if (vdev == dev->vfd_dec)
		rockchip_vpu_dec_exit(ctx);
err_fh_free:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
err_leave:
	vpu_debug_leave();

	return ret;
}

static int rockchip_vpu_release(struct file *filp)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(filp->private_data);
	struct video_device *vdev = video_devdata(filp);
	struct rockchip_vpu_dev *dev = ctx->dev;

	/*
	 * No need for extra locking because this was the last reference
	 * to this file.
	 */

	vpu_debug_enter();

	/*
	 * vb2_queue_release() ensures that streaming is stopped, which
	 * in turn means that there are no frames still being processed
	 * by hardware.
	 */
	vb2_queue_release(&ctx->vq_src);
	vb2_queue_release(&ctx->vq_dst);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);

	if (vdev == dev->vfd_enc)
		rockchip_vpu_enc_exit(ctx);
	else if (vdev == dev->vfd_dec)
		rockchip_vpu_dec_exit(ctx);

	kfree(ctx);

	vpu_debug_leave();

	return 0;
}

static unsigned int rockchip_vpu_poll(struct file *filp,
				    struct poll_table_struct *wait)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(filp->private_data);
	struct vb2_queue *src_q, *dst_q;
	struct vb2_buffer *src_vb = NULL, *dst_vb = NULL;
	unsigned int rc = 0;
	unsigned long flags;

	vpu_debug_enter();

	src_q = &ctx->vq_src;
	dst_q = &ctx->vq_dst;

	/*
	 * There has to be at least one buffer queued on each queued_list, which
	 * means either in driver already or waiting for driver to claim it
	 * and start processing.
	 */
	if ((!vb2_is_streaming(src_q) || list_empty(&src_q->queued_list)) &&
	    (!vb2_is_streaming(dst_q) || list_empty(&dst_q->queued_list))) {
		vpu_debug(0, "src q streaming %d, dst q streaming %d, src list empty(%d), dst list empty(%d)\n",
				src_q->streaming, dst_q->streaming,
				list_empty(&src_q->queued_list),
				list_empty(&dst_q->queued_list));
		return POLLERR;
	}

	poll_wait(filp, &ctx->fh.wait, wait);
	poll_wait(filp, &src_q->done_wq, wait);
	poll_wait(filp, &dst_q->done_wq, wait);

	if (v4l2_event_pending(&ctx->fh))
		rc |= POLLPRI;

	spin_lock_irqsave(&src_q->done_lock, flags);

	if (!list_empty(&src_q->done_list))
		src_vb = list_first_entry(&src_q->done_list, struct vb2_buffer,
						done_entry);

	if (src_vb && (src_vb->state == VB2_BUF_STATE_DONE ||
			src_vb->state == VB2_BUF_STATE_ERROR))
		rc |= POLLOUT | POLLWRNORM;

	spin_unlock_irqrestore(&src_q->done_lock, flags);

	spin_lock_irqsave(&dst_q->done_lock, flags);

	if (!list_empty(&dst_q->done_list))
		dst_vb = list_first_entry(&dst_q->done_list, struct vb2_buffer,
						done_entry);

	if (dst_vb && (dst_vb->state == VB2_BUF_STATE_DONE ||
			dst_vb->state == VB2_BUF_STATE_ERROR))
		rc |= POLLIN | POLLRDNORM;

	spin_unlock_irqrestore(&dst_q->done_lock, flags);

	return rc;
}

static int rockchip_vpu_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(filp->private_data);
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	int ret;

	vpu_debug_enter();

	if (offset < DST_QUEUE_OFF_BASE) {
		vpu_debug(4, "mmaping source\n");

		ret = vb2_mmap(&ctx->vq_src, vma);
	} else {		/* capture */
		vpu_debug(4, "mmaping destination\n");

		vma->vm_pgoff -= (DST_QUEUE_OFF_BASE >> PAGE_SHIFT);
		ret = vb2_mmap(&ctx->vq_dst, vma);
	}

	vpu_debug_leave();

	return ret;
}

static const struct v4l2_file_operations rockchip_vpu_fops = {
	.owner = THIS_MODULE,
	.open = rockchip_vpu_open,
	.release = rockchip_vpu_release,
	.poll = rockchip_vpu_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = rockchip_vpu_mmap,
};

/*
 * Platform driver.
 */

static void* rockchip_get_drv_data(struct platform_device *pdev);

static int rockchip_vpu_probe(struct platform_device *pdev)
{
	struct rockchip_vpu_dev *vpu = NULL;
	DEFINE_DMA_ATTRS(attrs_novm);
	DEFINE_DMA_ATTRS(attrs_nohugepage);
	struct video_device *vfd;
	int ret = 0;

	vpu_debug_enter();

	vpu = devm_kzalloc(&pdev->dev, sizeof(*vpu), GFP_KERNEL);
	if (!vpu)
		return -ENOMEM;

	vpu->dev = &pdev->dev;
	vpu->pdev = pdev;
	mutex_init(&vpu->vpu_mutex);
	spin_lock_init(&vpu->irqlock);
	INIT_LIST_HEAD(&vpu->ready_ctxs);
	init_waitqueue_head(&vpu->run_wq);

	vpu->variant = rockchip_get_drv_data(pdev);

	ret = rockchip_vpu_hw_probe(vpu);
	if (ret) {
		dev_err(&pdev->dev, "rockchip_vpu_hw_probe failed\n");
		goto err_hw_probe;
	}

	/*
	 * We'll do mostly sequential access, so sacrifice TLB efficiency for
	 * faster allocation.
	 */
	dma_set_attr(DMA_ATTR_ALLOC_SINGLE_PAGES, &attrs_novm);

	dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, &attrs_novm);
	vpu->alloc_ctx = vb2_dma_contig_init_ctx_attrs(&pdev->dev,
								&attrs_novm);
	if (IS_ERR(vpu->alloc_ctx)) {
		ret = PTR_ERR(vpu->alloc_ctx);
		goto err_dma_contig;
	}

	dma_set_attr(DMA_ATTR_ALLOC_SINGLE_PAGES, &attrs_nohugepage);
	vpu->alloc_ctx_vm = vb2_dma_contig_init_ctx_attrs(&pdev->dev,
							  &attrs_nohugepage);
	if (IS_ERR(vpu->alloc_ctx_vm)) {
		ret = PTR_ERR(vpu->alloc_ctx_vm);
		goto err_dma_contig_vm;
	}

	ret = v4l2_device_register(&pdev->dev, &vpu->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register v4l2 device\n");
		goto err_v4l2_dev_reg;
	}

	platform_set_drvdata(pdev, vpu);

	/* workaround for rk3288 codecs */
	if (vpu->variant->codecs == RK3288_CODECS) {
		ret = rockchip_vpu_enc_init_dummy_ctx(vpu);
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to create dummy encode context\n");
			goto err_dummy_enc;
		}
	}

	/* encoder */
	if (!(vpu->variant->codecs & ROCKCHIP_VPU_ENCODERS))
		goto no_encoder;

	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&vpu->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto err_enc_alloc;
	}

	vfd->fops = &rockchip_vpu_fops;
	vfd->ioctl_ops = get_enc_v4l2_ioctl_ops();
	vfd->release = video_device_release;
	vfd->lock = &vpu->vpu_mutex;
	vfd->v4l2_dev = &vpu->v4l2_dev;
	vfd->vfl_dir = VFL_DIR_M2M;
	snprintf(vfd->name, sizeof(vfd->name), "%s", ROCKCHIP_VPU_ENC_NAME);
	vpu->vfd_enc = vfd;

	video_set_drvdata(vfd, vpu);

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		v4l2_err(&vpu->v4l2_dev, "Failed to register video device\n");
		video_device_release(vfd);
		goto err_enc_reg;
	}

	v4l2_info(&vpu->v4l2_dev,
		"Rockchip VPU encoder registered as /vpu/video%d\n",
		vfd->num);

no_encoder:
	/* decoder */
	if (!(vpu->variant->codecs & ROCKCHIP_VPU_DECODERS))
		goto no_decoder;

	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&vpu->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto err_dec_alloc;
	}

	vfd->fops = &rockchip_vpu_fops;
	vfd->ioctl_ops = get_dec_v4l2_ioctl_ops();
	vfd->release = video_device_release;
	vfd->lock = &vpu->vpu_mutex;
	vfd->v4l2_dev = &vpu->v4l2_dev;
	vfd->vfl_dir = VFL_DIR_M2M;
	snprintf(vfd->name, sizeof(vfd->name), "%s", ROCKCHIP_VPU_DEC_NAME);
	vpu->vfd_dec = vfd;

	video_set_drvdata(vfd, vpu);

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		v4l2_err(&vpu->v4l2_dev, "Failed to register video device\n");
		video_device_release(vfd);
		goto err_dec_reg;
	}

	v4l2_info(&vpu->v4l2_dev,
		"Rockchip VPU decoder registered as /vpu/video%d\n",
		vfd->num);

no_decoder:
	vpu_debug_leave();

	return 0;

err_dec_reg:
	video_device_release(vpu->vfd_dec);
err_dec_alloc:
	video_unregister_device(vpu->vfd_enc);
err_enc_reg:
	video_device_release(vpu->vfd_enc);
err_enc_alloc:
	rockchip_vpu_enc_free_dummy_ctx(vpu);
err_dummy_enc:
	v4l2_device_unregister(&vpu->v4l2_dev);
err_v4l2_dev_reg:
	vb2_dma_contig_cleanup_ctx(vpu->alloc_ctx_vm);
err_dma_contig_vm:
	vb2_dma_contig_cleanup_ctx(vpu->alloc_ctx);
err_dma_contig:
	rockchip_vpu_hw_remove(vpu);
err_hw_probe:
	pr_debug("%s-- with error\n", __func__);
	vpu_debug_leave();

	return ret;
}

static int rockchip_vpu_remove(struct platform_device *pdev)
{
	struct rockchip_vpu_dev *vpu = platform_get_drvdata(pdev);

	vpu_debug_enter();

	v4l2_info(&vpu->v4l2_dev, "Removing %s\n", pdev->name);

	/*
	 * We are safe here assuming that .remove() got called as
	 * a result of module removal, which guarantees that all
	 * contexts have been released.
	 */

	video_unregister_device(vpu->vfd_dec);
	video_unregister_device(vpu->vfd_enc);
	rockchip_vpu_enc_free_dummy_ctx(vpu);
	v4l2_device_unregister(&vpu->v4l2_dev);
	vb2_dma_contig_cleanup_ctx(vpu->alloc_ctx_vm);
	vb2_dma_contig_cleanup_ctx(vpu->alloc_ctx);
	rockchip_vpu_hw_remove(vpu);

	vpu_debug_leave();

	return 0;
}

/* Supported VPU variants. */
static const struct rockchip_vpu_variant rk3288_vpu_variant = {
	.name = "Rk3288 vpu",
	.codecs = RK3288_CODECS,
	.enc_offset = 0x0,
	.enc_reg_num = 164,
	.dec_offset = 0x400,
	.dec_reg_num = 60 + 41,
};

static struct platform_device_id vpu_driver_ids[] = {
	{
		.name = "rk3288-vpu",
		.driver_data = (unsigned long)&rk3288_vpu_variant,
	},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(platform, vpu_driver_ids);

#ifdef CONFIG_OF
static const struct of_device_id of_rockchip_vpu_match[] = {
	{ .compatible = "rockchip,rk3288-vpu", .data = &rk3288_vpu_variant, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_rockchip_vpu_match);
#endif

static void* rockchip_get_drv_data(struct platform_device *pdev)
{
#ifdef CONFIG_OF
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(of_rockchip_vpu_match,
							  pdev->dev.of_node);
		if (match)
			return (void *)match->data;
	}
#endif

	return (void *)platform_get_device_id(pdev)->driver_data;
}

#ifdef CONFIG_PM_SLEEP
static int rockchip_vpu_suspend(struct device *dev)
{
	struct rockchip_vpu_dev *vpu = dev_get_drvdata(dev);

	set_bit(VPU_SUSPENDED, &vpu->state);
	wait_event(vpu->run_wq, vpu->current_ctx == NULL);

	return 0;
}

static int rockchip_vpu_resume(struct device *dev)
{
	struct rockchip_vpu_dev *vpu = dev_get_drvdata(dev);

	clear_bit(VPU_SUSPENDED, &vpu->state);
	rockchip_vpu_try_run(vpu);

	return 0;
}
#endif

static const struct dev_pm_ops rockchip_vpu_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_vpu_suspend, rockchip_vpu_resume)
};

static struct platform_driver rockchip_vpu_driver = {
	.probe = rockchip_vpu_probe,
	.remove = rockchip_vpu_remove,
	.id_table = vpu_driver_ids,
	.driver = {
		   .name = ROCKCHIP_VPU_NAME,
		   .owner = THIS_MODULE,
#ifdef CONFIG_OF
		   .of_match_table = of_match_ptr(of_rockchip_vpu_match),
#endif
		   .pm = &rockchip_vpu_pm_ops,
	},
};
module_platform_driver(rockchip_vpu_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Alpha Lin <Alpha.Lin@Rock-Chips.com>");
MODULE_AUTHOR("Tomasz Figa <tfiga@chromium.org>");
MODULE_DESCRIPTION("Rockchip VPU codec driver");
