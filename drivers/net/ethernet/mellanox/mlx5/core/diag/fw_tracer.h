/*
 * Copyright (c) 2018, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __LIB_TRACER_H__
#define __LIB_TRACER_H__

#include <linux/mlx5/driver.h>
#include "mlx5_core.h"

#define STRINGS_DB_SECTIONS_NUM 8
#define STRINGS_DB_READ_SIZE_BYTES 256
#define STRINGS_DB_LEFTOVER_SIZE_BYTES 64
#define TRACER_BUFFER_PAGE_NUM 64
#define TRACER_BUFFER_CHUNK 4096
#define TRACE_BUFFER_SIZE_BYTE (TRACER_BUFFER_PAGE_NUM * TRACER_BUFFER_CHUNK)

#define TRACER_BLOCK_SIZE_BYTE 256
#define TRACES_PER_BLOCK 32

#define TRACE_STR_MSG 256
#define SAVED_TRACES_NUM 8192

#define TRACER_MAX_PARAMS 7
#define MESSAGE_HASH_BITS 6
#define MESSAGE_HASH_SIZE BIT(MESSAGE_HASH_BITS)

#define MASK_52_7 (0x1FFFFFFFFFFF80)
#define MASK_6_0  (0x7F)

struct mlx5_fw_trace_data {
	u64 timestamp;
	bool lost;
	u8 event_id;
	char msg[TRACE_STR_MSG];
};

struct mlx5_fw_tracer {
	struct mlx5_core_dev *dev;
	struct mlx5_nb        nb;
	bool owner;
	u8   trc_ver;
	struct workqueue_struct *work_queue;
	struct work_struct ownership_change_work;
	struct work_struct read_fw_strings_work;

	/* Strings DB */
	struct {
		u8 first_string_trace;
		u8 num_string_trace;
		u32 num_string_db;
		u32 base_address_out[STRINGS_DB_SECTIONS_NUM];
		u32 size_out[STRINGS_DB_SECTIONS_NUM];
		void *buffer[STRINGS_DB_SECTIONS_NUM];
		bool loaded;
	} str_db;

	/* Log Buffer */
	struct {
		u32 pdn;
		void *log_buf;
		dma_addr_t dma;
		u32 size;
		struct mlx5_core_mkey mkey;
		u32 consumer_index;
	} buff;

	/* Saved Traces Array */
	struct {
		struct mlx5_fw_trace_data straces[SAVED_TRACES_NUM];
		u32 saved_traces_index;
		struct mutex lock; /* Protect st_arr access */
	} st_arr;

	u64 last_timestamp;
	struct work_struct handle_traces_work;
	struct hlist_head hash[MESSAGE_HASH_SIZE];
	struct list_head ready_strings_list;
};

struct tracer_string_format {
	char *string;
	int params[TRACER_MAX_PARAMS];
	int num_of_params;
	int last_param_num;
	u8 event_id;
	u32 tmsn;
	struct hlist_node hlist;
	struct list_head list;
	u32 timestamp;
	bool lost;
};

enum mlx5_fw_tracer_ownership_state {
	MLX5_FW_TRACER_RELEASE_OWNERSHIP,
	MLX5_FW_TRACER_ACQUIRE_OWNERSHIP,
};

enum tracer_ctrl_fields_select {
	TRACE_STATUS = 1 << 0,
};

enum tracer_event_type {
	TRACER_EVENT_TYPE_STRING,
	TRACER_EVENT_TYPE_TIMESTAMP = 0xFF,
	TRACER_EVENT_TYPE_UNRECOGNIZED,
};

enum tracing_mode {
	TRACE_TO_MEMORY = 1 << 0,
};

struct tracer_timestamp_event {
	u64        timestamp;
	u8         unreliable;
};

struct tracer_string_event {
	u32        timestamp;
	u32        tmsn;
	u32        tdsn;
	u32        string_param;
};

struct tracer_event {
	bool      lost_event;
	u32       type;
	u8        event_id;
	union {
		struct tracer_string_event string_event;
		struct tracer_timestamp_event timestamp_event;
	};
};

struct mlx5_ifc_tracer_event_bits {
	u8         lost[0x1];
	u8         timestamp[0x7];
	u8         event_id[0x8];
	u8         event_data[0x30];
};

struct mlx5_ifc_tracer_string_event_bits {
	u8         lost[0x1];
	u8         timestamp[0x7];
	u8         event_id[0x8];
	u8         tmsn[0xd];
	u8         tdsn[0x3];
	u8         string_param[0x20];
};

struct mlx5_ifc_tracer_timestamp_event_bits {
	u8         timestamp7_0[0x8];
	u8         event_id[0x8];
	u8         urts[0x3];
	u8         timestamp52_40[0xd];
	u8         timestamp39_8[0x20];
};

struct mlx5_fw_tracer *mlx5_fw_tracer_create(struct mlx5_core_dev *dev);
int mlx5_fw_tracer_init(struct mlx5_fw_tracer *tracer);
void mlx5_fw_tracer_cleanup(struct mlx5_fw_tracer *tracer);
void mlx5_fw_tracer_destroy(struct mlx5_fw_tracer *tracer);
int mlx5_fw_tracer_trigger_core_dump_general(struct mlx5_core_dev *dev);
int mlx5_fw_tracer_get_saved_traces_objects(struct mlx5_fw_tracer *tracer,
					    struct devlink_fmsg *fmsg);

#endif
