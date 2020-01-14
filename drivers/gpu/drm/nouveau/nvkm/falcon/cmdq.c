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

static bool
cmd_queue_has_room(struct nvkm_msgqueue *priv,
		   struct nvkm_msgqueue_queue *queue, u32 size, bool *rewind)
{
	struct nvkm_falcon *falcon = priv->falcon;
	u32 head = nvkm_falcon_rd32(falcon, queue->head_reg);
	u32 tail = nvkm_falcon_rd32(falcon, queue->tail_reg);
	u32 free;

	size = ALIGN(size, QUEUE_ALIGNMENT);

	if (head >= tail) {
		free = queue->offset + queue->size - head;
		free -= HDR_SIZE;

		if (size > free) {
			*rewind = true;
			head = queue->offset;
		}
	}

	if (head < tail)
		free = tail - head - 1;

	return size <= free;
}

static void
cmd_queue_push(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_queue *queue,
	       void *data, u32 size)
{
	nvkm_falcon_load_dmem(priv->falcon, data, queue->position, size, 0);
	queue->position += ALIGN(size, QUEUE_ALIGNMENT);
}

/* REWIND unit is always 0x00 */
#define MSGQUEUE_UNIT_REWIND 0x00

static void
cmd_queue_rewind(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_queue *queue)
{
	struct nvkm_msgqueue_hdr cmd;

	cmd.unit_id = MSGQUEUE_UNIT_REWIND;
	cmd.size = sizeof(cmd);
	cmd_queue_push(priv, queue, &cmd, cmd.size);

	queue->position = queue->offset;
}

static int
cmd_queue_open(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_queue *queue,
	       u32 size)
{
	struct nvkm_falcon *falcon = priv->falcon;
	const struct nvkm_subdev *subdev = priv->falcon->owner;
	bool rewind = false;

	mutex_lock(&queue->mutex);

	if (!cmd_queue_has_room(priv, queue, size, &rewind)) {
		nvkm_error(subdev, "queue full\n");
		mutex_unlock(&queue->mutex);
		return -EAGAIN;
	}

	queue->position = nvkm_falcon_rd32(falcon, queue->head_reg);

	if (rewind)
		cmd_queue_rewind(priv, queue);

	return 0;
}

static void
cmd_queue_close(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_queue *queue)
{
	nvkm_falcon_wr32(queue->qmgr->falcon, queue->head_reg, queue->position);
	mutex_unlock(&queue->mutex);
}

static int
cmd_write(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_hdr *cmd,
	  struct nvkm_msgqueue_queue *queue)
{
	const struct nvkm_subdev *subdev = priv->falcon->owner;
	static unsigned timeout = 2000;
	unsigned long end_jiffies = jiffies + msecs_to_jiffies(timeout);
	int ret = -EAGAIN;

	while (ret == -EAGAIN && time_before(jiffies, end_jiffies))
		ret = cmd_queue_open(priv, queue, cmd->size);
	if (ret) {
		nvkm_error(subdev, "pmu_queue_open_write failed\n");
		return ret;
	}

	cmd_queue_push(priv, queue, cmd, cmd->size);
	cmd_queue_close(priv, queue);
	return ret;
}

/* specifies that we want to know the command status in the answer message */
#define CMD_FLAGS_STATUS BIT(0)
/* specifies that we want an interrupt when the answer message is queued */
#define CMD_FLAGS_INTR BIT(1)

int
nvkm_msgqueue_post(struct nvkm_msgqueue *priv, enum msgqueue_msg_priority prio,
		   struct nvkm_msgqueue_hdr *cmd, nvkm_falcon_qmgr_callback cb,
		   struct completion *completion, bool wait_init)
{
	struct nvkm_falcon_qmgr_seq *seq;
	struct nvkm_msgqueue_queue *queue;
	int ret;

	queue = priv->func->cmd_queue(priv, prio);
	if (IS_ERR(queue))
		return PTR_ERR(queue);

	if (!wait_for_completion_timeout(&queue->ready,
					 msecs_to_jiffies(1000))) {
		FLCNQ_ERR(queue, "timeout waiting for queue ready");
		return -ETIMEDOUT;
	}

	seq = nvkm_falcon_qmgr_seq_acquire(queue->qmgr);
	if (IS_ERR(seq))
		return PTR_ERR(seq);

	cmd->seq_id = seq->id;
	cmd->ctrl_flags = CMD_FLAGS_STATUS | CMD_FLAGS_INTR;

	seq->state = SEQ_STATE_USED;
	seq->async = !completion;
	seq->callback = cb;
	seq->priv = priv;

	ret = cmd_write(priv, cmd, queue);
	if (ret) {
		seq->state = SEQ_STATE_PENDING;
		nvkm_falcon_qmgr_seq_release(queue->qmgr, seq);
		return ret;
	}

	if (!seq->async) {
		if (!wait_for_completion_timeout(&seq->done,
						 msecs_to_jiffies(1000)))
			return -ETIMEDOUT;
		ret = seq->result;
		nvkm_falcon_qmgr_seq_release(queue->qmgr, seq);
	}

	return ret;
}

void
nvkm_falcon_cmdq_fini(struct nvkm_falcon_cmdq *cmdq)
{
	reinit_completion(&cmdq->ready);
}

void
nvkm_falcon_cmdq_init(struct nvkm_falcon_cmdq *cmdq,
		      u32 index, u32 offset, u32 size)
{
	const struct nvkm_falcon_func *func = cmdq->qmgr->falcon->func;

	cmdq->head_reg = func->cmdq.head + index * func->cmdq.stride;
	cmdq->tail_reg = func->cmdq.tail + index * func->cmdq.stride;
	cmdq->offset = offset;
	cmdq->size = size;
	complete_all(&cmdq->ready);

	FLCNQ_DBG(cmdq, "initialised @ index %d offset 0x%08x size 0x%08x",
		  index, cmdq->offset, cmdq->size);
}

void
nvkm_falcon_cmdq_del(struct nvkm_falcon_cmdq **pcmdq)
{
	struct nvkm_falcon_cmdq *cmdq = *pcmdq;
	if (cmdq) {
		kfree(*pcmdq);
		*pcmdq = NULL;
	}
}

int
nvkm_falcon_cmdq_new(struct nvkm_falcon_qmgr *qmgr, const char *name,
		     struct nvkm_falcon_cmdq **pcmdq)
{
	struct nvkm_falcon_cmdq *cmdq = *pcmdq;

	if (!(cmdq = *pcmdq = kzalloc(sizeof(*cmdq), GFP_KERNEL)))
		return -ENOMEM;

	cmdq->qmgr = qmgr;
	cmdq->name = name;
	mutex_init(&cmdq->mutex);
	init_completion(&cmdq->ready);
	return 0;
}
