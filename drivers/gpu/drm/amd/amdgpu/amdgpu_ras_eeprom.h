/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef _AMDGPU_RAS_EEPROM_H
#define _AMDGPU_RAS_EEPROM_H

#include <linux/i2c.h>

#define RAS_TABLE_VER_V1           0x00010000
#define RAS_TABLE_VER_V2_1         0x00021000
#define RAS_TABLE_VER_V3           0x00030000

struct amdgpu_device;

enum amdgpu_ras_gpu_health_status {
	GPU_HEALTH_USABLE = 0,
	GPU_RETIRED__ECC_REACH_THRESHOLD = 2,
};

enum amdgpu_ras_eeprom_err_type {
	AMDGPU_RAS_EEPROM_ERR_NA,
	AMDGPU_RAS_EEPROM_ERR_RECOVERABLE,
	AMDGPU_RAS_EEPROM_ERR_NON_RECOVERABLE,
	AMDGPU_RAS_EEPROM_ERR_COUNT,
};

struct amdgpu_ras_eeprom_table_header {
	uint32_t header;
	uint32_t version;
	uint32_t first_rec_offset;
	uint32_t tbl_size;
	uint32_t checksum;
} __packed;

struct amdgpu_ras_eeprom_table_ras_info {
	u8  rma_status;
	u8  health_percent;
	u16 ecc_page_threshold;
	u32 padding[64 - 1];
} __packed;

struct amdgpu_ras_eeprom_control {
	struct amdgpu_ras_eeprom_table_header tbl_hdr;

	struct amdgpu_ras_eeprom_table_ras_info tbl_rai;

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

	/* the bad page number is ras_num_recs or
	 * ras_num_recs * umc.retire_unit
	 */
	u32 ras_num_bad_pages;

	/* Number of records store mca address */
	u32 ras_num_mca_recs;

	/* Number of records store physical address */
	u32 ras_num_pa_recs;

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

	bool is_eeprom_valid;
};

/*
 * Represents single table record. Packed to be easily serialized into byte
 * stream.
 */
struct eeprom_table_record {

	union {
		uint64_t address;
		uint64_t offset;
	};

	uint64_t retired_page;
	uint64_t ts;

	enum amdgpu_ras_eeprom_err_type err_type;

	union {
		unsigned char bank;
		unsigned char cu;
	};

	unsigned char mem_channel;
	unsigned char mcumc_id;
} __packed;

int amdgpu_ras_eeprom_init(struct amdgpu_ras_eeprom_control *control);

int amdgpu_ras_eeprom_reset_table(struct amdgpu_ras_eeprom_control *control);

bool amdgpu_ras_eeprom_check_err_threshold(struct amdgpu_device *adev);

int amdgpu_ras_eeprom_read(struct amdgpu_ras_eeprom_control *control,
			   struct eeprom_table_record *records, const u32 num);

int amdgpu_ras_eeprom_append(struct amdgpu_ras_eeprom_control *control,
			     struct eeprom_table_record *records, const u32 num);

uint32_t amdgpu_ras_eeprom_max_record_count(struct amdgpu_ras_eeprom_control *control);

void amdgpu_ras_debugfs_set_ret_size(struct amdgpu_ras_eeprom_control *control);

int amdgpu_ras_eeprom_check(struct amdgpu_ras_eeprom_control *control);

void amdgpu_ras_eeprom_check_and_recover(struct amdgpu_device *adev);

extern const struct file_operations amdgpu_ras_debugfs_eeprom_size_ops;
extern const struct file_operations amdgpu_ras_debugfs_eeprom_table_ops;

#endif // _AMDGPU_RAS_EEPROM_H
