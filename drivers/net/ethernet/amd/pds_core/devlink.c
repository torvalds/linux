// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include "core.h"

int pdsc_fw_reporter_diagnose(struct devlink_health_reporter *reporter,
			      struct devlink_fmsg *fmsg,
			      struct netlink_ext_ack *extack)
{
	struct pdsc *pdsc = devlink_health_reporter_priv(reporter);
	int err;

	mutex_lock(&pdsc->config_lock);

	if (test_bit(PDSC_S_FW_DEAD, &pdsc->state))
		err = devlink_fmsg_string_pair_put(fmsg, "Status", "dead");
	else if (!pdsc_is_fw_good(pdsc))
		err = devlink_fmsg_string_pair_put(fmsg, "Status", "unhealthy");
	else
		err = devlink_fmsg_string_pair_put(fmsg, "Status", "healthy");

	mutex_unlock(&pdsc->config_lock);

	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "State",
					pdsc->fw_status &
						~PDS_CORE_FW_STS_F_GENERATION);
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "Generation",
					pdsc->fw_generation >> 4);
	if (err)
		return err;

	return devlink_fmsg_u32_pair_put(fmsg, "Recoveries",
					 pdsc->fw_recoveries);
}
