// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-mem2mem.h>

#include "iris_buffer.h"
#include "iris_instance.h"
#include "iris_power.h"
#include "iris_resources.h"
#include "iris_vpu_common.h"

static u32 iris_calc_bw(struct iris_inst *inst, struct icc_vote_data *data)
{
	const struct bw_info *bw_tbl = NULL;
	struct iris_core *core = inst->core;
	u32 num_rows, i, mbs, mbps;
	u32 icc_bw = 0;

	mbs = DIV_ROUND_UP(data->height, 16) * DIV_ROUND_UP(data->width, 16);
	mbps = mbs * data->fps;
	if (mbps == 0)
		goto exit;

	bw_tbl = core->iris_platform_data->bw_tbl_dec;
	num_rows = core->iris_platform_data->bw_tbl_dec_size;

	for (i = 0; i < num_rows; i++) {
		if (i != 0 && mbps > bw_tbl[i].mbs_per_sec)
			break;

		icc_bw = bw_tbl[i].bw_ddr;
	}

exit:
	return icc_bw;
}

static int iris_set_interconnects(struct iris_inst *inst)
{
	struct iris_core *core = inst->core;
	struct iris_inst *instance;
	u64 total_bw_ddr = 0;
	int ret;

	mutex_lock(&core->lock);
	list_for_each_entry(instance, &core->instances, list) {
		if (!instance->max_input_data_size)
			continue;

		total_bw_ddr += instance->power.icc_bw;
	}

	ret = iris_set_icc_bw(core, total_bw_ddr);

	mutex_unlock(&core->lock);

	return ret;
}

static int iris_vote_interconnects(struct iris_inst *inst)
{
	struct icc_vote_data *vote_data = &inst->icc_data;
	struct v4l2_format *inp_f = inst->fmt_src;

	vote_data->width = inp_f->fmt.pix_mp.width;
	vote_data->height = inp_f->fmt.pix_mp.height;
	vote_data->fps = DEFAULT_FPS;

	inst->power.icc_bw = iris_calc_bw(inst, vote_data);

	return iris_set_interconnects(inst);
}

static int iris_set_clocks(struct iris_inst *inst)
{
	struct iris_core *core = inst->core;
	struct iris_inst *instance;
	u64 freq = 0;
	int ret;

	mutex_lock(&core->lock);
	list_for_each_entry(instance, &core->instances, list) {
		if (!instance->max_input_data_size)
			continue;

		freq += instance->power.min_freq;
	}

	core->power.clk_freq = freq;
	ret = dev_pm_opp_set_rate(core->dev, freq);
	mutex_unlock(&core->lock);

	return ret;
}

static int iris_scale_clocks(struct iris_inst *inst)
{
	const struct vpu_ops *vpu_ops = inst->core->iris_platform_data->vpu_ops;
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_m2m_buffer *buffer, *n;
	struct iris_buffer *buf;
	size_t data_size = 0;

	v4l2_m2m_for_each_src_buf_safe(m2m_ctx, buffer, n) {
		buf = to_iris_buffer(&buffer->vb);
		data_size = max(data_size, buf->data_size);
	}

	inst->max_input_data_size = data_size;
	if (!inst->max_input_data_size)
		return 0;

	inst->power.min_freq = vpu_ops->calc_freq(inst, inst->max_input_data_size);

	return iris_set_clocks(inst);
}

int iris_scale_power(struct iris_inst *inst)
{
	struct iris_core *core = inst->core;
	int ret;

	if (pm_runtime_suspended(core->dev)) {
		ret = pm_runtime_resume_and_get(core->dev);
		if (ret < 0)
			return ret;

		pm_runtime_put_autosuspend(core->dev);
	}

	ret = iris_scale_clocks(inst);
	if (ret)
		return ret;

	return iris_vote_interconnects(inst);
}
