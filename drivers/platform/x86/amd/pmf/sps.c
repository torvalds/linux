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

static struct amd_pmf_static_slider_granular config_store;

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
				 config_store.prop[src][idx].stt_skin_temp[STT_TEMP_APU], NULL);
		amd_pmf_send_cmd(dev, SET_STT_LIMIT_HS2, false,
				 config_store.prop[src][idx].stt_skin_temp[STT_TEMP_HS2], NULL);
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

int amd_pmf_set_sps_power_limits(struct amd_pmf_dev *pmf)
{
	int mode;

	mode = amd_pmf_get_pprof_modes(pmf);
	if (mode < 0)
		return mode;

	amd_pmf_update_slider(pmf, SLIDER_OP_SET, mode, NULL);

	return 0;
}

bool is_pprof_balanced(struct amd_pmf_dev *pmf)
{
	return (pmf->current_profile == PLATFORM_PROFILE_BALANCED) ? true : false;
}

static int amd_pmf_profile_get(struct platform_profile_handler *pprof,
			       enum platform_profile_option *profile)
{
	struct amd_pmf_dev *pmf = container_of(pprof, struct amd_pmf_dev, pprof);

	*profile = pmf->current_profile;
	return 0;
}

int amd_pmf_get_pprof_modes(struct amd_pmf_dev *pmf)
{
	int mode;

	switch (pmf->current_profile) {
	case PLATFORM_PROFILE_PERFORMANCE:
		mode = POWER_MODE_PERFORMANCE;
		break;
	case PLATFORM_PROFILE_BALANCED:
		mode = POWER_MODE_BALANCED_POWER;
		break;
	case PLATFORM_PROFILE_LOW_POWER:
		mode = POWER_MODE_POWER_SAVER;
		break;
	default:
		dev_err(pmf->dev, "Unknown Platform Profile.\n");
		return -EOPNOTSUPP;
	}

	return mode;
}

static int amd_pmf_profile_set(struct platform_profile_handler *pprof,
			       enum platform_profile_option profile)
{
	struct amd_pmf_dev *pmf = container_of(pprof, struct amd_pmf_dev, pprof);

	pmf->current_profile = profile;

	return amd_pmf_set_sps_power_limits(pmf);
}

int amd_pmf_init_sps(struct amd_pmf_dev *dev)
{
	int err;

	dev->current_profile = PLATFORM_PROFILE_BALANCED;
	amd_pmf_load_defaults_sps(dev);

	/* update SPS balanced power mode thermals */
	amd_pmf_set_sps_power_limits(dev);

	dev->pprof.profile_get = amd_pmf_profile_get;
	dev->pprof.profile_set = amd_pmf_profile_set;

	/* Setup supported modes */
	set_bit(PLATFORM_PROFILE_LOW_POWER, dev->pprof.choices);
	set_bit(PLATFORM_PROFILE_BALANCED, dev->pprof.choices);
	set_bit(PLATFORM_PROFILE_PERFORMANCE, dev->pprof.choices);

	/* Create platform_profile structure and register */
	err = platform_profile_register(&dev->pprof);
	if (err)
		dev_err(dev->dev, "Failed to register SPS support, this is most likely an SBIOS bug: %d\n",
			err);

	return err;
}

void amd_pmf_deinit_sps(struct amd_pmf_dev *dev)
{
	platform_profile_remove();
}
