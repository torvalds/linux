// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include "core.h"

int pdsc_setup(struct pdsc *pdsc, bool init)
{
	int err;

	if (init)
		err = pdsc_dev_init(pdsc);
	else
		err = pdsc_dev_reinit(pdsc);
	if (err)
		return err;

	clear_bit(PDSC_S_FW_DEAD, &pdsc->state);
	return 0;
}

void pdsc_teardown(struct pdsc *pdsc, bool removing)
{
	pdsc_devcmd_reset(pdsc);

	if (removing) {
		kfree(pdsc->intr_info);
		pdsc->intr_info = NULL;
	}

	if (pdsc->kern_dbpage) {
		iounmap(pdsc->kern_dbpage);
		pdsc->kern_dbpage = NULL;
	}

	set_bit(PDSC_S_FW_DEAD, &pdsc->state);
}

static void pdsc_fw_down(struct pdsc *pdsc)
{
	if (test_and_set_bit(PDSC_S_FW_DEAD, &pdsc->state)) {
		dev_err(pdsc->dev, "%s: already happening\n", __func__);
		return;
	}

	devlink_health_report(pdsc->fw_reporter, "FW down reported", pdsc);

	pdsc_teardown(pdsc, PDSC_TEARDOWN_RECOVERY);
}

static void pdsc_fw_up(struct pdsc *pdsc)
{
	int err;

	if (!test_bit(PDSC_S_FW_DEAD, &pdsc->state)) {
		dev_err(pdsc->dev, "%s: fw not dead\n", __func__);
		return;
	}

	err = pdsc_setup(pdsc, PDSC_SETUP_RECOVERY);
	if (err)
		goto err_out;

	pdsc->fw_recoveries++;
	devlink_health_reporter_state_update(pdsc->fw_reporter,
					     DEVLINK_HEALTH_REPORTER_STATE_HEALTHY);

	return;

err_out:
	pdsc_teardown(pdsc, PDSC_TEARDOWN_RECOVERY);
}

void pdsc_health_thread(struct work_struct *work)
{
	struct pdsc *pdsc = container_of(work, struct pdsc, health_work);
	unsigned long mask;
	bool healthy;

	mutex_lock(&pdsc->config_lock);

	/* Don't do a check when in a transition state */
	mask = BIT_ULL(PDSC_S_INITING_DRIVER) |
	       BIT_ULL(PDSC_S_STOPPING_DRIVER);
	if (pdsc->state & mask)
		goto out_unlock;

	healthy = pdsc_is_fw_good(pdsc);
	dev_dbg(pdsc->dev, "%s: health %d fw_status %#02x fw_heartbeat %d\n",
		__func__, healthy, pdsc->fw_status, pdsc->last_hb);

	if (test_bit(PDSC_S_FW_DEAD, &pdsc->state)) {
		if (healthy)
			pdsc_fw_up(pdsc);
	} else {
		if (!healthy)
			pdsc_fw_down(pdsc);
	}

	pdsc->fw_generation = pdsc->fw_status & PDS_CORE_FW_STS_F_GENERATION;

out_unlock:
	mutex_unlock(&pdsc->config_lock);
}
