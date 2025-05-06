// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#ifdef CONFIG_THERMAL
#include <linux/sort.h>
#include <linux/thermal.h>
#endif

#include "fw/api/phy.h"

#include "thermal.h"
#include "mld.h"
#include "hcmd.h"

#define IWL_MLD_CT_KILL_DURATION (5 * HZ)

void iwl_mld_handle_ct_kill_notif(struct iwl_mld *mld,
				  struct iwl_rx_packet *pkt)
{
	const struct ct_kill_notif *notif = (const void *)pkt->data;

	IWL_ERR(mld,
		"CT Kill notification: temp = %d, DTS = 0x%x, Scheme 0x%x - Enter CT Kill\n",
		le16_to_cpu(notif->temperature), notif->dts,
		notif->scheme);

	iwl_mld_set_ctkill(mld, true);

	wiphy_delayed_work_queue(mld->wiphy, &mld->ct_kill_exit_wk,
				 round_jiffies_relative(IWL_MLD_CT_KILL_DURATION));
}

static void iwl_mld_exit_ctkill(struct wiphy *wiphy, struct wiphy_work *wk)
{
	struct iwl_mld *mld;

	mld = container_of(wk, struct iwl_mld, ct_kill_exit_wk.work);

	IWL_ERR(mld, "Exit CT Kill\n");
	iwl_mld_set_ctkill(mld, false);
}

void iwl_mld_handle_temp_notif(struct iwl_mld *mld, struct iwl_rx_packet *pkt)
{
	const struct iwl_dts_measurement_notif *notif =
		(const void *)pkt->data;
	int temp;
	u32 ths_crossed;

	temp = le32_to_cpu(notif->temp);

	/* shouldn't be negative, but since it's s32, make sure it isn't */
	if (IWL_FW_CHECK(mld, temp < 0, "negative temperature %d\n", temp))
		return;

	ths_crossed = le32_to_cpu(notif->threshold_idx);

	/* 0xFF in ths_crossed means the notification is not related
	 * to a trip, so we can ignore it here.
	 */
	if (ths_crossed == 0xFF)
		return;

	IWL_DEBUG_TEMP(mld, "Temp = %d Threshold crossed = %d\n",
		       temp, ths_crossed);

	if (IWL_FW_CHECK(mld, ths_crossed >= IWL_MAX_DTS_TRIPS,
			 "bad threshold: %d\n", ths_crossed))
		return;

#ifdef CONFIG_THERMAL
	if (mld->tzone)
		thermal_zone_device_update(mld->tzone, THERMAL_TRIP_VIOLATED);
#endif /* CONFIG_THERMAL */
}

#ifdef CONFIG_THERMAL
static int iwl_mld_get_temp(struct iwl_mld *mld, s32 *temp)
{
	struct iwl_host_cmd cmd = {
		.id = WIDE_ID(PHY_OPS_GROUP, CMD_DTS_MEASUREMENT_TRIGGER_WIDE),
		.flags = CMD_WANT_SKB,
	};
	const struct iwl_dts_measurement_resp *resp;
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	ret = iwl_mld_send_cmd(mld, &cmd);
	if (ret) {
		IWL_ERR(mld,
			"Failed to send the temperature measurement command (err=%d)\n",
			ret);
		return ret;
	}

	if (iwl_rx_packet_payload_len(cmd.resp_pkt) < sizeof(*resp)) {
		IWL_ERR(mld,
			"Failed to get a valid response to DTS measurement\n");
		ret = -EIO;
		goto free_resp;
	}

	resp = (const void *)cmd.resp_pkt->data;
	*temp = le32_to_cpu(resp->temp);

	IWL_DEBUG_TEMP(mld,
		       "Got temperature measurement response: temp=%d\n",
		       *temp);

free_resp:
	iwl_free_resp(&cmd);
	return ret;
}

static int compare_temps(const void *a, const void *b)
{
	return ((s16)le16_to_cpu(*(const __le16 *)a) -
		(s16)le16_to_cpu(*(const __le16 *)b));
}

struct iwl_trip_walk_data {
	__le16 *thresholds;
	int count;
};

static int iwl_trip_temp_iter(struct thermal_trip *trip, void *arg)
{
	struct iwl_trip_walk_data *twd = arg;

	if (trip->temperature == THERMAL_TEMP_INVALID)
		return 0;

	twd->thresholds[twd->count++] =
		cpu_to_le16((s16)(trip->temperature / 1000));
	return 0;
}
#endif

int iwl_mld_config_temp_report_ths(struct iwl_mld *mld)
{
	struct temp_report_ths_cmd cmd = {0};
	int ret;
#ifdef CONFIG_THERMAL
	struct iwl_trip_walk_data twd = {
		.thresholds = cmd.thresholds,
		.count = 0
	};

	if (!mld->tzone)
		goto send;

	/* The thermal core holds an array of temperature trips that are
	 * unsorted and uncompressed, the FW should get it compressed and
	 * sorted.
	 */

	/* compress trips to cmd array, remove uninitialized values*/
	for_each_thermal_trip(mld->tzone, iwl_trip_temp_iter, &twd);

	cmd.num_temps = cpu_to_le32(twd.count);
	if (twd.count)
		sort(cmd.thresholds, twd.count, sizeof(s16),
		     compare_temps, NULL);

send:
#endif
	lockdep_assert_wiphy(mld->wiphy);

	ret = iwl_mld_send_cmd_pdu(mld, WIDE_ID(PHY_OPS_GROUP,
						TEMP_REPORTING_THRESHOLDS_CMD),
				   &cmd);
	if (ret)
		IWL_ERR(mld, "TEMP_REPORT_THS_CMD command failed (err=%d)\n",
			ret);

	return ret;
}

#ifdef CONFIG_THERMAL
static int iwl_mld_tzone_get_temp(struct thermal_zone_device *device,
				  int *temperature)
{
	struct iwl_mld *mld = thermal_zone_device_priv(device);
	int temp;
	int ret = 0;

	wiphy_lock(mld->wiphy);

	if (!mld->fw_status.running) {
		/* Tell the core that there is no valid temperature value to
		 * return, but it need not worry about this.
		 */
		*temperature = THERMAL_TEMP_INVALID;
		goto unlock;
	}

	ret = iwl_mld_get_temp(mld, &temp);
	if (ret)
		goto unlock;

	*temperature = temp * 1000;
unlock:
	wiphy_unlock(mld->wiphy);
	return ret;
}

static int iwl_mld_tzone_set_trip_temp(struct thermal_zone_device *device,
				       const struct thermal_trip *trip,
				       int temp)
{
	struct iwl_mld *mld = thermal_zone_device_priv(device);
	int ret;

	wiphy_lock(mld->wiphy);

	if (!mld->fw_status.running) {
		ret = -EIO;
		goto unlock;
	}

	if ((temp / 1000) > S16_MAX) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = iwl_mld_config_temp_report_ths(mld);
unlock:
	wiphy_unlock(mld->wiphy);
	return ret;
}

static  struct thermal_zone_device_ops tzone_ops = {
	.get_temp = iwl_mld_tzone_get_temp,
	.set_trip_temp = iwl_mld_tzone_set_trip_temp,
};

static void iwl_mld_thermal_zone_register(struct iwl_mld *mld)
{
	int ret;
	char name[16];
	static atomic_t counter = ATOMIC_INIT(0);
	struct thermal_trip trips[IWL_MAX_DTS_TRIPS] = {
		[0 ... IWL_MAX_DTS_TRIPS - 1] = {
			.temperature = THERMAL_TEMP_INVALID,
			.type = THERMAL_TRIP_PASSIVE,
			.flags = THERMAL_TRIP_FLAG_RW_TEMP,
		},
	};

	BUILD_BUG_ON(ARRAY_SIZE(name) >= THERMAL_NAME_LENGTH);

	sprintf(name, "iwlwifi_%u", atomic_inc_return(&counter) & 0xFF);
	mld->tzone =
		thermal_zone_device_register_with_trips(name, trips,
							IWL_MAX_DTS_TRIPS,
							mld, &tzone_ops,
							NULL, 0, 0);
	if (IS_ERR(mld->tzone)) {
		IWL_DEBUG_TEMP(mld,
			       "Failed to register to thermal zone (err = %ld)\n",
			       PTR_ERR(mld->tzone));
		mld->tzone = NULL;
		return;
	}

	ret = thermal_zone_device_enable(mld->tzone);
	if (ret) {
		IWL_DEBUG_TEMP(mld, "Failed to enable thermal zone\n");
		thermal_zone_device_unregister(mld->tzone);
	}
}

/* budget in mWatt */
static const u32 iwl_mld_cdev_budgets[] = {
	2400,	/* cooling state 0 */
	2000,	/* cooling state 1 */
	1800,	/* cooling state 2 */
	1600,	/* cooling state 3 */
	1400,	/* cooling state 4 */
	1200,	/* cooling state 5 */
	1000,	/* cooling state 6 */
	900,	/* cooling state 7 */
	800,	/* cooling state 8 */
	700,	/* cooling state 9 */
	650,	/* cooling state 10 */
	600,	/* cooling state 11 */
	550,	/* cooling state 12 */
	500,	/* cooling state 13 */
	450,	/* cooling state 14 */
	400,	/* cooling state 15 */
	350,	/* cooling state 16 */
	300,	/* cooling state 17 */
	250,	/* cooling state 18 */
	200,	/* cooling state 19 */
	150,	/* cooling state 20 */
};

int iwl_mld_config_ctdp(struct iwl_mld *mld, u32 state,
			enum iwl_ctdp_cmd_operation op)
{
	struct iwl_ctdp_cmd cmd = {
		.operation = cpu_to_le32(op),
		.budget = cpu_to_le32(iwl_mld_cdev_budgets[state]),
		.window_size = 0,
	};
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	ret = iwl_mld_send_cmd_pdu(mld, WIDE_ID(PHY_OPS_GROUP, CTDP_CONFIG_CMD),
				   &cmd);

	if (ret) {
		IWL_ERR(mld, "cTDP command failed (err=%d)\n", ret);
		return ret;
	}

	if (op == CTDP_CMD_OPERATION_START)
		mld->cooling_dev.cur_state = state;

	return 0;
}

static int iwl_mld_tcool_get_max_state(struct thermal_cooling_device *cdev,
				       unsigned long *state)
{
	*state = ARRAY_SIZE(iwl_mld_cdev_budgets) - 1;

	return 0;
}

static int iwl_mld_tcool_get_cur_state(struct thermal_cooling_device *cdev,
				       unsigned long *state)
{
	struct iwl_mld *mld = (struct iwl_mld *)(cdev->devdata);

	*state = mld->cooling_dev.cur_state;

	return 0;
}

static int iwl_mld_tcool_set_cur_state(struct thermal_cooling_device *cdev,
				       unsigned long new_state)
{
	struct iwl_mld *mld = (struct iwl_mld *)(cdev->devdata);
	int ret;

	wiphy_lock(mld->wiphy);

	if (!mld->fw_status.running) {
		ret = -EIO;
		goto unlock;
	}

	if (new_state >= ARRAY_SIZE(iwl_mld_cdev_budgets)) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = iwl_mld_config_ctdp(mld, new_state, CTDP_CMD_OPERATION_START);

unlock:
	wiphy_unlock(mld->wiphy);
	return ret;
}

static const struct thermal_cooling_device_ops tcooling_ops = {
	.get_max_state = iwl_mld_tcool_get_max_state,
	.get_cur_state = iwl_mld_tcool_get_cur_state,
	.set_cur_state = iwl_mld_tcool_set_cur_state,
};

static void iwl_mld_cooling_device_register(struct iwl_mld *mld)
{
	char name[] = "iwlwifi";

	BUILD_BUG_ON(ARRAY_SIZE(name) >= THERMAL_NAME_LENGTH);

	mld->cooling_dev.cdev =
		thermal_cooling_device_register(name,
						mld,
						&tcooling_ops);

	if (IS_ERR(mld->cooling_dev.cdev)) {
		IWL_DEBUG_TEMP(mld,
			       "Failed to register to cooling device (err = %ld)\n",
			       PTR_ERR(mld->cooling_dev.cdev));
		mld->cooling_dev.cdev = NULL;
		return;
	}
}

static void iwl_mld_thermal_zone_unregister(struct iwl_mld *mld)
{
	if (!mld->tzone)
		return;

	IWL_DEBUG_TEMP(mld, "Thermal zone device unregister\n");
	if (mld->tzone) {
		thermal_zone_device_unregister(mld->tzone);
		mld->tzone = NULL;
	}
}

static void iwl_mld_cooling_device_unregister(struct iwl_mld *mld)
{
	if (!mld->cooling_dev.cdev)
		return;

	IWL_DEBUG_TEMP(mld, "Cooling device unregister\n");
	if (mld->cooling_dev.cdev) {
		thermal_cooling_device_unregister(mld->cooling_dev.cdev);
		mld->cooling_dev.cdev = NULL;
	}
}
#endif /* CONFIG_THERMAL */

void iwl_mld_thermal_initialize(struct iwl_mld *mld)
{
	wiphy_delayed_work_init(&mld->ct_kill_exit_wk, iwl_mld_exit_ctkill);

#ifdef CONFIG_THERMAL
	iwl_mld_cooling_device_register(mld);
	iwl_mld_thermal_zone_register(mld);
#endif
}

void iwl_mld_thermal_exit(struct iwl_mld *mld)
{
	wiphy_delayed_work_cancel(mld->wiphy, &mld->ct_kill_exit_wk);

#ifdef CONFIG_THERMAL
	iwl_mld_cooling_device_unregister(mld);
	iwl_mld_thermal_zone_unregister(mld);
#endif
}
