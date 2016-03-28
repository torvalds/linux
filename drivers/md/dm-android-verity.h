/*
 * Copyright (C) 2015 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef DM_ANDROID_VERITY_H
#define DM_ANDROID_VERITY_H

#include <crypto/sha.h>

#define RSANUMBYTES 256
#define VERITY_METADATA_MAGIC_NUMBER 0xb001b001
#define VERITY_METADATA_MAGIC_DISABLE 0x46464f56
#define VERITY_METADATA_VERSION 0
#define VERITY_STATE_DISABLE 1
#define DATA_BLOCK_SIZE (4 * 1024)
#define VERITY_METADATA_SIZE (8 * DATA_BLOCK_SIZE)
#define VERITY_TABLE_ARGS 10
#define VERITY_COMMANDLINE_PARAM_LENGTH 20

#define FEC_MAGIC 0xFECFECFE
#define FEC_BLOCK_SIZE (4 * 1024)
#define FEC_VERSION 0
#define FEC_RSM 255
#define FEC_ARG_LENGTH 300

#define VERITY_TABLE_OPT_RESTART "restart_on_corruption"
#define VERITY_TABLE_OPT_LOGGING "ignore_corruption"
#define VERITY_TABLE_OPT_IGNZERO "ignore_zero_blocks"

#define VERITY_TABLE_OPT_FEC_FORMAT \
	"use_fec_from_device %s fec_start %llu fec_blocks %llu fec_roots %u ignore_zero_blocks"
#define VERITY_TABLE_OPT_FEC_ARGS 9

#define VERITY_DEBUG 0

#define DM_MSG_PREFIX                   "android-verity"

#define DM_LINEAR_ARGS 2
#define DM_LINEAR_TARGET_OFFSET "0"

/*
 * There can be two formats.
 * if fec is present
 * <data_blocks> <verity_tree> <verity_metdata_32K><fec_data><fec_data_4K>
 * if fec is not present
 * <data_blocks> <verity_tree> <verity_metdata_32K>
 */
/* TODO: rearrange structure to reduce memory holes
 * depends on userspace change.
 */
struct fec_header {
	__le32 magic;
	__le32 version;
	__le32 size;
	__le32 roots;
	__le32 fec_size;
	__le64 inp_size;
	u8 hash[SHA256_DIGEST_SIZE];
};

struct android_metadata_header {
	__le32 magic_number;
	__le32 protocol_version;
	char signature[RSANUMBYTES];
	__le32 table_length;
};

struct android_metadata {
	struct android_metadata_header *header;
	char *verity_table;
};

struct fec_ecc_metadata {
	bool valid;
	u32 roots;
	u64 blocks;
	u64 rounds;
	u64 start;
};

struct bio_read {
	struct page **page_io;
	int number_of_pages;
};

extern struct target_type linear_target;
#endif /* DM_ANDROID_VERITY_H */
