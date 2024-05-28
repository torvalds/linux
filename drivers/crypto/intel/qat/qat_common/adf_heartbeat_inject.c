// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */
#include <linux/random.h>

#include "adf_admin.h"
#include "adf_common_drv.h"
#include "adf_heartbeat.h"

#define MAX_HB_TICKS 0xFFFFFFFF

static int adf_hb_set_timer_to_max(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	accel_dev->heartbeat->hb_timer = 0;

	if (hw_data->stop_timer)
		hw_data->stop_timer(accel_dev);

	return adf_send_admin_hb_timer(accel_dev, MAX_HB_TICKS);
}

static void adf_set_hb_counters_fail(struct adf_accel_dev *accel_dev, u32 ae,
				     u32 thr)
{
	struct hb_cnt_pair *stats = accel_dev->heartbeat->dma.virt_addr;
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	const size_t max_aes = hw_device->get_num_aes(hw_device);
	const size_t hb_ctrs = hw_device->num_hb_ctrs;
	size_t thr_id = ae * hb_ctrs + thr;
	u16 num_rsp = stats[thr_id].resp_heartbeat_cnt;

	/*
	 * Inject live.req != live.rsp and live.rsp == last.rsp
	 * to trigger the heartbeat error detection
	 */
	stats[thr_id].req_heartbeat_cnt++;
	stats += (max_aes * hb_ctrs);
	stats[thr_id].resp_heartbeat_cnt = num_rsp;
}

int adf_heartbeat_inject_error(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	const size_t max_aes = hw_device->get_num_aes(hw_device);
	const size_t hb_ctrs = hw_device->num_hb_ctrs;
	u32 rand, rand_ae, rand_thr;
	unsigned long ae_mask;
	int ret;

	ae_mask = hw_device->ae_mask;

	do {
		/* Ensure we have a valid ae */
		get_random_bytes(&rand, sizeof(rand));
		rand_ae = rand % max_aes;
	} while (!test_bit(rand_ae, &ae_mask));

	get_random_bytes(&rand, sizeof(rand));
	rand_thr = rand % hb_ctrs;

	/* Increase the heartbeat timer to prevent FW updating HB counters */
	ret = adf_hb_set_timer_to_max(accel_dev);
	if (ret)
		return ret;

	/* Configure worker threads to stop processing any packet */
	ret = adf_disable_arb_thd(accel_dev, rand_ae, rand_thr);
	if (ret)
		return ret;

	/* Change HB counters memory to simulate a hang */
	adf_set_hb_counters_fail(accel_dev, rand_ae, rand_thr);

	return 0;
}
