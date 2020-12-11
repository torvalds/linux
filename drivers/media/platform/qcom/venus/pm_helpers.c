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
#include <linux/types.h>
#include <media/v4l2-mem2mem.h>

#include "core.h"
#include "hfi_parser.h"
#include "hfi_venus_io.h"
#include "pm_helpers.h"

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
	unsigned int i;
	int ret;

	for (i = 0; i < res->clks_num; i++) {
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

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		if (inst->session_type != session_type)
			continue;

		mbs_per_sec += load_per_instance(inst);
	}
	mutex_unlock(&core->lock);

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
		if (mbs > bw_tbl[i].mbs_per_sec)
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

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		mbs_per_sec = load_per_instance(inst);
		mbs_to_bw(inst, mbs_per_sec, &avg, &peak);
		total_avg += avg;
		total_peak += peak;
	}
	mutex_unlock(&core->lock);

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
	int ret;

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
		return ret;
	}

	ret = load_scale_bw(core);
	if (ret) {
		dev_err(dev, "failed to set bandwidth (%d)\n",
			ret);
		return ret;
	}

	return 0;
}

static int core_get_v1(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);

	return core_clks_get(core);
}

static int core_power_v1(struct device *dev, int on)
{
	struct venus_core *core = dev_get_drvdata(dev);
	int ret = 0;

	if (on == POWER_ON)
		ret = core_clks_enable(core);
	else
		core_clks_disable(core);

	return ret;
}

static const struct venus_pm_ops pm_ops_v1 = {
	.core_get = core_get_v1,
	.core_power = core_power_v1,
	.load_scale = load_scale_v1,
};

static void
vcodec_control_v3(struct venus_core *core, u32 session_type, bool enable)
{
	void __iomem *ctrl;

	if (session_type == VIDC_SESSION_TYPE_DEC)
		ctrl = core->base + WRAPPER_VDEC_VCODEC_POWER_CONTROL;
	else
		ctrl = core->base + WRAPPER_VENC_VCODEC_POWER_CONTROL;

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

	if (coreid == VIDC_CORE_ID_1) {
		ctrl = core->base + WRAPPER_VCODEC0_MMCC_POWER_CONTROL;
		stat = core->base + WRAPPER_VCODEC0_MMCC_POWER_STATUS;
	} else {
		ctrl = core->base + WRAPPER_VCODEC1_MMCC_POWER_CONTROL;
		stat = core->base + WRAPPER_VCODEC1_MMCC_POWER_STATUS;
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

		ret = pm_runtime_put_sync(core->pmdomains[1]);
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

		ret = pm_runtime_put_sync(core->pmdomains[2]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int poweron_coreid(struct venus_core *core, unsigned int coreid_mask)
{
	int ret;

	if (coreid_mask & VIDC_CORE_ID_1) {
		ret = pm_runtime_get_sync(core->pmdomains[1]);
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
		ret = pm_runtime_get_sync(core->pmdomains[2]);
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

static void
min_loaded_core(struct venus_inst *inst, u32 *min_coreid, u32 *min_load)
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

		vpp_freq = inst_pos->clk_data.codec_freq_data->vpp_freq;
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
	u32 min_coreid, min_load, inst_load;
	struct hfi_videocores_usage_type cu;
	unsigned long max_freq;

	if (legacy_binding) {
		if (inst->session_type == VIDC_SESSION_TYPE_DEC)
			cu.video_core_enable_mask = VIDC_CORE_ID_1;
		else
			cu.video_core_enable_mask = VIDC_CORE_ID_2;

		goto done;
	}

	if (inst->clk_data.core_id != VIDC_CORE_ID_DEFAULT)
		return 0;

	inst_load = load_per_instance(inst);
	inst_load *= inst->clk_data.codec_freq_data->vpp_freq;
	max_freq = core->res->freq_tbl[0].freq;

	min_loaded_core(inst, &min_coreid, &min_load);

	if ((inst_load + min_load) > max_freq) {
		dev_warn(core->dev, "HW is overloaded, needed: %u max: %lu\n",
			 inst_load, max_freq);
		return -EINVAL;
	}

	inst->clk_data.core_id = min_coreid;
	cu.video_core_enable_mask = min_coreid;

done:
	return hfi_session_set_property(inst, ptype, &cu);
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

static int vcodec_domains_get(struct device *dev)
{
	int ret;
	struct opp_table *opp_table;
	struct device **opp_virt_dev;
	struct venus_core *core = dev_get_drvdata(dev);
	const struct venus_resources *res = core->res;
	struct device *pd;
	unsigned int i;

	if (!res->vcodec_pmdomains_num)
		goto skip_pmdomains;

	for (i = 0; i < res->vcodec_pmdomains_num; i++) {
		pd = dev_pm_domain_attach_by_name(dev,
						  res->vcodec_pmdomains[i]);
		if (IS_ERR(pd))
			return PTR_ERR(pd);
		core->pmdomains[i] = pd;
	}

	core->pd_dl_venus = device_link_add(dev, core->pmdomains[0],
					    DL_FLAG_PM_RUNTIME |
					    DL_FLAG_STATELESS |
					    DL_FLAG_RPM_ACTIVE);
	if (!core->pd_dl_venus)
		return -ENODEV;

skip_pmdomains:
	if (!core->has_opp_table)
		return 0;

	/* Attach the power domain for setting performance state */
	opp_table = dev_pm_opp_attach_genpd(dev, res->opp_pmdomain, &opp_virt_dev);
	if (IS_ERR(opp_table)) {
		ret = PTR_ERR(opp_table);
		goto opp_attach_err;
	}

	core->opp_pmdomain = *opp_virt_dev;
	core->opp_dl_venus = device_link_add(dev, core->opp_pmdomain,
					     DL_FLAG_RPM_ACTIVE |
					     DL_FLAG_PM_RUNTIME |
					     DL_FLAG_STATELESS);
	if (!core->opp_dl_venus) {
		ret = -ENODEV;
		goto opp_dl_add_err;
	}

	return 0;

opp_dl_add_err:
	dev_pm_domain_detach(core->opp_pmdomain, true);
opp_attach_err:
	if (core->pd_dl_venus) {
		device_link_del(core->pd_dl_venus);
		for (i = 0; i < res->vcodec_pmdomains_num; i++) {
			if (IS_ERR_OR_NULL(core->pmdomains[i]))
				continue;
			dev_pm_domain_detach(core->pmdomains[i], true);
		}
	}
	return ret;
}

static void vcodec_domains_put(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);
	const struct venus_resources *res = core->res;
	unsigned int i;

	if (!res->vcodec_pmdomains_num)
		goto skip_pmdomains;

	if (core->pd_dl_venus)
		device_link_del(core->pd_dl_venus);

	for (i = 0; i < res->vcodec_pmdomains_num; i++) {
		if (IS_ERR_OR_NULL(core->pmdomains[i]))
			continue;
		dev_pm_domain_detach(core->pmdomains[i], true);
	}

skip_pmdomains:
	if (!core->has_opp_table)
		return;

	if (core->opp_dl_venus)
		device_link_del(core->opp_dl_venus);

	dev_pm_domain_detach(core->opp_pmdomain, true);
}

static int core_get_v4(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);
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

	if (legacy_binding)
		return 0;

	core->opp_table = dev_pm_opp_set_clkname(dev, "core");
	if (IS_ERR(core->opp_table))
		return PTR_ERR(core->opp_table);

	if (core->res->opp_pmdomain) {
		ret = dev_pm_opp_of_add_table(dev);
		if (!ret) {
			core->has_opp_table = true;
		} else if (ret != -ENODEV) {
			dev_err(dev, "invalid OPP table in device tree\n");
			dev_pm_opp_put_clkname(core->opp_table);
			return ret;
		}
	}

	ret = vcodec_domains_get(dev);
	if (ret) {
		if (core->has_opp_table)
			dev_pm_opp_of_remove_table(dev);
		dev_pm_opp_put_clkname(core->opp_table);
		return ret;
	}

	return 0;
}

static void core_put_v4(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);

	if (legacy_binding)
		return;

	vcodec_domains_put(dev);

	if (core->has_opp_table)
		dev_pm_opp_of_remove_table(dev);
	if (core->opp_table)
		dev_pm_opp_put_clkname(core->opp_table);

}

static int core_power_v4(struct device *dev, int on)
{
	struct venus_core *core = dev_get_drvdata(dev);
	int ret = 0;

	if (on == POWER_ON) {
		ret = core_clks_enable(core);
	} else {
		/* Drop the performance state vote */
		if (core->opp_pmdomain)
			dev_pm_opp_set_rate(dev, 0);

		core_clks_disable(core);
	}

	return ret;
}

static unsigned long calculate_inst_freq(struct venus_inst *inst,
					 unsigned long filled_len)
{
	unsigned long vpp_freq = 0, vsp_freq = 0;
	u32 fps = (u32)inst->fps;
	u32 mbs_per_sec;

	mbs_per_sec = load_per_instance(inst) / fps;

	vpp_freq = mbs_per_sec * inst->clk_data.codec_freq_data->vpp_freq;
	/* 21 / 20 is overhead factor */
	vpp_freq += vpp_freq / 20;
	vsp_freq = mbs_per_sec * inst->clk_data.codec_freq_data->vsp_freq;

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
	int i, ret;

	for (i = 0; i < inst->num_input_bufs; i++)
		filled_len = max(filled_len, inst->payloads[i]);

	if (inst->session_type == VIDC_SESSION_TYPE_DEC && !filled_len)
		return 0;

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
	mutex_unlock(&core->lock);

	freq = max(freq_core1, freq_core2);

	if (freq >= table[0].freq) {
		freq = table[0].freq;
		dev_warn(dev, "HW is overloaded, needed: %lu max: %lu\n",
			 freq, table[0].freq);
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
		return ret;
	}

	ret = load_scale_bw(core);
	if (ret) {
		dev_err(dev, "failed to set bandwidth (%d)\n",
			ret);
		return ret;
	}

	return 0;
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
		return &pm_ops_v4;
	}

	return NULL;
}
