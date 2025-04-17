// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Platform Management Framework (PMF) Driver
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#include "pmf.h"

static struct amd_pmf_static_slider_granular_v2 config_store_v2;
static struct amd_pmf_static_slider_granular config_store;
static struct amd_pmf_apts_granular apts_config_store;

#ifdef CONFIG_AMD_PMF_DEBUG
static const char *slider_v2_as_str(unsigned int state)
{
	switch (state) {
	case POWER_MODE_BEST_PERFORMANCE:
		return "Best Performance";
	case POWER_MODE_BALANCED:
		return "Balanced";
	case POWER_MODE_BEST_POWER_EFFICIENCY:
		return "Best Power Efficiency";
	case POWER_MODE_ENERGY_SAVE:
		return "Energy Save";
	default:
		return "Unknown Power Mode";
	}
}

static const char *slider_as_str(unsigned int state)
{
	switch (state) {
	case POWER_MODE_PERFORMANCE:
		return "PERFORMANCE";
	case POWER_MODE_BALANCED_POWER:
		return "BALANCED_POWER";
	case POWER_MODE_POWER_SAVER:
		return "POWER_SAVER";
	default:
		return "Unknown Slider State";
	}
}

const char *amd_pmf_source_as_str(unsigned int state)
{
	switch (state) {
	case POWER_SOURCE_AC:
		return "AC";
	case POWER_SOURCE_DC:
		return "DC";
	default:
		return "Unknown Power State";
	}
}

static void amd_pmf_dump_sps_defaults(struct amd_pmf_static_slider_granular *data)
{
	int i, j;

	pr_debug("Static Slider Data - BEGIN\n");

	for (i = 0; i < POWER_SOURCE_MAX; i++) {
		for (j = 0; j < POWER_MODE_MAX; j++) {
			pr_debug("--- Source:%s Mode:%s ---\n", amd_pmf_source_as_str(i),
				 slider_as_str(j));
			pr_debug("SPL: %u mW\n", data->prop[i][j].spl);
			pr_debug("SPPT: %u mW\n", data->prop[i][j].sppt);
			pr_debug("SPPT_ApuOnly: %u mW\n", data->prop[i][j].sppt_apu_only);
			pr_debug("FPPT: %u mW\n", data->prop[i][j].fppt);
			pr_debug("STTMinLimit: %u mW\n", data->prop[i][j].stt_min);
			pr_debug("STT_SkinTempLimit_APU: %u C\n",
				 data->prop[i][j].stt_skin_temp[STT_TEMP_APU]);
			pr_debug("STT_SkinTempLimit_HS2: %u C\n",
				 data->prop[i][j].stt_skin_temp[STT_TEMP_HS2]);
		}
	}

	pr_debug("Static Slider Data - END\n");
}

static void amd_pmf_dump_sps_defaults_v2(struct amd_pmf_static_slider_granular_v2 *data)
{
	unsigned int i, j;

	pr_debug("Static Slider APTS state index data - BEGIN");
	pr_debug("size: %u\n", data->size);

	for (i = 0; i < POWER_SOURCE_MAX; i++)
		for (j = 0; j < POWER_MODE_V2_MAX; j++)
			pr_debug("%s %s: %u\n", amd_pmf_source_as_str(i), slider_v2_as_str(j),
				 data->sps_idx.power_states[i][j]);

	pr_debug("Static Slider APTS state index data - END\n");
}

static void amd_pmf_dump_apts_sps_defaults(struct amd_pmf_apts_granular *info)
{
	int i;

	pr_debug("Static Slider APTS index default values data - BEGIN");

	for (i = 0; i < APTS_MAX_STATES; i++) {
		pr_debug("Table Version[%d] = %u\n", i, info->val[i].table_version);
		pr_debug("Fan Index[%d] = %u\n", i, info->val[i].fan_table_idx);
		pr_debug("PPT[%d] = %u\n", i, info->val[i].pmf_ppt);
		pr_debug("PPT APU[%d] = %u\n", i, info->val[i].ppt_pmf_apu_only);
		pr_debug("STT Min[%d] = %u\n", i, info->val[i].stt_min_limit);
		pr_debug("STT APU[%d] = %u\n", i, info->val[i].stt_skin_temp_limit_apu);
		pr_debug("STT HS2[%d] = %u\n", i, info->val[i].stt_skin_temp_limit_hs2);
	}

	pr_debug("Static Slider APTS index default values data - END");
}
#else
static void amd_pmf_dump_sps_defaults(struct amd_pmf_static_slider_granular *data) {}
static void amd_pmf_dump_sps_defaults_v2(struct amd_pmf_static_slider_granular_v2 *data) {}
static void amd_pmf_dump_apts_sps_defaults(struct amd_pmf_apts_granular *info) {}
#endif

static void amd_pmf_load_apts_defaults_sps_v2(struct amd_pmf_dev *pdev)
{
	struct amd_pmf_apts_granular_output output;
	struct amd_pmf_apts_output *ps;
	int i;

	memset(&apts_config_store, 0, sizeof(apts_config_store));

	ps = apts_config_store.val;

	for (i = 0; i < APTS_MAX_STATES; i++) {
		apts_get_static_slider_granular_v2(pdev, &output, i);
		ps[i].table_version = output.val.table_version;
		ps[i].fan_table_idx = output.val.fan_table_idx;
		ps[i].pmf_ppt = output.val.pmf_ppt;
		ps[i].ppt_pmf_apu_only = output.val.ppt_pmf_apu_only;
		ps[i].stt_min_limit = output.val.stt_min_limit;
		ps[i].stt_skin_temp_limit_apu = output.val.stt_skin_temp_limit_apu;
		ps[i].stt_skin_temp_limit_hs2 = output.val.stt_skin_temp_limit_hs2;
	}

	amd_pmf_dump_apts_sps_defaults(&apts_config_store);
}

static void amd_pmf_load_defaults_sps_v2(struct amd_pmf_dev *dev)
{
	struct apmf_static_slider_granular_output_v2 output;
	unsigned int i, j;

	memset(&config_store_v2, 0, sizeof(config_store_v2));
	apmf_get_static_slider_granular_v2(dev, &output);

	config_store_v2.size = output.size;

	for (i = 0; i < POWER_SOURCE_MAX; i++)
		for (j = 0; j < POWER_MODE_V2_MAX; j++)
			config_store_v2.sps_idx.power_states[i][j] =
							output.sps_idx.power_states[i][j];

	amd_pmf_dump_sps_defaults_v2(&config_store_v2);
}

static void amd_pmf_load_defaults_sps(struct amd_pmf_dev *dev)
{
	struct apmf_static_slider_granular_output output;
	int i, j, idx = 0;

	memset(&config_store, 0, sizeof(config_store));
	apmf_get_static_slider_granular(dev, &output);

	for (i = 0; i < POWER_SOURCE_MAX; i++) {
		for (j = 0; j < POWER_MODE_MAX; j++) {
			config_store.prop[i][j].spl = output.prop[idx].spl;
			config_store.prop[i][j].sppt = output.prop[idx].sppt;
			config_store.prop[i][j].sppt_apu_only =
						output.prop[idx].sppt_apu_only;
			config_store.prop[i][j].fppt = output.prop[idx].fppt;
			config_store.prop[i][j].stt_min = output.prop[idx].stt_min;
			config_store.prop[i][j].stt_skin_temp[STT_TEMP_APU] =
					output.prop[idx].stt_skin_temp[STT_TEMP_APU];
			config_store.prop[i][j].stt_skin_temp[STT_TEMP_HS2] =
					output.prop[idx].stt_skin_temp[STT_TEMP_HS2];
			config_store.prop[i][j].fan_id = output.prop[idx].fan_id;
			idx++;
		}
	}
	amd_pmf_dump_sps_defaults(&config_store);
}

static void amd_pmf_update_slider_v2(struct amd_pmf_dev *dev, int idx)
{
	amd_pmf_send_cmd(dev, SET_PMF_PPT, false, apts_config_store.val[idx].pmf_ppt, NULL);
	amd_pmf_send_cmd(dev, SET_PMF_PPT_APU_ONLY, false,
			 apts_config_store.val[idx].ppt_pmf_apu_only, NULL);
	amd_pmf_send_cmd(dev, SET_STT_MIN_LIMIT, false,
			 apts_config_store.val[idx].stt_min_limit, NULL);
	amd_pmf_send_cmd(dev, SET_STT_LIMIT_APU, false,
			 fixp_q88_fromint(apts_config_store.val[idx].stt_skin_temp_limit_apu),
			 NULL);
	amd_pmf_send_cmd(dev, SET_STT_LIMIT_HS2, false,
			 fixp_q88_fromint(apts_config_store.val[idx].stt_skin_temp_limit_hs2),
			 NULL);
}

void amd_pmf_update_slider(struct amd_pmf_dev *dev, bool op, int idx,
			   struct amd_pmf_static_slider_granular *table)
{
	int src = amd_pmf_get_power_source();

	if (op == SLIDER_OP_SET) {
		amd_pmf_send_cmd(dev, SET_SPL, false, config_store.prop[src][idx].spl, NULL);
		amd_pmf_send_cmd(dev, SET_FPPT, false, config_store.prop[src][idx].fppt, NULL);
		amd_pmf_send_cmd(dev, SET_SPPT, false, config_store.prop[src][idx].sppt, NULL);
		amd_pmf_send_cmd(dev, SET_SPPT_APU_ONLY, false,
				 config_store.prop[src][idx].sppt_apu_only, NULL);
		amd_pmf_send_cmd(dev, SET_STT_MIN_LIMIT, false,
				 config_store.prop[src][idx].stt_min, NULL);
		amd_pmf_send_cmd(dev, SET_STT_LIMIT_APU, false,
				 fixp_q88_fromint(config_store.prop[src][idx].stt_skin_temp[STT_TEMP_APU]),
				 NULL);
		amd_pmf_send_cmd(dev, SET_STT_LIMIT_HS2, false,
				 fixp_q88_fromint(config_store.prop[src][idx].stt_skin_temp[STT_TEMP_HS2]),
				 NULL);
	} else if (op == SLIDER_OP_GET) {
		amd_pmf_send_cmd(dev, GET_SPL, true, ARG_NONE, &table->prop[src][idx].spl);
		amd_pmf_send_cmd(dev, GET_FPPT, true, ARG_NONE, &table->prop[src][idx].fppt);
		amd_pmf_send_cmd(dev, GET_SPPT, true, ARG_NONE, &table->prop[src][idx].sppt);
		amd_pmf_send_cmd(dev, GET_SPPT_APU_ONLY, true, ARG_NONE,
				 &table->prop[src][idx].sppt_apu_only);
		amd_pmf_send_cmd(dev, GET_STT_MIN_LIMIT, true, ARG_NONE,
				 &table->prop[src][idx].stt_min);
		amd_pmf_send_cmd(dev, GET_STT_LIMIT_APU, true, ARG_NONE,
				 (u32 *)&table->prop[src][idx].stt_skin_temp[STT_TEMP_APU]);
		amd_pmf_send_cmd(dev, GET_STT_LIMIT_HS2, true, ARG_NONE,
				 (u32 *)&table->prop[src][idx].stt_skin_temp[STT_TEMP_HS2]);
	}
}

static int amd_pmf_update_sps_power_limits_v2(struct amd_pmf_dev *pdev, int pwr_mode)
{
	int src, index;

	src = amd_pmf_get_power_source();

	switch (pwr_mode) {
	case POWER_MODE_PERFORMANCE:
		index = config_store_v2.sps_idx.power_states[src][POWER_MODE_BEST_PERFORMANCE];
		amd_pmf_update_slider_v2(pdev, index);
		break;
	case POWER_MODE_BALANCED_POWER:
		index = config_store_v2.sps_idx.power_states[src][POWER_MODE_BALANCED];
		amd_pmf_update_slider_v2(pdev, index);
		break;
	case POWER_MODE_POWER_SAVER:
		index = config_store_v2.sps_idx.power_states[src][POWER_MODE_BEST_POWER_EFFICIENCY];
		amd_pmf_update_slider_v2(pdev, index);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int amd_pmf_set_sps_power_limits(struct amd_pmf_dev *pmf)
{
	int mode;

	mode = amd_pmf_get_pprof_modes(pmf);
	if (mode < 0)
		return mode;

	if (pmf->pmf_if_version == PMF_IF_V2)
		return amd_pmf_update_sps_power_limits_v2(pmf, mode);

	amd_pmf_update_slider(pmf, SLIDER_OP_SET, mode, NULL);

	return 0;
}

bool is_pprof_balanced(struct amd_pmf_dev *pmf)
{
	return (pmf->current_profile == PLATFORM_PROFILE_BALANCED) ? true : false;
}

static int amd_pmf_profile_get(struct device *dev,
			       enum platform_profile_option *profile)
{
	struct amd_pmf_dev *pmf = dev_get_drvdata(dev);

	*profile = pmf->current_profile;
	return 0;
}

int amd_pmf_get_pprof_modes(struct amd_pmf_dev *pmf)
{
	int mode;

	switch (pmf->current_profile) {
	case PLATFORM_PROFILE_PERFORMANCE:
	case PLATFORM_PROFILE_BALANCED_PERFORMANCE:
		mode = POWER_MODE_PERFORMANCE;
		break;
	case PLATFORM_PROFILE_BALANCED:
		mode = POWER_MODE_BALANCED_POWER;
		break;
	case PLATFORM_PROFILE_LOW_POWER:
	case PLATFORM_PROFILE_QUIET:
		mode = POWER_MODE_POWER_SAVER;
		break;
	default:
		dev_err(pmf->dev, "Unknown Platform Profile.\n");
		return -EOPNOTSUPP;
	}

	return mode;
}

int amd_pmf_power_slider_update_event(struct amd_pmf_dev *dev)
{
	u8 flag = 0;
	int mode;
	int src;

	mode = amd_pmf_get_pprof_modes(dev);
	if (mode < 0)
		return mode;

	src = amd_pmf_get_power_source();

	if (src == POWER_SOURCE_AC) {
		switch (mode) {
		case POWER_MODE_PERFORMANCE:
			flag |= BIT(AC_BEST_PERF);
			break;
		case POWER_MODE_BALANCED_POWER:
			flag |= BIT(AC_BETTER_PERF);
			break;
		case POWER_MODE_POWER_SAVER:
			flag |= BIT(AC_BETTER_BATTERY);
			break;
		default:
			dev_err(dev->dev, "unsupported platform profile\n");
			return -EOPNOTSUPP;
		}

	} else if (src == POWER_SOURCE_DC) {
		switch (mode) {
		case POWER_MODE_PERFORMANCE:
			flag |= BIT(DC_BEST_PERF);
			break;
		case POWER_MODE_BALANCED_POWER:
			flag |= BIT(DC_BETTER_PERF);
			break;
		case POWER_MODE_POWER_SAVER:
			flag |= BIT(DC_BATTERY_SAVER);
			break;
		default:
			dev_err(dev->dev, "unsupported platform profile\n");
			return -EOPNOTSUPP;
		}
	}

	apmf_os_power_slider_update(dev, flag);

	return 0;
}

static int amd_pmf_profile_set(struct device *dev,
			       enum platform_profile_option profile)
{
	struct amd_pmf_dev *pmf = dev_get_drvdata(dev);
	int ret = 0;

	pmf->current_profile = profile;

	/* Notify EC about the slider position change */
	if (is_apmf_func_supported(pmf, APMF_FUNC_OS_POWER_SLIDER_UPDATE)) {
		ret = amd_pmf_power_slider_update_event(pmf);
		if (ret)
			return ret;
	}

	if (is_apmf_func_supported(pmf, APMF_FUNC_STATIC_SLIDER_GRANULAR)) {
		ret = amd_pmf_set_sps_power_limits(pmf);
		if (ret)
			return ret;
	}

	return 0;
}

static int amd_pmf_hidden_choices(void *drvdata, unsigned long *choices)
{
	set_bit(PLATFORM_PROFILE_QUIET, choices);
	set_bit(PLATFORM_PROFILE_BALANCED_PERFORMANCE, choices);

	return 0;
}

static int amd_pmf_profile_probe(void *drvdata, unsigned long *choices)
{
	set_bit(PLATFORM_PROFILE_LOW_POWER, choices);
	set_bit(PLATFORM_PROFILE_BALANCED, choices);
	set_bit(PLATFORM_PROFILE_PERFORMANCE, choices);

	return 0;
}

static const struct platform_profile_ops amd_pmf_profile_ops = {
	.probe = amd_pmf_profile_probe,
	.hidden_choices = amd_pmf_hidden_choices,
	.profile_get = amd_pmf_profile_get,
	.profile_set = amd_pmf_profile_set,
};

int amd_pmf_init_sps(struct amd_pmf_dev *dev)
{
	dev->current_profile = PLATFORM_PROFILE_BALANCED;

	if (is_apmf_func_supported(dev, APMF_FUNC_STATIC_SLIDER_GRANULAR)) {
		if (dev->pmf_if_version == PMF_IF_V2) {
			amd_pmf_load_defaults_sps_v2(dev);
			amd_pmf_load_apts_defaults_sps_v2(dev);
		} else {
			amd_pmf_load_defaults_sps(dev);
		}

		/* update SPS balanced power mode thermals */
		amd_pmf_set_sps_power_limits(dev);
	}

	/* Create platform_profile structure and register */
	dev->ppdev = devm_platform_profile_register(dev->dev, "amd-pmf", dev,
						    &amd_pmf_profile_ops);
	if (IS_ERR(dev->ppdev))
		dev_err(dev->dev, "Failed to register SPS support, this is most likely an SBIOS bug: %ld\n",
			PTR_ERR(dev->ppdev));

	return PTR_ERR_OR_ZERO(dev->ppdev);
}
