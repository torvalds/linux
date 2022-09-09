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

static void
nvkm_falcon_msgq_open(struct nvkm_falcon_msgq *msgq)
{
	mutex_lock(&msgq->mutex);
	msgq->position = nvkm_falcon_rd32(msgq->qmgr->falcon, msgq->tail_reg);
}

static void
nvkm_falcon_msgq_close(struct nvkm_falcon_msgq *msgq, bool commit)
{
	struct nvkm_falcon *falcon = msgq->qmgr->falcon;

	if (commit)
		nvkm_falcon_wr32(falcon, msgq->tail_reg, msgq->position);

	mutex_unlock(&msgq->mutex);
}

static bool
nvkm_falcon_msgq_empty(struct nvkm_falcon_msgq *msgq)
{
	u32 head = nvkm_falcon_rd32(msgq->qmgr->falcon, msgq->head_reg);
	u32 tail = nvkm_falcon_rd32(msgq->qmgr->falcon, msgq->tail_reg);
	return head == tail;
}

static int
nvkm_falcon_msgq_pop(struct nvkm_falcon_msgq *msgq, void *data, u32 size)
{
	struct nvkm_falcon *falcon = msgq->qmgr->falcon;
	u32 head, tail, available;

	head = nvkm_falcon_rd32(falcon, msgq->head_reg);
	/* has the buffer looped? */
	if (head < msgq->position)
		msgq->position = msgq->offset;

	tail = msgq->position;

	available = head - tail;
	if (size > available) {
		FLCNQ_ERR(msgq, "requested %d bytes, but only %d available",
			  size, available);
		return -EINVAL;
	}

	nvkm_falcon_read_dmem(falcon, tail, size, 0, data);
	msgq->position += ALIGN(size, QUEUE_ALIGNMENT);
	return 0;
}

static int
nvkm_falcon_msgq_read(struct nvkm_falcon_msgq *msgq, struct nvfw_falcon_msg *hdr)
{
	int ret = 0;

	nvkm_falcon_msgq_open(msgq);

	if (nvkm_falcon_msgq_empty(msgq))
		goto close;

	ret = nvkm_falcon_msgq_pop(msgq, hdr, HDR_SIZE);
	if (ret) {
		FLCNQ_ERR(msgq, "failed to read message header");
		goto close;
	}

	if (hdr->size > MSG_BUF_SIZE) {
		FLCNQ_ERR(msgq, "message too big, %d bytes", hdr->size);
		ret = -ENOSPC;
		goto close;
	}

	if (hdr->size > HDR_SIZE) {
		u32 read_size = hdr->size - HDR_SIZE;

		ret = nvkm_falcon_msgq_pop(msgq, (hdr + 1), read_size);
		if (ret) {
			FLCNQ_ERR(msgq, "failed to read message data");
			goto close;
		}
	}

	ret = 1;
close:
	nvkm_falcon_msgq_close(msgq, (ret >= 0));
	return ret;
}

static int
nvkm_falcon_msgq_exec(struct nvkm_falcon_msgq *msgq, struct nvfw_falcon_msg *hdr)
{
	struct nvkm_falcon_qmgr_seq *seq;

	seq = &msgq->qmgr->seq.id[hdr->seq_id];
	if (seq->state != SEQ_STATE_USED && seq->state != SEQ_STATE_CANCELLED) {
		FLCNQ_ERR(msgq, "message for unknown sequence %08x", seq->id);
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

void
nvkm_falcon_msgq_recv(struct nvkm_falcon_msgq *msgq)
{
	/*
	 * We are invoked from a worker thread, so normally we have plenty of
	 * stack space to work with.
	 */
	u8 msg_buffer[MSG_BUF_SIZE];
	struct nvfw_falcon_msg *hdr = (void *)msg_buffer;

	while (nvkm_falcon_msgq_read(msgq, hdr) > 0)
		nvkm_falcon_msgq_exec(msgq, hdr);
}

int
nvkm_falcon_msgq_recv_initmsg(struct nvkm_falcon_msgq *msgq,
			      void *data, u32 size)
{
	struct nvkm_falcon *falcon = msgq->qmgr->falcon;
	struct nvfw_falcon_msg *hdr = data;
	int ret;

	msgq->head_reg = falcon->func->msgq.head;
	msgq->tail_reg = falcon->func->msgq.tail;
	msgq->offset = nvkm_falcon_rd32(falcon, falcon->func->msgq.tail);

	nvkm_falcon_msgq_open(msgq);
	ret = nvkm_falcon_msgq_pop(msgq, data, size);
	if (ret == 0 && hdr->size != size) {
		FLCN_ERR(falcon, "unexpected init message size %d vs %d",
			 hdr->size, size);
		ret = -EINVAL;
	}
	nvkm_falcon_msgq_close(msgq, ret == 0);
	return ret;
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
