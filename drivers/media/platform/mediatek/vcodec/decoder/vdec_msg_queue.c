// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#include <linux/freezer.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>

#include "mtk_vcodec_dec_drv.h"
#include "mtk_vcodec_dec_pm.h"
#include "vdec_msg_queue.h"

#define VDEC_MSG_QUEUE_TIMEOUT_MS 1500

/* the size used to store lat slice header information */
#define VDEC_LAT_SLICE_HEADER_SZ    (640 * SZ_1K)

/* the size used to store avc error information */
#define VDEC_ERR_MAP_SZ_AVC         (17 * SZ_1K)

#define VDEC_RD_MV_BUFFER_SZ        (((SZ_4K * 2304 >> 4) + SZ_1K) << 1)
#define VDEC_LAT_TILE_SZ            (64 * V4L2_AV1_MAX_TILE_COUNT)

/* core will read the trans buffer which decoded by lat to decode again.
 * The trans buffer size of FHD and 4K bitstreams are different.
 */
static int vde_msg_queue_get_trans_size(int width, int height)
{
	if (width > 1920 || height > 1088)
		return 30 * SZ_1M;
	else
		return 6 * SZ_1M;
}

void vdec_msg_queue_init_ctx(struct vdec_msg_queue_ctx *ctx, int hardware_index)
{
	init_waitqueue_head(&ctx->ready_to_use);
	INIT_LIST_HEAD(&ctx->ready_queue);
	spin_lock_init(&ctx->ready_lock);
	ctx->ready_num = 0;
	ctx->hardware_index = hardware_index;
}

static struct list_head *vdec_get_buf_list(int hardware_index, struct vdec_lat_buf *buf)
{
	switch (hardware_index) {
	case MTK_VDEC_CORE:
		return &buf->core_list;
	case MTK_VDEC_LAT0:
		return &buf->lat_list;
	default:
		return NULL;
	}
}

static void vdec_msg_queue_inc(struct vdec_msg_queue *msg_queue, int hardware_index)
{
	if (hardware_index == MTK_VDEC_CORE)
		atomic_inc(&msg_queue->core_list_cnt);
	else
		atomic_inc(&msg_queue->lat_list_cnt);
}

static void vdec_msg_queue_dec(struct vdec_msg_queue *msg_queue, int hardware_index)
{
	if (hardware_index == MTK_VDEC_CORE)
		atomic_dec(&msg_queue->core_list_cnt);
	else
		atomic_dec(&msg_queue->lat_list_cnt);
}

int vdec_msg_queue_qbuf(struct vdec_msg_queue_ctx *msg_ctx, struct vdec_lat_buf *buf)
{
	struct list_head *head;

	head = vdec_get_buf_list(msg_ctx->hardware_index, buf);
	if (!head) {
		mtk_v4l2_vdec_err(buf->ctx, "fail to qbuf: %d", msg_ctx->hardware_index);
		return -EINVAL;
	}

	spin_lock(&msg_ctx->ready_lock);
	list_add_tail(head, &msg_ctx->ready_queue);
	msg_ctx->ready_num++;

	vdec_msg_queue_inc(&buf->ctx->msg_queue, msg_ctx->hardware_index);
	if (msg_ctx->hardware_index != MTK_VDEC_CORE) {
		wake_up_all(&msg_ctx->ready_to_use);
	} else {
		if (!(buf->ctx->msg_queue.status & CONTEXT_LIST_QUEUED)) {
			queue_work(buf->ctx->dev->core_workqueue, &buf->ctx->msg_queue.core_work);
			buf->ctx->msg_queue.status |= CONTEXT_LIST_QUEUED;
		}
	}

	mtk_v4l2_vdec_dbg(3, buf->ctx, "enqueue buf type: %d addr: 0x%p num: %d",
			  msg_ctx->hardware_index, buf, msg_ctx->ready_num);
	spin_unlock(&msg_ctx->ready_lock);

	return 0;
}

static bool vdec_msg_queue_wait_event(struct vdec_msg_queue_ctx *msg_ctx)
{
	int ret;

	ret = wait_event_timeout(msg_ctx->ready_to_use,
				 !list_empty(&msg_ctx->ready_queue),
				 msecs_to_jiffies(VDEC_MSG_QUEUE_TIMEOUT_MS));
	if (!ret)
		return false;

	return true;
}

struct vdec_lat_buf *vdec_msg_queue_dqbuf(struct vdec_msg_queue_ctx *msg_ctx)
{
	struct vdec_lat_buf *buf;
	struct list_head *head;
	int ret;

	spin_lock(&msg_ctx->ready_lock);
	if (list_empty(&msg_ctx->ready_queue)) {
		spin_unlock(&msg_ctx->ready_lock);

		if (msg_ctx->hardware_index == MTK_VDEC_CORE)
			return NULL;

		ret = vdec_msg_queue_wait_event(msg_ctx);
		if (!ret)
			return NULL;
		spin_lock(&msg_ctx->ready_lock);
	}

	if (msg_ctx->hardware_index == MTK_VDEC_CORE)
		buf = list_first_entry(&msg_ctx->ready_queue,
				       struct vdec_lat_buf, core_list);
	else
		buf = list_first_entry(&msg_ctx->ready_queue,
				       struct vdec_lat_buf, lat_list);

	head = vdec_get_buf_list(msg_ctx->hardware_index, buf);
	if (!head) {
		spin_unlock(&msg_ctx->ready_lock);
		mtk_v4l2_vdec_err(buf->ctx, "fail to dqbuf: %d", msg_ctx->hardware_index);
		return NULL;
	}
	list_del(head);
	vdec_msg_queue_dec(&buf->ctx->msg_queue, msg_ctx->hardware_index);

	msg_ctx->ready_num--;
	mtk_v4l2_vdec_dbg(3, buf->ctx, "dqueue buf type:%d addr: 0x%p num: %d",
			  msg_ctx->hardware_index, buf, msg_ctx->ready_num);
	spin_unlock(&msg_ctx->ready_lock);

	return buf;
}

void vdec_msg_queue_update_ube_rptr(struct vdec_msg_queue *msg_queue, uint64_t ube_rptr)
{
	spin_lock(&msg_queue->lat_ctx.ready_lock);
	msg_queue->wdma_rptr_addr = ube_rptr;
	mtk_v4l2_vdec_dbg(3, msg_queue->ctx, "update ube rprt (0x%llx)", ube_rptr);
	spin_unlock(&msg_queue->lat_ctx.ready_lock);
}

void vdec_msg_queue_update_ube_wptr(struct vdec_msg_queue *msg_queue, uint64_t ube_wptr)
{
	spin_lock(&msg_queue->lat_ctx.ready_lock);
	msg_queue->wdma_wptr_addr = ube_wptr;
	mtk_v4l2_vdec_dbg(3, msg_queue->ctx, "update ube wprt: (0x%llx 0x%llx) offset: 0x%llx",
			  msg_queue->wdma_rptr_addr, msg_queue->wdma_wptr_addr,
			  ube_wptr);
	spin_unlock(&msg_queue->lat_ctx.ready_lock);
}

bool vdec_msg_queue_wait_lat_buf_full(struct vdec_msg_queue *msg_queue)
{
	if (atomic_read(&msg_queue->lat_list_cnt) == NUM_BUFFER_COUNT) {
		mtk_v4l2_vdec_dbg(3, msg_queue->ctx, "wait buf full: (%d %d) ready:%d status:%d",
				  atomic_read(&msg_queue->lat_list_cnt),
				  atomic_read(&msg_queue->core_list_cnt),
				  msg_queue->lat_ctx.ready_num, msg_queue->status);
		return true;
	}

	msg_queue->flush_done = false;
	vdec_msg_queue_qbuf(&msg_queue->core_ctx, &msg_queue->empty_lat_buf);
	wait_event(msg_queue->core_dec_done, msg_queue->flush_done);

	mtk_v4l2_vdec_dbg(3, msg_queue->ctx, "flush done => ready_num:%d status:%d list(%d %d)",
			  msg_queue->lat_ctx.ready_num, msg_queue->status,
			  atomic_read(&msg_queue->lat_list_cnt),
			  atomic_read(&msg_queue->core_list_cnt));

	return false;
}

void vdec_msg_queue_deinit(struct vdec_msg_queue *msg_queue,
			   struct mtk_vcodec_dec_ctx *ctx)
{
	struct vdec_lat_buf *lat_buf;
	struct mtk_vcodec_mem *mem;
	int i;

	mem = &msg_queue->wdma_addr;
	if (mem->va)
		mtk_vcodec_mem_free(ctx, mem);
	for (i = 0; i < NUM_BUFFER_COUNT; i++) {
		lat_buf = &msg_queue->lat_buf[i];

		mem = &lat_buf->wdma_err_addr;
		if (mem->va)
			mtk_vcodec_mem_free(ctx, mem);

		mem = &lat_buf->slice_bc_addr;
		if (mem->va)
			mtk_vcodec_mem_free(ctx, mem);

		mem = &lat_buf->rd_mv_addr;
		if (mem->va)
			mtk_vcodec_mem_free(ctx, mem);

		mem = &lat_buf->tile_addr;
		if (mem->va)
			mtk_vcodec_mem_free(ctx, mem);

		kfree(lat_buf->private_data);
		lat_buf->private_data = NULL;
	}

	if (msg_queue->wdma_addr.size)
		cancel_work_sync(&msg_queue->core_work);
}

static void vdec_msg_queue_core_work(struct work_struct *work)
{
	struct vdec_msg_queue *msg_queue =
		container_of(work, struct vdec_msg_queue, core_work);
	struct mtk_vcodec_dec_ctx *ctx =
		container_of(msg_queue, struct mtk_vcodec_dec_ctx, msg_queue);
	struct mtk_vcodec_dec_dev *dev = ctx->dev;
	struct vdec_lat_buf *lat_buf;

	spin_lock(&msg_queue->core_ctx.ready_lock);
	ctx->msg_queue.status &= ~CONTEXT_LIST_QUEUED;
	spin_unlock(&msg_queue->core_ctx.ready_lock);

	lat_buf = vdec_msg_queue_dqbuf(&msg_queue->core_ctx);
	if (!lat_buf)
		return;

	if (lat_buf->is_last_frame) {
		ctx->msg_queue.status = CONTEXT_LIST_DEC_DONE;
		msg_queue->flush_done = true;
		wake_up(&ctx->msg_queue.core_dec_done);

		return;
	}

	ctx = lat_buf->ctx;
	mtk_vcodec_dec_enable_hardware(ctx, MTK_VDEC_CORE);
	mtk_vcodec_set_curr_ctx(dev, ctx, MTK_VDEC_CORE);

	lat_buf->core_decode(lat_buf);

	mtk_vcodec_set_curr_ctx(dev, NULL, MTK_VDEC_CORE);
	mtk_vcodec_dec_disable_hardware(ctx, MTK_VDEC_CORE);
	vdec_msg_queue_qbuf(&ctx->msg_queue.lat_ctx, lat_buf);

	if (!(ctx->msg_queue.status & CONTEXT_LIST_QUEUED) &&
	    atomic_read(&msg_queue->core_list_cnt)) {
		spin_lock(&msg_queue->core_ctx.ready_lock);
		ctx->msg_queue.status |= CONTEXT_LIST_QUEUED;
		spin_unlock(&msg_queue->core_ctx.ready_lock);
		queue_work(ctx->dev->core_workqueue, &msg_queue->core_work);
	}
}

int vdec_msg_queue_init(struct vdec_msg_queue *msg_queue,
			struct mtk_vcodec_dec_ctx *ctx, core_decode_cb_t core_decode,
			int private_size)
{
	struct vdec_lat_buf *lat_buf;
	int i, err;

	/* already init msg queue */
	if (msg_queue->wdma_addr.size)
		return 0;

	vdec_msg_queue_init_ctx(&msg_queue->lat_ctx, MTK_VDEC_LAT0);
	vdec_msg_queue_init_ctx(&msg_queue->core_ctx, MTK_VDEC_CORE);
	INIT_WORK(&msg_queue->core_work, vdec_msg_queue_core_work);

	atomic_set(&msg_queue->lat_list_cnt, 0);
	atomic_set(&msg_queue->core_list_cnt, 0);
	init_waitqueue_head(&msg_queue->core_dec_done);
	msg_queue->status = CONTEXT_LIST_EMPTY;

	msg_queue->wdma_addr.size =
		vde_msg_queue_get_trans_size(ctx->picinfo.buf_w,
					     ctx->picinfo.buf_h);
	err = mtk_vcodec_mem_alloc(ctx, &msg_queue->wdma_addr);
	if (err) {
		mtk_v4l2_vdec_err(ctx, "failed to allocate wdma_addr buf");
		msg_queue->wdma_addr.size = 0;
		return -ENOMEM;
	}
	msg_queue->wdma_rptr_addr = msg_queue->wdma_addr.dma_addr;
	msg_queue->wdma_wptr_addr = msg_queue->wdma_addr.dma_addr;

	msg_queue->empty_lat_buf.ctx = ctx;
	msg_queue->empty_lat_buf.core_decode = NULL;
	msg_queue->empty_lat_buf.is_last_frame = true;

	msg_queue->ctx = ctx;
	for (i = 0; i < NUM_BUFFER_COUNT; i++) {
		lat_buf = &msg_queue->lat_buf[i];

		lat_buf->wdma_err_addr.size = VDEC_ERR_MAP_SZ_AVC;
		err = mtk_vcodec_mem_alloc(ctx, &lat_buf->wdma_err_addr);
		if (err) {
			mtk_v4l2_vdec_err(ctx, "failed to allocate wdma_err_addr buf[%d]", i);
			goto mem_alloc_err;
		}

		lat_buf->slice_bc_addr.size = VDEC_LAT_SLICE_HEADER_SZ;
		err = mtk_vcodec_mem_alloc(ctx, &lat_buf->slice_bc_addr);
		if (err) {
			mtk_v4l2_vdec_err(ctx, "failed to allocate wdma_addr buf[%d]", i);
			goto mem_alloc_err;
		}

		if (ctx->current_codec == V4L2_PIX_FMT_AV1_FRAME) {
			lat_buf->rd_mv_addr.size = VDEC_RD_MV_BUFFER_SZ;
			err = mtk_vcodec_mem_alloc(ctx, &lat_buf->rd_mv_addr);
			if (err) {
				mtk_v4l2_vdec_err(ctx, "failed to allocate rd_mv_addr buf[%d]", i);
				goto mem_alloc_err;
			}

			lat_buf->tile_addr.size = VDEC_LAT_TILE_SZ;
			err = mtk_vcodec_mem_alloc(ctx, &lat_buf->tile_addr);
			if (err) {
				mtk_v4l2_vdec_err(ctx, "failed to allocate tile_addr buf[%d]", i);
				goto mem_alloc_err;
			}
		}

		lat_buf->private_data = kzalloc(private_size, GFP_KERNEL);
		if (!lat_buf->private_data) {
			err = -ENOMEM;
			goto mem_alloc_err;
		}

		lat_buf->ctx = ctx;
		lat_buf->core_decode = core_decode;
		lat_buf->is_last_frame = false;
		err = vdec_msg_queue_qbuf(&msg_queue->lat_ctx, lat_buf);
		if (err) {
			mtk_v4l2_vdec_err(ctx, "failed to qbuf buf[%d]", i);
			goto mem_alloc_err;
		}
	}
	return 0;

mem_alloc_err:
	vdec_msg_queue_deinit(msg_queue, ctx);
	return err;
}
