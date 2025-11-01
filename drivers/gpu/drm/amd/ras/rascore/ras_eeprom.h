/* SPDX-License-Identifier: MIT */
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

#ifndef __RAS_EEPROM_H__
#define __RAS_EEPROM_H__
#include "ras_sys.h"

#define RAS_TABLE_VER_V1           0x00010000
#define RAS_TABLE_VER_V2_1         0x00021000
#define RAS_TABLE_VER_V3           0x00030000

#define NONSTOP_OVER_THRESHOLD              -2
#define WARN_NONSTOP_OVER_THRESHOLD         -1
#define DISABLE_RETIRE_PAGE                 0

/*
 * Bad address pfn : eeprom_umc_record.retired_row_pfn[39:0],
 * nps mode: eeprom_umc_record.retired_row_pfn[47:40]
 */
#define EEPROM_RECORD_UMC_ADDR_MASK 0xFFFFFFFFFFULL
#define EEPROM_RECORD_UMC_NPS_MASK  0xFF0000000000ULL
#define EEPROM_RECORD_UMC_NPS_SHIFT 40

#define EEPROM_RECORD_UMC_NPS_MODE(RECORD) \
	(((RECORD)->retired_row_pfn & EEPROM_RECORD_UMC_NPS_MASK) >> \
		EEPROM_RECORD_UMC_NPS_SHIFT)

#define EEPROM_RECORD_UMC_ADDR_PFN(RECORD) \
	((RECORD)->retired_row_pfn & EEPROM_RECORD_UMC_ADDR_MASK)

#define EEPROM_RECORD_SETUP_UMC_ADDR_AND_NPS(RECORD, ADDR, NPS) \
do { \
	uint64_t tmp = (NPS); \
	tmp = ((tmp << EEPROM_RECORD_UMC_NPS_SHIFT) & EEPROM_RECORD_UMC_NPS_MASK); \
	tmp |= (ADDR) & EEPROM_RECORD_UMC_ADDR_MASK; \
	(RECORD)->retired_row_pfn = tmp; \
} while (0)

enum ras_gpu_health_status {
	RAS_GPU_HEALTH_NONE = 0,
	RAS_GPU_HEALTH_USABLE = 1,
	RAS_GPU_RETIRED__ECC_REACH_THRESHOLD = 2,
	RAS_GPU_IN_BAD_STATUS = 3,
};

enum ras_eeprom_err_type {
	RAS_EEPROM_ERR_NA,
	RAS_EEPROM_ERR_RECOVERABLE,
	RAS_EEPROM_ERR_NON_RECOVERABLE,
	RAS_EEPROM_ERR_COUNT,
};

struct ras_eeprom_table_header {
	uint32_t header;
	uint32_t version;
	uint32_t first_rec_offset;
	uint32_t tbl_size;
	uint32_t checksum;
} __packed;

struct ras_eeprom_table_ras_info {
	u8  rma_status;
	u8  health_percent;
	u16 ecc_page_threshold;
	u32 padding[64 - 1];
} __packed;

struct ras_eeprom_control {
	struct ras_eeprom_table_header tbl_hdr;
	struct ras_eeprom_table_ras_info tbl_rai;

	/* record threshold */
	int record_threshold_config;
	uint32_t record_threshold_count;
	bool update_channel_flag;

	const struct ras_eeprom_sys_func *sys_func;
	void *i2c_adapter;
	u32 i2c_port;
	u16 max_read_len;
	u16 max_write_len;

	/* Base I2C EEPPROM 19-bit memory address,
	 * where the table is located. For more information,
	 * see top of amdgpu_eeprom.c.
	 */
	u32 i2c_address;

	/* The byte offset off of @i2c_address
	 * where the table header is found,
	 * and where the records start--always
	 * right after the header.
	 */
	u32 ras_header_offset;
	u32 ras_info_offset;
	u32 ras_record_offset;

	/* Number of records in the table.
	 */
	u32 ras_num_recs;

	/* First record index to read, 0-based.
	 * Range is [0, num_recs-1]. This is
	 * an absolute index, starting right after
	 * the table header.
	 */
	u32 ras_fri;

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

/*
 * Represents single table record. Packed to be easily serialized into byte
 * stream.
 */
struct eeprom_umc_record {

	union {
		uint64_t address;
		uint64_t offset;
	};

	uint64_t retired_row_pfn;
	uint64_t ts;

	enum ras_eeprom_err_type err_type;

	union {
		unsigned char bank;
		unsigned char cu;
	};

	unsigned char mem_channel;
	unsigned char mcumc_id;

	/* The following variables will not be saved to eeprom.
	 */
	uint64_t cur_nps_retired_row_pfn;
	uint32_t cur_nps_bank;
	uint32_t cur_nps;
};

struct ras_core_context;
int ras_eeprom_hw_init(struct ras_core_context *ras_core);
int ras_eeprom_hw_fini(struct ras_core_context *ras_core);

int ras_eeprom_reset_table(struct ras_core_context *ras_core);

bool ras_eeprom_check_safety_watermark(struct ras_core_context *ras_core);

int ras_eeprom_read(struct ras_core_context *ras_core,
			 struct eeprom_umc_record *records, const u32 num);

int ras_eeprom_append(struct ras_core_context *ras_core,
			   struct eeprom_umc_record *records, const u32 num);

uint32_t ras_eeprom_max_record_count(struct ras_core_context *ras_core);
uint32_t ras_eeprom_get_record_count(struct ras_core_context *ras_core);
void ras_eeprom_sync_info(struct ras_core_context *ras_core);

int ras_eeprom_check_storage_status(struct ras_core_context *ras_core);
enum ras_gpu_health_status
	ras_eeprom_check_gpu_status(struct ras_core_context *ras_core);
#endif
