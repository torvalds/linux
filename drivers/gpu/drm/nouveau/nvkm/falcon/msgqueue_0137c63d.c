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
#include <subdev/pmu.h>
#include <subdev/secboot.h>

/* Queues identifiers */
enum {
	MSGQUEUE_0137C63D_NUM_QUEUES = 5,
};

struct msgqueue_0137c63d {
	struct nvkm_msgqueue base;
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

static void
msgqueue_0137c63d_process_msgs(struct nvkm_msgqueue *queue)
{
	nvkm_msgqueue_process_msgs(queue, queue->falcon->owner->device->pmu->msgq);
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
	struct nvkm_pmu *pmu = subdev->device->pmu;

	if (init->base.hdr.unit_id != MSGQUEUE_0137C63D_UNIT_INIT) {
		nvkm_error(subdev, "expected message from init unit\n");
		return -EINVAL;
	}

	if (init->base.msg_type != INIT_MSG_INIT) {
		nvkm_error(subdev, "expected PMU init msg\n");
		return -EINVAL;
	}

	nvkm_falcon_cmdq_init(pmu->hpq, init->queue_info[0].index,
					init->queue_info[0].offset,
					init->queue_info[0].size);
	nvkm_falcon_cmdq_init(pmu->lpq, init->queue_info[1].index,
					init->queue_info[1].offset,
					init->queue_info[1].size);
	nvkm_falcon_msgq_init(pmu->msgq, init->queue_info[4].index,
					 init->queue_info[4].offset,
					 init->queue_info[4].size);

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

static int
acr_init_wpr_callback(void *priv, struct nv_falcon_msg *hdr)
{
	struct nvkm_pmu *pmu = priv;
	struct nvkm_subdev *subdev = &pmu->subdev;
	struct {
		struct nv_falcon_msg base;
		u8 msg_type;
		u32 error_code;
	} *msg = (void *)hdr;

	if (msg->error_code) {
		nvkm_error(subdev, "ACR WPR init failure: %d\n",
			   msg->error_code);
		return -EINVAL;
	}

	nvkm_debug(subdev, "ACR WPR init complete\n");
	complete_all(&pmu->wpr_ready);
	return 0;
}

static int
acr_init_wpr(struct nvkm_msgqueue *queue)
{
	struct nvkm_pmu *pmu = queue->falcon->owner->device->pmu;
	/*
	 * region_id:	region ID in WPR region
	 * wpr_offset:	offset in WPR region
	 */
	struct {
		struct nv_falcon_cmd hdr;
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
	return nvkm_falcon_cmdq_send(pmu->hpq, &cmd.hdr, acr_init_wpr_callback,
				     pmu, 0);
}


static int
acr_boot_falcon_callback(void *priv, struct nv_falcon_msg *hdr)
{
	struct acr_bootstrap_falcon_msg {
		struct nv_falcon_msg base;
		u8 msg_type;
		u32 falcon_id;
	} *msg = (void *)hdr;
	struct nvkm_subdev *subdev = priv;
	u32 falcon_id = msg->falcon_id;

	if (falcon_id >= NVKM_SECBOOT_FALCON_END) {
		nvkm_error(subdev, "in bootstrap falcon callback:\n");
		nvkm_error(subdev, "invalid falcon ID 0x%x\n", falcon_id);
		return -EINVAL;
	}

	nvkm_debug(subdev, "%s booted\n", nvkm_secboot_falcon_name[falcon_id]);
	return 0;
}

enum {
	ACR_CMD_BOOTSTRAP_FALCON_FLAGS_RESET_YES = 0,
	ACR_CMD_BOOTSTRAP_FALCON_FLAGS_RESET_NO = 1,
};

static int
acr_boot_falcon(struct nvkm_msgqueue *priv, enum nvkm_secboot_falcon falcon)
{
	struct nvkm_pmu *pmu = priv->falcon->owner->device->pmu;
	/*
	 * flags      - Flag specifying RESET or no RESET.
	 * falcon id  - Falcon id specifying falcon to bootstrap.
	 */
	struct {
		struct nv_falcon_cmd hdr;
		u8 cmd_type;
		u32 flags;
		u32 falcon_id;
	} cmd;

	if (!wait_for_completion_timeout(&pmu->wpr_ready,
					 msecs_to_jiffies(1000))) {
		nvkm_error(&pmu->subdev, "timeout waiting for WPR init\n");
		return -ETIMEDOUT;
	}

	memset(&cmd, 0, sizeof(cmd));

	cmd.hdr.unit_id = MSGQUEUE_0137C63D_UNIT_ACR;
	cmd.hdr.size = sizeof(cmd);
	cmd.cmd_type = ACR_CMD_BOOTSTRAP_FALCON;
	cmd.flags = ACR_CMD_BOOTSTRAP_FALCON_FLAGS_RESET_YES;
	cmd.falcon_id = falcon;
	return nvkm_falcon_cmdq_send(pmu->hpq, &cmd.hdr,
				     acr_boot_falcon_callback, &pmu->subdev,
				     msecs_to_jiffies(1000));
}

static int
acr_boot_multiple_falcons_callback(void *priv, struct nv_falcon_msg *hdr)
{
	struct acr_bootstrap_falcon_msg {
		struct nv_falcon_msg base;
		u8 msg_type;
		u32 falcon_mask;
	} *msg = (void *)hdr;
	const struct nvkm_subdev *subdev = priv;
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
		return -EINVAL;
	}

	return 0;
}

static int
acr_boot_multiple_falcons(struct nvkm_msgqueue *priv, unsigned long falcon_mask)
{
	struct nvkm_pmu *pmu = priv->falcon->owner->device->pmu;
	/*
	 * flags      - Flag specifying RESET or no RESET.
	 * falcon id  - Falcon id specifying falcon to bootstrap.
	 */
	struct {
		struct nv_falcon_cmd hdr;
		u8 cmd_type;
		u32 flags;
		u32 falcon_mask;
		u32 use_va_mask;
		u32 wpr_lo;
		u32 wpr_hi;
	} cmd;
	struct msgqueue_0137bca5 *queue = msgqueue_0137bca5(priv);

	if (!wait_for_completion_timeout(&pmu->wpr_ready,
					 msecs_to_jiffies(1000))) {
		nvkm_error(&pmu->subdev, "timeout waiting for WPR init\n");
		return -ETIMEDOUT;
	}

	memset(&cmd, 0, sizeof(cmd));

	cmd.hdr.unit_id = MSGQUEUE_0137C63D_UNIT_ACR;
	cmd.hdr.size = sizeof(cmd);
	cmd.cmd_type = ACR_CMD_BOOTSTRAP_MULTIPLE_FALCONS;
	cmd.flags = ACR_CMD_BOOTSTRAP_FALCON_FLAGS_RESET_YES;
	cmd.falcon_mask = falcon_mask;
	cmd.wpr_lo = lower_32_bits(queue->wpr_addr);
	cmd.wpr_hi = upper_32_bits(queue->wpr_addr);
	return nvkm_falcon_cmdq_send(pmu->hpq, &cmd.hdr,
				     acr_boot_multiple_falcons_callback,
				     &pmu->subdev, msecs_to_jiffies(1000));
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
