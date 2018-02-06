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

struct mlx5_fw_tracer {
	struct mlx5_core_dev *dev;
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

	} buff;
};

enum mlx5_fw_tracer_ownership_state {
	MLX5_FW_TRACER_RELEASE_OWNERSHIP,
	MLX5_FW_TRACER_ACQUIRE_OWNERSHIP,
};

struct mlx5_fw_tracer *mlx5_fw_tracer_create(struct mlx5_core_dev *dev);
void mlx5_fw_tracer_destroy(struct mlx5_fw_tracer *tracer);

#endif
