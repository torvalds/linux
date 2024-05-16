// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Platform Management Framework Driver
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#include <linux/acpi.h>
#include <linux/workqueue.h>
#include "pmf.h"

static struct auto_mode_mode_config config_store;
static const char *state_as_str(unsigned int state);

#ifdef CONFIG_AMD_PMF_DEBUG
static void amd_pmf_dump_auto_mode_defaults(struct auto_mode_mode_config *data)
{
	struct auto_mode_mode_settings *its_mode;

	pr_debug("Auto Mode Data - BEGIN\n");

	/* time constant */
	pr_debug("balanced_to_perf: %u ms\n",
		 data->transition[AUTO_TRANSITION_TO_PERFORMANCE].time_constant);
	pr_debug("perf_to_balanced: %u ms\n",
		 data->transition[AUTO_TRANSITION_FROM_PERFORMANCE_TO_BALANCE].time_constant);
	pr_debug("quiet_to_balanced: %u ms\n",
		 data->transition[AUTO_TRANSITION_FROM_QUIET_TO_BALANCE].time_constant);
	pr_debug("balanced_to_quiet: %u ms\n",
		 data->transition[AUTO_TRANSITION_TO_QUIET].time_constant);

	/* power floor */
	pr_debug("pfloor_perf: %u mW\n", data->mode_set[AUTO_PERFORMANCE].power_floor);
	pr_debug("pfloor_balanced: %u mW\n", data->mode_set[AUTO_BALANCE].power_floor);
	pr_debug("pfloor_quiet: %u mW\n", data->mode_set[AUTO_QUIET].power_floor);

	/* Power delta for mode change */
	pr_debug("pd_balanced_to_perf: %u mW\n",
		 data->transition[AUTO_TRANSITION_TO_PERFORMANCE].power_delta);
	pr_debug("pd_perf_to_balanced: %u mW\n",
		 data->transition[AUTO_TRANSITION_FROM_PERFORMANCE_TO_BALANCE].power_delta);
	pr_debug("pd_quiet_to_balanced: %u mW\n",
		 data->transition[AUTO_TRANSITION_FROM_QUIET_TO_BALANCE].power_delta);
	pr_debug("pd_balanced_to_quiet: %u mW\n",
		 data->transition[AUTO_TRANSITION_TO_QUIET].power_delta);

	/* skin temperature limits */
	its_mode = &data->mode_set[AUTO_PERFORMANCE_ON_LAP];
	pr_debug("stt_apu_perf_on_lap: %u C\n",
		 its_mode->power_control.stt_skin_temp[STT_TEMP_APU]);
	pr_debug("stt_hs2_perf_on_lap: %u C\n",
		 its_mode->power_control.stt_skin_temp[STT_TEMP_HS2]);
	pr_debug("stt_min_limit_perf_on_lap: %u mW\n", its_mode->power_control.stt_min);

	its_mode = &data->mode_set[AUTO_PERFORMANCE];
	pr_debug("stt_apu_perf: %u C\n", its_mode->power_control.stt_skin_temp[STT_TEMP_APU]);
	pr_debug("stt_hs2_perf: %u C\n", its_mode->power_control.stt_skin_temp[STT_TEMP_HS2]);
	pr_debug("stt_min_limit_perf: %u mW\n", its_mode->power_control.stt_min);

	its_mode = &data->mode_set[AUTO_BALANCE];
	pr_debug("stt_apu_balanced: %u C\n", its_mode->power_control.stt_skin_temp[STT_TEMP_APU]);
	pr_debug("stt_hs2_balanced: %u C\n", its_mode->power_control.stt_skin_temp[STT_TEMP_HS2]);
	pr_debug("stt_min_limit_balanced: %u mW\n", its_mode->power_control.stt_min);

	its_mode = &data->mode_set[AUTO_QUIET];
	pr_debug("stt_apu_quiet: %u C\n", its_mode->power_control.stt_skin_temp[STT_TEMP_APU]);
	pr_debug("stt_hs2_quiet: %u C\n", its_mode->power_control.stt_skin_temp[STT_TEMP_HS2]);
	pr_debug("stt_min_limit_quiet: %u mW\n", its_mode->power_control.stt_min);

	/* SPL based power limits */
	its_mode = &data->mode_set[AUTO_PERFORMANCE_ON_LAP];
	pr_debug("fppt_perf_on_lap: %u mW\n", its_mode->power_control.fppt);
	pr_debug("sppt_perf_on_lap: %u mW\n", its_mode->power_control.sppt);
	pr_debug("spl_perf_on_lap: %u mW\n", its_mode->power_control.spl);
	pr_debug("sppt_apu_only_perf_on_lap: %u mW\n", its_mode->power_control.sppt_apu_only);

	its_mode = &data->mode_set[AUTO_PERFORMANCE];
	pr_debug("fppt_perf: %u mW\n", its_mode->power_control.fppt);
	pr_debug("sppt_perf: %u mW\n", its_mode->power_control.sppt);
	pr_debug("spl_perf: %u mW\n", its_mode->power_control.spl);
	pr_debug("sppt_apu_only_perf: %u mW\n", its_mode->power_control.sppt_apu_only);

	its_mode = &data->mode_set[AUTO_BALANCE];
	pr_debug("fppt_balanced: %u mW\n", its_mode->power_control.fppt);
	pr_debug("sppt_balanced: %u mW\n", its_mode->power_control.sppt);
	pr_debug("spl_balanced: %u mW\n", its_mode->power_control.spl);
	pr_debug("sppt_apu_only_balanced: %u mW\n", its_mode->power_control.sppt_apu_only);

	its_mode = &data->mode_set[AUTO_QUIET];
	pr_debug("fppt_quiet: %u mW\n", its_mode->power_control.fppt);
	pr_debug("sppt_quiet: %u mW\n", its_mode->power_control.sppt);
	pr_debug("spl_quiet: %u mW\n", its_mode->power_control.spl);
	pr_debug("sppt_apu_only_quiet: %u mW\n", its_mode->power_control.sppt_apu_only);

	/* Fan ID */
	pr_debug("fan_id_perf: %lu\n",
		 data->mode_set[AUTO_PERFORMANCE].fan_control.fan_id);
	pr_debug("fan_id_balanced: %lu\n",
		 data->mode_set[AUTO_BALANCE].fan_control.fan_id);
	pr_debug("fan_id_quiet: %lu\n",
		 data->mode_set[AUTO_QUIET].fan_control.fan_id);

	pr_debug("Auto Mode Data - END\n");
}
#else
static void amd_pmf_dump_auto_mode_defaults(struct auto_mode_mode_config *data) {}
#endif

static void amd_pmf_set_automode(struct amd_pmf_dev *dev, int idx,
				 struct auto_mode_mode_config *table)
{
	struct power_table_control *pwr_ctrl = &config_store.mode_set[idx].power_control;

	amd_pmf_send_cmd(dev, SET_SPL, false, pwr_ctrl->spl, NULL);
	amd_pmf_send_cmd(dev, SET_FPPT, false, pwr_ctrl->fppt, NULL);
	amd_pmf_send_cmd(dev, SET_SPPT, false, pwr_ctrl->sppt, NULL);
	amd_pmf_send_cmd(dev, SET_SPPT_APU_ONLY, false, pwr_ctrl->sppt_apu_only, NULL);
	amd_pmf_send_cmd(dev, SET_STT_MIN_LIMIT, false, pwr_ctrl->stt_min, NULL);
	amd_pmf_send_cmd(dev, SET_STT_LIMIT_APU, false,
			 pwr_ctrl->stt_skin_temp[STT_TEMP_APU], NULL);
	amd_pmf_send_cmd(dev, SET_STT_LIMIT_HS2, false,
			 pwr_ctrl->stt_skin_temp[STT_TEMP_HS2], NULL);

	if (is_apmf_func_supported(dev, APMF_FUNC_SET_FAN_IDX))
		apmf_update_fan_idx(dev, config_store.mode_set[idx].fan_control.manual,
				    config_store.mode_set[idx].fan_control.fan_id);
}

static int amd_pmf_get_moving_avg(struct amd_pmf_dev *pdev, int socket_power)
{
	int i, total = 0;

	if (pdev->socket_power_history_idx == -1) {
		for (i = 0; i < AVG_SAMPLE_SIZE; i++)
			pdev->socket_power_history[i] = socket_power;
	}

	pdev->socket_power_history_idx = (pdev->socket_power_history_idx + 1) % AVG_SAMPLE_SIZE;
	pdev->socket_power_history[pdev->socket_power_history_idx] = socket_power;

	for (i = 0; i < AVG_SAMPLE_SIZE; i++)
		total += pdev->socket_power_history[i];

	return total / AVG_SAMPLE_SIZE;
}

void amd_pmf_trans_automode(struct amd_pmf_dev *dev, int socket_power, ktime_t time_elapsed_ms)
{
	int avg_power = 0;
	bool update = false;
	int i, j;

	/* Get the average moving average computed by auto mode algorithm */
	avg_power = amd_pmf_get_moving_avg(dev, socket_power);

	for (i = 0; i < AUTO_TRANSITION_MAX; i++) {
		if ((config_store.transition[i].shifting_up && avg_power >=
		     config_store.transition[i].power_threshold) ||
		    (!config_store.transition[i].shifting_up && avg_power <=
		     config_store.transition[i].power_threshold)) {
			if (config_store.transition[i].timer <
			    config_store.transition[i].time_constant)
				config_store.transition[i].timer += time_elapsed_ms;
		} else {
			config_store.transition[i].timer = 0;
		}

		if (config_store.transition[i].timer >=
		    config_store.transition[i].time_constant &&
		    !config_store.transition[i].applied) {
			config_store.transition[i].applied = true;
			update = true;
		} else if (config_store.transition[i].timer <=
			   config_store.transition[i].time_constant &&
			   config_store.transition[i].applied) {
			config_store.transition[i].applied = false;
			update = true;
		}

#ifdef CONFIG_AMD_PMF_DEBUG
		dev_dbg(dev->dev, "[AUTO MODE] average_power : %d mW mode: %s\n", avg_power,
			state_as_str(config_store.current_mode));

		dev_dbg(dev->dev, "[AUTO MODE] time: %lld ms timer: %u ms tc: %u ms\n",
			time_elapsed_ms, config_store.transition[i].timer,
			config_store.transition[i].time_constant);

		dev_dbg(dev->dev, "[AUTO MODE] shiftup: %u pt: %u mW pf: %u mW pd: %u mW\n",
			config_store.transition[i].shifting_up,
			config_store.transition[i].power_threshold,
			config_store.mode_set[i].power_floor,
			config_store.transition[i].power_delta);
#endif
	}

	dev_dbg(dev->dev, "[AUTO_MODE] avg power: %u mW mode: %s\n", avg_power,
		state_as_str(config_store.current_mode));

#ifdef CONFIG_AMD_PMF_DEBUG
	dev_dbg(dev->dev, "[AUTO MODE] priority1: %u priority2: %u priority3: %u priority4: %u\n",
		config_store.transition[0].applied,
		config_store.transition[1].applied,
		config_store.transition[2].applied,
		config_store.transition[3].applied);
#endif

	if (update) {
		for (j = 0; j < AUTO_TRANSITION_MAX; j++) {
			/* Apply the mode with highest priority indentified */
			if (config_store.transition[j].applied) {
				if (config_store.current_mode !=
				    config_store.transition[j].target_mode) {
					config_store.current_mode =
							config_store.transition[j].target_mode;
					dev_dbg(dev->dev, "[AUTO_MODE] moving to mode:%s\n",
						state_as_str(config_store.current_mode));
					amd_pmf_set_automode(dev, config_store.current_mode, NULL);
				}
				break;
			}
		}
	}
}

void amd_pmf_update_2_cql(struct amd_pmf_dev *dev, bool is_cql_event)
{
	int mode = config_store.current_mode;

	config_store.transition[AUTO_TRANSITION_TO_PERFORMANCE].target_mode =
				   is_cql_event ? AUTO_PERFORMANCE_ON_LAP : AUTO_PERFORMANCE;

	if ((mode == AUTO_PERFORMANCE || mode == AUTO_PERFORMANCE_ON_LAP) &&
	    mode != config_store.transition[AUTO_TRANSITION_TO_PERFORMANCE].target_mode) {
		mode = config_store.transition[AUTO_TRANSITION_TO_PERFORMANCE].target_mode;
		amd_pmf_set_automode(dev, mode, NULL);
	}
	dev_dbg(dev->dev, "updated CQL thermals\n");
}

static void amd_pmf_get_power_threshold(void)
{
	config_store.transition[AUTO_TRANSITION_TO_QUIET].power_threshold =
				config_store.mode_set[AUTO_BALANCE].power_floor -
				config_store.transition[AUTO_TRANSITION_TO_QUIET].power_delta;

	config_store.transition[AUTO_TRANSITION_TO_PERFORMANCE].power_threshold =
				config_store.mode_set[AUTO_BALANCE].power_floor -
				config_store.transition[AUTO_TRANSITION_TO_PERFORMANCE].power_delta;

	config_store.transition[AUTO_TRANSITION_FROM_QUIET_TO_BALANCE].power_threshold =
			config_store.mode_set[AUTO_QUIET].power_floor -
			config_store.transition[AUTO_TRANSITION_FROM_QUIET_TO_BALANCE].power_delta;

	config_store.transition[AUTO_TRANSITION_FROM_PERFORMANCE_TO_BALANCE].power_threshold =
		config_store.mode_set[AUTO_PERFORMANCE].power_floor -
		config_store.transition[AUTO_TRANSITION_FROM_PERFORMANCE_TO_BALANCE].power_delta;

#ifdef CONFIG_AMD_PMF_DEBUG
	pr_debug("[AUTO MODE TO_QUIET] pt: %u mW pf: %u mW pd: %u mW\n",
		 config_store.transition[AUTO_TRANSITION_TO_QUIET].power_threshold,
		 config_store.mode_set[AUTO_BALANCE].power_floor,
		 config_store.transition[AUTO_TRANSITION_TO_QUIET].power_delta);

	pr_debug("[AUTO MODE TO_PERFORMANCE] pt: %u mW pf: %u mW pd: %u mW\n",
		 config_store.transition[AUTO_TRANSITION_TO_PERFORMANCE].power_threshold,
		 config_store.mode_set[AUTO_BALANCE].power_floor,
		 config_store.transition[AUTO_TRANSITION_TO_PERFORMANCE].power_delta);

	pr_debug("[AUTO MODE QUIET_TO_BALANCE] pt: %u mW pf: %u mW pd: %u mW\n",
		 config_store.transition[AUTO_TRANSITION_FROM_QUIET_TO_BALANCE]
		 .power_threshold,
		 config_store.mode_set[AUTO_QUIET].power_floor,
		 config_store.transition[AUTO_TRANSITION_FROM_QUIET_TO_BALANCE].power_delta);

	pr_debug("[AUTO MODE PERFORMANCE_TO_BALANCE] pt: %u mW pf: %u mW pd: %u mW\n",
		 config_store.transition[AUTO_TRANSITION_FROM_PERFORMANCE_TO_BALANCE]
		 .power_threshold,
		 config_store.mode_set[AUTO_PERFORMANCE].power_floor,
		 config_store.transition[AUTO_TRANSITION_FROM_PERFORMANCE_TO_BALANCE].power_delta);
#endif
}

static const char *state_as_str(unsigned int state)
{
	switch (state) {
	case AUTO_QUIET:
		return "QUIET";
	case AUTO_BALANCE:
		return "BALANCED";
	case AUTO_PERFORMANCE_ON_LAP:
		return "ON_LAP";
	case AUTO_PERFORMANCE:
		return "PERFORMANCE";
	default:
		return "Unknown Auto Mode State";
	}
}

static void amd_pmf_load_defaults_auto_mode(struct amd_pmf_dev *dev)
{
	struct apmf_auto_mode output;
	struct power_table_control *pwr_ctrl;
	int i;

	apmf_get_auto_mode_def(dev, &output);
	/* time constant */
	config_store.transition[AUTO_TRANSITION_TO_QUIET].time_constant =
								output.balanced_to_quiet;
	config_store.transition[AUTO_TRANSITION_TO_PERFORMANCE].time_constant =
								output.balanced_to_perf;
	config_store.transition[AUTO_TRANSITION_FROM_QUIET_TO_BALANCE].time_constant =
								output.quiet_to_balanced;
	config_store.transition[AUTO_TRANSITION_FROM_PERFORMANCE_TO_BALANCE].time_constant =
								output.perf_to_balanced;

	/* power floor */
	config_store.mode_set[AUTO_QUIET].power_floor = output.pfloor_quiet;
	config_store.mode_set[AUTO_BALANCE].power_floor = output.pfloor_balanced;
	config_store.mode_set[AUTO_PERFORMANCE].power_floor = output.pfloor_perf;
	config_store.mode_set[AUTO_PERFORMANCE_ON_LAP].power_floor = output.pfloor_perf;

	/* Power delta for mode change */
	config_store.transition[AUTO_TRANSITION_TO_QUIET].power_delta =
								output.pd_balanced_to_quiet;
	config_store.transition[AUTO_TRANSITION_TO_PERFORMANCE].power_delta =
								output.pd_balanced_to_perf;
	config_store.transition[AUTO_TRANSITION_FROM_QUIET_TO_BALANCE].power_delta =
								output.pd_quiet_to_balanced;
	config_store.transition[AUTO_TRANSITION_FROM_PERFORMANCE_TO_BALANCE].power_delta =
								output.pd_perf_to_balanced;

	/* Power threshold */
	amd_pmf_get_power_threshold();

	/* skin temperature limits */
	pwr_ctrl = &config_store.mode_set[AUTO_QUIET].power_control;
	pwr_ctrl->spl = output.spl_quiet;
	pwr_ctrl->sppt = output.sppt_quiet;
	pwr_ctrl->fppt = output.fppt_quiet;
	pwr_ctrl->sppt_apu_only = output.sppt_apu_only_quiet;
	pwr_ctrl->stt_min = output.stt_min_limit_quiet;
	pwr_ctrl->stt_skin_temp[STT_TEMP_APU] = output.stt_apu_quiet;
	pwr_ctrl->stt_skin_temp[STT_TEMP_HS2] = output.stt_hs2_quiet;

	pwr_ctrl = &config_store.mode_set[AUTO_BALANCE].power_control;
	pwr_ctrl->spl = output.spl_balanced;
	pwr_ctrl->sppt = output.sppt_balanced;
	pwr_ctrl->fppt = output.fppt_balanced;
	pwr_ctrl->sppt_apu_only = output.sppt_apu_only_balanced;
	pwr_ctrl->stt_min = output.stt_min_limit_balanced;
	pwr_ctrl->stt_skin_temp[STT_TEMP_APU] = output.stt_apu_balanced;
	pwr_ctrl->stt_skin_temp[STT_TEMP_HS2] = output.stt_hs2_balanced;

	pwr_ctrl = &config_store.mode_set[AUTO_PERFORMANCE].power_control;
	pwr_ctrl->spl = output.spl_perf;
	pwr_ctrl->sppt = output.sppt_perf;
	pwr_ctrl->fppt = output.fppt_perf;
	pwr_ctrl->sppt_apu_only = output.sppt_apu_only_perf;
	pwr_ctrl->stt_min = output.stt_min_limit_perf;
	pwr_ctrl->stt_skin_temp[STT_TEMP_APU] = output.stt_apu_perf;
	pwr_ctrl->stt_skin_temp[STT_TEMP_HS2] = output.stt_hs2_perf;

	pwr_ctrl = &config_store.mode_set[AUTO_PERFORMANCE_ON_LAP].power_control;
	pwr_ctrl->spl = output.spl_perf_on_lap;
	pwr_ctrl->sppt = output.sppt_perf_on_lap;
	pwr_ctrl->fppt = output.fppt_perf_on_lap;
	pwr_ctrl->sppt_apu_only = output.sppt_apu_only_perf_on_lap;
	pwr_ctrl->stt_min = output.stt_min_limit_perf_on_lap;
	pwr_ctrl->stt_skin_temp[STT_TEMP_APU] = output.stt_apu_perf_on_lap;
	pwr_ctrl->stt_skin_temp[STT_TEMP_HS2] = output.stt_hs2_perf_on_lap;

	/* Fan ID */
	config_store.mode_set[AUTO_QUIET].fan_control.fan_id = output.fan_id_quiet;
	config_store.mode_set[AUTO_BALANCE].fan_control.fan_id = output.fan_id_balanced;
	config_store.mode_set[AUTO_PERFORMANCE].fan_control.fan_id = output.fan_id_perf;
	config_store.mode_set[AUTO_PERFORMANCE_ON_LAP].fan_control.fan_id =
									output.fan_id_perf;

	config_store.transition[AUTO_TRANSITION_TO_QUIET].target_mode = AUTO_QUIET;
	config_store.transition[AUTO_TRANSITION_TO_PERFORMANCE].target_mode =
								AUTO_PERFORMANCE;
	config_store.transition[AUTO_TRANSITION_FROM_QUIET_TO_BALANCE].target_mode =
									AUTO_BALANCE;
	config_store.transition[AUTO_TRANSITION_FROM_PERFORMANCE_TO_BALANCE].target_mode =
									AUTO_BALANCE;

	config_store.transition[AUTO_TRANSITION_TO_QUIET].shifting_up = false;
	config_store.transition[AUTO_TRANSITION_TO_PERFORMANCE].shifting_up = true;
	config_store.transition[AUTO_TRANSITION_FROM_QUIET_TO_BALANCE].shifting_up = true;
	config_store.transition[AUTO_TRANSITION_FROM_PERFORMANCE_TO_BALANCE].shifting_up =
										false;

	for (i = 0 ; i < AUTO_MODE_MAX ; i++) {
		if (config_store.mode_set[i].fan_control.fan_id == FAN_INDEX_AUTO)
			config_store.mode_set[i].fan_control.manual = false;
		else
			config_store.mode_set[i].fan_control.manual = true;
	}

	/* set to initial default values */
	config_store.current_mode = AUTO_BALANCE;
	dev->socket_power_history_idx = -1;

	amd_pmf_dump_auto_mode_defaults(&config_store);
}

int amd_pmf_reset_amt(struct amd_pmf_dev *dev)
{
	/*
	 * OEM BIOS implementation guide says that if the auto mode is enabled
	 * the platform_profile registration shall be done by the OEM driver.
	 * There could be cases where both static slider and auto mode BIOS
	 * functions are enabled, in that case enable static slider updates
	 * only if it advertised as supported.
	 */

	if (is_apmf_func_supported(dev, APMF_FUNC_STATIC_SLIDER_GRANULAR)) {
		dev_dbg(dev->dev, "resetting AMT thermals\n");
		amd_pmf_set_sps_power_limits(dev);
	}
	return 0;
}

void amd_pmf_handle_amt(struct amd_pmf_dev *dev)
{
	amd_pmf_set_automode(dev, config_store.current_mode, NULL);
}

void amd_pmf_deinit_auto_mode(struct amd_pmf_dev *dev)
{
	cancel_delayed_work_sync(&dev->work_buffer);
}

void amd_pmf_init_auto_mode(struct amd_pmf_dev *dev)
{
	amd_pmf_load_defaults_auto_mode(dev);
	amd_pmf_init_metrics_table(dev);
}
