// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "ras.h"
#include "ras_core_status.h"

#define RAS_SEQNO_FIFO_SIZE (128 * sizeof(uint64_t))

#define IS_LEAP_YEAR(x) ((x % 4 == 0 && x % 100 != 0) || x % 400 == 0)

static const char * const ras_block_name[] = {
	"umc",
	"sdma",
	"gfx",
	"mmhub",
	"athub",
	"pcie_bif",
	"hdp",
	"xgmi_wafl",
	"df",
	"smn",
	"sem",
	"mp0",
	"mp1",
	"fuse",
	"mca",
	"vcn",
	"jpeg",
	"ih",
	"mpio",
};

const char *ras_core_get_ras_block_name(enum ras_block_id block_id)
{
	if (block_id >= ARRAY_SIZE(ras_block_name))
		return "";

	return ras_block_name[block_id];
}

int ras_core_convert_timestamp_to_time(struct ras_core_context *ras_core,
			uint64_t timestamp, struct ras_time *tm)
{
	int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	uint64_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
	uint32_t year = 0;
	int seconds_per_day = 24 * 60 * 60;
	int seconds_per_hour = 60 * 60;
	int seconds_per_minute = 60;
	int days, remaining_seconds;

	days = div64_u64_rem(timestamp, seconds_per_day, (uint64_t *)&remaining_seconds);

	/* utc_timestamp follows the Unix epoch */
	year = 1970;
	while (days >= 365) {
		if (IS_LEAP_YEAR(year)) {
			if (days < 366)
				break;
			days -= 366;
		} else {
			days -= 365;
		}
		year++;
	}

	days_in_month[1] += IS_LEAP_YEAR(year);

	month = 0;
	while (days >= days_in_month[month]) {
		days -= days_in_month[month];
		month++;
	}
	month++;
	day = days + 1;

	if (remaining_seconds) {
		hour = remaining_seconds / seconds_per_hour;
		minute = (remaining_seconds % seconds_per_hour) / seconds_per_minute;
		second = remaining_seconds % seconds_per_minute;
	}

	tm->tm_year = year;
	tm->tm_mon = month;
	tm->tm_mday = day;
	tm->tm_hour = hour;
	tm->tm_min = minute;
	tm->tm_sec = second;

	return 0;
}

bool ras_core_gpu_in_reset(struct ras_core_context *ras_core)
{
	uint32_t status = 0;

	if (ras_core->sys_fn &&
		ras_core->sys_fn->check_gpu_status)
		ras_core->sys_fn->check_gpu_status(ras_core, &status);

	return (status & RAS_GPU_STATUS__IN_RESET) ? true : false;
}

bool ras_core_gpu_is_vf(struct ras_core_context *ras_core)
{
	uint32_t status = 0;

	if (ras_core->sys_fn &&
		ras_core->sys_fn->check_gpu_status)
		ras_core->sys_fn->check_gpu_status(ras_core, &status);

	return (status & RAS_GPU_STATUS__IS_VF) ? true : false;
}

bool ras_core_gpu_is_rma(struct ras_core_context *ras_core)
{
	if (!ras_core)
		return false;

	return ras_core->is_rma;
}

static int ras_core_seqno_fifo_write(struct ras_core_context *ras_core,
		enum ras_seqno_fifo fifo_type, uint64_t seqno)
{
	int ret = 0;
	struct kfifo *seqno_fifo = NULL;

	if (fifo_type == SEQNO_FIFO_POISON_CREATION)
		seqno_fifo = &ras_core->de_seqno_fifo;
	else if (fifo_type == SEQNO_FIFO_POISON_CONSUMPTION)
		seqno_fifo = &ras_core->consumption_seqno_fifo;

	if (seqno_fifo)
		ret = kfifo_in_spinlocked(seqno_fifo,
			&seqno, sizeof(seqno), &ras_core->seqno_lock);

	return ret ? 0 : -EINVAL;
}

static int ras_core_seqno_fifo_read(struct ras_core_context *ras_core,
		enum ras_seqno_fifo fifo_type, uint64_t *seqno, bool pop)
{
	int ret = 0;
	struct kfifo *seqno_fifo = NULL;

	if (fifo_type == SEQNO_FIFO_POISON_CREATION)
		seqno_fifo = &ras_core->de_seqno_fifo;
	else if (fifo_type == SEQNO_FIFO_POISON_CONSUMPTION)
		seqno_fifo = &ras_core->consumption_seqno_fifo;

	if (seqno_fifo) {
		if (pop)
			ret = kfifo_out_spinlocked(seqno_fifo,
				seqno, sizeof(*seqno), &ras_core->seqno_lock);
		else
			ret = kfifo_out_peek(seqno_fifo, seqno, sizeof(*seqno));
	}

	return ret ? 0 : -EINVAL;
}

uint64_t ras_core_gen_seqno(struct ras_core_context *ras_core,
			enum ras_seqno_type type)
{
	uint64_t seqno = 0;

	if (ras_core->sys_fn &&
		ras_core->sys_fn->gen_seqno)
		ras_core->sys_fn->gen_seqno(ras_core, type, &seqno);

	return seqno;
}

int ras_core_put_seqno(struct ras_core_context *ras_core,
		enum ras_seqno_type seqno_type, uint64_t seqno)
{
	int ret = 0;

	if (seqno_type >= RAS_SEQNO_TYPE_COUNT_MAX)
		return -EINVAL;

	if (seqno_type == RAS_SEQNO_TYPE_DE)
		ret = ras_core_seqno_fifo_write(ras_core,
				SEQNO_FIFO_POISON_CREATION, seqno);
	else if (seqno_type == RAS_SEQNO_TYPE_POISON_CONSUMPTION)
		ret = ras_core_seqno_fifo_write(ras_core,
				SEQNO_FIFO_POISON_CONSUMPTION, seqno);
	else
		ret = -EINVAL;

	return ret;
}

uint64_t ras_core_get_seqno(struct ras_core_context *ras_core,
			enum ras_seqno_type seqno_type, bool pop)
{
	uint64_t seq_no;
	int ret = -ENODATA;

	if (seqno_type >= RAS_SEQNO_TYPE_COUNT_MAX)
		return 0;

	if (seqno_type == RAS_SEQNO_TYPE_DE)
		ret = ras_core_seqno_fifo_read(ras_core,
				SEQNO_FIFO_POISON_CREATION, &seq_no, pop);
	else if (seqno_type == RAS_SEQNO_TYPE_POISON_CONSUMPTION)
		ret = ras_core_seqno_fifo_read(ras_core,
				SEQNO_FIFO_POISON_CONSUMPTION, &seq_no, pop);

	if (ret)
		seq_no = ras_core_gen_seqno(ras_core, seqno_type);

	return seq_no;
}

static int ras_core_eeprom_recovery(struct ras_core_context *ras_core)
{
	int count;
	int ret;

	count = ras_eeprom_get_record_count(ras_core);
	if (!count)
		return 0;

	/* Avoid bad page to be loaded again after gpu reset */
	if (ras_umc_get_saved_eeprom_count(ras_core) >= count)
		return 0;

	ret = ras_umc_load_bad_pages(ras_core);
	if (ret) {
		RAS_DEV_ERR(ras_core->dev, "ras_umc_load_bad_pages failed: %d\n", ret);
		return ret;
	}

	ras_eeprom_sync_info(ras_core);

	return ret;
}

struct ras_core_context *ras_core_create(struct ras_core_config *init_config)
{
	struct ras_core_context *ras_core;
	struct ras_core_config *config;

	ras_core = kzalloc(sizeof(*ras_core), GFP_KERNEL);
	if (!ras_core)
		return NULL;

	config = kzalloc(sizeof(*config), GFP_KERNEL);
	if (!config) {
		kfree(ras_core);
		return NULL;
	}

	memcpy(config, init_config, sizeof(*config));
	ras_core->config = config;

	return ras_core;
}

void ras_core_destroy(struct ras_core_context *ras_core)
{
	if (ras_core)
		kfree(ras_core->config);

	kfree(ras_core);
}

int ras_core_sw_init(struct ras_core_context *ras_core)
{
	int ret;

	if (!ras_core->config) {
		RAS_DEV_ERR(ras_core->dev, "No ras core config!\n");
		return -EINVAL;
	}

	ras_core->sys_fn = ras_core->config->sys_fn;
	if (!ras_core->sys_fn)
		return -EINVAL;

	ret = kfifo_alloc(&ras_core->de_seqno_fifo,
		 RAS_SEQNO_FIFO_SIZE, GFP_KERNEL);
	if (ret)
		return ret;

	ret = kfifo_alloc(&ras_core->consumption_seqno_fifo,
		 RAS_SEQNO_FIFO_SIZE, GFP_KERNEL);
	if (ret)
		return ret;

	spin_lock_init(&ras_core->seqno_lock);

	ret = ras_aca_sw_init(ras_core);
	if (ret)
		return ret;

	ret = ras_umc_sw_init(ras_core);
	if (ret)
		return ret;

	ret = ras_cmd_init(ras_core);
	if (ret)
		return ret;

	ret = ras_log_ring_sw_init(ras_core);
	if (ret)
		return ret;

	ret = ras_psp_sw_init(ras_core);
	if (ret)
		return ret;

	return 0;
}

int ras_core_sw_fini(struct ras_core_context *ras_core)
{
	kfifo_free(&ras_core->de_seqno_fifo);
	kfifo_free(&ras_core->consumption_seqno_fifo);

	ras_psp_sw_fini(ras_core);
	ras_log_ring_sw_fini(ras_core);
	ras_cmd_fini(ras_core);
	ras_umc_sw_fini(ras_core);
	ras_aca_sw_fini(ras_core);

	return 0;
}

int ras_core_hw_init(struct ras_core_context *ras_core)
{
	int ret;

	ras_core->ras_eeprom_supported =
			ras_core->config->ras_eeprom_supported;

	ras_core->poison_supported = ras_core->config->poison_supported;

	ret = ras_psp_hw_init(ras_core);
	if (ret)
		return ret;

	ret = ras_aca_hw_init(ras_core);
	if (ret)
		goto init_err1;

	ret = ras_mp1_hw_init(ras_core);
	if (ret)
		goto init_err2;

	ret = ras_nbio_hw_init(ras_core);
	if (ret)
		goto init_err3;

	ret = ras_umc_hw_init(ras_core);
	if (ret)
		goto init_err4;

	ret = ras_gfx_hw_init(ras_core);
	if (ret)
		goto init_err5;

	ret = ras_eeprom_hw_init(ras_core);
	if (ret)
		goto init_err6;

	ret = ras_core_eeprom_recovery(ras_core);
	if (ret) {
		RAS_DEV_ERR(ras_core->dev,
			"Failed to recovery ras core, ret:%d\n", ret);
		goto init_err6;
	}

	ret = ras_eeprom_check_storage_status(ras_core);
	if (ret)
		goto init_err6;

	ret = ras_process_init(ras_core);
	if (ret)
		goto init_err7;

	ras_core->is_initialized = true;

	return 0;

init_err7:
	ras_eeprom_hw_fini(ras_core);
init_err6:
	ras_gfx_hw_fini(ras_core);
init_err5:
	ras_umc_hw_fini(ras_core);
init_err4:
	ras_nbio_hw_fini(ras_core);
init_err3:
	ras_mp1_hw_fini(ras_core);
init_err2:
	ras_aca_hw_fini(ras_core);
init_err1:
	ras_psp_hw_fini(ras_core);
	return ret;
}

int ras_core_hw_fini(struct ras_core_context *ras_core)
{
	ras_core->is_initialized = false;

	ras_process_fini(ras_core);
	ras_eeprom_hw_fini(ras_core);
	ras_gfx_hw_fini(ras_core);
	ras_nbio_hw_fini(ras_core);
	ras_umc_hw_fini(ras_core);
	ras_mp1_hw_fini(ras_core);
	ras_aca_hw_fini(ras_core);
	ras_psp_hw_fini(ras_core);

	return 0;
}

bool ras_core_handle_nbio_irq(struct ras_core_context *ras_core, void *data)
{
	return ras_nbio_handle_irq_error(ras_core, data);
}

int ras_core_handle_fatal_error(struct ras_core_context *ras_core)
{
	int ret = 0;

	ras_aca_mark_fatal_flag(ras_core);

	ret = ras_core_event_notify(ras_core,
			RAS_EVENT_ID__FATAL_ERROR_DETECTED, NULL);

	return ret;
}

uint32_t ras_core_get_curr_nps_mode(struct ras_core_context *ras_core)
{
	if (ras_core->ras_nbio.ip_func &&
	    ras_core->ras_nbio.ip_func->get_memory_partition_mode)
		return ras_core->ras_nbio.ip_func->get_memory_partition_mode(ras_core);

	RAS_DEV_ERR(ras_core->dev, "Failed to get gpu memory nps mode!\n");
	return 0;
}

int ras_core_update_ecc_info(struct ras_core_context *ras_core)
{
	int ret;

	ret = ras_aca_update_ecc(ras_core, RAS_ERR_TYPE__CE, NULL);
	if (!ret)
		ret = ras_aca_update_ecc(ras_core, RAS_ERR_TYPE__UE, NULL);

	return ret;
}

int ras_core_query_block_ecc_data(struct ras_core_context *ras_core,
			enum ras_block_id block, struct ras_ecc_count *ecc_count)
{
	int ret;

	if (!ecc_count || (block >= RAS_BLOCK_ID__LAST) || !ras_core)
		return -EINVAL;

	ret = ras_aca_get_block_ecc_count(ras_core, block, ecc_count);
	if (!ret)
		ras_aca_clear_block_new_ecc_count(ras_core, block);

	return ret;
}

int ras_core_set_status(struct ras_core_context *ras_core, bool enable)
{
	ras_core->ras_core_enabled = enable;

	return 0;
}

bool ras_core_is_enabled(struct ras_core_context *ras_core)
{
	return ras_core->ras_core_enabled;
}

uint64_t ras_core_get_utc_second_timestamp(struct ras_core_context *ras_core)
{
	if (ras_core && ras_core->sys_fn &&
		ras_core->sys_fn->get_utc_second_timestamp)
		return ras_core->sys_fn->get_utc_second_timestamp(ras_core);

	RAS_DEV_ERR(ras_core->dev, "Failed to get system timestamp!\n");
	return 0;
}

int ras_core_translate_soc_pa_and_bank(struct ras_core_context *ras_core,
	uint64_t *soc_pa, struct umc_bank_addr *bank_addr, bool bank_to_pa)
{
	if (!ras_core || !soc_pa || !bank_addr)
		return -EINVAL;

	return ras_umc_translate_soc_pa_and_bank(ras_core, soc_pa, bank_addr, bank_to_pa);
}

bool ras_core_ras_interrupt_detected(struct ras_core_context *ras_core)
{
	if (ras_core && ras_core->sys_fn &&
		ras_core->sys_fn->detect_ras_interrupt)
		return ras_core->sys_fn->detect_ras_interrupt(ras_core);

	RAS_DEV_ERR(ras_core->dev, "Failed to detect ras interrupt!\n");
	return false;
}

int ras_core_get_gpu_mem(struct ras_core_context *ras_core,
	enum gpu_mem_type mem_type, struct gpu_mem_block *gpu_mem)
{
	if (ras_core->sys_fn && ras_core->sys_fn->get_gpu_mem)
		return ras_core->sys_fn->get_gpu_mem(ras_core, mem_type, gpu_mem);

	RAS_DEV_ERR(ras_core->dev, "Not config get gpu memory API!\n");
	return -EACCES;
}

int ras_core_put_gpu_mem(struct ras_core_context *ras_core,
	enum gpu_mem_type mem_type, struct gpu_mem_block *gpu_mem)
{
	if (ras_core->sys_fn && ras_core->sys_fn->put_gpu_mem)
		return ras_core->sys_fn->put_gpu_mem(ras_core, mem_type, gpu_mem);

	RAS_DEV_ERR(ras_core->dev, "Not config put gpu memory API!!\n");
	return -EACCES;
}

bool ras_core_is_ready(struct ras_core_context *ras_core)
{
	return ras_core ? ras_core->is_initialized : false;
}

bool ras_core_check_safety_watermark(struct ras_core_context *ras_core)
{
	return ras_eeprom_check_safety_watermark(ras_core);
}

int ras_core_down_trylock_gpu_reset_lock(struct ras_core_context *ras_core)
{
	if (ras_core->sys_fn && ras_core->sys_fn->gpu_reset_lock)
		return ras_core->sys_fn->gpu_reset_lock(ras_core, true, true);

	return 1;
}

void ras_core_down_gpu_reset_lock(struct ras_core_context *ras_core)
{
	if (ras_core->sys_fn && ras_core->sys_fn->gpu_reset_lock)
		ras_core->sys_fn->gpu_reset_lock(ras_core, true, false);
}

void ras_core_up_gpu_reset_lock(struct ras_core_context *ras_core)
{
	if (ras_core->sys_fn && ras_core->sys_fn->gpu_reset_lock)
		ras_core->sys_fn->gpu_reset_lock(ras_core, false, false);
}

int ras_core_event_notify(struct ras_core_context *ras_core,
		enum ras_notify_event event_id, void *data)
{
	if (ras_core && ras_core->sys_fn &&
		ras_core->sys_fn->ras_notifier)
		return ras_core->sys_fn->ras_notifier(ras_core, event_id, data);

	return -RAS_CORE_NOT_SUPPORTED;
}

int ras_core_get_device_system_info(struct ras_core_context *ras_core,
		struct device_system_info *dev_info)
{
	if (ras_core && ras_core->sys_fn &&
		ras_core->sys_fn->get_device_system_info)
		return ras_core->sys_fn->get_device_system_info(ras_core, dev_info);

	return -RAS_CORE_NOT_SUPPORTED;
}

int ras_core_convert_soc_pa_to_cur_nps_pages(struct ras_core_context *ras_core,
		uint64_t soc_pa, uint64_t *page_pfn, uint32_t max_pages)
{
	struct eeprom_umc_record record;
	uint32_t cur_nps_mode;
	int count = 0;

	if (!ras_core || !page_pfn || !max_pages)
		return -EINVAL;

	cur_nps_mode = ras_core_get_curr_nps_mode(ras_core);
	if (!cur_nps_mode || cur_nps_mode > UMC_MEMORY_PARTITION_MODE_NPS8)
		return -EINVAL;

	memset(&record, 0, sizeof(record));
	record.cur_nps_retired_row_pfn = RAS_ADDR_TO_PFN(soc_pa);

	count = ras_umc_convert_record_to_nps_pages(ras_core,
				&record, cur_nps_mode, page_pfn, max_pages);

	return count;
}
