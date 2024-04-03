// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Linaro Ltd.
 *
 * Author: Stanimir Varbanov <stanimir.varbanov@linaro.org>
 */
#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/types.h>
#include <media/v4l2-mem2mem.h>

#include "core.h"
#include "hfi_parser.h"
#include "hfi_venus_io.h"
#include "pm_helpers.h"
#include "hfi_platform.h"

static bool legacy_binding;

static int core_clks_get(struct venus_core *core)
{
	const struct venus_resources *res = core->res;
	struct device *dev = core->dev;
	unsigned int i;

	for (i = 0; i < res->clks_num; i++) {
		core->clks[i] = devm_clk_get(dev, res->clks[i]);
		if (IS_ERR(core->clks[i]))
			return PTR_ERR(core->clks[i]);
	}

	return 0;
}

static int core_clks_enable(struct venus_core *core)
{
	const struct venus_resources *res = core->res;
	const struct freq_tbl *freq_tbl = core->res->freq_tbl;
	unsigned int freq_tbl_size = core->res->freq_tbl_size;
	unsigned long freq;
	unsigned int i;
	int ret;

	if (!freq_tbl)
		return -EINVAL;

	freq = freq_tbl[freq_tbl_size - 1].freq;

	for (i = 0; i < res->clks_num; i++) {
		if (IS_V6(core)) {
			ret = clk_set_rate(core->clks[i], freq);
			if (ret)
				goto err;
		}

		ret = clk_prepare_enable(core->clks[i]);
		if (ret)
			goto err;
	}

	return 0;
err:
	while (i--)
		clk_disable_unprepare(core->clks[i]);

	return ret;
}

static void core_clks_disable(struct venus_core *core)
{
	const struct venus_resources *res = core->res;
	unsigned int i = res->clks_num;

	while (i--)
		clk_disable_unprepare(core->clks[i]);
}

static int core_clks_set_rate(struct venus_core *core, unsigned long freq)
{
	int ret;

	ret = dev_pm_opp_set_rate(core->dev, freq);
	if (ret)
		return ret;

	ret = clk_set_rate(core->vcodec0_clks[0], freq);
	if (ret)
		return ret;

	ret = clk_set_rate(core->vcodec1_clks[0], freq);
	if (ret)
		return ret;

	return 0;
}

static int vcodec_clks_get(struct venus_core *core, struct device *dev,
			   struct clk **clks, const char * const *id)
{
	const struct venus_resources *res = core->res;
	unsigned int i;

	for (i = 0; i < res->vcodec_clks_num; i++) {
		if (!id[i])
			continue;
		clks[i] = devm_clk_get(dev, id[i]);
		if (IS_ERR(clks[i]))
			return PTR_ERR(clks[i]);
	}

	return 0;
}

static int vcodec_clks_enable(struct venus_core *core, struct clk **clks)
{
	const struct venus_resources *res = core->res;
	unsigned int i;
	int ret;

	for (i = 0; i < res->vcodec_clks_num; i++) {
		ret = clk_prepare_enable(clks[i]);
		if (ret)
			goto err;
	}

	return 0;
err:
	while (i--)
		clk_disable_unprepare(clks[i]);

	return ret;
}

static void vcodec_clks_disable(struct venus_core *core, struct clk **clks)
{
	const struct venus_resources *res = core->res;
	unsigned int i = res->vcodec_clks_num;

	while (i--)
		clk_disable_unprepare(clks[i]);
}

static u32 load_per_instance(struct venus_inst *inst)
{
	u32 mbs;

	if (!inst || !(inst->state >= INST_INIT && inst->state < INST_STOP))
		return 0;

	mbs = (ALIGN(inst->width, 16) / 16) * (ALIGN(inst->height, 16) / 16);

	return mbs * inst->fps;
}

static u32 load_per_type(struct venus_core *core, u32 session_type)
{
	struct venus_inst *inst = NULL;
	u32 mbs_per_sec = 0;

	list_for_each_entry(inst, &core->instances, list) {
		if (inst->session_type != session_type)
			continue;

		mbs_per_sec += load_per_instance(inst);
	}

	return mbs_per_sec;
}

static void mbs_to_bw(struct venus_inst *inst, u32 mbs, u32 *avg, u32 *peak)
{
	const struct venus_resources *res = inst->core->res;
	const struct bw_tbl *bw_tbl;
	unsigned int num_rows, i;

	*avg = 0;
	*peak = 0;

	if (mbs == 0)
		return;

	if (inst->session_type == VIDC_SESSION_TYPE_ENC) {
		num_rows = res->bw_tbl_enc_size;
		bw_tbl = res->bw_tbl_enc;
	} else if (inst->session_type == VIDC_SESSION_TYPE_DEC) {
		num_rows = res->bw_tbl_dec_size;
		bw_tbl = res->bw_tbl_dec;
	} else {
		return;
	}

	if (!bw_tbl || num_rows == 0)
		return;

	for (i = 0; i < num_rows; i++) {
		if (i != 0 && mbs > bw_tbl[i].mbs_per_sec)
			break;

		if (inst->dpb_fmt & HFI_COLOR_FORMAT_10_BIT_BASE) {
			*avg = bw_tbl[i].avg_10bit;
			*peak = bw_tbl[i].peak_10bit;
		} else {
			*avg = bw_tbl[i].avg;
			*peak = bw_tbl[i].peak;
		}
	}
}

static int load_scale_bw(struct venus_core *core)
{
	struct venus_inst *inst = NULL;
	u32 mbs_per_sec, avg, peak, total_avg = 0, total_peak = 0;

	list_for_each_entry(inst, &core->instances, list) {
		mbs_per_sec = load_per_instance(inst);
		mbs_to_bw(inst, mbs_per_sec, &avg, &peak);
		total_avg += avg;
		total_peak += peak;
	}

	/*
	 * keep minimum bandwidth vote for "video-mem" path,
	 * so that clks can be disabled during vdec_session_release().
	 * Actual bandwidth drop will be done during device supend
	 * so that device can power down without any warnings.
	 */

	if (!total_avg && !total_peak)
		total_avg = kbps_to_icc(1000);

	dev_dbg(core->dev, VDBGL "total: avg_bw: %u, peak_bw: %u\n",
		total_avg, total_peak);

	return icc_set_bw(core->video_path, total_avg, total_peak);
}

static int load_scale_v1(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	const struct freq_tbl *table = core->res->freq_tbl;
	unsigned int num_rows = core->res->freq_tbl_size;
	unsigned long freq = table[0].freq;
	struct device *dev = core->dev;
	u32 mbs_per_sec;
	unsigned int i;
	int ret = 0;

	mutex_lock(&core->lock);
	mbs_per_sec = load_per_type(core, VIDC_SESSION_TYPE_ENC) +
		      load_per_type(core, VIDC_SESSION_TYPE_DEC);

	if (mbs_per_sec > core->res->max_load)
		dev_warn(dev, "HW is overloaded, needed: %d max: %d\n",
			 mbs_per_sec, core->res->max_load);

	if (!mbs_per_sec && num_rows > 1) {
		freq = table[num_rows - 1].freq;
		goto set_freq;
	}

	for (i = 0; i < num_rows; i++) {
		if (mbs_per_sec > table[i].load)
			break;
		freq = table[i].freq;
	}

set_freq:

	ret = core_clks_set_rate(core, freq);
	if (ret) {
		dev_err(dev, "failed to set clock rate %lu (%d)\n",
			freq, ret);
		goto exit;
	}

	ret = load_scale_bw(core);
	if (ret) {
		dev_err(dev, "failed to set bandwidth (%d)\n",
			ret);
		goto exit;
	}

exit:
	mutex_unlock(&core->lock);
	return ret;
}

static int core_get_v1(struct venus_core *core)
{
	int ret;

	ret = core_clks_get(core);
	if (ret)
		return ret;

	ret = devm_pm_opp_set_clkname(core->dev, "core");
	if (ret)
		return ret;

	return 0;
}

static void core_put_v1(struct venus_core *core)
{
}

static int core_power_v1(struct venus_core *core, int on)
{
	int ret = 0;

	if (on == POWER_ON)
		ret = core_clks_enable(core);
	else
		core_clks_disable(core);

	return ret;
}

static const struct venus_pm_ops pm_ops_v1 = {
	.core_get = core_get_v1,
	.core_put = core_put_v1,
	.core_power = core_power_v1,
	.load_scale = load_scale_v1,
};

static void
vcodec_control_v3(struct venus_core *core, u32 session_type, bool enable)
{
	void __iomem *ctrl;

	if (session_type == VIDC_SESSION_TYPE_DEC)
		ctrl = core->wrapper_base + WRAPPER_VDEC_VCODEC_POWER_CONTROL;
	else
		ctrl = core->wrapper_base + WRAPPER_VENC_VCODEC_POWER_CONTROL;

	if (enable)
		writel(0, ctrl);
	else
		writel(1, ctrl);
}

static int vdec_get_v3(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);

	return vcodec_clks_get(core, dev, core->vcodec0_clks,
			       core->res->vcodec0_clks);
}

static int vdec_power_v3(struct device *dev, int on)
{
	struct venus_core *core = dev_get_drvdata(dev);
	int ret = 0;

	vcodec_control_v3(core, VIDC_SESSION_TYPE_DEC, true);

	if (on == POWER_ON)
		ret = vcodec_clks_enable(core, core->vcodec0_clks);
	else
		vcodec_clks_disable(core, core->vcodec0_clks);

	vcodec_control_v3(core, VIDC_SESSION_TYPE_DEC, false);

	return ret;
}

static int venc_get_v3(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);

	return vcodec_clks_get(core, dev, core->vcodec1_clks,
			       core->res->vcodec1_clks);
}

static int venc_power_v3(struct device *dev, int on)
{
	struct venus_core *core = dev_get_drvdata(dev);
	int ret = 0;

	vcodec_control_v3(core, VIDC_SESSION_TYPE_ENC, true);

	if (on == POWER_ON)
		ret = vcodec_clks_enable(core, core->vcodec1_clks);
	else
		vcodec_clks_disable(core, core->vcodec1_clks);

	vcodec_control_v3(core, VIDC_SESSION_TYPE_ENC, false);

	return ret;
}

static const struct venus_pm_ops pm_ops_v3 = {
	.core_get = core_get_v1,
	.core_put = core_put_v1,
	.core_power = core_power_v1,
	.vdec_get = vdec_get_v3,
	.vdec_power = vdec_power_v3,
	.venc_get = venc_get_v3,
	.venc_power = venc_power_v3,
	.load_scale = load_scale_v1,
};

static int vcodec_control_v4(struct venus_core *core, u32 coreid, bool enable)
{
	void __iomem *ctrl, *stat;
	u32 val;
	int ret;

	if (IS_V6(core)) {
		ctrl = core->wrapper_base + WRAPPER_CORE_POWER_CONTROL_V6;
		stat = core->wrapper_base + WRAPPER_CORE_POWER_STATUS_V6;
	} else if (coreid == VIDC_CORE_ID_1) {
		ctrl = core->wrapper_base + WRAPPER_VCODEC0_MMCC_POWER_CONTROL;
		stat = core->wrapper_base + WRAPPER_VCODEC0_MMCC_POWER_STATUS;
	} else {
		ctrl = core->wrapper_base + WRAPPER_VCODEC1_MMCC_POWER_CONTROL;
		stat = core->wrapper_base + WRAPPER_VCODEC1_MMCC_POWER_STATUS;
	}

	if (enable) {
		writel(0, ctrl);

		ret = readl_poll_timeout(stat, val, val & BIT(1), 1, 100);
		if (ret)
			return ret;
	} else {
		writel(1, ctrl);

		ret = readl_poll_timeout(stat, val, !(val & BIT(1)), 1, 100);
		if (ret)
			return ret;
	}

	return 0;
}

static int poweroff_coreid(struct venus_core *core, unsigned int coreid_mask)
{
	int ret;

	if (coreid_mask & VIDC_CORE_ID_1) {
		ret = vcodec_control_v4(core, VIDC_CORE_ID_1, true);
		if (ret)
			return ret;

		vcodec_clks_disable(core, core->vcodec0_clks);

		ret = vcodec_control_v4(core, VIDC_CORE_ID_1, false);
		if (ret)
			return ret;

		ret = pm_runtime_put_sync(core->pmdomains->pd_devs[1]);
		if (ret < 0)
			return ret;
	}

	if (coreid_mask & VIDC_CORE_ID_2) {
		ret = vcodec_control_v4(core, VIDC_CORE_ID_2, true);
		if (ret)
			return ret;

		vcodec_clks_disable(core, core->vcodec1_clks);

		ret = vcodec_control_v4(core, VIDC_CORE_ID_2, false);
		if (ret)
			return ret;

		ret = pm_runtime_put_sync(core->pmdomains->pd_devs[2]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int poweron_coreid(struct venus_core *core, unsigned int coreid_mask)
{
	int ret;

	if (coreid_mask & VIDC_CORE_ID_1) {
		ret = pm_runtime_get_sync(core->pmdomains->pd_devs[1]);
		if (ret < 0)
			return ret;

		ret = vcodec_control_v4(core, VIDC_CORE_ID_1, true);
		if (ret)
			return ret;

		ret = vcodec_clks_enable(core, core->vcodec0_clks);
		if (ret)
			return ret;

		ret = vcodec_control_v4(core, VIDC_CORE_ID_1, false);
		if (ret < 0)
			return ret;
	}

	if (coreid_mask & VIDC_CORE_ID_2) {
		ret = pm_runtime_get_sync(core->pmdomains->pd_devs[2]);
		if (ret < 0)
			return ret;

		ret = vcodec_control_v4(core, VIDC_CORE_ID_2, true);
		if (ret)
			return ret;

		ret = vcodec_clks_enable(core, core->vcodec1_clks);
		if (ret)
			return ret;

		ret = vcodec_control_v4(core, VIDC_CORE_ID_2, false);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static inline int power_save_mode_enable(struct venus_inst *inst,
					 bool enable)
{
	struct venc_controls *enc_ctr = &inst->controls.enc;
	const u32 ptype = HFI_PROPERTY_CONFIG_VENC_PERF_MODE;
	u32 venc_mode;
	int ret = 0;

	if (inst->session_type != VIDC_SESSION_TYPE_ENC)
		return 0;

	if (enc_ctr->bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CQ)
		enable = false;

	venc_mode = enable ? HFI_VENC_PERFMODE_POWER_SAVE :
		HFI_VENC_PERFMODE_MAX_QUALITY;

	ret = hfi_session_set_property(inst, ptype, &venc_mode);
	if (ret)
		return ret;

	inst->flags = enable ? inst->flags | VENUS_LOW_POWER :
		inst->flags & ~VENUS_LOW_POWER;

	return ret;
}

static int move_core_to_power_save_mode(struct venus_core *core,
					u32 core_id)
{
	struct venus_inst *inst = NULL;

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		if (inst->clk_data.core_id == core_id &&
		    inst->session_type == VIDC_SESSION_TYPE_ENC)
			power_save_mode_enable(inst, true);
	}
	mutex_unlock(&core->lock);
	return 0;
}

static void
min_loaded_core(struct venus_inst *inst, u32 *min_coreid, u32 *min_load, bool low_power)
{
	u32 mbs_per_sec, load, core1_load = 0, core2_load = 0;
	u32 cores_max = core_num_max(inst);
	struct venus_core *core = inst->core;
	struct venus_inst *inst_pos;
	unsigned long vpp_freq;
	u32 coreid;

	mutex_lock(&core->lock);

	list_for_each_entry(inst_pos, &core->instances, list) {
		if (inst_pos == inst)
			continue;

		if (inst_pos->state != INST_START)
			continue;

		if (inst->session_type == VIDC_SESSION_TYPE_DEC)
			vpp_freq = inst_pos->clk_data.vpp_freq;
		else if (inst->session_type == VIDC_SESSION_TYPE_ENC)
			vpp_freq = low_power ? inst_pos->clk_data.low_power_freq :
				inst_pos->clk_data.vpp_freq;
		else
			continue;

		coreid = inst_pos->clk_data.core_id;

		mbs_per_sec = load_per_instance(inst_pos);
		load = mbs_per_sec * vpp_freq;

		if ((coreid & VIDC_CORE_ID_3) == VIDC_CORE_ID_3) {
			core1_load += load / 2;
			core2_load += load / 2;
		} else if (coreid & VIDC_CORE_ID_1) {
			core1_load += load;
		} else if (coreid & VIDC_CORE_ID_2) {
			core2_load += load;
		}
	}

	*min_coreid = core1_load <= core2_load ?
			VIDC_CORE_ID_1 : VIDC_CORE_ID_2;
	*min_load = min(core1_load, core2_load);

	if (cores_max < VIDC_CORE_ID_2 || core->res->vcodec_num < 2) {
		*min_coreid = VIDC_CORE_ID_1;
		*min_load = core1_load;
	}

	mutex_unlock(&core->lock);
}

static int decide_core(struct venus_inst *inst)
{
	const u32 ptype = HFI_PROPERTY_CONFIG_VIDEOCORES_USAGE;
	struct venus_core *core = inst->core;
	u32 min_coreid, min_load, cur_inst_load;
	u32 min_lp_coreid, min_lp_load, cur_inst_lp_load;
	struct hfi_videocores_usage_type cu;
	unsigned long max_freq;
	int ret = 0;

	if (legacy_binding) {
		if (inst->session_type == VIDC_SESSION_TYPE_DEC)
			cu.video_core_enable_mask = VIDC_CORE_ID_1;
		else
			cu.video_core_enable_mask = VIDC_CORE_ID_2;

		goto done;
	}

	if (inst->clk_data.core_id != VIDC_CORE_ID_DEFAULT)
		return 0;

	cur_inst_load = load_per_instance(inst);
	cur_inst_load *= inst->clk_data.vpp_freq;
	/*TODO : divide this inst->load by work_route */

	cur_inst_lp_load = load_per_instance(inst);
	cur_inst_lp_load *= inst->clk_data.low_power_freq;
	/*TODO : divide this inst->load by work_route */

	max_freq = core->res->freq_tbl[0].freq;

	min_loaded_core(inst, &min_coreid, &min_load, false);
	min_loaded_core(inst, &min_lp_coreid, &min_lp_load, true);

	if (cur_inst_load + min_load <= max_freq) {
		inst->clk_data.core_id = min_coreid;
		cu.video_core_enable_mask = min_coreid;
	} else if (cur_inst_lp_load + min_load <= max_freq) {
		/* Move current instance to LP and return */
		inst->clk_data.core_id = min_coreid;
		cu.video_core_enable_mask = min_coreid;
		power_save_mode_enable(inst, true);
	} else if (cur_inst_lp_load + min_lp_load <= max_freq) {
		/* Move all instances to LP mode and return */
		inst->clk_data.core_id = min_lp_coreid;
		cu.video_core_enable_mask = min_lp_coreid;
		move_core_to_power_save_mode(core, min_lp_coreid);
	} else {
		dev_warn(core->dev, "HW can't support this load");
		return -EINVAL;
	}

done:
	ret = hfi_session_set_property(inst, ptype, &cu);
	if (ret)
		return ret;

	return ret;
}

static int acquire_core(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	unsigned int coreid_mask = 0;

	if (inst->core_acquired)
		return 0;

	inst->core_acquired = true;

	if (inst->clk_data.core_id & VIDC_CORE_ID_1) {
		if (core->core0_usage_count++)
			return 0;

		coreid_mask = VIDC_CORE_ID_1;
	}

	if (inst->clk_data.core_id & VIDC_CORE_ID_2) {
		if (core->core1_usage_count++)
			return 0;

		coreid_mask |= VIDC_CORE_ID_2;
	}

	return poweron_coreid(core, coreid_mask);
}

static int release_core(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	unsigned int coreid_mask = 0;
	int ret;

	if (!inst->core_acquired)
		return 0;

	if (inst->clk_data.core_id & VIDC_CORE_ID_1) {
		if (--core->core0_usage_count)
			goto done;

		coreid_mask = VIDC_CORE_ID_1;
	}

	if (inst->clk_data.core_id & VIDC_CORE_ID_2) {
		if (--core->core1_usage_count)
			goto done;

		coreid_mask |= VIDC_CORE_ID_2;
	}

	ret = poweroff_coreid(core, coreid_mask);
	if (ret)
		return ret;

done:
	inst->clk_data.core_id = VIDC_CORE_ID_DEFAULT;
	inst->core_acquired = false;
	return 0;
}

static int coreid_power_v4(struct venus_inst *inst, int on)
{
	struct venus_core *core = inst->core;
	int ret;

	if (legacy_binding)
		return 0;

	if (on == POWER_ON) {
		ret = decide_core(inst);
		if (ret)
			return ret;

		mutex_lock(&core->lock);
		ret = acquire_core(inst);
		mutex_unlock(&core->lock);
	} else {
		mutex_lock(&core->lock);
		ret = release_core(inst);
		mutex_unlock(&core->lock);
	}

	return ret;
}

static int vdec_get_v4(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);

	if (!legacy_binding)
		return 0;

	return vcodec_clks_get(core, dev, core->vcodec0_clks,
			       core->res->vcodec0_clks);
}

static void vdec_put_v4(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);
	unsigned int i;

	if (!legacy_binding)
		return;

	for (i = 0; i < core->res->vcodec_clks_num; i++)
		core->vcodec0_clks[i] = NULL;
}

static int vdec_power_v4(struct device *dev, int on)
{
	struct venus_core *core = dev_get_drvdata(dev);
	int ret;

	if (!legacy_binding)
		return 0;

	ret = vcodec_control_v4(core, VIDC_CORE_ID_1, true);
	if (ret)
		return ret;

	if (on == POWER_ON)
		ret = vcodec_clks_enable(core, core->vcodec0_clks);
	else
		vcodec_clks_disable(core, core->vcodec0_clks);

	vcodec_control_v4(core, VIDC_CORE_ID_1, false);

	return ret;
}

static int venc_get_v4(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);

	if (!legacy_binding)
		return 0;

	return vcodec_clks_get(core, dev, core->vcodec1_clks,
			       core->res->vcodec1_clks);
}

static void venc_put_v4(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);
	unsigned int i;

	if (!legacy_binding)
		return;

	for (i = 0; i < core->res->vcodec_clks_num; i++)
		core->vcodec1_clks[i] = NULL;
}

static int venc_power_v4(struct device *dev, int on)
{
	struct venus_core *core = dev_get_drvdata(dev);
	int ret;

	if (!legacy_binding)
		return 0;

	ret = vcodec_control_v4(core, VIDC_CORE_ID_2, true);
	if (ret)
		return ret;

	if (on == POWER_ON)
		ret = vcodec_clks_enable(core, core->vcodec1_clks);
	else
		vcodec_clks_disable(core, core->vcodec1_clks);

	vcodec_control_v4(core, VIDC_CORE_ID_2, false);

	return ret;
}

static int vcodec_domains_get(struct venus_core *core)
{
	int ret;
	struct device **opp_virt_dev;
	struct device *dev = core->dev;
	const struct venus_resources *res = core->res;
	struct dev_pm_domain_attach_data vcodec_data = {
		.pd_names = res->vcodec_pmdomains,
		.num_pd_names = res->vcodec_pmdomains_num,
		.pd_flags = PD_FLAG_NO_DEV_LINK,
	};

	if (!res->vcodec_pmdomains_num)
		goto skip_pmdomains;

	ret = dev_pm_domain_attach_list(dev, &vcodec_data, &core->pmdomains);
	if (ret < 0)
		return ret;

skip_pmdomains:
	if (!core->res->opp_pmdomain)
		return 0;

	/* Attach the power domain for setting performance state */
	ret = devm_pm_opp_attach_genpd(dev, res->opp_pmdomain, &opp_virt_dev);
	if (ret)
		goto opp_attach_err;

	core->opp_pmdomain = *opp_virt_dev;
	core->opp_dl_venus = device_link_add(dev, core->opp_pmdomain,
					     DL_FLAG_RPM_ACTIVE |
					     DL_FLAG_PM_RUNTIME |
					     DL_FLAG_STATELESS);
	if (!core->opp_dl_venus) {
		ret = -ENODEV;
		goto opp_attach_err;
	}

	return 0;

opp_attach_err:
	dev_pm_domain_detach_list(core->pmdomains);
	return ret;
}

static void vcodec_domains_put(struct venus_core *core)
{
	dev_pm_domain_detach_list(core->pmdomains);

	if (!core->has_opp_table)
		return;

	if (core->opp_dl_venus)
		device_link_del(core->opp_dl_venus);
}

static int core_resets_reset(struct venus_core *core)
{
	const struct venus_resources *res = core->res;
	unsigned int i;
	int ret;

	if (!res->resets_num)
		return 0;

	for (i = 0; i < res->resets_num; i++) {
		ret = reset_control_assert(core->resets[i]);
		if (ret)
			goto err;

		usleep_range(150, 250);
		ret = reset_control_deassert(core->resets[i]);
		if (ret)
			goto err;
	}

err:
	return ret;
}

static int core_resets_get(struct venus_core *core)
{
	struct device *dev = core->dev;
	const struct venus_resources *res = core->res;
	unsigned int i;
	int ret;

	if (!res->resets_num)
		return 0;

	for (i = 0; i < res->resets_num; i++) {
		core->resets[i] =
			devm_reset_control_get_exclusive(dev, res->resets[i]);
		if (IS_ERR(core->resets[i])) {
			ret = PTR_ERR(core->resets[i]);
			return ret;
		}
	}

	return 0;
}

static int core_get_v4(struct venus_core *core)
{
	struct device *dev = core->dev;
	const struct venus_resources *res = core->res;
	int ret;

	ret = core_clks_get(core);
	if (ret)
		return ret;

	if (!res->vcodec_pmdomains_num)
		legacy_binding = true;

	dev_info(dev, "%s legacy binding\n", legacy_binding ? "" : "non");

	ret = vcodec_clks_get(core, dev, core->vcodec0_clks, res->vcodec0_clks);
	if (ret)
		return ret;

	ret = vcodec_clks_get(core, dev, core->vcodec1_clks, res->vcodec1_clks);
	if (ret)
		return ret;

	ret = core_resets_get(core);
	if (ret)
		return ret;

	if (legacy_binding)
		return 0;

	ret = devm_pm_opp_set_clkname(dev, "core");
	if (ret)
		return ret;

	ret = vcodec_domains_get(core);
	if (ret)
		return ret;

	if (core->res->opp_pmdomain) {
		ret = devm_pm_opp_of_add_table(dev);
		if (!ret) {
			core->has_opp_table = true;
		} else if (ret != -ENODEV) {
			dev_err(dev, "invalid OPP table in device tree\n");
			return ret;
		}
	}

	return 0;
}

static void core_put_v4(struct venus_core *core)
{
	if (legacy_binding)
		return;

	vcodec_domains_put(core);
}

static int core_power_v4(struct venus_core *core, int on)
{
	struct device *dev = core->dev;
	struct device *pmctrl = core->pmdomains ?
			core->pmdomains->pd_devs[0] : NULL;
	int ret = 0;

	if (on == POWER_ON) {
		if (pmctrl) {
			ret = pm_runtime_resume_and_get(pmctrl);
			if (ret < 0) {
				return ret;
			}
		}

		ret = core_resets_reset(core);
		if (ret) {
			if (pmctrl)
				pm_runtime_put_sync(pmctrl);
			return ret;
		}

		ret = core_clks_enable(core);
		if (ret < 0 && pmctrl)
			pm_runtime_put_sync(pmctrl);
	} else {
		/* Drop the performance state vote */
		if (core->opp_pmdomain)
			dev_pm_opp_set_rate(dev, 0);

		core_clks_disable(core);

		ret = core_resets_reset(core);

		if (pmctrl)
			pm_runtime_put_sync(pmctrl);
	}

	return ret;
}

static unsigned long calculate_inst_freq(struct venus_inst *inst,
					 unsigned long filled_len)
{
	unsigned long vpp_freq_per_mb = 0, vpp_freq = 0, vsp_freq = 0;
	u32 fps = (u32)inst->fps;
	u32 mbs_per_sec;

	mbs_per_sec = load_per_instance(inst);

	if (inst->state != INST_START)
		return 0;

	if (inst->session_type == VIDC_SESSION_TYPE_ENC) {
		vpp_freq_per_mb = inst->flags & VENUS_LOW_POWER ?
			inst->clk_data.low_power_freq :
			inst->clk_data.vpp_freq;

		vpp_freq = mbs_per_sec * vpp_freq_per_mb;
	} else {
		vpp_freq = mbs_per_sec * inst->clk_data.vpp_freq;
	}

	/* 21 / 20 is overhead factor */
	vpp_freq += vpp_freq / 20;
	vsp_freq = mbs_per_sec * inst->clk_data.vsp_freq;

	/* 10 / 7 is overhead factor */
	if (inst->session_type == VIDC_SESSION_TYPE_ENC)
		vsp_freq += (inst->controls.enc.bitrate * 10) / 7;
	else
		vsp_freq += ((fps * filled_len * 8) * 10) / 7;

	return max(vpp_freq, vsp_freq);
}

static int load_scale_v4(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	const struct freq_tbl *table = core->res->freq_tbl;
	unsigned int num_rows = core->res->freq_tbl_size;
	struct device *dev = core->dev;
	unsigned long freq = 0, freq_core1 = 0, freq_core2 = 0;
	unsigned long filled_len = 0;
	int i, ret = 0;

	for (i = 0; i < inst->num_input_bufs; i++)
		filled_len = max(filled_len, inst->payloads[i]);

	if (inst->session_type == VIDC_SESSION_TYPE_DEC && !filled_len)
		return ret;

	freq = calculate_inst_freq(inst, filled_len);
	inst->clk_data.freq = freq;

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		if (inst->clk_data.core_id == VIDC_CORE_ID_1) {
			freq_core1 += inst->clk_data.freq;
		} else if (inst->clk_data.core_id == VIDC_CORE_ID_2) {
			freq_core2 += inst->clk_data.freq;
		} else if (inst->clk_data.core_id == VIDC_CORE_ID_3) {
			freq_core1 += inst->clk_data.freq;
			freq_core2 += inst->clk_data.freq;
		}
	}

	freq = max(freq_core1, freq_core2);

	if (freq > table[0].freq) {
		dev_dbg(dev, VDBGL "requested clock rate: %lu scaling clock rate : %lu\n",
			freq, table[0].freq);

		freq = table[0].freq;
		goto set_freq;
	}

	for (i = num_rows - 1 ; i >= 0; i--) {
		if (freq <= table[i].freq) {
			freq = table[i].freq;
			break;
		}
	}

set_freq:

	ret = core_clks_set_rate(core, freq);
	if (ret) {
		dev_err(dev, "failed to set clock rate %lu (%d)\n",
			freq, ret);
		goto exit;
	}

	ret = load_scale_bw(core);
	if (ret) {
		dev_err(dev, "failed to set bandwidth (%d)\n",
			ret);
		goto exit;
	}

exit:
	mutex_unlock(&core->lock);
	return ret;
}

static const struct venus_pm_ops pm_ops_v4 = {
	.core_get = core_get_v4,
	.core_put = core_put_v4,
	.core_power = core_power_v4,
	.vdec_get = vdec_get_v4,
	.vdec_put = vdec_put_v4,
	.vdec_power = vdec_power_v4,
	.venc_get = venc_get_v4,
	.venc_put = venc_put_v4,
	.venc_power = venc_power_v4,
	.coreid_power = coreid_power_v4,
	.load_scale = load_scale_v4,
};

const struct venus_pm_ops *venus_pm_get(enum hfi_version version)
{
	switch (version) {
	case HFI_VERSION_1XX:
	default:
		return &pm_ops_v1;
	case HFI_VERSION_3XX:
		return &pm_ops_v3;
	case HFI_VERSION_4XX:
	case HFI_VERSION_6XX:
		return &pm_ops_v4;
	}

	return NULL;
}
