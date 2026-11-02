/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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

#ifndef __RAS_EEPROM_MGR_H__
#define __RAS_EEPROM_MGR_H__
#include "ras_sys.h"

enum ras_gpu_op_status {
	RAS_GPU_OP_STATUS_UNKNOWN = 0,
	RAS_GPU_OP_STATUS_OK      = 1,
	RAS_GPU_OP_STATUS_LOCKED  = 2,
	RAS_GPU_OP_STATUS_FAULT   = 3,
	RAS_GPU_OP_STATUS_RMA     = 4,
};

enum ras_eeprom_status {
	RAS_EEPROM_UNKNOWN,
	/*Read and write */
	RAS_EEPROM_OK,
	/* Only read */
	RAS_EEPROM_LOCKED,
	/* Invalid data */
	RAS_EEPROM_FAULT,
};

struct ras_eeprom_param {
	u32 eeprom_ip_version;
	uint32_t record_threshold;
	void *eeprom_i2c_adapter;
	u32 eeprom_i2c_addr;
	u32 eeprom_i2c_port;
	u16 max_i2c_read_len;
	u16 max_i2c_write_len;
	const struct ras_eeprom_sys_func *sys_fn;
};

struct ras_eeprom_info {
	u32 record_format_version;
	u32 eeprom_status;
	u32 rma_status;
	/* Raw record count in eeprom, but data may be duplicated,
	 * therefore not a valid record count
	 */
	u32 record_count;
	u32 max_record_count;
	u64 bad_channel_bitmap;
};

struct ras_core_context;
struct eeprom_umc_record;

struct ras_eeprom_ops {
	int (*sw_init)(struct ras_core_context *ras_core, struct ras_eeprom_param *param);
	int (*sw_fini)(struct ras_core_context *ras_core);
	int (*hw_init)(struct ras_core_context *ras_core, struct ras_eeprom_param *param);
	int (*hw_fini)(struct ras_core_context *ras_core);
	int (*reset_table)(struct ras_core_context *ras_core);
	int (*get_records)(struct ras_core_context *ras_core, u32 start,
		struct eeprom_umc_record *records, u32 num);
	int (*append_records)(struct ras_core_context *ras_core,
		struct eeprom_umc_record *records, u32 num);
	int (*get_record_count)(struct ras_core_context *ras_core);
	int (*get_eeprom_info)(struct ras_core_context *ras_core,
		struct ras_eeprom_info *eeprom_info, bool fast_mode);
	int (*unlock_eeprom)(struct ras_core_context *ras_core);
};

struct ras_eeprom_mgr {
	u32 eeprom_ip_version;
	const struct ras_eeprom_sys_func *sys_func;
	void *ras_eeprom;
	const struct ras_eeprom_ops *eeprom_ops;
	u32 ras_err_threshold;
	u32 work_mode_over_thresh;
	bool eeprom_early_init_service_supported;
	bool fw_record_enabled;
};

struct ras_core_context;
int ras_eeprom_mgr_sw_init(struct ras_core_context *ras_core);
int ras_eeprom_mgr_sw_fini(struct ras_core_context *ras_core);
int ras_eeprom_mgr_hw_init(struct ras_core_context *ras_core);
int ras_eeprom_mgr_hw_fini(struct ras_core_context *ras_core);
int ras_eeprom_mgr_reset_table(struct ras_core_context *ras_core);
int ras_eeprom_mgr_get_record_count(struct ras_core_context *ras_core);
int ras_eeprom_mgr_get_records(struct ras_core_context *ras_core, u32 start,
		struct eeprom_umc_record *record, const u32 num);
int ras_eeprom_mgr_append_records(struct ras_core_context *ras_core,
		struct eeprom_umc_record *record, const u32 num);
int ras_eeprom_mgr_check_and_report_status(struct ras_core_context *ras_core,
		bool check_hw);
enum ras_gpu_op_status
	ras_eeprom_mgr_get_gpu_op_status(struct ras_core_context *ras_core);
int ras_eeprom_mgr_get_version(struct ras_core_context *ras_core, u32 *version);
bool ras_eeprom_mgr_check_safety_watermark(struct ras_core_context *ras_core);
int ras_eeprom_mgr_get_eeprom_info(struct ras_core_context *ras_core,
			struct ras_eeprom_info *eeprom_info);
int ras_core_enable_early_init_service(struct ras_core_context *ras_core,
		bool enabled);
bool ras_eeprom_mgr_early_init_service_supported(struct ras_core_context *ras_core);
bool ras_eeprom_mgr_fw_record_enabled(struct ras_core_context *ras_core);
int  ras_eeprom_mgr_dump_fw_records(struct ras_core_context *ras_core);
#endif
