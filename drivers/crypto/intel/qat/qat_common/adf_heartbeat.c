// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */

#include <linux/dev_printk.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/overflow.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <asm/errno.h>
#include "adf_accel_devices.h"
#include "adf_admin.h"
#include "adf_cfg.h"
#include "adf_cfg_strings.h"
#include "adf_clock.h"
#include "adf_common_drv.h"
#include "adf_heartbeat.h"
#include "adf_transport_internal.h"
#include "icp_qat_fw_init_admin.h"

#define ADF_HB_EMPTY_SIG 0xA5A5A5A5

static int adf_hb_check_polling_freq(struct adf_accel_dev *accel_dev)
{
	u64 curr_time = adf_clock_get_current_time();
	u64 polling_time = curr_time - accel_dev->heartbeat->last_hb_check_time;

	if (polling_time < accel_dev->heartbeat->hb_timer) {
		dev_warn(&GET_DEV(accel_dev),
			 "HB polling too frequent. Configured HB timer %d ms\n",
			 accel_dev->heartbeat->hb_timer);
		return -EINVAL;
	}

	accel_dev->heartbeat->last_hb_check_time = curr_time;
	return 0;
}

/**
 * validate_hb_ctrs_cnt() - checks if the number of heartbeat counters should
 * be updated by one to support the currently loaded firmware.
 * @accel_dev: Pointer to acceleration device.
 *
 * Return:
 * * true - hb_ctrs must increased by ADF_NUM_PKE_STRAND
 * * false - no changes needed
 */
static bool validate_hb_ctrs_cnt(struct adf_accel_dev *accel_dev)
{
	const size_t hb_ctrs = accel_dev->hw_device->num_hb_ctrs;
	const size_t max_aes = accel_dev->hw_device->num_engines;
	const size_t hb_struct_size = sizeof(struct hb_cnt_pair);
	const size_t exp_diff_size = array3_size(ADF_NUM_PKE_STRAND, max_aes,
						 hb_struct_size);
	const size_t dev_ctrs = size_mul(max_aes, hb_ctrs);
	const size_t stats_size = size_mul(dev_ctrs, hb_struct_size);
	const u32 exp_diff_cnt = exp_diff_size / sizeof(u32);
	const u32 stats_el_cnt = stats_size / sizeof(u32);
	struct hb_cnt_pair *hb_stats = accel_dev->heartbeat->dma.virt_addr;
	const u32 *mem_to_chk = (u32 *)(hb_stats + dev_ctrs);
	u32 el_diff_cnt = 0;
	int i;

	/* count how many bytes are different from pattern */
	for (i = 0; i < stats_el_cnt; i++) {
		if (mem_to_chk[i] == ADF_HB_EMPTY_SIG)
			break;

		el_diff_cnt++;
	}

	return el_diff_cnt && el_diff_cnt == exp_diff_cnt;
}

void adf_heartbeat_check_ctrs(struct adf_accel_dev *accel_dev)
{
	struct hb_cnt_pair *hb_stats = accel_dev->heartbeat->dma.virt_addr;
	const size_t hb_ctrs = accel_dev->hw_device->num_hb_ctrs;
	const size_t max_aes = accel_dev->hw_device->num_engines;
	const size_t dev_ctrs = size_mul(max_aes, hb_ctrs);
	const size_t stats_size = size_mul(dev_ctrs, sizeof(struct hb_cnt_pair));
	const size_t mem_items_to_fill = size_mul(stats_size, 2) / sizeof(u32);

	/* fill hb stats memory with pattern */
	memset32((uint32_t *)hb_stats, ADF_HB_EMPTY_SIG, mem_items_to_fill);
	accel_dev->heartbeat->ctrs_cnt_checked = false;
}
EXPORT_SYMBOL_GPL(adf_heartbeat_check_ctrs);

static int get_timer_ticks(struct adf_accel_dev *accel_dev, unsigned int *value)
{
	char timer_str[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = { };
	u32 timer_ms = ADF_CFG_HB_TIMER_DEFAULT_MS;
	int cfg_read_status;
	u32 ticks;
	int ret;

	cfg_read_status = adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC,
						  ADF_HEARTBEAT_TIMER, timer_str);
	if (cfg_read_status == 0) {
		if (kstrtouint(timer_str, 10, &timer_ms))
			dev_dbg(&GET_DEV(accel_dev),
				"kstrtouint failed to parse the %s, param value",
				ADF_HEARTBEAT_TIMER);
	}

	if (timer_ms < ADF_CFG_HB_TIMER_MIN_MS) {
		dev_err(&GET_DEV(accel_dev), "Timer cannot be less than %u\n",
			ADF_CFG_HB_TIMER_MIN_MS);
		return -EINVAL;
	}

	/*
	 * On 4xxx devices adf_timer is responsible for HB updates and
	 * its period is fixed to 200ms
	 */
	if (accel_dev->timer)
		timer_ms = ADF_CFG_HB_TIMER_MIN_MS;

	ret = adf_heartbeat_ms_to_ticks(accel_dev, timer_ms, &ticks);
	if (ret)
		return ret;

	adf_heartbeat_save_cfg_param(accel_dev, timer_ms);

	accel_dev->heartbeat->hb_timer = timer_ms;
	*value = ticks;

	return 0;
}

static int check_ae(struct hb_cnt_pair *curr, struct hb_cnt_pair *prev,
		    u16 *count, const size_t hb_ctrs)
{
	size_t thr;

	/* loop through all threads in AE */
	for (thr = 0; thr < hb_ctrs; thr++) {
		u16 req = curr[thr].req_heartbeat_cnt;
		u16 resp = curr[thr].resp_heartbeat_cnt;
		u16 last = prev[thr].resp_heartbeat_cnt;

		if ((thr == ADF_AE_ADMIN_THREAD || req != resp) && resp == last) {
			u16 retry = ++count[thr];

			if (retry >= ADF_CFG_HB_COUNT_THRESHOLD)
				return -EIO;

		} else {
			count[thr] = 0;
		}
	}
	return 0;
}

static int adf_hb_get_status(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	struct hb_cnt_pair *live_stats, *last_stats, *curr_stats;
	const size_t hb_ctrs = hw_device->num_hb_ctrs;
	const unsigned long ae_mask = hw_device->ae_mask;
	const size_t max_aes = hw_device->num_engines;
	const size_t dev_ctrs = size_mul(max_aes, hb_ctrs);
	const size_t stats_size = size_mul(dev_ctrs, sizeof(*curr_stats));
	struct hb_cnt_pair *ae_curr_p, *ae_prev_p;
	u16 *count_fails, *ae_count_p;
	size_t ae_offset;
	size_t ae = 0;
	int ret = 0;

	if (!accel_dev->heartbeat->ctrs_cnt_checked) {
		if (validate_hb_ctrs_cnt(accel_dev))
			hw_device->num_hb_ctrs += ADF_NUM_PKE_STRAND;

		accel_dev->heartbeat->ctrs_cnt_checked = true;
	}

	live_stats = accel_dev->heartbeat->dma.virt_addr;
	last_stats = live_stats + dev_ctrs;
	count_fails = (u16 *)(last_stats + dev_ctrs);

	curr_stats = kmemdup(live_stats, stats_size, GFP_KERNEL);
	if (!curr_stats)
		return -ENOMEM;

	/* loop through active AEs */
	for_each_set_bit(ae, &ae_mask, max_aes) {
		ae_offset = size_mul(ae, hb_ctrs);
		ae_curr_p = curr_stats + ae_offset;
		ae_prev_p = last_stats + ae_offset;
		ae_count_p = count_fails + ae_offset;

		ret = check_ae(ae_curr_p, ae_prev_p, ae_count_p, hb_ctrs);
		if (ret)
			break;
	}

	/* Copy current stats for the next iteration */
	memcpy(last_stats, curr_stats, stats_size);
	kfree(curr_stats);

	return ret;
}

void adf_heartbeat_status(struct adf_accel_dev *accel_dev,
			  enum adf_device_heartbeat_status *hb_status)
{
	struct adf_heartbeat *hb;

	if (!adf_dev_started(accel_dev) ||
	    test_bit(ADF_STATUS_RESTARTING, &accel_dev->status)) {
		*hb_status = HB_DEV_UNRESPONSIVE;
		return;
	}

	if (adf_hb_check_polling_freq(accel_dev) == -EINVAL) {
		*hb_status = HB_DEV_UNSUPPORTED;
		return;
	}

	hb = accel_dev->heartbeat;
	hb->hb_sent_counter++;

	if (adf_hb_get_status(accel_dev)) {
		dev_err(&GET_DEV(accel_dev),
			"Heartbeat ERROR: QAT is not responding.\n");
		*hb_status = HB_DEV_UNRESPONSIVE;
		hb->hb_failed_counter++;
		return;
	}

	*hb_status = HB_DEV_ALIVE;
}

int adf_heartbeat_ms_to_ticks(struct adf_accel_dev *accel_dev, unsigned int time_ms,
			      u32 *value)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 clk_per_sec;

	/* HB clock may be different than AE clock */
	if (!hw_data->get_hb_clock)
		return -EINVAL;

	clk_per_sec = hw_data->get_hb_clock(hw_data);
	*value = time_ms * (clk_per_sec / MSEC_PER_SEC);

	return 0;
}

int adf_heartbeat_save_cfg_param(struct adf_accel_dev *accel_dev,
				 unsigned int timer_ms)
{
	char timer_str[ADF_CFG_MAX_VAL_LEN_IN_BYTES];

	snprintf(timer_str, sizeof(timer_str), "%u", timer_ms);
	return adf_cfg_add_key_value_param(accel_dev, ADF_GENERAL_SEC,
					  ADF_HEARTBEAT_TIMER, timer_str,
					  ADF_STR);
}
EXPORT_SYMBOL_GPL(adf_heartbeat_save_cfg_param);

int adf_heartbeat_init(struct adf_accel_dev *accel_dev)
{
	struct adf_heartbeat *hb;

	hb = kzalloc(sizeof(*hb), GFP_KERNEL);
	if (!hb)
		goto err_ret;

	hb->dma.virt_addr = dma_alloc_coherent(&GET_DEV(accel_dev), PAGE_SIZE,
					       &hb->dma.phy_addr, GFP_KERNEL);
	if (!hb->dma.virt_addr)
		goto err_free;

	/*
	 * Default set this flag as true to avoid unnecessary checks,
	 * it will be reset on platforms that need such a check
	 */
	hb->ctrs_cnt_checked = true;
	accel_dev->heartbeat = hb;

	return 0;

err_free:
	kfree(hb);
err_ret:
	return -ENOMEM;
}

int adf_heartbeat_start(struct adf_accel_dev *accel_dev)
{
	unsigned int timer_ticks;
	int ret;

	if (!accel_dev->heartbeat) {
		dev_warn(&GET_DEV(accel_dev), "Heartbeat instance not found!");
		return -EFAULT;
	}

	if (accel_dev->hw_device->check_hb_ctrs)
		accel_dev->hw_device->check_hb_ctrs(accel_dev);

	ret = get_timer_ticks(accel_dev, &timer_ticks);
	if (ret)
		return ret;

	ret = adf_send_admin_hb_timer(accel_dev, timer_ticks);
	if (ret)
		dev_warn(&GET_DEV(accel_dev), "Heartbeat not supported!");

	return ret;
}

void adf_heartbeat_shutdown(struct adf_accel_dev *accel_dev)
{
	struct adf_heartbeat *hb = accel_dev->heartbeat;

	if (!hb)
		return;

	if (hb->dma.virt_addr)
		dma_free_coherent(&GET_DEV(accel_dev), PAGE_SIZE,
				  hb->dma.virt_addr, hb->dma.phy_addr);

	kfree(hb);
	accel_dev->heartbeat = NULL;
}
