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

#include "msgqueue.h"
#include <engine/falcon.h>

#include <subdev/secboot.h>


#define HDR_SIZE sizeof(struct nvkm_msgqueue_hdr)
#define QUEUE_ALIGNMENT 4
/* max size of the messages we can receive */
#define MSG_BUF_SIZE 128

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
	u32 head, tail;

	head = nvkm_falcon_rd32(falcon, queue->head_reg);
	tail = nvkm_falcon_rd32(falcon, queue->tail_reg);

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
	       struct nvkm_msgqueue_hdr *hdr)
{
	const struct nvkm_subdev *subdev = priv->falcon->owner;
	int err;

	err = msg_queue_open(priv, queue);
	if (err) {
		nvkm_error(subdev, "fail to open queue %d\n", queue->index);
		return err;
	}

	if (msg_queue_empty(priv, queue)) {
		err = 0;
		goto close;
	}

	err = msg_queue_pop(priv, queue, hdr, HDR_SIZE);
	if (err >= 0 && err != HDR_SIZE)
		err = -EINVAL;
	if (err < 0) {
		nvkm_error(subdev, "failed to read message header: %d\n", err);
		goto close;
	}

	if (hdr->size > MSG_BUF_SIZE) {
		nvkm_error(subdev, "message too big (%d bytes)\n", hdr->size);
		err = -ENOSPC;
		goto close;
	}

	if (hdr->size > HDR_SIZE) {
		u32 read_size = hdr->size - HDR_SIZE;

		err = msg_queue_pop(priv, queue, (hdr + 1), read_size);
		if (err >= 0 && err != read_size)
			err = -EINVAL;
		if (err < 0) {
			nvkm_error(subdev, "failed to read message: %d\n", err);
			goto close;
		}
	}

close:
	msg_queue_close(priv, queue, (err >= 0));

	return err;
}

static bool
cmd_queue_has_room(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_queue *queue,
		   u32 size, bool *rewind)
{
	struct nvkm_falcon *falcon = priv->falcon;
	u32 head, tail, free;

	size = ALIGN(size, QUEUE_ALIGNMENT);

	head = nvkm_falcon_rd32(falcon, queue->head_reg);
	tail = nvkm_falcon_rd32(falcon, queue->tail_reg);

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

static int
cmd_queue_push(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_queue *queue,
	       void *data, u32 size)
{
	nvkm_falcon_load_dmem(priv->falcon, data, queue->position, size, 0);
	queue->position += ALIGN(size, QUEUE_ALIGNMENT);

	return 0;
}

/* REWIND unit is always 0x00 */
#define MSGQUEUE_UNIT_REWIND 0x00

static void
cmd_queue_rewind(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_queue *queue)
{
	const struct nvkm_subdev *subdev = priv->falcon->owner;
	struct nvkm_msgqueue_hdr cmd;
	int err;

	cmd.unit_id = MSGQUEUE_UNIT_REWIND;
	cmd.size = sizeof(cmd);
	err = cmd_queue_push(priv, queue, &cmd, cmd.size);
	if (err)
		nvkm_error(subdev, "queue %d rewind failed\n", queue->index);
	else
		nvkm_error(subdev, "queue %d rewinded\n", queue->index);

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
cmd_queue_close(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_queue *queue,
		bool commit)
{
	struct nvkm_falcon *falcon = priv->falcon;

	if (commit)
		nvkm_falcon_wr32(falcon, queue->head_reg, queue->position);

	mutex_unlock(&queue->mutex);
}

static int
cmd_write(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_hdr *cmd,
	  struct nvkm_msgqueue_queue *queue)
{
	const struct nvkm_subdev *subdev = priv->falcon->owner;
	static unsigned long timeout = ~0;
	unsigned long end_jiffies = jiffies + msecs_to_jiffies(timeout);
	int ret = -EAGAIN;
	bool commit = true;

	while (ret == -EAGAIN && time_before(jiffies, end_jiffies))
		ret = cmd_queue_open(priv, queue, cmd->size);
	if (ret) {
		nvkm_error(subdev, "pmu_queue_open_write failed\n");
		return ret;
	}

	ret = cmd_queue_push(priv, queue, cmd, cmd->size);
	if (ret) {
		nvkm_error(subdev, "pmu_queue_push failed\n");
		commit = false;
	}

	   cmd_queue_close(priv, queue, commit);

	return ret;
}

static struct nvkm_msgqueue_seq *
msgqueue_seq_acquire(struct nvkm_msgqueue *priv)
{
	const struct nvkm_subdev *subdev = priv->falcon->owner;
	struct nvkm_msgqueue_seq *seq;
	u32 index;

	mutex_lock(&priv->seq_lock);

	index = find_first_zero_bit(priv->seq_tbl, NVKM_MSGQUEUE_NUM_SEQUENCES);

	if (index >= NVKM_MSGQUEUE_NUM_SEQUENCES) {
		nvkm_error(subdev, "no free sequence available\n");
		mutex_unlock(&priv->seq_lock);
		return ERR_PTR(-EAGAIN);
	}

	set_bit(index, priv->seq_tbl);

	mutex_unlock(&priv->seq_lock);

	seq = &priv->seq[index];
	seq->state = SEQ_STATE_PENDING;

	return seq;
}

static void
msgqueue_seq_release(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_seq *seq)
{
	/* no need to acquire seq_lock since clear_bit is atomic */
	seq->state = SEQ_STATE_FREE;
	seq->callback = NULL;
	seq->completion = NULL;
	clear_bit(seq->id, priv->seq_tbl);
}

/* specifies that we want to know the command status in the answer message */
#define CMD_FLAGS_STATUS BIT(0)
/* specifies that we want an interrupt when the answer message is queued */
#define CMD_FLAGS_INTR BIT(1)

int
nvkm_msgqueue_post(struct nvkm_msgqueue *priv, enum msgqueue_msg_priority prio,
		   struct nvkm_msgqueue_hdr *cmd, nvkm_msgqueue_callback cb,
		   struct completion *completion, bool wait_init)
{
	struct nvkm_msgqueue_seq *seq;
	struct nvkm_msgqueue_queue *queue;
	int ret;

	if (wait_init && !wait_for_completion_timeout(&priv->init_done,
					 msecs_to_jiffies(1000)))
		return -ETIMEDOUT;

	queue = priv->func->cmd_queue(priv, prio);
	if (IS_ERR(queue))
		return PTR_ERR(queue);

	seq = msgqueue_seq_acquire(priv);
	if (IS_ERR(seq))
		return PTR_ERR(seq);

	cmd->seq_id = seq->id;
	cmd->ctrl_flags = CMD_FLAGS_STATUS | CMD_FLAGS_INTR;

	seq->callback = cb;
	seq->state = SEQ_STATE_USED;
	seq->completion = completion;

	ret = cmd_write(priv, cmd, queue);
	if (ret) {
		seq->state = SEQ_STATE_PENDING;
		      msgqueue_seq_release(priv, seq);
	}

	return ret;
}

static int
msgqueue_msg_handle(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_hdr *hdr)
{
	const struct nvkm_subdev *subdev = priv->falcon->owner;
	struct nvkm_msgqueue_seq *seq;

	seq = &priv->seq[hdr->seq_id];
	if (seq->state != SEQ_STATE_USED && seq->state != SEQ_STATE_CANCELLED) {
		nvkm_error(subdev, "msg for unknown sequence %d", seq->id);
		return -EINVAL;
	}

	if (seq->state == SEQ_STATE_USED) {
		if (seq->callback)
			seq->callback(priv, hdr);
	}

	if (seq->completion)
		complete(seq->completion);

	   msgqueue_seq_release(priv, seq);

	return 0;
}

static int
msgqueue_handle_init_msg(struct nvkm_msgqueue *priv,
			 struct nvkm_msgqueue_hdr *hdr)
{
	struct nvkm_falcon *falcon = priv->falcon;
	const struct nvkm_subdev *subdev = falcon->owner;
	u32 tail;
	u32 tail_reg;
	int ret;

	/*
	 * Of course the message queue registers vary depending on the falcon
	 * used...
	 */
	switch (falcon->owner->index) {
	case NVKM_SUBDEV_PMU:
		tail_reg = 0x4cc;
		break;
	case NVKM_ENGINE_SEC2:
		tail_reg = 0xa34;
		break;
	default:
		nvkm_error(subdev, "falcon %s unsupported for msgqueue!\n",
			   nvkm_subdev_name[falcon->owner->index]);
		return -EINVAL;
	}

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
	struct nvkm_msgqueue_hdr *hdr = (void *)msg_buffer;
	int ret;

	/* the first message we receive must be the init message */
	if ((!priv->init_msg_received)) {
		ret = msgqueue_handle_init_msg(priv, hdr);
		if (!ret)
			priv->init_msg_received = true;
	} else {
		while (msg_queue_read(priv, queue, hdr) > 0)
			msgqueue_msg_handle(priv, hdr);
	}
}

void
nvkm_msgqueue_write_cmdline(struct nvkm_msgqueue *queue, void *buf)
{
	if (!queue || !queue->func || !queue->func->init_func)
		return;

	queue->func->init_func->gen_cmdline(queue, buf);
}

int
nvkm_msgqueue_acr_boot_falcons(struct nvkm_msgqueue *queue,
			       unsigned long falcon_mask)
{
	unsigned long falcon;

	if (!queue || !queue->func->acr_func)
		return -ENODEV;

	/* Does the firmware support booting multiple falcons? */
	if (queue->func->acr_func->boot_multiple_falcons)
		return queue->func->acr_func->boot_multiple_falcons(queue,
								   falcon_mask);

	/* Else boot all requested falcons individually */
	if (!queue->func->acr_func->boot_falcon)
		return -ENODEV;

	for_each_set_bit(falcon, &falcon_mask, NVKM_SECBOOT_FALCON_END) {
		int ret = queue->func->acr_func->boot_falcon(queue, falcon);

		if (ret)
			return ret;
	}

	return 0;
}

int
nvkm_msgqueue_new(u32 version, struct nvkm_falcon *falcon,
		  const struct nvkm_secboot *sb, struct nvkm_msgqueue **queue)
{
	const struct nvkm_subdev *subdev = falcon->owner;
	int ret = -EINVAL;

	switch (version) {
	case 0x0137c63d:
		ret = msgqueue_0137c63d_new(falcon, sb, queue);
		break;
	case 0x0137bca5:
		ret = msgqueue_0137bca5_new(falcon, sb, queue);
		break;
	case 0x0148cdec:
		ret = msgqueue_0148cdec_new(falcon, sb, queue);
		break;
	default:
		nvkm_error(subdev, "unhandled firmware version 0x%08x\n",
			   version);
		break;
	}

	if (ret == 0) {
		nvkm_debug(subdev, "firmware version: 0x%08x\n", version);
		(*queue)->fw_version = version;
	}

	return ret;
}

void
nvkm_msgqueue_del(struct nvkm_msgqueue **queue)
{
	if (*queue) {
		(*queue)->func->dtor(*queue);
		*queue = NULL;
	}
}

void
nvkm_msgqueue_recv(struct nvkm_msgqueue *queue)
{
	if (!queue->func || !queue->func->recv) {
		const struct nvkm_subdev *subdev = queue->falcon->owner;

		nvkm_warn(subdev, "missing msgqueue recv function\n");
		return;
	}

	queue->func->recv(queue);
}

int
nvkm_msgqueue_reinit(struct nvkm_msgqueue *queue)
{
	/* firmware not set yet... */
	if (!queue)
		return 0;

	queue->init_msg_received = false;
	reinit_completion(&queue->init_done);

	return 0;
}

void
nvkm_msgqueue_ctor(const struct nvkm_msgqueue_func *func,
		   struct nvkm_falcon *falcon,
		   struct nvkm_msgqueue *queue)
{
	int i;

	queue->func = func;
	queue->falcon = falcon;
	mutex_init(&queue->seq_lock);
	for (i = 0; i < NVKM_MSGQUEUE_NUM_SEQUENCES; i++)
		queue->seq[i].id = i;

	init_completion(&queue->init_done);


}
