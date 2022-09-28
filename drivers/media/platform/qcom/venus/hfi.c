// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>

#include "core.h"
#include "hfi.h"
#include "hfi_cmds.h"
#include "hfi_venus.h"

#define TIMEOUT		msecs_to_jiffies(1000)

static u32 to_codec_type(u32 pixfmt)
{
	switch (pixfmt) {
	case V4L2_PIX_FMT_H264:
	case V4L2_PIX_FMT_H264_NO_SC:
		return HFI_VIDEO_CODEC_H264;
	case V4L2_PIX_FMT_H263:
		return HFI_VIDEO_CODEC_H263;
	case V4L2_PIX_FMT_MPEG1:
		return HFI_VIDEO_CODEC_MPEG1;
	case V4L2_PIX_FMT_MPEG2:
		return HFI_VIDEO_CODEC_MPEG2;
	case V4L2_PIX_FMT_MPEG4:
		return HFI_VIDEO_CODEC_MPEG4;
	case V4L2_PIX_FMT_VC1_ANNEX_G:
	case V4L2_PIX_FMT_VC1_ANNEX_L:
		return HFI_VIDEO_CODEC_VC1;
	case V4L2_PIX_FMT_VP8:
		return HFI_VIDEO_CODEC_VP8;
	case V4L2_PIX_FMT_VP9:
		return HFI_VIDEO_CODEC_VP9;
	case V4L2_PIX_FMT_XVID:
		return HFI_VIDEO_CODEC_DIVX;
	case V4L2_PIX_FMT_HEVC:
		return HFI_VIDEO_CODEC_HEVC;
	default:
		return 0;
	}
}

int hfi_core_init(struct venus_core *core)
{
	int ret = 0;

	mutex_lock(&core->lock);

	if (core->state >= CORE_INIT)
		goto unlock;

	reinit_completion(&core->done);

	ret = core->ops->core_init(core);
	if (ret)
		goto unlock;

	ret = wait_for_completion_timeout(&core->done, TIMEOUT);
	if (!ret) {
		ret = -ETIMEDOUT;
		goto unlock;
	}

	ret = 0;

	if (core->error != HFI_ERR_NONE) {
		ret = -EIO;
		goto unlock;
	}

	core->state = CORE_INIT;
unlock:
	mutex_unlock(&core->lock);
	return ret;
}

int hfi_core_deinit(struct venus_core *core, bool blocking)
{
	int ret = 0, empty;

	mutex_lock(&core->lock);

	if (core->state == CORE_UNINIT)
		goto unlock;

	empty = list_empty(&core->instances);

	if (!empty && !blocking) {
		ret = -EBUSY;
		goto unlock;
	}

	if (!empty) {
		mutex_unlock(&core->lock);
		wait_var_event(&core->insts_count,
			       !atomic_read(&core->insts_count));
		mutex_lock(&core->lock);
	}

	if (!core->ops)
		goto unlock;

	ret = core->ops->core_deinit(core);

	if (!ret)
		core->state = CORE_UNINIT;

unlock:
	mutex_unlock(&core->lock);
	return ret;
}

int hfi_core_suspend(struct venus_core *core)
{
	if (core->state != CORE_INIT)
		return 0;

	return core->ops->suspend(core);
}

int hfi_core_resume(struct venus_core *core, bool force)
{
	if (!force && core->state != CORE_INIT)
		return 0;

	return core->ops->resume(core);
}

int hfi_core_trigger_ssr(struct venus_core *core, u32 type)
{
	return core->ops->core_trigger_ssr(core, type);
}

int hfi_core_ping(struct venus_core *core)
{
	int ret;

	mutex_lock(&core->lock);

	ret = core->ops->core_ping(core, 0xbeef);
	if (ret)
		goto unlock;

	ret = wait_for_completion_timeout(&core->done, TIMEOUT);
	if (!ret) {
		ret = -ETIMEDOUT;
		goto unlock;
	}
	ret = 0;
	if (core->error != HFI_ERR_NONE)
		ret = -ENODEV;
unlock:
	mutex_unlock(&core->lock);
	return ret;
}

static int wait_session_msg(struct venus_inst *inst)
{
	int ret;

	ret = wait_for_completion_timeout(&inst->done, TIMEOUT);
	if (!ret)
		return -ETIMEDOUT;

	if (inst->error != HFI_ERR_NONE)
		return -EIO;

	return 0;
}

int hfi_session_create(struct venus_inst *inst, const struct hfi_inst_ops *ops)
{
	struct venus_core *core = inst->core;

	if (!ops)
		return -EINVAL;

	inst->state = INST_UNINIT;
	init_completion(&inst->done);
	inst->ops = ops;

	mutex_lock(&core->lock);
	list_add_tail(&inst->list, &core->instances);
	atomic_inc(&core->insts_count);
	mutex_unlock(&core->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(hfi_session_create);

int hfi_session_init(struct venus_inst *inst, u32 pixfmt)
{
	struct venus_core *core = inst->core;
	const struct hfi_ops *ops = core->ops;
	int ret;

	if (inst->state != INST_UNINIT)
		return -EINVAL;

	inst->hfi_codec = to_codec_type(pixfmt);
	reinit_completion(&inst->done);

	ret = ops->session_init(inst, inst->session_type, inst->hfi_codec);
	if (ret)
		return ret;

	ret = wait_session_msg(inst);
	if (ret)
		return ret;

	inst->state = INST_INIT;

	return 0;
}
EXPORT_SYMBOL_GPL(hfi_session_init);

void hfi_session_destroy(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;

	mutex_lock(&core->lock);
	list_del_init(&inst->list);
	if (atomic_dec_and_test(&core->insts_count))
		wake_up_var(&core->insts_count);
	mutex_unlock(&core->lock);
}
EXPORT_SYMBOL_GPL(hfi_session_destroy);

int hfi_session_deinit(struct venus_inst *inst)
{
	const struct hfi_ops *ops = inst->core->ops;
	int ret;

	if (inst->state == INST_UNINIT)
		return 0;

	if (inst->state < INST_INIT)
		return -EINVAL;

	reinit_completion(&inst->done);

	ret = ops->session_end(inst);
	if (ret)
		return ret;

	ret = wait_session_msg(inst);
	if (ret)
		return ret;

	inst->state = INST_UNINIT;

	return 0;
}
EXPORT_SYMBOL_GPL(hfi_session_deinit);

int hfi_session_start(struct venus_inst *inst)
{
	const struct hfi_ops *ops = inst->core->ops;
	int ret;

	if (inst->state != INST_LOAD_RESOURCES)
		return -EINVAL;

	reinit_completion(&inst->done);

	ret = ops->session_start(inst);
	if (ret)
		return ret;

	ret = wait_session_msg(inst);
	if (ret)
		return ret;

	inst->state = INST_START;

	return 0;
}
EXPORT_SYMBOL_GPL(hfi_session_start);

int hfi_session_stop(struct venus_inst *inst)
{
	const struct hfi_ops *ops = inst->core->ops;
	int ret;

	if (inst->state != INST_START)
		return -EINVAL;

	reinit_completion(&inst->done);

	ret = ops->session_stop(inst);
	if (ret)
		return ret;

	ret = wait_session_msg(inst);
	if (ret)
		return ret;

	inst->state = INST_STOP;

	return 0;
}
EXPORT_SYMBOL_GPL(hfi_session_stop);

int hfi_session_continue(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;

	if (core->res->hfi_version == HFI_VERSION_1XX)
		return 0;

	return core->ops->session_continue(inst);
}
EXPORT_SYMBOL_GPL(hfi_session_continue);

int hfi_session_abort(struct venus_inst *inst)
{
	const struct hfi_ops *ops = inst->core->ops;
	int ret;

	reinit_completion(&inst->done);

	ret = ops->session_abort(inst);
	if (ret)
		return ret;

	ret = wait_session_msg(inst);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(hfi_session_abort);

int hfi_session_load_res(struct venus_inst *inst)
{
	const struct hfi_ops *ops = inst->core->ops;
	int ret;

	if (inst->state != INST_INIT)
		return -EINVAL;

	reinit_completion(&inst->done);

	ret = ops->session_load_res(inst);
	if (ret)
		return ret;

	ret = wait_session_msg(inst);
	if (ret)
		return ret;

	inst->state = INST_LOAD_RESOURCES;

	return 0;
}

int hfi_session_unload_res(struct venus_inst *inst)
{
	const struct hfi_ops *ops = inst->core->ops;
	int ret;

	if (inst->state != INST_STOP)
		return -EINVAL;

	reinit_completion(&inst->done);

	ret = ops->session_release_res(inst);
	if (ret)
		return ret;

	ret = wait_session_msg(inst);
	if (ret)
		return ret;

	inst->state = INST_RELEASE_RESOURCES;

	return 0;
}
EXPORT_SYMBOL_GPL(hfi_session_unload_res);

int hfi_session_flush(struct venus_inst *inst, u32 type, bool block)
{
	const struct hfi_ops *ops = inst->core->ops;
	int ret;

	reinit_completion(&inst->done);

	ret = ops->session_flush(inst, type);
	if (ret)
		return ret;

	if (block) {
		ret = wait_session_msg(inst);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(hfi_session_flush);

int hfi_session_set_buffers(struct venus_inst *inst, struct hfi_buffer_desc *bd)
{
	const struct hfi_ops *ops = inst->core->ops;

	return ops->session_set_buffers(inst, bd);
}

int hfi_session_unset_buffers(struct venus_inst *inst,
			      struct hfi_buffer_desc *bd)
{
	const struct hfi_ops *ops = inst->core->ops;
	int ret;

	reinit_completion(&inst->done);

	ret = ops->session_unset_buffers(inst, bd);
	if (ret)
		return ret;

	if (!bd->response_required)
		return 0;

	ret = wait_session_msg(inst);
	if (ret)
		return ret;

	return 0;
}

int hfi_session_get_property(struct venus_inst *inst, u32 ptype,
			     union hfi_get_property *hprop)
{
	const struct hfi_ops *ops = inst->core->ops;
	int ret;

	if (inst->state < INST_INIT || inst->state >= INST_STOP)
		return -EINVAL;

	reinit_completion(&inst->done);

	ret = ops->session_get_property(inst, ptype);
	if (ret)
		return ret;

	ret = wait_session_msg(inst);
	if (ret)
		return ret;

	*hprop = inst->hprop;

	return 0;
}
EXPORT_SYMBOL_GPL(hfi_session_get_property);

int hfi_session_set_property(struct venus_inst *inst, u32 ptype, void *pdata)
{
	const struct hfi_ops *ops = inst->core->ops;

	if (inst->state < INST_INIT || inst->state >= INST_STOP)
		return -EINVAL;

	return ops->session_set_property(inst, ptype, pdata);
}
EXPORT_SYMBOL_GPL(hfi_session_set_property);

int hfi_session_process_buf(struct venus_inst *inst, struct hfi_frame_data *fd)
{
	const struct hfi_ops *ops = inst->core->ops;

	if (fd->buffer_type == HFI_BUFFER_INPUT)
		return ops->session_etb(inst, fd);
	else if (fd->buffer_type == HFI_BUFFER_OUTPUT ||
		 fd->buffer_type == HFI_BUFFER_OUTPUT2)
		return ops->session_ftb(inst, fd);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(hfi_session_process_buf);

irqreturn_t hfi_isr_thread(int irq, void *dev_id)
{
	struct venus_core *core = dev_id;

	return core->ops->isr_thread(core);
}

irqreturn_t hfi_isr(int irq, void *dev)
{
	struct venus_core *core = dev;

	return core->ops->isr(core);
}

int hfi_create(struct venus_core *core, const struct hfi_core_ops *ops)
{
	int ret;

	if (!ops)
		return -EINVAL;

	atomic_set(&core->insts_count, 0);
	core->core_ops = ops;
	core->state = CORE_UNINIT;
	init_completion(&core->done);
	pkt_set_version(core->res->hfi_version);
	ret = venus_hfi_create(core);

	return ret;
}

void hfi_destroy(struct venus_core *core)
{
	venus_hfi_destroy(core);
}

void hfi_reinit(struct venus_core *core)
{
	venus_hfi_queues_reinit(core);
}
