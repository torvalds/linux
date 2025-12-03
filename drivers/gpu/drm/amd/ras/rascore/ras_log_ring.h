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
#ifndef __RAS_LOG_RING_H__
#define __RAS_LOG_RING_H__
#include "ras_aca.h"

#define MAX_RECORD_PER_BATCH 32

#define RAS_LOG_SEQNO_TO_BATCH_IDX(seqno) ((seqno) >> 8)

enum ras_log_event {
	RAS_LOG_EVENT_NONE,
	RAS_LOG_EVENT_UE,
	RAS_LOG_EVENT_DE,
	RAS_LOG_EVENT_CE,
	RAS_LOG_EVENT_POISON_CREATION,
	RAS_LOG_EVENT_POISON_CONSUMPTION,
	RAS_LOG_EVENT_RMA,
	RAS_LOG_EVENT_COUNT_MAX,
};

struct ras_aca_reg {
	uint64_t regs[ACA_REG_MAX_COUNT];
};

struct ras_log_info {
	uint64_t seqno;
	uint64_t timestamp;
	enum ras_log_event event;
	union {
		struct ras_aca_reg aca_reg;
	};
};

struct ras_log_batch_tag {
	uint64_t batch_id;
	uint64_t timestamp;
	uint32_t sub_seqno;
};

struct ras_log_ring {
	void *ras_log_mempool;
	struct radix_tree_root ras_log_root;
	spinlock_t spin_lock;
	uint64_t mono_upward_batch_id;
	uint64_t last_del_batch_id;
	int logged_ecc_count;
};

struct ras_log_batch_overview {
	uint64_t first_batch_id;
	uint64_t last_batch_id;
	uint32_t logged_batch_count;
};

struct ras_core_context;

int ras_log_ring_sw_init(struct ras_core_context *ras_core);
int ras_log_ring_sw_fini(struct ras_core_context *ras_core);

struct ras_log_batch_tag *ras_log_ring_create_batch_tag(struct ras_core_context *ras_core);
void ras_log_ring_destroy_batch_tag(struct ras_core_context *ras_core,
			struct ras_log_batch_tag *tag);
void ras_log_ring_add_log_event(struct ras_core_context *ras_core,
		enum ras_log_event event, void *data, struct ras_log_batch_tag *tag);

int ras_log_ring_get_batch_records(struct ras_core_context *ras_core, uint64_t batch_idx,
		struct ras_log_info **log_arr, uint32_t arr_num);

int ras_log_ring_get_batch_overview(struct ras_core_context *ras_core,
		struct ras_log_batch_overview *overview);
#endif
