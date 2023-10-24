// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#define pr_fmt(fmt)	"habanalabs: " fmt

#include <uapi/drm/habanalabs_accel.h>
#include "habanalabs.h"

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <asm/msr.h>

static u32 hl_debug_struct_size[HL_DEBUG_OP_TIMESTAMP + 1] = {
	[HL_DEBUG_OP_ETR] = sizeof(struct hl_debug_params_etr),
	[HL_DEBUG_OP_ETF] = sizeof(struct hl_debug_params_etf),
	[HL_DEBUG_OP_STM] = sizeof(struct hl_debug_params_stm),
	[HL_DEBUG_OP_FUNNEL] = 0,
	[HL_DEBUG_OP_BMON] = sizeof(struct hl_debug_params_bmon),
	[HL_DEBUG_OP_SPMU] = sizeof(struct hl_debug_params_spmu),
	[HL_DEBUG_OP_TIMESTAMP] = 0

};

static int device_status_info(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_device_status dev_stat = {0};
	u32 size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!size) || (!out))
		return -EINVAL;

	dev_stat.status = hl_device_status(hdev);

	return copy_to_user(out, &dev_stat,
			min((size_t)size, sizeof(dev_stat))) ? -EFAULT : 0;
}

static int hw_ip_info(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_hw_ip_info hw_ip = {0};
	u32 size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 sram_kmd_size, dram_kmd_size, dram_available_size;

	if ((!size) || (!out))
		return -EINVAL;

	sram_kmd_size = (prop->sram_user_base_address -
				prop->sram_base_address);
	dram_kmd_size = (prop->dram_user_base_address -
				prop->dram_base_address);

	hw_ip.device_id = hdev->asic_funcs->get_pci_id(hdev);
	hw_ip.sram_base_address = prop->sram_user_base_address;
	hw_ip.dram_base_address =
			prop->dram_supports_virtual_memory ?
			prop->dmmu.start_addr : prop->dram_user_base_address;
	hw_ip.tpc_enabled_mask = prop->tpc_enabled_mask & 0xFF;
	hw_ip.tpc_enabled_mask_ext = prop->tpc_enabled_mask;

	hw_ip.sram_size = prop->sram_size - sram_kmd_size;

	dram_available_size = prop->dram_size - dram_kmd_size;

	hw_ip.dram_size = DIV_ROUND_DOWN_ULL(dram_available_size, prop->dram_page_size) *
				prop->dram_page_size;

	if (hw_ip.dram_size > PAGE_SIZE)
		hw_ip.dram_enabled = 1;

	hw_ip.dram_page_size = prop->dram_page_size;
	hw_ip.device_mem_alloc_default_page_size = prop->device_mem_alloc_default_page_size;
	hw_ip.num_of_events = prop->num_of_events;

	memcpy(hw_ip.cpucp_version, prop->cpucp_info.cpucp_version,
		min(VERSION_MAX_LEN, HL_INFO_VERSION_MAX_LEN));

	memcpy(hw_ip.card_name, prop->cpucp_info.card_name,
		min(CARD_NAME_MAX_LEN, HL_INFO_CARD_NAME_MAX_LEN));

	hw_ip.cpld_version = le32_to_cpu(prop->cpucp_info.cpld_version);
	hw_ip.module_id = le32_to_cpu(prop->cpucp_info.card_location);

	hw_ip.psoc_pci_pll_nr = prop->psoc_pci_pll_nr;
	hw_ip.psoc_pci_pll_nf = prop->psoc_pci_pll_nf;
	hw_ip.psoc_pci_pll_od = prop->psoc_pci_pll_od;
	hw_ip.psoc_pci_pll_div_factor = prop->psoc_pci_pll_div_factor;

	hw_ip.decoder_enabled_mask = prop->decoder_enabled_mask;
	hw_ip.mme_master_slave_mode = prop->mme_master_slave_mode;
	hw_ip.first_available_interrupt_id = prop->first_available_user_interrupt;
	hw_ip.number_of_user_interrupts = prop->user_interrupt_count;
	hw_ip.tpc_interrupt_id = prop->tpc_interrupt_id;

	hw_ip.edma_enabled_mask = prop->edma_enabled_mask;
	hw_ip.server_type = prop->server_type;
	hw_ip.security_enabled = prop->fw_security_enabled;
	hw_ip.revision_id = hdev->pdev->revision;
	hw_ip.rotator_enabled_mask = prop->rotator_enabled_mask;
	hw_ip.engine_core_interrupt_reg_addr = prop->engine_core_interrupt_reg_addr;
	hw_ip.reserved_dram_size = dram_kmd_size;

	return copy_to_user(out, &hw_ip,
		min((size_t) size, sizeof(hw_ip))) ? -EFAULT : 0;
}

static int hw_events_info(struct hl_device *hdev, bool aggregate,
			struct hl_info_args *args)
{
	u32 size, max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	void *arr;

	if ((!max_size) || (!out))
		return -EINVAL;

	arr = hdev->asic_funcs->get_events_stat(hdev, aggregate, &size);
	if (!arr) {
		dev_err(hdev->dev, "Events info not supported\n");
		return -EOPNOTSUPP;
	}

	return copy_to_user(out, arr, min(max_size, size)) ? -EFAULT : 0;
}

static int events_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	u32 max_size = args->return_size;
	u64 events_mask;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((max_size < sizeof(u64)) || (!out))
		return -EINVAL;

	mutex_lock(&hpriv->notifier_event.lock);
	events_mask = hpriv->notifier_event.events_mask;
	hpriv->notifier_event.events_mask = 0;
	mutex_unlock(&hpriv->notifier_event.lock);

	return copy_to_user(out, &events_mask, sizeof(u64)) ? -EFAULT : 0;
}

static int dram_usage_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_info_dram_usage dram_usage = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 dram_kmd_size;

	if ((!max_size) || (!out))
		return -EINVAL;

	dram_kmd_size = (prop->dram_user_base_address -
				prop->dram_base_address);
	dram_usage.dram_free_mem = (prop->dram_size - dram_kmd_size) -
					atomic64_read(&hdev->dram_used_mem);
	if (hpriv->ctx)
		dram_usage.ctx_dram_mem =
			atomic64_read(&hpriv->ctx->dram_phys_mem);

	return copy_to_user(out, &dram_usage,
		min((size_t) max_size, sizeof(dram_usage))) ? -EFAULT : 0;
}

static int hw_idle(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_hw_idle hw_idle = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	hw_idle.is_idle = hdev->asic_funcs->is_device_idle(hdev,
					hw_idle.busy_engines_mask_ext,
					HL_BUSY_ENGINES_MASK_EXT_SIZE, NULL);
	hw_idle.busy_engines_mask =
			lower_32_bits(hw_idle.busy_engines_mask_ext[0]);

	return copy_to_user(out, &hw_idle,
		min((size_t) max_size, sizeof(hw_idle))) ? -EFAULT : 0;
}

static int debug_coresight(struct hl_device *hdev, struct hl_ctx *ctx, struct hl_debug_args *args)
{
	struct hl_debug_params *params;
	void *input = NULL, *output = NULL;
	int rc;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	params->reg_idx = args->reg_idx;
	params->enable = args->enable;
	params->op = args->op;

	if (args->input_ptr && args->input_size) {
		input = kzalloc(hl_debug_struct_size[args->op], GFP_KERNEL);
		if (!input) {
			rc = -ENOMEM;
			goto out;
		}

		if (copy_from_user(input, u64_to_user_ptr(args->input_ptr),
					args->input_size)) {
			rc = -EFAULT;
			dev_err(hdev->dev, "failed to copy input debug data\n");
			goto out;
		}

		params->input = input;
	}

	if (args->output_ptr && args->output_size) {
		output = kzalloc(args->output_size, GFP_KERNEL);
		if (!output) {
			rc = -ENOMEM;
			goto out;
		}

		params->output = output;
		params->output_size = args->output_size;
	}

	rc = hdev->asic_funcs->debug_coresight(hdev, ctx, params);
	if (rc) {
		dev_err(hdev->dev,
			"debug coresight operation failed %d\n", rc);
		goto out;
	}

	if (output && copy_to_user((void __user *) (uintptr_t) args->output_ptr,
					output, args->output_size)) {
		dev_err(hdev->dev, "copy to user failed in debug ioctl\n");
		rc = -EFAULT;
		goto out;
	}


out:
	kfree(params);
	kfree(output);
	kfree(input);

	return rc;
}

static int device_utilization(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_device_utilization device_util = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_device_utilization(hdev, &device_util.utilization);
	if (rc)
		return -EINVAL;

	return copy_to_user(out, &device_util,
		min((size_t) max_size, sizeof(device_util))) ? -EFAULT : 0;
}

static int get_clk_rate(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_clk_rate clk_rate = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_fw_get_clk_rate(hdev, &clk_rate.cur_clk_rate_mhz, &clk_rate.max_clk_rate_mhz);
	if (rc)
		return rc;

	return copy_to_user(out, &clk_rate, min_t(size_t, max_size, sizeof(clk_rate)))
										? -EFAULT : 0;
}

static int get_reset_count(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_reset_count reset_count = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	reset_count.hard_reset_cnt = hdev->reset_info.hard_reset_cnt;
	reset_count.soft_reset_cnt = hdev->reset_info.compute_reset_cnt;

	return copy_to_user(out, &reset_count,
		min((size_t) max_size, sizeof(reset_count))) ? -EFAULT : 0;
}

static int time_sync_info(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_time_sync time_sync = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	time_sync.device_time = hdev->asic_funcs->get_device_time(hdev);
	time_sync.host_time = ktime_get_raw_ns();
	time_sync.tsc_time = rdtsc();

	return copy_to_user(out, &time_sync,
		min((size_t) max_size, sizeof(time_sync))) ? -EFAULT : 0;
}

static int pci_counters_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_info_pci_counters pci_counters = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_fw_cpucp_pci_counters_get(hdev, &pci_counters);
	if (rc)
		return rc;

	return copy_to_user(out, &pci_counters,
		min((size_t) max_size, sizeof(pci_counters))) ? -EFAULT : 0;
}

static int clk_throttle_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_device *hdev = hpriv->hdev;
	struct hl_info_clk_throttle clk_throttle = {0};
	ktime_t end_time, zero_time = ktime_set(0, 0);
	u32 max_size = args->return_size;
	int i;

	if ((!max_size) || (!out))
		return -EINVAL;

	mutex_lock(&hdev->clk_throttling.lock);

	clk_throttle.clk_throttling_reason = hdev->clk_throttling.current_reason;

	for (i = 0 ; i < HL_CLK_THROTTLE_TYPE_MAX ; i++) {
		if (!(hdev->clk_throttling.aggregated_reason & BIT(i)))
			continue;

		clk_throttle.clk_throttling_timestamp_us[i] =
			ktime_to_us(hdev->clk_throttling.timestamp[i].start);

		if (ktime_compare(hdev->clk_throttling.timestamp[i].end, zero_time))
			end_time = hdev->clk_throttling.timestamp[i].end;
		else
			end_time = ktime_get();

		clk_throttle.clk_throttling_duration_ns[i] =
			ktime_to_ns(ktime_sub(end_time,
				hdev->clk_throttling.timestamp[i].start));

	}
	mutex_unlock(&hdev->clk_throttling.lock);

	return copy_to_user(out, &clk_throttle,
		min((size_t) max_size, sizeof(clk_throttle))) ? -EFAULT : 0;
}

static int cs_counters_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_info_cs_counters cs_counters = {0};
	struct hl_device *hdev = hpriv->hdev;
	struct hl_cs_counters_atomic *cntr;
	u32 max_size = args->return_size;

	cntr = &hdev->aggregated_cs_counters;

	if ((!max_size) || (!out))
		return -EINVAL;

	cs_counters.total_out_of_mem_drop_cnt =
			atomic64_read(&cntr->out_of_mem_drop_cnt);
	cs_counters.total_parsing_drop_cnt =
			atomic64_read(&cntr->parsing_drop_cnt);
	cs_counters.total_queue_full_drop_cnt =
			atomic64_read(&cntr->queue_full_drop_cnt);
	cs_counters.total_device_in_reset_drop_cnt =
			atomic64_read(&cntr->device_in_reset_drop_cnt);
	cs_counters.total_max_cs_in_flight_drop_cnt =
			atomic64_read(&cntr->max_cs_in_flight_drop_cnt);
	cs_counters.total_validation_drop_cnt =
			atomic64_read(&cntr->validation_drop_cnt);

	if (hpriv->ctx) {
		cs_counters.ctx_out_of_mem_drop_cnt =
				atomic64_read(
				&hpriv->ctx->cs_counters.out_of_mem_drop_cnt);
		cs_counters.ctx_parsing_drop_cnt =
				atomic64_read(
				&hpriv->ctx->cs_counters.parsing_drop_cnt);
		cs_counters.ctx_queue_full_drop_cnt =
				atomic64_read(
				&hpriv->ctx->cs_counters.queue_full_drop_cnt);
		cs_counters.ctx_device_in_reset_drop_cnt =
				atomic64_read(
			&hpriv->ctx->cs_counters.device_in_reset_drop_cnt);
		cs_counters.ctx_max_cs_in_flight_drop_cnt =
				atomic64_read(
			&hpriv->ctx->cs_counters.max_cs_in_flight_drop_cnt);
		cs_counters.ctx_validation_drop_cnt =
				atomic64_read(
				&hpriv->ctx->cs_counters.validation_drop_cnt);
	}

	return copy_to_user(out, &cs_counters,
		min((size_t) max_size, sizeof(cs_counters))) ? -EFAULT : 0;
}

static int sync_manager_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_info_sync_manager sm_info = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	if (args->dcore_id >= HL_MAX_DCORES)
		return -EINVAL;

	sm_info.first_available_sync_object =
			prop->first_available_user_sob[args->dcore_id];
	sm_info.first_available_monitor =
			prop->first_available_user_mon[args->dcore_id];
	sm_info.first_available_cq =
			prop->first_available_cq[args->dcore_id];

	return copy_to_user(out, &sm_info, min_t(size_t, (size_t) max_size,
			sizeof(sm_info))) ? -EFAULT : 0;
}

static int total_energy_consumption_info(struct hl_fpriv *hpriv,
			struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_info_energy total_energy = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_fw_cpucp_total_energy_get(hdev,
			&total_energy.total_energy_consumption);
	if (rc)
		return rc;

	return copy_to_user(out, &total_energy,
		min((size_t) max_size, sizeof(total_energy))) ? -EFAULT : 0;
}

static int pll_frequency_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_pll_frequency_info freq_info = { {0} };
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_fw_cpucp_pll_info_get(hdev, args->pll_index, freq_info.output);
	if (rc)
		return rc;

	return copy_to_user(out, &freq_info,
		min((size_t) max_size, sizeof(freq_info))) ? -EFAULT : 0;
}

static int power_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	struct hl_power_info power_info = {0};
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_fw_cpucp_power_get(hdev, &power_info.power);
	if (rc)
		return rc;

	return copy_to_user(out, &power_info,
		min((size_t) max_size, sizeof(power_info))) ? -EFAULT : 0;
}

static int open_stats_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	struct hl_open_stats_info open_stats_info = {0};
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	open_stats_info.last_open_period_ms = jiffies64_to_msecs(
		hdev->last_open_session_duration_jif);
	open_stats_info.open_counter = hdev->open_counter;
	open_stats_info.is_compute_ctx_active = hdev->is_compute_ctx_active;
	open_stats_info.compute_ctx_in_release = hdev->compute_ctx_in_release;

	return copy_to_user(out, &open_stats_info,
		min((size_t) max_size, sizeof(open_stats_info))) ? -EFAULT : 0;
}

static int dram_pending_rows_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	u32 pend_rows_num = 0;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_fw_dram_pending_row_get(hdev, &pend_rows_num);
	if (rc)
		return rc;

	return copy_to_user(out, &pend_rows_num,
			min_t(size_t, max_size, sizeof(pend_rows_num))) ? -EFAULT : 0;
}

static int dram_replaced_rows_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	struct cpucp_hbm_row_info info = {0};
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_fw_dram_replaced_row_get(hdev, &info);
	if (rc)
		return rc;

	return copy_to_user(out, &info, min_t(size_t, max_size, sizeof(info))) ? -EFAULT : 0;
}

static int last_err_open_dev_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_info_last_err_open_dev_time info = {0};
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	info.timestamp = ktime_to_ns(hdev->last_successful_open_ktime);

	return copy_to_user(out, &info, min_t(size_t, max_size, sizeof(info))) ? -EFAULT : 0;
}

static int cs_timeout_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_info_cs_timeout_event info = {0};
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	info.seq = hdev->captured_err_info.cs_timeout.seq;
	info.timestamp = ktime_to_ns(hdev->captured_err_info.cs_timeout.timestamp);

	return copy_to_user(out, &info, min_t(size_t, max_size, sizeof(info))) ? -EFAULT : 0;
}

static int razwi_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	struct razwi_info *razwi_info;

	if ((!max_size) || (!out))
		return -EINVAL;

	razwi_info = &hdev->captured_err_info.razwi_info;
	if (!razwi_info->razwi_info_available)
		return 0;

	return copy_to_user(out, &razwi_info->razwi,
			min_t(size_t, max_size, sizeof(struct hl_info_razwi_event))) ? -EFAULT : 0;
}

static int undefined_opcode_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	struct hl_info_undefined_opcode_event info = {0};
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	info.timestamp = ktime_to_ns(hdev->captured_err_info.undef_opcode.timestamp);
	info.engine_id = hdev->captured_err_info.undef_opcode.engine_id;
	info.cq_addr = hdev->captured_err_info.undef_opcode.cq_addr;
	info.cq_size = hdev->captured_err_info.undef_opcode.cq_size;
	info.stream_id = hdev->captured_err_info.undef_opcode.stream_id;
	info.cb_addr_streams_len = hdev->captured_err_info.undef_opcode.cb_addr_streams_len;
	memcpy(info.cb_addr_streams, hdev->captured_err_info.undef_opcode.cb_addr_streams,
			sizeof(info.cb_addr_streams));

	return copy_to_user(out, &info, min_t(size_t, max_size, sizeof(info))) ? -EFAULT : 0;
}

static int dev_mem_alloc_page_sizes_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_info_dev_memalloc_page_sizes info = {0};
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;

	if ((!max_size) || (!out))
		return -EINVAL;

	/*
	 * Future ASICs that will support multiple DRAM page sizes will support only "powers of 2"
	 * pages (unlike some of the ASICs before supporting multiple page sizes).
	 * For this reason for all ASICs that not support multiple page size the function will
	 * return an empty bitmask indicating that multiple page sizes is not supported.
	 */
	info.page_order_bitmask = hdev->asic_prop.dmmu.supported_pages_mask;

	return copy_to_user(out, &info, min_t(size_t, max_size, sizeof(info))) ? -EFAULT : 0;
}

static int sec_attest_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct cpucp_sec_attest_info *sec_attest_info;
	struct hl_info_sec_attest *info;
	u32 max_size = args->return_size;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	sec_attest_info = kmalloc(sizeof(*sec_attest_info), GFP_KERNEL);
	if (!sec_attest_info)
		return -ENOMEM;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		rc = -ENOMEM;
		goto free_sec_attest_info;
	}

	rc = hl_fw_get_sec_attest_info(hpriv->hdev, sec_attest_info, args->sec_attest_nonce);
	if (rc)
		goto free_info;

	info->nonce = le32_to_cpu(sec_attest_info->nonce);
	info->pcr_quote_len = le16_to_cpu(sec_attest_info->pcr_quote_len);
	info->pub_data_len = le16_to_cpu(sec_attest_info->pub_data_len);
	info->certificate_len = le16_to_cpu(sec_attest_info->certificate_len);
	info->pcr_num_reg = sec_attest_info->pcr_num_reg;
	info->pcr_reg_len = sec_attest_info->pcr_reg_len;
	info->quote_sig_len = sec_attest_info->quote_sig_len;
	memcpy(&info->pcr_data, &sec_attest_info->pcr_data, sizeof(info->pcr_data));
	memcpy(&info->pcr_quote, &sec_attest_info->pcr_quote, sizeof(info->pcr_quote));
	memcpy(&info->public_data, &sec_attest_info->public_data, sizeof(info->public_data));
	memcpy(&info->certificate, &sec_attest_info->certificate, sizeof(info->certificate));
	memcpy(&info->quote_sig, &sec_attest_info->quote_sig, sizeof(info->quote_sig));

	rc = copy_to_user(out, info,
				min_t(size_t, max_size, sizeof(*info))) ? -EFAULT : 0;

free_info:
	kfree(info);
free_sec_attest_info:
	kfree(sec_attest_info);

	return rc;
}

static int eventfd_register(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	int rc;

	/* check if there is already a registered on that process */
	mutex_lock(&hpriv->notifier_event.lock);
	if (hpriv->notifier_event.eventfd) {
		mutex_unlock(&hpriv->notifier_event.lock);
		return -EINVAL;
	}

	hpriv->notifier_event.eventfd = eventfd_ctx_fdget(args->eventfd);
	if (IS_ERR(hpriv->notifier_event.eventfd)) {
		rc = PTR_ERR(hpriv->notifier_event.eventfd);
		hpriv->notifier_event.eventfd = NULL;
		mutex_unlock(&hpriv->notifier_event.lock);
		return rc;
	}

	mutex_unlock(&hpriv->notifier_event.lock);
	return 0;
}

static int eventfd_unregister(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	mutex_lock(&hpriv->notifier_event.lock);
	if (!hpriv->notifier_event.eventfd) {
		mutex_unlock(&hpriv->notifier_event.lock);
		return -EINVAL;
	}

	eventfd_ctx_put(hpriv->notifier_event.eventfd);
	hpriv->notifier_event.eventfd = NULL;
	mutex_unlock(&hpriv->notifier_event.lock);
	return 0;
}

static int engine_status_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	u32 status_buf_size = args->return_size;
	struct hl_device *hdev = hpriv->hdev;
	struct engines_data eng_data;
	int rc;

	if ((status_buf_size < SZ_1K) || (status_buf_size > HL_ENGINES_DATA_MAX_SIZE) || (!out))
		return -EINVAL;

	eng_data.actual_size = 0;
	eng_data.allocated_buf_size = status_buf_size;
	eng_data.buf = vmalloc(status_buf_size);
	if (!eng_data.buf)
		return -ENOMEM;

	hdev->asic_funcs->is_device_idle(hdev, NULL, 0, &eng_data);

	if (eng_data.actual_size > eng_data.allocated_buf_size) {
		dev_err(hdev->dev,
			"Engines data size (%d Bytes) is bigger than allocated size (%u Bytes)\n",
			eng_data.actual_size, status_buf_size);
		vfree(eng_data.buf);
		return -ENOMEM;
	}

	args->user_buffer_actual_size = eng_data.actual_size;
	rc = copy_to_user(out, eng_data.buf, min_t(size_t, status_buf_size, eng_data.actual_size)) ?
				-EFAULT : 0;

	vfree(eng_data.buf);

	return rc;
}

static int page_fault_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	struct page_fault_info *pgf_info;

	if ((!max_size) || (!out))
		return -EINVAL;

	pgf_info = &hdev->captured_err_info.page_fault_info;
	if (!pgf_info->page_fault_info_available)
		return 0;

	return copy_to_user(out, &pgf_info->page_fault,
			min_t(size_t, max_size, sizeof(struct hl_page_fault_info))) ? -EFAULT : 0;
}

static int user_mappings_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	u32 user_buf_size = args->return_size;
	struct hl_device *hdev = hpriv->hdev;
	struct page_fault_info *pgf_info;
	u64 actual_size;

	if (!out)
		return -EINVAL;

	pgf_info = &hdev->captured_err_info.page_fault_info;
	if (!pgf_info->page_fault_info_available)
		return 0;

	args->array_size = pgf_info->num_of_user_mappings;

	actual_size = pgf_info->num_of_user_mappings * sizeof(struct hl_user_mapping);
	if (user_buf_size < actual_size)
		return -ENOMEM;

	return copy_to_user(out, pgf_info->user_mappings, actual_size) ? -EFAULT : 0;
}

static int hw_err_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *user_buf = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_device *hdev = hpriv->hdev;
	u32 user_buf_size = args->return_size;
	struct hw_err_info *info;
	int rc;

	if (!user_buf)
		return -EINVAL;

	info = &hdev->captured_err_info.hw_err;
	if (!info->event_info_available)
		return 0;

	if (user_buf_size < sizeof(struct hl_info_hw_err_event))
		return -ENOMEM;

	rc = copy_to_user(user_buf, &info->event, sizeof(struct hl_info_hw_err_event));
	return rc ? -EFAULT : 0;
}

static int fw_err_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *user_buf = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_device *hdev = hpriv->hdev;
	u32 user_buf_size = args->return_size;
	struct fw_err_info *info;
	int rc;

	if (!user_buf)
		return -EINVAL;

	info = &hdev->captured_err_info.fw_err;
	if (!info->event_info_available)
		return 0;

	if (user_buf_size < sizeof(struct hl_info_fw_err_event))
		return -ENOMEM;

	rc = copy_to_user(user_buf, &info->event, sizeof(struct hl_info_fw_err_event));
	return rc ? -EFAULT : 0;
}

static int engine_err_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *user_buf = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_device *hdev = hpriv->hdev;
	u32 user_buf_size = args->return_size;
	struct engine_err_info *info;
	int rc;

	if (!user_buf)
		return -EINVAL;

	info = &hdev->captured_err_info.engine_err;
	if (!info->event_info_available)
		return 0;

	if (user_buf_size < sizeof(struct hl_info_engine_err_event))
		return -ENOMEM;

	rc = copy_to_user(user_buf, &info->event, sizeof(struct hl_info_engine_err_event));
	return rc ? -EFAULT : 0;
}

static int send_fw_generic_request(struct hl_device *hdev, struct hl_info_args *info_args)
{
	void __user *buff = (void __user *) (uintptr_t) info_args->return_pointer;
	u32 size = info_args->return_size;
	dma_addr_t dma_handle;
	bool need_input_buff;
	void *fw_buff;
	int rc = 0;

	switch (info_args->fw_sub_opcode) {
	case HL_PASSTHROUGH_VERSIONS:
		need_input_buff = false;
		break;
	default:
		return -EINVAL;
	}

	if (size > SZ_1M) {
		dev_err(hdev->dev, "buffer size cannot exceed 1MB\n");
		return -EINVAL;
	}

	fw_buff = hl_cpu_accessible_dma_pool_alloc(hdev, size, &dma_handle);
	if (!fw_buff)
		return -ENOMEM;


	if (need_input_buff && copy_from_user(fw_buff, buff, size)) {
		dev_dbg(hdev->dev, "Failed to copy from user FW buff\n");
		rc = -EFAULT;
		goto free_buff;
	}

	rc = hl_fw_send_generic_request(hdev, info_args->fw_sub_opcode, dma_handle, &size);
	if (rc)
		goto free_buff;

	if (copy_to_user(buff, fw_buff, min(size, info_args->return_size))) {
		dev_dbg(hdev->dev, "Failed to copy to user FW generic req output\n");
		rc = -EFAULT;
	}

free_buff:
	hl_cpu_accessible_dma_pool_free(hdev, info_args->return_size, fw_buff);

	return rc;
}

static int _hl_info_ioctl(struct hl_fpriv *hpriv, void *data,
				struct device *dev)
{
	enum hl_device_status status;
	struct hl_info_args *args = data;
	struct hl_device *hdev = hpriv->hdev;
	int rc;

	if (args->pad) {
		dev_dbg(hdev->dev, "Padding bytes must be 0\n");
		return -EINVAL;
	}

	/*
	 * Information is returned for the following opcodes even if the device
	 * is disabled or in reset.
	 */
	switch (args->op) {
	case HL_INFO_HW_IP_INFO:
		return hw_ip_info(hdev, args);

	case HL_INFO_DEVICE_STATUS:
		return device_status_info(hdev, args);

	case HL_INFO_RESET_COUNT:
		return get_reset_count(hdev, args);

	case HL_INFO_HW_EVENTS:
		return hw_events_info(hdev, false, args);

	case HL_INFO_HW_EVENTS_AGGREGATE:
		return hw_events_info(hdev, true, args);

	case HL_INFO_CS_COUNTERS:
		return cs_counters_info(hpriv, args);

	case HL_INFO_CLK_THROTTLE_REASON:
		return clk_throttle_info(hpriv, args);

	case HL_INFO_SYNC_MANAGER:
		return sync_manager_info(hpriv, args);

	case HL_INFO_OPEN_STATS:
		return open_stats_info(hpriv, args);

	case HL_INFO_LAST_ERR_OPEN_DEV_TIME:
		return last_err_open_dev_info(hpriv, args);

	case HL_INFO_CS_TIMEOUT_EVENT:
		return cs_timeout_info(hpriv, args);

	case HL_INFO_RAZWI_EVENT:
		return razwi_info(hpriv, args);

	case HL_INFO_UNDEFINED_OPCODE_EVENT:
		return undefined_opcode_info(hpriv, args);

	case HL_INFO_DEV_MEM_ALLOC_PAGE_SIZES:
		return dev_mem_alloc_page_sizes_info(hpriv, args);

	case HL_INFO_GET_EVENTS:
		return events_info(hpriv, args);

	case HL_INFO_PAGE_FAULT_EVENT:
		return page_fault_info(hpriv, args);

	case HL_INFO_USER_MAPPINGS:
		return user_mappings_info(hpriv, args);

	case HL_INFO_UNREGISTER_EVENTFD:
		return eventfd_unregister(hpriv, args);

	case HL_INFO_HW_ERR_EVENT:
		return hw_err_info(hpriv, args);

	case HL_INFO_FW_ERR_EVENT:
		return fw_err_info(hpriv, args);

	case HL_INFO_USER_ENGINE_ERR_EVENT:
		return engine_err_info(hpriv, args);

	case HL_INFO_DRAM_USAGE:
		return dram_usage_info(hpriv, args);
	default:
		break;
	}

	if (!hl_device_operational(hdev, &status)) {
		dev_dbg_ratelimited(dev,
			"Device is %s. Can't execute INFO IOCTL\n",
			hdev->status[status]);
		return -EBUSY;
	}

	switch (args->op) {
	case HL_INFO_HW_IDLE:
		rc = hw_idle(hdev, args);
		break;

	case HL_INFO_DEVICE_UTILIZATION:
		rc = device_utilization(hdev, args);
		break;

	case HL_INFO_CLK_RATE:
		rc = get_clk_rate(hdev, args);
		break;

	case HL_INFO_TIME_SYNC:
		return time_sync_info(hdev, args);

	case HL_INFO_PCI_COUNTERS:
		return pci_counters_info(hpriv, args);

	case HL_INFO_TOTAL_ENERGY:
		return total_energy_consumption_info(hpriv, args);

	case HL_INFO_PLL_FREQUENCY:
		return pll_frequency_info(hpriv, args);

	case HL_INFO_POWER:
		return power_info(hpriv, args);


	case HL_INFO_DRAM_REPLACED_ROWS:
		return dram_replaced_rows_info(hpriv, args);

	case HL_INFO_DRAM_PENDING_ROWS:
		return dram_pending_rows_info(hpriv, args);

	case HL_INFO_SECURED_ATTESTATION:
		return sec_attest_info(hpriv, args);

	case HL_INFO_REGISTER_EVENTFD:
		return eventfd_register(hpriv, args);

	case HL_INFO_ENGINE_STATUS:
		return engine_status_info(hpriv, args);

	case HL_INFO_FW_GENERIC_REQ:
		return send_fw_generic_request(hdev, args);

	default:
		dev_err(dev, "Invalid request %d\n", args->op);
		rc = -EINVAL;
		break;
	}

	return rc;
}

int hl_info_ioctl(struct drm_device *ddev, void *data, struct drm_file *file_priv)
{
	struct hl_fpriv *hpriv = file_priv->driver_priv;

	return _hl_info_ioctl(hpriv, data, hpriv->hdev->dev);
}

static int hl_info_ioctl_control(struct hl_fpriv *hpriv, void *data)
{
	struct hl_info_args *args = data;

	switch (args->op) {
	case HL_INFO_GET_EVENTS:
	case HL_INFO_UNREGISTER_EVENTFD:
	case HL_INFO_REGISTER_EVENTFD:
		return -EOPNOTSUPP;
	default:
		break;
	}

	return _hl_info_ioctl(hpriv, data, hpriv->hdev->dev_ctrl);
}

int hl_debug_ioctl(struct drm_device *ddev, void *data, struct drm_file *file_priv)
{
	struct hl_fpriv *hpriv = file_priv->driver_priv;
	struct hl_device *hdev = hpriv->hdev;
	struct hl_debug_args *args = data;
	enum hl_device_status status;

	int rc = 0;

	if (!hl_device_operational(hdev, &status)) {
		dev_dbg_ratelimited(hdev->dev,
			"Device is %s. Can't execute DEBUG IOCTL\n",
			hdev->status[status]);
		return -EBUSY;
	}

	switch (args->op) {
	case HL_DEBUG_OP_ETR:
	case HL_DEBUG_OP_ETF:
	case HL_DEBUG_OP_STM:
	case HL_DEBUG_OP_FUNNEL:
	case HL_DEBUG_OP_BMON:
	case HL_DEBUG_OP_SPMU:
	case HL_DEBUG_OP_TIMESTAMP:
		if (!hdev->in_debug) {
			dev_err_ratelimited(hdev->dev,
				"Rejecting debug configuration request because device not in debug mode\n");
			return -EFAULT;
		}
		args->input_size = min(args->input_size, hl_debug_struct_size[args->op]);
		rc = debug_coresight(hdev, hpriv->ctx, args);
		break;

	case HL_DEBUG_OP_SET_MODE:
		rc = hl_device_set_debug_mode(hdev, hpriv->ctx, (bool) args->enable);
		break;

	default:
		dev_err(hdev->dev, "Invalid request %d\n", args->op);
		rc = -EINVAL;
		break;
	}

	return rc;
}

#define HL_IOCTL_DEF(ioctl, _func) \
	[_IOC_NR(ioctl) - HL_COMMAND_START] = {.cmd = ioctl, .func = _func}

static const struct hl_ioctl_desc hl_ioctls_control[] = {
	HL_IOCTL_DEF(DRM_IOCTL_HL_INFO, hl_info_ioctl_control)
};

static long _hl_ioctl(struct hl_fpriv *hpriv, unsigned int cmd, unsigned long arg,
			const struct hl_ioctl_desc *ioctl, struct device *dev)
{
	unsigned int nr = _IOC_NR(cmd);
	char stack_kdata[128] = {0};
	char *kdata = NULL;
	unsigned int usize, asize;
	hl_ioctl_t *func;
	u32 hl_size;
	int retcode;

	/* Do not trust userspace, use our own definition */
	func = ioctl->func;

	if (unlikely(!func)) {
		dev_dbg(dev, "no function\n");
		retcode = -ENOTTY;
		goto out_err;
	}

	hl_size = _IOC_SIZE(ioctl->cmd);
	usize = asize = _IOC_SIZE(cmd);
	if (hl_size > asize)
		asize = hl_size;

	cmd = ioctl->cmd;

	if (cmd & (IOC_IN | IOC_OUT)) {
		if (asize <= sizeof(stack_kdata)) {
			kdata = stack_kdata;
		} else {
			kdata = kzalloc(asize, GFP_KERNEL);
			if (!kdata) {
				retcode = -ENOMEM;
				goto out_err;
			}
		}
	}

	if (cmd & IOC_IN) {
		if (copy_from_user(kdata, (void __user *)arg, usize)) {
			retcode = -EFAULT;
			goto out_err;
		}
	}

	retcode = func(hpriv, kdata);

	if ((cmd & IOC_OUT) && copy_to_user((void __user *)arg, kdata, usize))
		retcode = -EFAULT;

out_err:
	if (retcode) {
		char task_comm[TASK_COMM_LEN];

		dev_dbg_ratelimited(dev,
				"error in ioctl: pid=%d, comm=\"%s\", cmd=%#010x, nr=%#04x\n",
				task_pid_nr(current), get_task_comm(task_comm, current), cmd, nr);
	}

	if (kdata != stack_kdata)
		kfree(kdata);

	return retcode;
}

long hl_ioctl_control(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct hl_fpriv *hpriv = filep->private_data;
	struct hl_device *hdev = hpriv->hdev;
	const struct hl_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);

	if (!hdev) {
		pr_err_ratelimited("Sending ioctl after device was removed! Please close FD\n");
		return -ENODEV;
	}

	if (nr == _IOC_NR(DRM_IOCTL_HL_INFO)) {
		ioctl = &hl_ioctls_control[nr - HL_COMMAND_START];
	} else {
		char task_comm[TASK_COMM_LEN];

		dev_dbg_ratelimited(hdev->dev_ctrl,
				"invalid ioctl: pid=%d, comm=\"%s\", cmd=%#010x, nr=%#04x\n",
				task_pid_nr(current), get_task_comm(task_comm, current), cmd, nr);
		return -ENOTTY;
	}

	return _hl_ioctl(hpriv, cmd, arg, ioctl, hdev->dev_ctrl);
}
