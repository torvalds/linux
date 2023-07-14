/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#ifndef _VDEC_MSG_QUEUE_H_
#define _VDEC_MSG_QUEUE_H_

#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <media/videobuf2-v4l2.h>

#include "mtk_vcodec_util.h"

#define NUM_BUFFER_COUNT 3

struct vdec_lat_buf;
struct mtk_vcodec_ctx;
struct mtk_vcodec_dev;
typedef int (*core_decode_cb_t)(struct vdec_lat_buf *lat_buf);

/**
 * enum core_ctx_status - Context decode status for core hardwre.
 * @CONTEXT_LIST_EMPTY: No buffer queued on core hardware(must always be 0)
 * @CONTEXT_LIST_QUEUED: Buffer queued to core work list
 * @CONTEXT_LIST_DEC_DONE: context decode done
 */
enum core_ctx_status {
	CONTEXT_LIST_EMPTY = 0,
	CONTEXT_LIST_QUEUED,
	CONTEXT_LIST_DEC_DONE,
};

/**
 * struct vdec_msg_queue_ctx - represents a queue for buffers ready to be processed
 * @ready_to_use: ready used queue used to signalize when get a job queue
 * @ready_queue: list of ready lat buffer queues
 * @ready_lock: spin lock to protect the lat buffer usage
 * @ready_num: number of buffers ready to be processed
 * @hardware_index: hardware id that this queue is used for
 */
struct vdec_msg_queue_ctx {
	wait_queue_head_t ready_to_use;
	struct list_head ready_queue;
	/* protect lat buffer */
	spinlock_t ready_lock;
	int ready_num;
	int hardware_index;
};

/**
 * struct vdec_lat_buf - lat buffer message used to store lat info for core decode
 * @wdma_err_addr: wdma error address used for lat hardware
 * @slice_bc_addr: slice bc address used for lat hardware
 * @rd_mv_addr:	mv addr for av1 lat hardware output, core hardware input
 * @tile_addr:	tile buffer for av1 core input
 * @ts_info: need to set timestamp from output to capture
 * @src_buf_req: output buffer media request object
 *
 * @private_data: shared information used to lat and core hardware
 * @ctx: mtk vcodec context information
 * @core_decode: different codec use different decode callback function
 * @lat_list: add lat buffer to lat head list
 * @core_list: add lat buffer to core head list
 *
 * @is_last_frame: meaning this buffer is the last frame
 */
struct vdec_lat_buf {
	struct mtk_vcodec_mem wdma_err_addr;
	struct mtk_vcodec_mem slice_bc_addr;
	struct mtk_vcodec_mem rd_mv_addr;
	struct mtk_vcodec_mem tile_addr;
	struct vb2_v4l2_buffer ts_info;
	struct media_request *src_buf_req;

	void *private_data;
	struct mtk_vcodec_ctx *ctx;
	core_decode_cb_t core_decode;
	struct list_head lat_list;
	struct list_head core_list;

	bool is_last_frame;
};

/**
 * struct vdec_msg_queue - used to store lat buffer message
 * @lat_buf: lat buffer used to store lat buffer information
 * @wdma_addr: wdma address used for ube
 * @wdma_rptr_addr: ube read point
 * @wdma_wptr_addr: ube write point
 * @core_work: core hardware work
 * @lat_ctx: used to store lat buffer list
 * @core_ctx: used to store core buffer list
 *
 * @lat_list_cnt: used to record each instance lat list count
 * @core_list_cnt: used to record each instance core list count
 * @flush_done: core flush done status
 * @empty_lat_buf: the last lat buf used to flush decode
 * @core_dec_done: core work queue decode done event
 * @status: current context decode status for core hardware
 */
struct vdec_msg_queue {
	struct vdec_lat_buf lat_buf[NUM_BUFFER_COUNT];

	struct mtk_vcodec_mem wdma_addr;
	u64 wdma_rptr_addr;
	u64 wdma_wptr_addr;

	struct work_struct core_work;
	struct vdec_msg_queue_ctx lat_ctx;
	struct vdec_msg_queue_ctx core_ctx;

	atomic_t lat_list_cnt;
	atomic_t core_list_cnt;
	bool flush_done;
	struct vdec_lat_buf empty_lat_buf;
	wait_queue_head_t core_dec_done;
	int status;
};

/**
 * vdec_msg_queue_init - init lat buffer information.
 * @msg_queue: used to store the lat buffer information
 * @ctx: v4l2 ctx
 * @core_decode: core decode callback for each codec
 * @private_size: the private data size used to share with core
 *
 * Return: returns 0 if init successfully, or fail.
 */
int vdec_msg_queue_init(struct vdec_msg_queue *msg_queue,
			struct mtk_vcodec_ctx *ctx, core_decode_cb_t core_decode,
			int private_size);

/**
 * vdec_msg_queue_init_ctx - used to init msg queue context information.
 * @ctx: message queue context
 * @hardware_index: hardware index
 */
void vdec_msg_queue_init_ctx(struct vdec_msg_queue_ctx *ctx, int hardware_index);

/**
 * vdec_msg_queue_qbuf - enqueue lat buffer to queue list.
 * @ctx: message queue context
 * @buf: current lat buffer
 *
 * Return: returns 0 if qbuf successfully, or fail.
 */
int vdec_msg_queue_qbuf(struct vdec_msg_queue_ctx *ctx, struct vdec_lat_buf *buf);

/**
 * vdec_msg_queue_dqbuf - dequeue lat buffer from queue list.
 * @ctx: message queue context
 *
 * Return: returns not null if dq successfully, or fail.
 */
struct vdec_lat_buf *vdec_msg_queue_dqbuf(struct vdec_msg_queue_ctx *ctx);

/**
 * vdec_msg_queue_update_ube_rptr - used to updata the ube read point.
 * @msg_queue: used to store the lat buffer information
 * @ube_rptr: current ube read point
 */
void vdec_msg_queue_update_ube_rptr(struct vdec_msg_queue *msg_queue, uint64_t ube_rptr);

/**
 * vdec_msg_queue_update_ube_wptr - used to updata the ube write point.
 * @msg_queue: used to store the lat buffer information
 * @ube_wptr: current ube write point
 */
void vdec_msg_queue_update_ube_wptr(struct vdec_msg_queue *msg_queue, uint64_t ube_wptr);

/**
 * vdec_msg_queue_wait_lat_buf_full - used to check whether all lat buffer
 *                                    in lat list.
 * @msg_queue: used to store the lat buffer information
 *
 * Return: returns true if successfully, or fail.
 */
bool vdec_msg_queue_wait_lat_buf_full(struct vdec_msg_queue *msg_queue);

/**
 * vdec_msg_queue_deinit - deinit lat buffer information.
 * @msg_queue: used to store the lat buffer information
 * @ctx: v4l2 ctx
 */
void vdec_msg_queue_deinit(struct vdec_msg_queue *msg_queue,
			   struct mtk_vcodec_ctx *ctx);

#endif
