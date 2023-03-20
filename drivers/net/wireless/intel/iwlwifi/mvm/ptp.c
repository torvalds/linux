// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2021 - 2023 Intel Corporation
 */

#include "mvm.h"
#include "iwl-debug.h"
#include <linux/timekeeping.h>

#define IWL_PTP_GP2_WRAP	0x100000000ULL
#define IWL_PTP_WRAP_TIME	(3600 * HZ)

static void iwl_mvm_ptp_update_new_read(struct iwl_mvm *mvm, u32 gp2)
{
	if (gp2 < mvm->ptp_data.last_gp2) {
		mvm->ptp_data.wrap_counter++;
		IWL_DEBUG_INFO(mvm,
			       "PTP: wraparound detected (new counter=%u)\n",
			       mvm->ptp_data.wrap_counter);
	}

	mvm->ptp_data.last_gp2 = gp2;
	schedule_delayed_work(&mvm->ptp_data.dwork, IWL_PTP_WRAP_TIME);
}

static int
iwl_mvm_get_crosstimestamp_fw(struct iwl_mvm *mvm, u32 *gp2, u64 *sys_time)
{
	struct iwl_synced_time_cmd synced_time_cmd = {
		.operation = cpu_to_le32(IWL_SYNCED_TIME_OPERATION_READ_BOTH)
	};
	struct iwl_host_cmd cmd = {
		.id = WIDE_ID(DATA_PATH_GROUP, WNM_PLATFORM_PTM_REQUEST_CMD),
		.flags = CMD_WANT_SKB,
		.data[0] = &synced_time_cmd,
		.len[0] = sizeof(synced_time_cmd),
	};
	struct iwl_synced_time_rsp *resp;
	struct iwl_rx_packet *pkt;
	int ret;
	u64 gp2_10ns;

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret)
		return ret;

	pkt = cmd.resp_pkt;

	if (iwl_rx_packet_payload_len(pkt) != sizeof(*resp)) {
		IWL_ERR(mvm, "PTP: Invalid command response\n");
		iwl_free_resp(&cmd);
		return -EIO;
	}

	resp = (void *)pkt->data;

	gp2_10ns = (u64)le32_to_cpu(resp->gp2_timestamp_hi) << 32 |
		le32_to_cpu(resp->gp2_timestamp_lo);
	*gp2 = gp2_10ns / 100;

	*sys_time = (u64)le32_to_cpu(resp->platform_timestamp_hi) << 32 |
		le32_to_cpu(resp->platform_timestamp_lo);

	return ret;
}

static int
iwl_mvm_phc_get_crosstimestamp(struct ptp_clock_info *ptp,
			       struct system_device_crosststamp *xtstamp)
{
	struct iwl_mvm *mvm = container_of(ptp, struct iwl_mvm,
					   ptp_data.ptp_clock_info);
	int ret = 0;
	/* Raw value read from GP2 register in usec */
	u32 gp2;
	/* GP2 value in ns*/
	s64 gp2_ns;
	/* System (wall) time */
	ktime_t sys_time;

	memset(xtstamp, 0, sizeof(struct system_device_crosststamp));

	if (!mvm->ptp_data.ptp_clock) {
		IWL_ERR(mvm, "No PHC clock registered\n");
		return -ENODEV;
	}

	mutex_lock(&mvm->mutex);
	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_SYNCED_TIME)) {
		ret = iwl_mvm_get_crosstimestamp_fw(mvm, &gp2, &sys_time);

		if (ret)
			goto out;
	} else {
		iwl_mvm_get_sync_time(mvm, CLOCK_REALTIME, &gp2, NULL,
				      &sys_time);
	}

	iwl_mvm_ptp_update_new_read(mvm, gp2);

	gp2_ns = (gp2 + (mvm->ptp_data.wrap_counter * IWL_PTP_GP2_WRAP)) *
		NSEC_PER_USEC;

	IWL_INFO(mvm, "Got Sync Time: GP2:%u, last_GP2: %u, GP2_ns: %lld, sys_time: %lld\n",
		 gp2, mvm->ptp_data.last_gp2, gp2_ns, (s64)sys_time);

	/* System monotonic raw time is not used */
	xtstamp->device = (ktime_t)gp2_ns;
	xtstamp->sys_realtime = sys_time;

out:
	mutex_unlock(&mvm->mutex);
	return ret;
}

static void iwl_mvm_ptp_work(struct work_struct *wk)
{
	struct iwl_mvm *mvm = container_of(wk, struct iwl_mvm,
					   ptp_data.dwork.work);
	u32 gp2;

	mutex_lock(&mvm->mutex);
	gp2 = iwl_mvm_get_systime(mvm);
	iwl_mvm_ptp_update_new_read(mvm, gp2);
	mutex_unlock(&mvm->mutex);
}

/* iwl_mvm_ptp_init - initialize PTP for devices which support it.
 * @mvm: internal mvm structure, see &struct iwl_mvm.
 *
 * Performs the required steps for enabling PTP support.
 */
void iwl_mvm_ptp_init(struct iwl_mvm *mvm)
{
	/* Warn if the interface already has a ptp_clock defined */
	if (WARN_ON(mvm->ptp_data.ptp_clock))
		return;

	mvm->ptp_data.ptp_clock_info.owner = THIS_MODULE;
	mvm->ptp_data.ptp_clock_info.max_adj = 0x7fffffff;
	mvm->ptp_data.ptp_clock_info.getcrosststamp =
					iwl_mvm_phc_get_crosstimestamp;

	/* Give a short 'friendly name' to identify the PHC clock */
	snprintf(mvm->ptp_data.ptp_clock_info.name,
		 sizeof(mvm->ptp_data.ptp_clock_info.name),
		 "%s", "iwlwifi-PTP");

	INIT_DELAYED_WORK(&mvm->ptp_data.dwork, iwl_mvm_ptp_work);

	mvm->ptp_data.ptp_clock =
		ptp_clock_register(&mvm->ptp_data.ptp_clock_info, mvm->dev);

	if (IS_ERR(mvm->ptp_data.ptp_clock)) {
		IWL_ERR(mvm, "Failed to register PHC clock (%ld)\n",
			PTR_ERR(mvm->ptp_data.ptp_clock));
		mvm->ptp_data.ptp_clock = NULL;
	} else if (mvm->ptp_data.ptp_clock) {
		IWL_INFO(mvm, "Registered PHC clock: %s, with index: %d\n",
			 mvm->ptp_data.ptp_clock_info.name,
			 ptp_clock_index(mvm->ptp_data.ptp_clock));
	}
}

/* iwl_mvm_ptp_remove - disable PTP device.
 * @mvm: internal mvm structure, see &struct iwl_mvm.
 *
 * Disable PTP support.
 */
void iwl_mvm_ptp_remove(struct iwl_mvm *mvm)
{
	if (mvm->ptp_data.ptp_clock) {
		IWL_INFO(mvm, "Unregistering PHC clock: %s, with index: %d\n",
			 mvm->ptp_data.ptp_clock_info.name,
			 ptp_clock_index(mvm->ptp_data.ptp_clock));

		ptp_clock_unregister(mvm->ptp_data.ptp_clock);
		mvm->ptp_data.ptp_clock = NULL;
		memset(&mvm->ptp_data.ptp_clock_info, 0,
		       sizeof(mvm->ptp_data.ptp_clock_info));
		mvm->ptp_data.last_gp2 = 0;
		cancel_delayed_work_sync(&mvm->ptp_data.dwork);
	}
}
