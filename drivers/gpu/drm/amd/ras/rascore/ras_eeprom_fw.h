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
#ifndef __RAS_EEPROM_FW_H__
#define __RAS_EEPROM_FW_H__

struct ras_fw_eeprom_control {
	uint32_t version;
	/* record threshold */
	int record_threshold_config;
	uint32_t record_threshold_count;
	bool update_channel_flag;

	/* Number of records in the table.
	 */
	u32 ras_num_recs;

	/* Maximum possible number of records
	 * we could store, i.e. the maximum capacity
	 * of the table.
	 */
	u32 ras_max_record_count;

	/* Protect table access via this mutex.
	 */
	struct mutex ras_tbl_mutex;

	/* Record channel info which occurred bad pages
	 */
	u32 bad_channel_bitmap;
};

void ras_fw_init_feature_flags(struct ras_core_context *ras_core);
bool ras_fw_eeprom_supported(struct ras_core_context *ras_core);
int ras_fw_get_table_version(struct ras_core_context *ras_core,
				     uint32_t *table_version);
int ras_fw_get_badpage_count(struct ras_core_context *ras_core,
				     uint32_t *count, uint32_t timeout);
int ras_fw_get_badpage_mca_addr(struct ras_core_context *ras_core,
					uint16_t index, uint64_t *mca_addr);
int ras_fw_set_timestamp(struct ras_core_context *ras_core,
				 uint64_t timestamp);
int ras_fw_get_timestamp(struct ras_core_context *ras_core,
				 uint16_t index, uint64_t *timestamp);
int ras_fw_get_badpage_ipid(struct ras_core_context *ras_core,
				    uint16_t index, uint64_t *ipid);
int ras_fw_erase_ras_table(struct ras_core_context *ras_core,
				   uint32_t *result);
int ras_fw_eeprom_reset_table(struct ras_core_context *ras_core);
bool ras_fw_eeprom_check_safety_watermark(struct ras_core_context *ras_core);

#endif
