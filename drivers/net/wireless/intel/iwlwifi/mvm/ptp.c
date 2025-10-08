// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2021 - 2023, 2025 Intel Corporation
 */

#include "mvm.h"
#include "iwl-debug.h"
#include <linux/timekeeping.h>
#include <linux/math64.h>

#define IWL_PTP_GP2_WRAP	0x100000000ULL
#define IWL_PTP_WRAP_TIME	(3600 * HZ)

/* The scaled_ppm parameter is ppm (parts per million) with a 16-bit fractional
 * part, which means that a value of 1 in one of those fields actually means
 * 2^-16 ppm, and 2^16=65536 is 1 ppm.
 */
#define SCALE_FACTOR	65536000000ULL
#define IWL_PTP_WRAP_THRESHOLD_USEC	(5000)

#define IWL_PTP_GET_CROSS_TS_NUM	5

static void iwl_mvm_ptp_update_new_read(struct iwl_mvm *mvm, u32 gp2)
{
	/* If the difference is above the threshold, assume it's a wraparound.
	 * Otherwise assume it's an old read and ignore it.
	 */
	if (gp2 < mvm->ptp_data.last_gp2 &&
	    mvm->ptp_data.last_gp2 - gp2 < IWL_PTP_WRAP_THRESHOLD_USEC) {
		IWL_DEBUG_INFO(mvm,
			       "PTP: ignore old read (gp2=%u, last_gp2=%u)\n",
			       gp2, mvm->ptp_data.last_gp2);
		return;
	}

	if (gp2 < mvm->ptp_data.last_gp2) {
		mvm->ptp_data.wrap_counter++;
		IWL_DEBUG_INFO(mvm,
			       "PTP: wraparound detected (new counter=%u)\n",
			       mvm->ptp_data.wrap_counter);
	}

	mvm->ptp_data.last_gp2 = gp2;
	schedule_delayed_work(&mvm->ptp_data.dwork, IWL_PTP_WRAP_TIME);
}

u64 iwl_mvm_ptp_get_adj_time(struct iwl_mvm *mvm, u64 base_time_ns)
{
	struct ptp_data *data = &mvm->ptp_data;
	u64 last_gp2_ns = mvm->ptp_data.scale_update_gp2 * NSEC_PER_USEC;
	u64 res;
	u64 diff;

	iwl_mvm_ptp_update_new_read(mvm,
				    div64_u64(base_time_ns, NSEC_PER_USEC));

	IWL_DEBUG_INFO(mvm, "base_time_ns=%llu, wrap_counter=%u\n",
		       (unsigned long long)base_time_ns, data->wrap_counter);

	base_time_ns = base_time_ns +
		(data->wrap_counter * IWL_PTP_GP2_WRAP * NSEC_PER_USEC);

	/* It is possible that a GP2 timestamp was received from fw before the
	 * last scale update. Since we don't know how to scale - ignore it.
	 */
	if (base_time_ns < last_gp2_ns) {
		IWL_DEBUG_INFO(mvm, "Time before scale update - ignore\n");
		return 0;
	}

	diff = base_time_ns - last_gp2_ns;
	IWL_DEBUG_INFO(mvm, "diff ns=%llu\n", (unsigned long long)diff);

	diff = mul_u64_u64_div_u64(diff, data->scaled_freq,
				   SCALE_FACTOR);
	IWL_DEBUG_INFO(mvm, "scaled diff ns=%llu\n", (unsigned long long)diff);

	res = data->scale_update_adj_time_ns + data->delta + diff;

	IWL_DEBUG_INFO(mvm, "base=%llu delta=%lld adj=%llu\n",
		       (unsigned long long)base_time_ns, (long long)data->delta,
		       (unsigned long long)res);
	return res;
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
	*gp2 = div_u64(gp2_10ns, 100);

	*sys_time = (u64)le32_to_cpu(resp->platform_timestamp_hi) << 32 |
		le32_to_cpu(resp->platform_timestamp_lo);

	return ret;
}

static void iwl_mvm_phc_get_crosstimestamp_loop(struct iwl_mvm *mvm,
						ktime_t *sys_time, u32 *gp2)
{
	u64 diff = 0, new_diff;
	u64 tmp_sys_time;
	u32 tmp_gp2;
	int i;

	for (i = 0; i < IWL_PTP_GET_CROSS_TS_NUM; i++) {
		iwl_mvm_get_sync_time(mvm, CLOCK_REALTIME, &tmp_gp2, NULL,
				      &tmp_sys_time);
		new_diff = tmp_sys_time - ((u64)tmp_gp2 * NSEC_PER_USEC);
		if (!diff || new_diff < diff) {
			*sys_time = tmp_sys_time;
			*gp2 = tmp_gp2;
			diff = new_diff;
			IWL_DEBUG_INFO(mvm, "PTP: new times: gp2=%u sys=%lld\n",
				       *gp2, *sys_time);
		}
	}
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
		iwl_mvm_phc_get_crosstimestamp_loop(mvm, &sys_time, &gp2);
	}

	gp2_ns = iwl_mvm_ptp_get_adj_time(mvm, (u64)gp2 * NSEC_PER_USEC);

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

static int iwl_mvm_ptp_gettime(struct ptp_clock_info *ptp,
			       struct timespec64 *ts)
{
	struct iwl_mvm *mvm = container_of(ptp, struct iwl_mvm,
					   ptp_data.ptp_clock_info);
	u64 gp2;
	u64 ns;

	mutex_lock(&mvm->mutex);
	gp2 = iwl_mvm_get_systime(mvm);
	ns = iwl_mvm_ptp_get_adj_time(mvm, gp2 * NSEC_PER_USEC);
	mutex_unlock(&mvm->mutex);

	*ts = ns_to_timespec64(ns);
	return 0;
}

static int iwl_mvm_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct iwl_mvm *mvm = container_of(ptp, struct iwl_mvm,
					   ptp_data.ptp_clock_info);
	struct ptp_data *data = container_of(ptp, struct ptp_data,
					     ptp_clock_info);

	mutex_lock(&mvm->mutex);
	data->delta += delta;
	IWL_DEBUG_INFO(mvm, "delta=%lld, new delta=%lld\n", (long long)delta,
		       (long long)data->delta);
	mutex_unlock(&mvm->mutex);
	return 0;
}

static int iwl_mvm_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct iwl_mvm *mvm = container_of(ptp, struct iwl_mvm,
					   ptp_data.ptp_clock_info);
	struct ptp_data *data = &mvm->ptp_data;
	u32 gp2;

	mutex_lock(&mvm->mutex);

	/* Must call _iwl_mvm_ptp_get_adj_time() before updating
	 * data->scale_update_gp2 or data->scaled_freq since
	 * scale_update_adj_time_ns should reflect the previous scaled_freq.
	 */
	gp2 = iwl_mvm_get_systime(mvm);
	data->scale_update_adj_time_ns =
		iwl_mvm_ptp_get_adj_time(mvm, gp2 * NSEC_PER_USEC);
	data->scale_update_gp2 = gp2;
	data->wrap_counter = 0;
	data->delta = 0;

	data->scaled_freq = SCALE_FACTOR + scaled_ppm;
	IWL_DEBUG_INFO(mvm, "adjfine: scaled_ppm=%ld new=%llu\n",
		       scaled_ppm, (unsigned long long)data->scaled_freq);

	mutex_unlock(&mvm->mutex);
	return 0;
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
	mvm->ptp_data.ptp_clock_info.adjfine = iwl_mvm_ptp_adjfine;
	mvm->ptp_data.ptp_clock_info.adjtime = iwl_mvm_ptp_adjtime;
	mvm->ptp_data.ptp_clock_info.gettime64 = iwl_mvm_ptp_gettime;
	mvm->ptp_data.scaled_freq = SCALE_FACTOR;

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
		IWL_DEBUG_INFO(mvm, "Registered PHC clock: %s, with index: %d\n",
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
		IWL_DEBUG_INFO(mvm, "Unregistering PHC clock: %s, with index: %d\n",
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
