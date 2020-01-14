/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "qmgr.h"

static int
msg_queue_open(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_queue *queue)
{
	struct nvkm_falcon *falcon = priv->falcon;
	mutex_lock(&queue->mutex);
	queue->position = nvkm_falcon_rd32(falcon, queue->tail_reg);
	return 0;
}

static void
msg_queue_close(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_queue *queue,
		bool commit)
{
	struct nvkm_falcon *falcon = priv->falcon;

	if (commit)
		nvkm_falcon_wr32(falcon, queue->tail_reg, queue->position);

	mutex_unlock(&queue->mutex);
}

static bool
msg_queue_empty(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_queue *queue)
{
	struct nvkm_falcon *falcon = priv->falcon;
	u32 head = nvkm_falcon_rd32(falcon, queue->head_reg);
	u32 tail = nvkm_falcon_rd32(falcon, queue->tail_reg);
	return head == tail;
}

static int
msg_queue_pop(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_queue *queue,
	      void *data, u32 size)
{
	struct nvkm_falcon *falcon = priv->falcon;
	const struct nvkm_subdev *subdev = priv->falcon->owner;
	u32 head, tail, available;

	head = nvkm_falcon_rd32(falcon, queue->head_reg);
	/* has the buffer looped? */
	if (head < queue->position)
		queue->position = queue->offset;

	tail = queue->position;

	available = head - tail;

	if (available == 0) {
		nvkm_warn(subdev, "no message data available\n");
		return 0;
	}

	if (size > available) {
		nvkm_warn(subdev, "message data smaller than read request\n");
		size = available;
	}

	nvkm_falcon_read_dmem(priv->falcon, tail, size, 0, data);
	queue->position += ALIGN(size, QUEUE_ALIGNMENT);
	return size;
}

static int
msg_queue_read(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_queue *queue,
	       struct nv_falcon_msg *hdr)
{
	const struct nvkm_subdev *subdev = priv->falcon->owner;
	int ret;

	ret = msg_queue_open(priv, queue);
	if (ret) {
		nvkm_error(subdev, "fail to open queue %d\n", queue->index);
		return ret;
	}

	if (msg_queue_empty(priv, queue)) {
		ret = 0;
		goto close;
	}

	ret = msg_queue_pop(priv, queue, hdr, HDR_SIZE);
	if (ret >= 0 && ret != HDR_SIZE)
		ret = -EINVAL;
	if (ret < 0) {
		nvkm_error(subdev, "failed to read message header: %d\n", ret);
		goto close;
	}

	if (hdr->size > MSG_BUF_SIZE) {
		nvkm_error(subdev, "message too big (%d bytes)\n", hdr->size);
		ret = -ENOSPC;
		goto close;
	}

	if (hdr->size > HDR_SIZE) {
		u32 read_size = hdr->size - HDR_SIZE;

		ret = msg_queue_pop(priv, queue, (hdr + 1), read_size);
		if (ret >= 0 && ret != read_size)
			ret = -EINVAL;
		if (ret < 0) {
			nvkm_error(subdev, "failed to read message: %d\n", ret);
			goto close;
		}
	}

close:
	msg_queue_close(priv, queue, (ret >= 0));
	return ret;
}

static int
msgqueue_msg_handle(struct nvkm_msgqueue *priv,
		    struct nvkm_falcon_msgq *msgq,
		    struct nv_falcon_msg *hdr)
{
	const struct nvkm_subdev *subdev = priv->falcon->owner;
	struct nvkm_falcon_qmgr_seq *seq;

	seq = &msgq->qmgr->seq.id[hdr->seq_id];
	if (seq->state != SEQ_STATE_USED && seq->state != SEQ_STATE_CANCELLED) {
		nvkm_error(subdev, "msg for unknown sequence %d", seq->id);
		return -EINVAL;
	}

	if (seq->state == SEQ_STATE_USED) {
		if (seq->callback)
			seq->result = seq->callback(seq->priv, hdr);
	}

	if (seq->async) {
		nvkm_falcon_qmgr_seq_release(msgq->qmgr, seq);
		return 0;
	}

	complete_all(&seq->done);
	return 0;
}

static int
msgqueue_handle_init_msg(struct nvkm_msgqueue *priv)
{
	struct nvkm_falcon *falcon = priv->falcon;
	const struct nvkm_subdev *subdev = falcon->owner;
	const u32 tail_reg = falcon->func->msgq.tail;
	u8 msg_buffer[MSG_BUF_SIZE];
	struct nvkm_msgqueue_hdr *hdr = (void *)msg_buffer;
	u32 tail;
	int ret;

	/*
	 * Read the message - queues are not initialized yet so we cannot rely
	 * on msg_queue_read()
	 */
	tail = nvkm_falcon_rd32(falcon, tail_reg);
	nvkm_falcon_read_dmem(falcon, tail, HDR_SIZE, 0, hdr);

	if (hdr->size > MSG_BUF_SIZE) {
		nvkm_error(subdev, "message too big (%d bytes)\n", hdr->size);
		return -ENOSPC;
	}

	nvkm_falcon_read_dmem(falcon, tail + HDR_SIZE, hdr->size - HDR_SIZE, 0,
			      (hdr + 1));

	tail += ALIGN(hdr->size, QUEUE_ALIGNMENT);
	nvkm_falcon_wr32(falcon, tail_reg, tail);

	ret = priv->func->init_func->init_callback(priv, hdr);
	if (ret)
		return ret;

	return 0;
}

void
nvkm_msgqueue_process_msgs(struct nvkm_msgqueue *priv,
			   struct nvkm_msgqueue_queue *queue)
{
	/*
	 * We are invoked from a worker thread, so normally we have plenty of
	 * stack space to work with.
	 */
	u8 msg_buffer[MSG_BUF_SIZE];
	struct nv_falcon_msg *hdr = (void *)msg_buffer;
	int ret;

	/* the first message we receive must be the init message */
	if ((!priv->init_msg_received)) {
		ret = msgqueue_handle_init_msg(priv);
		if (!ret)
			priv->init_msg_received = true;
	} else {
		while (msg_queue_read(priv, queue, hdr) > 0)
			msgqueue_msg_handle(priv, queue, hdr);
	}
}

void
nvkm_falcon_msgq_init(struct nvkm_falcon_msgq *msgq,
		      u32 index, u32 offset, u32 size)
{
	const struct nvkm_falcon_func *func = msgq->qmgr->falcon->func;

	msgq->head_reg = func->msgq.head + index * func->msgq.stride;
	msgq->tail_reg = func->msgq.tail + index * func->msgq.stride;
	msgq->offset = offset;

	FLCNQ_DBG(msgq, "initialised @ index %d offset 0x%08x size 0x%08x",
		  index, msgq->offset, size);
}

void
nvkm_falcon_msgq_del(struct nvkm_falcon_msgq **pmsgq)
{
	struct nvkm_falcon_msgq *msgq = *pmsgq;
	if (msgq) {
		kfree(*pmsgq);
		*pmsgq = NULL;
	}
}

int
nvkm_falcon_msgq_new(struct nvkm_falcon_qmgr *qmgr, const char *name,
		     struct nvkm_falcon_msgq **pmsgq)
{
	struct nvkm_falcon_msgq *msgq = *pmsgq;

	if (!(msgq = *pmsgq = kzalloc(sizeof(*msgq), GFP_KERNEL)))
		return -ENOMEM;

	msgq->qmgr = qmgr;
	msgq->name = name;
	mutex_init(&msgq->mutex);
	return 0;
}
