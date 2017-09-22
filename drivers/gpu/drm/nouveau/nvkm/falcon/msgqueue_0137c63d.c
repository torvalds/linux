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

/* Queues identifiers */
enum {
	/* High Priority Command Queue for Host -> PMU communication */
	MSGQUEUE_0137C63D_COMMAND_QUEUE_HPQ = 0,
	/* Low Priority Command Queue for Host -> PMU communication */
	MSGQUEUE_0137C63D_COMMAND_QUEUE_LPQ = 1,
	/* Message queue for PMU -> Host communication */
	MSGQUEUE_0137C63D_MESSAGE_QUEUE = 4,
	MSGQUEUE_0137C63D_NUM_QUEUES = 5,
};

struct msgqueue_0137c63d {
	struct nvkm_msgqueue base;

	struct nvkm_msgqueue_queue queue[MSGQUEUE_0137C63D_NUM_QUEUES];
};
#define msgqueue_0137c63d(q) \
	container_of(q, struct msgqueue_0137c63d, base)

struct msgqueue_0137bca5 {
	struct msgqueue_0137c63d base;

	u64 wpr_addr;
};
#define msgqueue_0137bca5(q) \
	container_of(container_of(q, struct msgqueue_0137c63d, base), \
		     struct msgqueue_0137bca5, base);

static struct nvkm_msgqueue_queue *
msgqueue_0137c63d_cmd_queue(struct nvkm_msgqueue *queue,
			    enum msgqueue_msg_priority priority)
{
	struct msgqueue_0137c63d *priv = msgqueue_0137c63d(queue);
	const struct nvkm_subdev *subdev = priv->base.falcon->owner;

	switch (priority) {
	case MSGQUEUE_MSG_PRIORITY_HIGH:
		return &priv->queue[MSGQUEUE_0137C63D_COMMAND_QUEUE_HPQ];
	case MSGQUEUE_MSG_PRIORITY_LOW:
		return &priv->queue[MSGQUEUE_0137C63D_COMMAND_QUEUE_LPQ];
	default:
		nvkm_error(subdev, "invalid command queue!\n");
		return ERR_PTR(-EINVAL);
	}
}

static void
msgqueue_0137c63d_process_msgs(struct nvkm_msgqueue *queue)
{
	struct msgqueue_0137c63d *priv = msgqueue_0137c63d(queue);
	struct nvkm_msgqueue_queue *q_queue =
		&priv->queue[MSGQUEUE_0137C63D_MESSAGE_QUEUE];

	nvkm_msgqueue_process_msgs(&priv->base, q_queue);
}

/* Init unit */
#define MSGQUEUE_0137C63D_UNIT_INIT 0x07

enum {
	INIT_MSG_INIT = 0x0,
};

static void
init_gen_cmdline(struct nvkm_msgqueue *queue, void *buf)
{
	struct {
		u32 reserved;
		u32 freq_hz;
		u32 trace_size;
		u32 trace_dma_base;
		u16 trace_dma_base1;
		u8 trace_dma_offset;
		u32 trace_dma_idx;
		bool secure_mode;
		bool raise_priv_sec;
		struct {
			u32 dma_base;
			u16 dma_base1;
			u8 dma_offset;
			u16 fb_size;
			u8 dma_idx;
		} gc6_ctx;
		u8 pad;
	} *args = buf;

	args->secure_mode = 1;
}

/* forward declaration */
static int acr_init_wpr(struct nvkm_msgqueue *queue);

static int
init_callback(struct nvkm_msgqueue *_queue, struct nvkm_msgqueue_hdr *hdr)
{
	struct msgqueue_0137c63d *priv = msgqueue_0137c63d(_queue);
	struct {
		struct nvkm_msgqueue_msg base;

		u8 pad;
		u16 os_debug_entry_point;

		struct {
			u16 size;
			u16 offset;
			u8 index;
			u8 pad;
		} queue_info[MSGQUEUE_0137C63D_NUM_QUEUES];

		u16 sw_managed_area_offset;
		u16 sw_managed_area_size;
	} *init = (void *)hdr;
	const struct nvkm_subdev *subdev = _queue->falcon->owner;
	int i;

	if (init->base.hdr.unit_id != MSGQUEUE_0137C63D_UNIT_INIT) {
		nvkm_error(subdev, "expected message from init unit\n");
		return -EINVAL;
	}

	if (init->base.msg_type != INIT_MSG_INIT) {
		nvkm_error(subdev, "expected PMU init msg\n");
		return -EINVAL;
	}

	for (i = 0; i < MSGQUEUE_0137C63D_NUM_QUEUES; i++) {
		struct nvkm_msgqueue_queue *queue = &priv->queue[i];

		mutex_init(&queue->mutex);

		queue->index = init->queue_info[i].index;
		queue->offset = init->queue_info[i].offset;
		queue->size = init->queue_info[i].size;

		if (i != MSGQUEUE_0137C63D_MESSAGE_QUEUE) {
			queue->head_reg = 0x4a0 + (queue->index * 4);
			queue->tail_reg = 0x4b0 + (queue->index * 4);
		} else {
			queue->head_reg = 0x4c8;
			queue->tail_reg = 0x4cc;
		}

		nvkm_debug(subdev,
			   "queue %d: index %d, offset 0x%08x, size 0x%08x\n",
			   i, queue->index, queue->offset, queue->size);
	}

	/* Complete initialization by initializing WPR region */
	return acr_init_wpr(&priv->base);
}

static const struct nvkm_msgqueue_init_func
msgqueue_0137c63d_init_func = {
	.gen_cmdline = init_gen_cmdline,
	.init_callback = init_callback,
};



/* ACR unit */
#define MSGQUEUE_0137C63D_UNIT_ACR 0x0a

enum {
	ACR_CMD_INIT_WPR_REGION = 0x00,
	ACR_CMD_BOOTSTRAP_FALCON = 0x01,
	ACR_CMD_BOOTSTRAP_MULTIPLE_FALCONS = 0x03,
};

static void
acr_init_wpr_callback(struct nvkm_msgqueue *queue,
		      struct nvkm_msgqueue_hdr *hdr)
{
	struct {
		struct nvkm_msgqueue_msg base;
		u32 error_code;
	} *msg = (void *)hdr;
	const struct nvkm_subdev *subdev = queue->falcon->owner;

	if (msg->error_code) {
		nvkm_error(subdev, "ACR WPR init failure: %d\n",
			   msg->error_code);
		return;
	}

	nvkm_debug(subdev, "ACR WPR init complete\n");
	complete_all(&queue->init_done);
}

static int
acr_init_wpr(struct nvkm_msgqueue *queue)
{
	/*
	 * region_id:	region ID in WPR region
	 * wpr_offset:	offset in WPR region
	 */
	struct {
		struct nvkm_msgqueue_hdr hdr;
		u8 cmd_type;
		u32 region_id;
		u32 wpr_offset;
	} cmd;
	memset(&cmd, 0, sizeof(cmd));

	cmd.hdr.unit_id = MSGQUEUE_0137C63D_UNIT_ACR;
	cmd.hdr.size = sizeof(cmd);
	cmd.cmd_type = ACR_CMD_INIT_WPR_REGION;
	cmd.region_id = 0x01;
	cmd.wpr_offset = 0x00;

	nvkm_msgqueue_post(queue, MSGQUEUE_MSG_PRIORITY_HIGH, &cmd.hdr,
			   acr_init_wpr_callback, NULL, false);

	return 0;
}


static void
acr_boot_falcon_callback(struct nvkm_msgqueue *priv,
			 struct nvkm_msgqueue_hdr *hdr)
{
	struct acr_bootstrap_falcon_msg {
		struct nvkm_msgqueue_msg base;

		u32 falcon_id;
	} *msg = (void *)hdr;
	const struct nvkm_subdev *subdev = priv->falcon->owner;
	u32 falcon_id = msg->falcon_id;

	if (falcon_id >= NVKM_SECBOOT_FALCON_END) {
		nvkm_error(subdev, "in bootstrap falcon callback:\n");
		nvkm_error(subdev, "invalid falcon ID 0x%x\n", falcon_id);
		return;
	}
	nvkm_debug(subdev, "%s booted\n", nvkm_secboot_falcon_name[falcon_id]);
}

enum {
	ACR_CMD_BOOTSTRAP_FALCON_FLAGS_RESET_YES = 0,
	ACR_CMD_BOOTSTRAP_FALCON_FLAGS_RESET_NO = 1,
};

static int
acr_boot_falcon(struct nvkm_msgqueue *priv, enum nvkm_secboot_falcon falcon)
{
	DECLARE_COMPLETION_ONSTACK(completed);
	/*
	 * flags      - Flag specifying RESET or no RESET.
	 * falcon id  - Falcon id specifying falcon to bootstrap.
	 */
	struct {
		struct nvkm_msgqueue_hdr hdr;
		u8 cmd_type;
		u32 flags;
		u32 falcon_id;
	} cmd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.hdr.unit_id = MSGQUEUE_0137C63D_UNIT_ACR;
	cmd.hdr.size = sizeof(cmd);
	cmd.cmd_type = ACR_CMD_BOOTSTRAP_FALCON;
	cmd.flags = ACR_CMD_BOOTSTRAP_FALCON_FLAGS_RESET_YES;
	cmd.falcon_id = falcon;
	nvkm_msgqueue_post(priv, MSGQUEUE_MSG_PRIORITY_HIGH, &cmd.hdr,
			acr_boot_falcon_callback, &completed, true);

	if (!wait_for_completion_timeout(&completed, msecs_to_jiffies(1000)))
		return -ETIMEDOUT;

	return 0;
}

static void
acr_boot_multiple_falcons_callback(struct nvkm_msgqueue *priv,
				   struct nvkm_msgqueue_hdr *hdr)
{
	struct acr_bootstrap_falcon_msg {
		struct nvkm_msgqueue_msg base;

		u32 falcon_mask;
	} *msg = (void *)hdr;
	const struct nvkm_subdev *subdev = priv->falcon->owner;
	unsigned long falcon_mask = msg->falcon_mask;
	u32 falcon_id, falcon_treated = 0;

	for_each_set_bit(falcon_id, &falcon_mask, NVKM_SECBOOT_FALCON_END) {
		nvkm_debug(subdev, "%s booted\n",
			   nvkm_secboot_falcon_name[falcon_id]);
		falcon_treated |= BIT(falcon_id);
	}

	if (falcon_treated != msg->falcon_mask) {
		nvkm_error(subdev, "in bootstrap falcon callback:\n");
		nvkm_error(subdev, "invalid falcon mask 0x%x\n",
			   msg->falcon_mask);
		return;
	}
}

static int
acr_boot_multiple_falcons(struct nvkm_msgqueue *priv, unsigned long falcon_mask)
{
	DECLARE_COMPLETION_ONSTACK(completed);
	/*
	 * flags      - Flag specifying RESET or no RESET.
	 * falcon id  - Falcon id specifying falcon to bootstrap.
	 */
	struct {
		struct nvkm_msgqueue_hdr hdr;
		u8 cmd_type;
		u32 flags;
		u32 falcon_mask;
		u32 use_va_mask;
		u32 wpr_lo;
		u32 wpr_hi;
	} cmd;
	struct msgqueue_0137bca5 *queue = msgqueue_0137bca5(priv);

	memset(&cmd, 0, sizeof(cmd));

	cmd.hdr.unit_id = MSGQUEUE_0137C63D_UNIT_ACR;
	cmd.hdr.size = sizeof(cmd);
	cmd.cmd_type = ACR_CMD_BOOTSTRAP_MULTIPLE_FALCONS;
	cmd.flags = ACR_CMD_BOOTSTRAP_FALCON_FLAGS_RESET_YES;
	cmd.falcon_mask = falcon_mask;
	cmd.wpr_lo = lower_32_bits(queue->wpr_addr);
	cmd.wpr_hi = upper_32_bits(queue->wpr_addr);
	nvkm_msgqueue_post(priv, MSGQUEUE_MSG_PRIORITY_HIGH, &cmd.hdr,
			acr_boot_multiple_falcons_callback, &completed, true);

	if (!wait_for_completion_timeout(&completed, msecs_to_jiffies(1000)))
		return -ETIMEDOUT;

	return 0;
}

static const struct nvkm_msgqueue_acr_func
msgqueue_0137c63d_acr_func = {
	.boot_falcon = acr_boot_falcon,
};

static const struct nvkm_msgqueue_acr_func
msgqueue_0137bca5_acr_func = {
	.boot_falcon = acr_boot_falcon,
	.boot_multiple_falcons = acr_boot_multiple_falcons,
};

static void
msgqueue_0137c63d_dtor(struct nvkm_msgqueue *queue)
{
	kfree(msgqueue_0137c63d(queue));
}

static const struct nvkm_msgqueue_func
msgqueue_0137c63d_func = {
	.init_func = &msgqueue_0137c63d_init_func,
	.acr_func = &msgqueue_0137c63d_acr_func,
	.cmd_queue = msgqueue_0137c63d_cmd_queue,
	.recv = msgqueue_0137c63d_process_msgs,
	.dtor = msgqueue_0137c63d_dtor,
};

int
msgqueue_0137c63d_new(struct nvkm_falcon *falcon, const struct nvkm_secboot *sb,
		      struct nvkm_msgqueue **queue)
{
	struct msgqueue_0137c63d *ret;

	ret = kzalloc(sizeof(*ret), GFP_KERNEL);
	if (!ret)
		return -ENOMEM;

	*queue = &ret->base;

	nvkm_msgqueue_ctor(&msgqueue_0137c63d_func, falcon, &ret->base);

	return 0;
}

static const struct nvkm_msgqueue_func
msgqueue_0137bca5_func = {
	.init_func = &msgqueue_0137c63d_init_func,
	.acr_func = &msgqueue_0137bca5_acr_func,
	.cmd_queue = msgqueue_0137c63d_cmd_queue,
	.recv = msgqueue_0137c63d_process_msgs,
	.dtor = msgqueue_0137c63d_dtor,
};

int
msgqueue_0137bca5_new(struct nvkm_falcon *falcon, const struct nvkm_secboot *sb,
		      struct nvkm_msgqueue **queue)
{
	struct msgqueue_0137bca5 *ret;

	ret = kzalloc(sizeof(*ret), GFP_KERNEL);
	if (!ret)
		return -ENOMEM;

	*queue = &ret->base.base;

	/*
	 * FIXME this must be set to the address of a *GPU* mapping within the
	 * ACR address space!
	 */
	/* ret->wpr_addr = sb->wpr_addr; */

	nvkm_msgqueue_ctor(&msgqueue_0137bca5_func, falcon, &ret->base.base);

	return 0;
}
