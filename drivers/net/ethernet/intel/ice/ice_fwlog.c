// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022, Intel Corporation. */

#include "ice.h"
#include "ice_common.h"
#include "ice_fwlog.h"

/**
 * ice_fwlog_init - Initialize FW logging configuration
 * @hw: pointer to the HW structure
 *
 * This function should be called on driver initialization during
 * ice_init_hw().
 */
int ice_fwlog_init(struct ice_hw *hw)
{
	/* only support fw log commands on PF 0 */
	if (hw->bus.func)
		return -EINVAL;

	ice_fwlog_set_supported(hw);

	if (ice_fwlog_supported(hw)) {
		int status;

		/* read the current config from the FW and store it */
		status = ice_fwlog_get(hw, &hw->fwlog_cfg);
		if (status)
			return status;

		ice_debugfs_fwlog_init(hw->back);
	} else {
		dev_warn(ice_hw_to_dev(hw), "FW logging is not supported in this NVM image. Please update the NVM to get FW log support\n");
	}

	return 0;
}

/**
 * ice_fwlog_deinit - unroll FW logging configuration
 * @hw: pointer to the HW structure
 *
 * This function should be called in ice_deinit_hw().
 */
void ice_fwlog_deinit(struct ice_hw *hw)
{
	struct ice_pf *pf = hw->back;
	int status;

	/* only support fw log commands on PF 0 */
	if (hw->bus.func)
		return;

	/* make sure FW logging is disabled to not put the FW in a weird state
	 * for the next driver load
	 */
	hw->fwlog_cfg.options &= ~ICE_FWLOG_OPTION_ARQ_ENA;
	status = ice_fwlog_set(hw, &hw->fwlog_cfg);
	if (status)
		dev_warn(ice_hw_to_dev(hw), "Unable to turn off FW logging, status: %d\n",
			 status);

	kfree(pf->ice_debugfs_pf_fwlog_modules);

	pf->ice_debugfs_pf_fwlog_modules = NULL;
}

/**
 * ice_fwlog_supported - Cached for whether FW supports FW logging or not
 * @hw: pointer to the HW structure
 *
 * This will always return false if called before ice_init_hw(), so it must be
 * called after ice_init_hw().
 */
bool ice_fwlog_supported(struct ice_hw *hw)
{
	return hw->fwlog_supported;
}

/**
 * ice_aq_fwlog_set - Set FW logging configuration AQ command (0xFF30)
 * @hw: pointer to the HW structure
 * @entries: entries to configure
 * @num_entries: number of @entries
 * @options: options from ice_fwlog_cfg->options structure
 * @log_resolution: logging resolution
 */
static int
ice_aq_fwlog_set(struct ice_hw *hw, struct ice_fwlog_module_entry *entries,
		 u16 num_entries, u16 options, u16 log_resolution)
{
	struct ice_aqc_fw_log_cfg_resp *fw_modules;
	struct ice_aqc_fw_log *cmd;
	struct ice_aq_desc desc;
	int status;
	int i;

	fw_modules = kcalloc(num_entries, sizeof(*fw_modules), GFP_KERNEL);
	if (!fw_modules)
		return -ENOMEM;

	for (i = 0; i < num_entries; i++) {
		fw_modules[i].module_identifier =
			cpu_to_le16(entries[i].module_id);
		fw_modules[i].log_level = entries[i].log_level;
	}

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_fw_logs_config);
	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	cmd = &desc.params.fw_log;

	cmd->cmd_flags = ICE_AQC_FW_LOG_CONF_SET_VALID;
	cmd->ops.cfg.log_resolution = cpu_to_le16(log_resolution);
	cmd->ops.cfg.mdl_cnt = cpu_to_le16(num_entries);

	if (options & ICE_FWLOG_OPTION_ARQ_ENA)
		cmd->cmd_flags |= ICE_AQC_FW_LOG_CONF_AQ_EN;
	if (options & ICE_FWLOG_OPTION_UART_ENA)
		cmd->cmd_flags |= ICE_AQC_FW_LOG_CONF_UART_EN;

	status = ice_aq_send_cmd(hw, &desc, fw_modules,
				 sizeof(*fw_modules) * num_entries,
				 NULL);

	kfree(fw_modules);

	return status;
}

/**
 * ice_fwlog_set - Set the firmware logging settings
 * @hw: pointer to the HW structure
 * @cfg: config used to set firmware logging
 *
 * This function should be called whenever the driver needs to set the firmware
 * logging configuration. It can be called on initialization, reset, or during
 * runtime.
 *
 * If the PF wishes to receive FW logging then it must register via
 * ice_fwlog_register. Note, that ice_fwlog_register does not need to be called
 * for init.
 */
int ice_fwlog_set(struct ice_hw *hw, struct ice_fwlog_cfg *cfg)
{
	if (!ice_fwlog_supported(hw))
		return -EOPNOTSUPP;

	return ice_aq_fwlog_set(hw, cfg->module_entries,
				ICE_AQC_FW_LOG_ID_MAX, cfg->options,
				cfg->log_resolution);
}

/**
 * ice_aq_fwlog_get - Get the current firmware logging configuration (0xFF32)
 * @hw: pointer to the HW structure
 * @cfg: firmware logging configuration to populate
 */
static int ice_aq_fwlog_get(struct ice_hw *hw, struct ice_fwlog_cfg *cfg)
{
	struct ice_aqc_fw_log_cfg_resp *fw_modules;
	struct ice_aqc_fw_log *cmd;
	struct ice_aq_desc desc;
	u16 module_id_cnt;
	int status;
	void *buf;
	int i;

	memset(cfg, 0, sizeof(*cfg));

	buf = kzalloc(ICE_AQ_MAX_BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_fw_logs_query);
	cmd = &desc.params.fw_log;

	cmd->cmd_flags = ICE_AQC_FW_LOG_AQ_QUERY;

	status = ice_aq_send_cmd(hw, &desc, buf, ICE_AQ_MAX_BUF_LEN, NULL);
	if (status) {
		ice_debug(hw, ICE_DBG_FW_LOG, "Failed to get FW log configuration\n");
		goto status_out;
	}

	module_id_cnt = le16_to_cpu(cmd->ops.cfg.mdl_cnt);
	if (module_id_cnt < ICE_AQC_FW_LOG_ID_MAX) {
		ice_debug(hw, ICE_DBG_FW_LOG, "FW returned less than the expected number of FW log module IDs\n");
	} else if (module_id_cnt > ICE_AQC_FW_LOG_ID_MAX) {
		ice_debug(hw, ICE_DBG_FW_LOG, "FW returned more than expected number of FW log module IDs, setting module_id_cnt to software expected max %u\n",
			  ICE_AQC_FW_LOG_ID_MAX);
		module_id_cnt = ICE_AQC_FW_LOG_ID_MAX;
	}

	cfg->log_resolution = le16_to_cpu(cmd->ops.cfg.log_resolution);
	if (cmd->cmd_flags & ICE_AQC_FW_LOG_CONF_AQ_EN)
		cfg->options |= ICE_FWLOG_OPTION_ARQ_ENA;
	if (cmd->cmd_flags & ICE_AQC_FW_LOG_CONF_UART_EN)
		cfg->options |= ICE_FWLOG_OPTION_UART_ENA;

	fw_modules = (struct ice_aqc_fw_log_cfg_resp *)buf;

	for (i = 0; i < module_id_cnt; i++) {
		struct ice_aqc_fw_log_cfg_resp *fw_module = &fw_modules[i];

		cfg->module_entries[i].module_id =
			le16_to_cpu(fw_module->module_identifier);
		cfg->module_entries[i].log_level = fw_module->log_level;
	}

status_out:
	kfree(buf);
	return status;
}

/**
 * ice_fwlog_get - Get the firmware logging settings
 * @hw: pointer to the HW structure
 * @cfg: config to populate based on current firmware logging settings
 */
int ice_fwlog_get(struct ice_hw *hw, struct ice_fwlog_cfg *cfg)
{
	if (!ice_fwlog_supported(hw))
		return -EOPNOTSUPP;

	return ice_aq_fwlog_get(hw, cfg);
}

/**
 * ice_fwlog_set_supported - Set if FW logging is supported by FW
 * @hw: pointer to the HW struct
 *
 * If FW returns success to the ice_aq_fwlog_get call then it supports FW
 * logging, else it doesn't. Set the fwlog_supported flag accordingly.
 *
 * This function is only meant to be called during driver init to determine if
 * the FW support FW logging.
 */
void ice_fwlog_set_supported(struct ice_hw *hw)
{
	struct ice_fwlog_cfg *cfg;
	int status;

	hw->fwlog_supported = false;

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return;

	/* don't call ice_fwlog_get() because that would check to see if FW
	 * logging is supported which is what the driver is determining now
	 */
	status = ice_aq_fwlog_get(hw, cfg);
	if (status)
		ice_debug(hw, ICE_DBG_FW_LOG, "ice_aq_fwlog_get failed, FW logging is not supported on this version of FW, status %d\n",
			  status);
	else
		hw->fwlog_supported = true;

	kfree(cfg);
}
