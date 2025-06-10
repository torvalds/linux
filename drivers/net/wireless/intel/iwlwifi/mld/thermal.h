/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_mld_thermal_h__
#define __iwl_mld_thermal_h__

#include "iwl-trans.h"

struct iwl_mld;

#ifdef CONFIG_THERMAL
#include <linux/thermal.h>

/*
 * struct iwl_mld_cooling_device
 * @cur_state: current state
 * @cdev: struct thermal cooling device
 */
struct iwl_mld_cooling_device {
	u32 cur_state;
	struct thermal_cooling_device *cdev;
};

int iwl_mld_config_ctdp(struct iwl_mld *mld, u32 state,
			enum iwl_ctdp_cmd_operation op);
#endif

void iwl_mld_handle_temp_notif(struct iwl_mld *mld, struct iwl_rx_packet *pkt);
void iwl_mld_handle_ct_kill_notif(struct iwl_mld *mld,
				  struct iwl_rx_packet *pkt);
int iwl_mld_config_temp_report_ths(struct iwl_mld *mld);
void iwl_mld_thermal_initialize(struct iwl_mld *mld);
void iwl_mld_thermal_exit(struct iwl_mld *mld);

#endif /* __iwl_mld_thermal_h__ */
