// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Platform Management Framework Driver - Metrics Support
 *
 * Copyright (c) 2026, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *	    Patil Rajesh Reddy <Patil.Reddy@amd.com>
 */

#include <linux/array_size.h>
#include <linux/cleanup.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/device/devres.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/limits.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/string.h>
#include <linux/units.h>
#include <linux/wordpart.h>
#include <linux/workqueue.h>

#include "pmf.h"

static struct device *pmf_device;

static u16 amd_pmf_q10_acc_to_mW(u64 avg_acc)
{
	u64 val = DIV_U64_ROUND_CLOSEST(avg_acc, 1024) * MILLIWATT_PER_WATT;

	return min_t(u16, val, U16_MAX);
}

static u16 amd_pmf_q10_acc_to_int(u64 avg_acc)
{
	u64 val = DIV_U64_ROUND_CLOSEST(avg_acc, 1024);

	return min_t(u16, val, U16_MAX);
}

static u64 amd_pmf_avg_acc_metric(u32 curr_counter, u32 prev_counter,
				  u64 curr_value, u64 prev_value)
{
	u32 counter_diff;
	u64 val_diff;

	/* Check for counter reset */
	if (curr_counter <= prev_counter)
		return 0;

	counter_diff = curr_counter - prev_counter;

	if (curr_value < prev_value)
		return 0;

	val_diff = curr_value - prev_value;

	return DIV_U64_ROUND_CLOSEST(val_diff, counter_diff);
}

int amd_pmf_get_tbl_dram_addr(struct amd_pmf_dev *dev)
{
	int ret;

	ret = amd_pmf_send_cmd(dev, GET_1AH_M80H_METRICS_TABLE_DRAM_ADDR, GET_CMD,
			       ARG_NONE, &dev->dram_addr.lo);
	if (ret) {
		dev_err(dev->dev, "Failed to get DRAM address: %d\n", ret);
		return ret;
	}

	dev->metrics_table_phys = ((u64)dev->dram_addr.hi << 32) | dev->dram_addr.lo;
	dev->mtable_size = dev->dram_addr.size;

	if (dev->mtable_size != sizeof(dev->mtable_v3)) {
		dev_err(dev->dev, "Metrics table size mismatch: got %u, expected %zu\n",
			dev->dram_addr.size, sizeof(dev->mtable_v3));
		return -EINVAL;
	}

	dev->metrics_table_virt = devm_ioremap(dev->dev, dev->metrics_table_phys, dev->mtable_size);
	if (!dev->metrics_table_virt) {
		dev_err(dev->dev, "Failed to map DRAM address for PMF metrics table\n");
		dev->metrics_table_phys = 0;
		return -ENOMEM;
	}

	return 0;
}

static int amd_pmf_get_metrics_table_log_sample(struct amd_pmf_dev *dev)
{
	int ret;

	if (!dev->metrics_table_virt) {
		dev_err(dev->dev, "Metrics DRAM not mapped\n");
		return -EINVAL;
	}

	/* Send command to PMFW to update metrics table log sample */
	ret = amd_pmf_send_cmd(dev, GET_1AH_M80H_METRICS_TABLE_LOG_SAMPLE, SET_CMD, ARG_NONE, NULL);
	if (ret) {
		dev_err(dev->dev, "Failed to request metrics log sample: %d\n", ret);
		return ret;
	}

	return 0;
}

static void amd_pmf_get_metrics(struct work_struct *work)
{
	struct amd_pmf_dev *dev = container_of(work, struct amd_pmf_dev, work_buffer.work);
	ktime_t time_elapsed_ms;
	int socket_power;

	guard(mutex)(&dev->update_mutex);

	/* Transfer table contents */
	memset(dev->buf, 0, sizeof(dev->m_table));
	amd_pmf_send_cmd(dev, SET_TRANSFER_TABLE, SET_CMD, METRICS_TABLE_ID, NULL);
	memcpy(&dev->m_table, dev->buf, sizeof(dev->m_table));

	time_elapsed_ms = ktime_to_ms(ktime_get()) - dev->start_time;
	/* Calculate the avg SoC power consumption */
	socket_power = dev->m_table.apu_power + dev->m_table.dgpu_power;

	if (dev->amt_enabled) {
		/* Apply the Auto Mode transition */
		amd_pmf_trans_automode(dev, socket_power, time_elapsed_ms);
	}

	if (dev->cnqf_enabled) {
		/* Apply the CnQF transition */
		amd_pmf_trans_cnqf(dev, socket_power, time_elapsed_ms);
	}

	dev->start_time = ktime_to_ms(ktime_get());
	schedule_delayed_work(&dev->work_buffer, msecs_to_jiffies(metrics_table_loop_ms));
}

int amd_pmf_set_dram_addr(struct amd_pmf_dev *dev, bool alloc_buffer)
{
	u64 phys_addr;
	u32 hi, low;

	/* Get Metrics Table Address */
	if (alloc_buffer) {
		switch (dev->cpu_id) {
		case AMD_CPU_ID_PS:
		case AMD_CPU_ID_RMB:
			dev->mtable_size = sizeof(dev->m_table);
			break;
		case PCI_DEVICE_ID_AMD_1AH_M20H_ROOT:
		case PCI_DEVICE_ID_AMD_1AH_M60H_ROOT:
			dev->mtable_size = sizeof(dev->m_table_v2);
			break;
		default:
			dev_err(dev->dev, "Invalid CPU id: 0x%x\n", dev->cpu_id);
		}

		dev->buf = devm_kzalloc(dev->dev, dev->mtable_size, GFP_KERNEL);
		if (!dev->buf)
			return -ENOMEM;
	}

	phys_addr = virt_to_phys(dev->buf);
	hi = upper_32_bits(phys_addr);
	low = lower_32_bits(phys_addr);

	amd_pmf_send_cmd(dev, SET_DRAM_ADDR_HIGH, SET_CMD, hi, NULL);
	amd_pmf_send_cmd(dev, SET_DRAM_ADDR_LOW, SET_CMD, low, NULL);

	return 0;
}

int amd_pmf_init_metrics_table(struct amd_pmf_dev *dev)
{
	int ret;

	INIT_DELAYED_WORK(&dev->work_buffer, amd_pmf_get_metrics);

	ret = amd_pmf_set_dram_addr(dev, true);
	if (ret)
		return ret;

	/*
	 * Start collecting the metrics data after a small delay
	 * or else, we might end up getting stale values from PMFW.
	 */
	schedule_delayed_work(&dev->work_buffer, msecs_to_jiffies(metrics_table_loop_ms * 3));

	return 0;
}

void amd_pmf_set_device(struct device *p_device)
{
	pmf_device = p_device;
}

static int is_npu_metrics_supported(struct amd_pmf_dev *pdev)
{
	switch (pdev->cpu_id) {
	case PCI_DEVICE_ID_AMD_1AH_M20H_ROOT:
	case PCI_DEVICE_ID_AMD_1AH_M60H_ROOT:
	case PCI_DEVICE_ID_AMD_1AH_M80H_ROOT:
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static void amd_pmf_calculate_acc_npu_metrics(struct amd_pmf_dev *dev,
					      struct amd_pmf_npu_metrics *data)
{
	struct amd_pmf_metrics_iod *curr, *prev;
	u64 val;
	int i;

	curr = &dev->mtable_v3.iod;
	prev = &dev->prev_metrics.iod;

	val = amd_pmf_avg_acc_metric(curr->counter_acc, prev->counter_acc,
				     curr->npu_temp_acc, prev->npu_temp_acc);
	data->npu_temp = amd_pmf_q10_acc_to_int(val);
	val = amd_pmf_avg_acc_metric(curr->counter_acc, prev->counter_acc,
				     curr->npu_power_acc, prev->npu_power_acc);
	data->npu_power = amd_pmf_q10_acc_to_mW(val);
	val = amd_pmf_avg_acc_metric(curr->counter_acc, prev->counter_acc,
				     curr->npuhclk_freq_eff_acc,
				     prev->npuhclk_freq_eff_acc);
	data->mpnpuclk_freq = amd_pmf_q10_acc_to_int(val);
	val = amd_pmf_avg_acc_metric(curr->counter_acc, prev->counter_acc,
				     curr->aieclk_freq_eff_acc,
				     prev->aieclk_freq_eff_acc);
	data->npuclk_freq = amd_pmf_q10_acc_to_int(val);

	for (i = 0; i < ARRAY_SIZE(curr->npu_busy_acc); i++) {
		val = amd_pmf_avg_acc_metric(curr->counter_acc, prev->counter_acc,
					     curr->npu_busy_acc[i],
					     prev->npu_busy_acc[i]);
		data->npu_busy[i] = amd_pmf_q10_acc_to_int(val);
	}
}

static int amd_pmf_get_smu_metrics(struct amd_pmf_dev *dev, struct amd_pmf_npu_metrics *data)
{
	int ret, i;

	guard(mutex)(&dev->metrics_mutex);

	ret = is_npu_metrics_supported(dev);
	if (ret)
		return ret;

	memset(data, 0, sizeof(*data));

	switch (dev->cpu_id) {
	case PCI_DEVICE_ID_AMD_1AH_M20H_ROOT:
	case PCI_DEVICE_ID_AMD_1AH_M60H_ROOT:
		ret = amd_pmf_set_dram_addr(dev, true);
		if (ret)
			return ret;

		memset(dev->buf, 0, dev->mtable_size);

		/* Send SMU command to get NPU metrics */
		ret = amd_pmf_send_cmd(dev, SET_TRANSFER_TABLE, SET_CMD, METRICS_TABLE_ID, NULL);
		if (ret) {
			dev_err(dev->dev, "SMU command failed to get NPU metrics: %d\n", ret);
			return ret;
		}

		memcpy(&dev->m_table_v2, dev->buf, dev->mtable_size);

		data->npuclk_freq = dev->m_table_v2.npuclk_freq;
		for (i = 0; i < ARRAY_SIZE(data->npu_busy); i++)
			data->npu_busy[i] = dev->m_table_v2.npu_busy[i];
		data->npu_power = dev->m_table_v2.npu_power;
		data->mpnpuclk_freq = dev->m_table_v2.mpnpuclk_freq;
		data->npu_reads = dev->m_table_v2.npu_reads;
		data->npu_writes = dev->m_table_v2.npu_writes;
		break;
	case PCI_DEVICE_ID_AMD_1AH_M80H_ROOT:
		ret = amd_pmf_get_metrics_table_log_sample(dev);
		if (ret)
			return ret;

		memcpy_fromio(&dev->mtable_v3, dev->metrics_table_virt, sizeof(dev->mtable_v3));

		/*
		 * Ignore the first sample, as previous metrics is uninitialized (zero)
		 * Metrics are calculated as:
		 *	metrics = current_metrics - previous_metrics
		 * Skipping the initial sample ensures accurate delta calculations.
		 */
		if (!dev->npu_metrics_have_prev) {
			memcpy(&dev->prev_metrics, &dev->mtable_v3, sizeof(dev->mtable_v3));
			dev->npu_metrics_have_prev = true;
			return 0;
		}

		amd_pmf_calculate_acc_npu_metrics(dev, data);
		memcpy(&dev->prev_metrics, &dev->mtable_v3, sizeof(dev->mtable_v3));
		break;
	}

	return 0;
}

int amd_pmf_get_npu_data(struct amd_pmf_npu_metrics *info)
{
	struct amd_pmf_dev *pdev;

	if (!info)
		return -EINVAL;

	if (!pmf_device)
		return -ENODEV;

	pdev = dev_get_drvdata(pmf_device);
	if (!pdev)
		return -ENODEV;

	return amd_pmf_get_smu_metrics(pdev, info);
}
EXPORT_SYMBOL_NS_GPL(amd_pmf_get_npu_data, "AMD_PMF");
