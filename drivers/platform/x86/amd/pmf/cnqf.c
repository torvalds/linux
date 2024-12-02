// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Platform Management Framework Driver
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#include <linux/workqueue.h>
#include "pmf.h"

static struct cnqf_config config_store;

static int amd_pmf_set_cnqf(struct amd_pmf_dev *dev, int src, int idx,
			    struct cnqf_config *table)
{
	struct power_table_control *pc;

	pc = &config_store.mode_set[src][idx].power_control;

	amd_pmf_send_cmd(dev, SET_SPL, false, pc->spl, NULL);
	amd_pmf_send_cmd(dev, SET_FPPT, false, pc->fppt, NULL);
	amd_pmf_send_cmd(dev, SET_SPPT, false, pc->sppt, NULL);
	amd_pmf_send_cmd(dev, SET_SPPT_APU_ONLY, false, pc->sppt_apu_only, NULL);
	amd_pmf_send_cmd(dev, SET_STT_MIN_LIMIT, false, pc->stt_min, NULL);
	amd_pmf_send_cmd(dev, SET_STT_LIMIT_APU, false, pc->stt_skin_temp[STT_TEMP_APU],
			 NULL);
	amd_pmf_send_cmd(dev, SET_STT_LIMIT_HS2, false, pc->stt_skin_temp[STT_TEMP_HS2],
			 NULL);

	if (is_apmf_func_supported(dev, APMF_FUNC_SET_FAN_IDX))
		apmf_update_fan_idx(dev,
				    config_store.mode_set[src][idx].fan_control.manual,
				    config_store.mode_set[src][idx].fan_control.fan_id);

	return 0;
}

static void amd_pmf_update_power_threshold(int src)
{
	struct cnqf_mode_settings *ts;
	struct cnqf_tran_params *tp;

	tp = &config_store.trans_param[src][CNQF_TRANSITION_TO_QUIET];
	ts = &config_store.mode_set[src][CNQF_MODE_BALANCE];
	tp->power_threshold = ts->power_floor;

	tp = &config_store.trans_param[src][CNQF_TRANSITION_TO_TURBO];
	ts = &config_store.mode_set[src][CNQF_MODE_PERFORMANCE];
	tp->power_threshold = ts->power_floor;

	tp = &config_store.trans_param[src][CNQF_TRANSITION_FROM_BALANCE_TO_PERFORMANCE];
	ts = &config_store.mode_set[src][CNQF_MODE_BALANCE];
	tp->power_threshold = ts->power_floor;

	tp = &config_store.trans_param[src][CNQF_TRANSITION_FROM_PERFORMANCE_TO_BALANCE];
	ts = &config_store.mode_set[src][CNQF_MODE_PERFORMANCE];
	tp->power_threshold = ts->power_floor;

	tp = &config_store.trans_param[src][CNQF_TRANSITION_FROM_QUIET_TO_BALANCE];
	ts = &config_store.mode_set[src][CNQF_MODE_QUIET];
	tp->power_threshold = ts->power_floor;

	tp = &config_store.trans_param[src][CNQF_TRANSITION_FROM_TURBO_TO_PERFORMANCE];
	ts = &config_store.mode_set[src][CNQF_MODE_TURBO];
	tp->power_threshold = ts->power_floor;
}

static const char *state_as_str(unsigned int state)
{
	switch (state) {
	case CNQF_MODE_QUIET:
		return "QUIET";
	case CNQF_MODE_BALANCE:
		return "BALANCED";
	case CNQF_MODE_TURBO:
		return "TURBO";
	case CNQF_MODE_PERFORMANCE:
		return "PERFORMANCE";
	default:
		return "Unknown CnQF mode";
	}
}

static int amd_pmf_cnqf_get_power_source(struct amd_pmf_dev *dev)
{
	if (is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_AC) &&
	    is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_DC))
		return amd_pmf_get_power_source();
	else if (is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_DC))
		return POWER_SOURCE_DC;
	else
		return POWER_SOURCE_AC;
}

int amd_pmf_trans_cnqf(struct amd_pmf_dev *dev, int socket_power, ktime_t time_lapsed_ms)
{
	struct cnqf_tran_params *tp;
	int src, i, j;
	u32 avg_power = 0;

	src = amd_pmf_cnqf_get_power_source(dev);

	if (is_pprof_balanced(dev)) {
		amd_pmf_set_cnqf(dev, src, config_store.current_mode, NULL);
	} else {
		/*
		 * Return from here if the platform_profile is not balanced
		 * so that preference is given to user mode selection, rather
		 * than enforcing CnQF to run all the time (if enabled)
		 */
		return -EINVAL;
	}

	for (i = 0; i < CNQF_TRANSITION_MAX; i++) {
		config_store.trans_param[src][i].timer += time_lapsed_ms;
		config_store.trans_param[src][i].total_power += socket_power;
		config_store.trans_param[src][i].count++;

		tp = &config_store.trans_param[src][i];
		if (tp->timer >= tp->time_constant && tp->count) {
			avg_power = tp->total_power / tp->count;

			/* Reset the indices */
			tp->timer = 0;
			tp->total_power = 0;
			tp->count = 0;

			if ((tp->shifting_up && avg_power >= tp->power_threshold) ||
			    (!tp->shifting_up && avg_power <= tp->power_threshold)) {
				tp->priority = true;
			} else {
				tp->priority = false;
			}
		}
	}

	dev_dbg(dev->dev, "[CNQF] Avg power: %u mW socket power: %u mW mode:%s\n",
		avg_power, socket_power, state_as_str(config_store.current_mode));

	for (j = 0; j < CNQF_TRANSITION_MAX; j++) {
		/* apply the highest priority */
		if (config_store.trans_param[src][j].priority) {
			if (config_store.current_mode !=
			    config_store.trans_param[src][j].target_mode) {
				config_store.current_mode =
						config_store.trans_param[src][j].target_mode;
				dev_dbg(dev->dev, "Moving to Mode :%s\n",
					state_as_str(config_store.current_mode));
				amd_pmf_set_cnqf(dev, src,
						 config_store.current_mode, NULL);
			}
			break;
		}
	}
	return 0;
}

static void amd_pmf_update_trans_data(int idx, struct apmf_dyn_slider_output out)
{
	struct cnqf_tran_params *tp;

	tp = &config_store.trans_param[idx][CNQF_TRANSITION_TO_QUIET];
	tp->time_constant = out.t_balanced_to_quiet;
	tp->target_mode = CNQF_MODE_QUIET;
	tp->shifting_up = false;

	tp = &config_store.trans_param[idx][CNQF_TRANSITION_FROM_BALANCE_TO_PERFORMANCE];
	tp->time_constant = out.t_balanced_to_perf;
	tp->target_mode = CNQF_MODE_PERFORMANCE;
	tp->shifting_up = true;

	tp = &config_store.trans_param[idx][CNQF_TRANSITION_FROM_QUIET_TO_BALANCE];
	tp->time_constant = out.t_quiet_to_balanced;
	tp->target_mode = CNQF_MODE_BALANCE;
	tp->shifting_up = true;

	tp = &config_store.trans_param[idx][CNQF_TRANSITION_FROM_PERFORMANCE_TO_BALANCE];
	tp->time_constant = out.t_perf_to_balanced;
	tp->target_mode = CNQF_MODE_BALANCE;
	tp->shifting_up = false;

	tp = &config_store.trans_param[idx][CNQF_TRANSITION_FROM_TURBO_TO_PERFORMANCE];
	tp->time_constant = out.t_turbo_to_perf;
	tp->target_mode = CNQF_MODE_PERFORMANCE;
	tp->shifting_up = false;

	tp = &config_store.trans_param[idx][CNQF_TRANSITION_TO_TURBO];
	tp->time_constant = out.t_perf_to_turbo;
	tp->target_mode = CNQF_MODE_TURBO;
	tp->shifting_up = true;
}

static void amd_pmf_update_mode_set(int idx, struct apmf_dyn_slider_output out)
{
	struct cnqf_mode_settings *ms;

	/* Quiet Mode */
	ms = &config_store.mode_set[idx][CNQF_MODE_QUIET];
	ms->power_floor = out.ps[APMF_CNQF_QUIET].pfloor;
	ms->power_control.fppt = out.ps[APMF_CNQF_QUIET].fppt;
	ms->power_control.sppt = out.ps[APMF_CNQF_QUIET].sppt;
	ms->power_control.sppt_apu_only = out.ps[APMF_CNQF_QUIET].sppt_apu_only;
	ms->power_control.spl = out.ps[APMF_CNQF_QUIET].spl;
	ms->power_control.stt_min = out.ps[APMF_CNQF_QUIET].stt_min_limit;
	ms->power_control.stt_skin_temp[STT_TEMP_APU] =
		out.ps[APMF_CNQF_QUIET].stt_skintemp[STT_TEMP_APU];
	ms->power_control.stt_skin_temp[STT_TEMP_HS2] =
		out.ps[APMF_CNQF_QUIET].stt_skintemp[STT_TEMP_HS2];
	ms->fan_control.fan_id = out.ps[APMF_CNQF_QUIET].fan_id;

	/* Balance Mode */
	ms = &config_store.mode_set[idx][CNQF_MODE_BALANCE];
	ms->power_floor = out.ps[APMF_CNQF_BALANCE].pfloor;
	ms->power_control.fppt = out.ps[APMF_CNQF_BALANCE].fppt;
	ms->power_control.sppt = out.ps[APMF_CNQF_BALANCE].sppt;
	ms->power_control.sppt_apu_only = out.ps[APMF_CNQF_BALANCE].sppt_apu_only;
	ms->power_control.spl = out.ps[APMF_CNQF_BALANCE].spl;
	ms->power_control.stt_min = out.ps[APMF_CNQF_BALANCE].stt_min_limit;
	ms->power_control.stt_skin_temp[STT_TEMP_APU] =
		out.ps[APMF_CNQF_BALANCE].stt_skintemp[STT_TEMP_APU];
	ms->power_control.stt_skin_temp[STT_TEMP_HS2] =
		out.ps[APMF_CNQF_BALANCE].stt_skintemp[STT_TEMP_HS2];
	ms->fan_control.fan_id = out.ps[APMF_CNQF_BALANCE].fan_id;

	/* Performance Mode */
	ms = &config_store.mode_set[idx][CNQF_MODE_PERFORMANCE];
	ms->power_floor = out.ps[APMF_CNQF_PERFORMANCE].pfloor;
	ms->power_control.fppt = out.ps[APMF_CNQF_PERFORMANCE].fppt;
	ms->power_control.sppt = out.ps[APMF_CNQF_PERFORMANCE].sppt;
	ms->power_control.sppt_apu_only = out.ps[APMF_CNQF_PERFORMANCE].sppt_apu_only;
	ms->power_control.spl = out.ps[APMF_CNQF_PERFORMANCE].spl;
	ms->power_control.stt_min = out.ps[APMF_CNQF_PERFORMANCE].stt_min_limit;
	ms->power_control.stt_skin_temp[STT_TEMP_APU] =
		out.ps[APMF_CNQF_PERFORMANCE].stt_skintemp[STT_TEMP_APU];
	ms->power_control.stt_skin_temp[STT_TEMP_HS2] =
		out.ps[APMF_CNQF_PERFORMANCE].stt_skintemp[STT_TEMP_HS2];
	ms->fan_control.fan_id = out.ps[APMF_CNQF_PERFORMANCE].fan_id;

	/* Turbo Mode */
	ms = &config_store.mode_set[idx][CNQF_MODE_TURBO];
	ms->power_floor = out.ps[APMF_CNQF_TURBO].pfloor;
	ms->power_control.fppt = out.ps[APMF_CNQF_TURBO].fppt;
	ms->power_control.sppt = out.ps[APMF_CNQF_TURBO].sppt;
	ms->power_control.sppt_apu_only = out.ps[APMF_CNQF_TURBO].sppt_apu_only;
	ms->power_control.spl = out.ps[APMF_CNQF_TURBO].spl;
	ms->power_control.stt_min = out.ps[APMF_CNQF_TURBO].stt_min_limit;
	ms->power_control.stt_skin_temp[STT_TEMP_APU] =
		out.ps[APMF_CNQF_TURBO].stt_skintemp[STT_TEMP_APU];
	ms->power_control.stt_skin_temp[STT_TEMP_HS2] =
		out.ps[APMF_CNQF_TURBO].stt_skintemp[STT_TEMP_HS2];
	ms->fan_control.fan_id = out.ps[APMF_CNQF_TURBO].fan_id;
}

static int amd_pmf_check_flags(struct amd_pmf_dev *dev)
{
	struct apmf_dyn_slider_output out = {};

	if (is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_AC))
		apmf_get_dyn_slider_def_ac(dev, &out);
	else if (is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_DC))
		apmf_get_dyn_slider_def_dc(dev, &out);

	return out.flags;
}

static int amd_pmf_load_defaults_cnqf(struct amd_pmf_dev *dev)
{
	struct apmf_dyn_slider_output out;
	int i, j, ret;

	for (i = 0; i < POWER_SOURCE_MAX; i++) {
		if (!is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_AC + i))
			continue;

		if (i == POWER_SOURCE_AC)
			ret = apmf_get_dyn_slider_def_ac(dev, &out);
		else
			ret = apmf_get_dyn_slider_def_dc(dev, &out);
		if (ret) {
			dev_err(dev->dev, "APMF apmf_get_dyn_slider_def_dc failed :%d\n", ret);
			return ret;
		}

		amd_pmf_update_mode_set(i, out);
		amd_pmf_update_trans_data(i, out);
		amd_pmf_update_power_threshold(i);

		for (j = 0; j < CNQF_MODE_MAX; j++) {
			if (config_store.mode_set[i][j].fan_control.fan_id == FAN_INDEX_AUTO)
				config_store.mode_set[i][j].fan_control.manual = false;
			else
				config_store.mode_set[i][j].fan_control.manual = true;
		}
	}

	/* set to initial default values */
	config_store.current_mode = CNQF_MODE_BALANCE;

	return 0;
}

static ssize_t cnqf_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct amd_pmf_dev *pdev = dev_get_drvdata(dev);
	int result, src;
	bool input;

	result = kstrtobool(buf, &input);
	if (result)
		return result;

	src = amd_pmf_cnqf_get_power_source(pdev);
	pdev->cnqf_enabled = input;

	if (pdev->cnqf_enabled && is_pprof_balanced(pdev)) {
		amd_pmf_set_cnqf(pdev, src, config_store.current_mode, NULL);
	} else {
		if (is_apmf_func_supported(pdev, APMF_FUNC_STATIC_SLIDER_GRANULAR))
			amd_pmf_set_sps_power_limits(pdev);
	}

	dev_dbg(pdev->dev, "Received CnQF %s\n", input ? "on" : "off");
	return count;
}

static ssize_t cnqf_enable_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct amd_pmf_dev *pdev = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", pdev->cnqf_enabled ? "on" : "off");
}

static DEVICE_ATTR_RW(cnqf_enable);

static umode_t cnqf_feature_is_visible(struct kobject *kobj,
				       struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct amd_pmf_dev *pdev = dev_get_drvdata(dev);

	return pdev->cnqf_supported ? attr->mode : 0;
}

static struct attribute *cnqf_feature_attrs[] = {
	&dev_attr_cnqf_enable.attr,
	NULL
};

const struct attribute_group cnqf_feature_attribute_group = {
	.is_visible = cnqf_feature_is_visible,
	.attrs = cnqf_feature_attrs,
};

void amd_pmf_deinit_cnqf(struct amd_pmf_dev *dev)
{
	cancel_delayed_work_sync(&dev->work_buffer);
}

int amd_pmf_init_cnqf(struct amd_pmf_dev *dev)
{
	int ret, src;

	/*
	 * Note the caller of this function has already checked that both
	 * APMF_FUNC_DYN_SLIDER_AC and APMF_FUNC_DYN_SLIDER_DC are supported.
	 */

	ret = amd_pmf_load_defaults_cnqf(dev);
	if (ret < 0)
		return ret;

	amd_pmf_init_metrics_table(dev);

	dev->cnqf_supported = true;
	dev->cnqf_enabled = amd_pmf_check_flags(dev);

	/* update the thermal for CnQF */
	if (dev->cnqf_enabled && is_pprof_balanced(dev)) {
		src = amd_pmf_cnqf_get_power_source(dev);
		amd_pmf_set_cnqf(dev, src, config_store.current_mode, NULL);
	}

	return 0;
}
