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
#include "ras_log_ring.h"

#define RAS_LOG_MAX_QUERY_SIZE   0xC000
#define RAS_LOG_MEM_TEMP_SIZE    0x200
#define RAS_LOG_MEMPOOL_SIZE \
	(RAS_LOG_MAX_QUERY_SIZE + RAS_LOG_MEM_TEMP_SIZE)

#define BATCH_IDX_TO_TREE_IDX(batch_idx, sn) (((batch_idx) << 8) | (sn))

static const uint64_t ras_rma_aca_reg[ACA_REG_MAX_COUNT] = {
	[ACA_REG_IDX__CTL]    = 0x1,
	[ACA_REG_IDX__STATUS] = 0xB000000000000137,
	[ACA_REG_IDX__ADDR]   = 0x0,
	[ACA_REG_IDX__MISC0]  = 0x0,
	[ACA_REG_IDX__CONFG] = 0x1ff00000002,
	[ACA_REG_IDX__IPID]   = 0x9600000000,
	[ACA_REG_IDX__SYND]   = 0x0,
};

static uint64_t ras_log_ring_get_logged_ecc_count(struct ras_core_context *ras_core)
{
	struct ras_log_ring *log_ring = &ras_core->ras_log_ring;
	uint64_t count = 0;

	if (log_ring->logged_ecc_count < 0) {
		RAS_DEV_WARN(ras_core->dev,
			"Error: the logged ras count should not less than 0!\n");
		count = 0;
	} else {
		count = log_ring->logged_ecc_count;
	}

	if (count > RAS_LOG_MEMPOOL_SIZE)
		RAS_DEV_WARN(ras_core->dev,
			"Error: the logged ras count is out of range!\n");

	return count;
}

static int ras_log_ring_add_data(struct ras_core_context *ras_core,
			struct ras_log_info *log, struct ras_log_batch_tag *batch_tag)
{
	struct ras_log_ring *log_ring = &ras_core->ras_log_ring;
	unsigned long flags = 0;
	int ret = 0;

	if (batch_tag && (batch_tag->sub_seqno >= MAX_RECORD_PER_BATCH)) {
		RAS_DEV_ERR(ras_core->dev,
			"Invalid batch sub seqno:%d, batch:0x%llx\n",
			batch_tag->sub_seqno, batch_tag->batch_id);
		return -EINVAL;
	}

	spin_lock_irqsave(&log_ring->spin_lock, flags);
	if (batch_tag) {
		log->seqno =
			BATCH_IDX_TO_TREE_IDX(batch_tag->batch_id, batch_tag->sub_seqno);
		batch_tag->sub_seqno++;
	} else {
		log->seqno = BATCH_IDX_TO_TREE_IDX(log_ring->mono_upward_batch_id, 0);
		log_ring->mono_upward_batch_id++;
	}
	ret = radix_tree_insert(&log_ring->ras_log_root, log->seqno, log);
	if (!ret)
		log_ring->logged_ecc_count++;
	spin_unlock_irqrestore(&log_ring->spin_lock, flags);

	if (ret) {
		RAS_DEV_ERR(ras_core->dev,
			"Failed to add ras log! seqno:0x%llx, ret:%d\n",
			log->seqno, ret);
		mempool_free(log, log_ring->ras_log_mempool);
	}

	return ret;
}

static int ras_log_ring_delete_data(struct ras_core_context *ras_core, uint32_t count)
{
	struct ras_log_ring *log_ring = &ras_core->ras_log_ring;
	unsigned long flags = 0;
	uint32_t i = 0, j = 0;
	uint64_t batch_id, idx;
	void *data;
	int ret = -ENODATA;

	if (count > ras_log_ring_get_logged_ecc_count(ras_core))
		return -EINVAL;

	spin_lock_irqsave(&log_ring->spin_lock, flags);
	batch_id = log_ring->last_del_batch_id;
	while (batch_id < log_ring->mono_upward_batch_id) {
		for (j = 0; j < MAX_RECORD_PER_BATCH; j++) {
			idx = BATCH_IDX_TO_TREE_IDX(batch_id, j);
			data = radix_tree_delete(&log_ring->ras_log_root, idx);
			if (data) {
				mempool_free(data, log_ring->ras_log_mempool);
				log_ring->logged_ecc_count--;
				i++;
			}
		}
		batch_id = ++log_ring->last_del_batch_id;
		if (i >= count) {
			ret = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&log_ring->spin_lock, flags);

	return ret;
}

static void ras_log_ring_clear_log_tree(struct ras_core_context *ras_core)
{
	struct ras_log_ring *log_ring = &ras_core->ras_log_ring;
	uint64_t batch_id, idx;
	unsigned long flags = 0;
	void *data;
	int j;

	if ((log_ring->mono_upward_batch_id <= log_ring->last_del_batch_id) &&
		!log_ring->logged_ecc_count)
		return;

	spin_lock_irqsave(&log_ring->spin_lock, flags);
	batch_id = log_ring->last_del_batch_id;
	while (batch_id < log_ring->mono_upward_batch_id) {
		for (j = 0; j < MAX_RECORD_PER_BATCH; j++) {
			idx = BATCH_IDX_TO_TREE_IDX(batch_id, j);
			data = radix_tree_delete(&log_ring->ras_log_root, idx);
			if (data) {
				mempool_free(data, log_ring->ras_log_mempool);
				log_ring->logged_ecc_count--;
			}
		}
		batch_id++;
	}
	spin_unlock_irqrestore(&log_ring->spin_lock, flags);

}

int ras_log_ring_sw_init(struct ras_core_context *ras_core)
{
	struct ras_log_ring *log_ring = &ras_core->ras_log_ring;

	memset(log_ring, 0, sizeof(*log_ring));

	log_ring->ras_log_mempool = mempool_create_kmalloc_pool(
			RAS_LOG_MEMPOOL_SIZE, sizeof(struct ras_log_info));
	if (!log_ring->ras_log_mempool)
		return -ENOMEM;

	INIT_RADIX_TREE(&log_ring->ras_log_root, GFP_KERNEL);

	spin_lock_init(&log_ring->spin_lock);

	return 0;
}

int ras_log_ring_sw_fini(struct ras_core_context *ras_core)
{
	struct ras_log_ring *log_ring = &ras_core->ras_log_ring;

	ras_log_ring_clear_log_tree(ras_core);
	log_ring->logged_ecc_count = 0;
	log_ring->last_del_batch_id = 0;
	log_ring->mono_upward_batch_id = 0;

	mempool_destroy(log_ring->ras_log_mempool);

	return 0;
}

struct ras_log_batch_tag *ras_log_ring_create_batch_tag(struct ras_core_context *ras_core)
{
	struct ras_log_ring *log_ring = &ras_core->ras_log_ring;
	struct ras_log_batch_tag *batch_tag;
	unsigned long flags = 0;

	batch_tag = kzalloc(sizeof(*batch_tag), GFP_KERNEL);
	if (!batch_tag)
		return NULL;

	spin_lock_irqsave(&log_ring->spin_lock, flags);
	batch_tag->batch_id = log_ring->mono_upward_batch_id;
	log_ring->mono_upward_batch_id++;
	spin_unlock_irqrestore(&log_ring->spin_lock, flags);

	batch_tag->sub_seqno = 0;
	batch_tag->timestamp = ras_core_get_utc_second_timestamp(ras_core);
	return batch_tag;
}

void ras_log_ring_destroy_batch_tag(struct ras_core_context *ras_core,
		struct ras_log_batch_tag *batch_tag)
{
	kfree(batch_tag);
}

void ras_log_ring_add_log_event(struct ras_core_context *ras_core,
		enum ras_log_event event, void *data, struct ras_log_batch_tag *batch_tag)
{
	struct ras_log_ring *log_ring = &ras_core->ras_log_ring;
	struct device_system_info dev_info = {0};
	struct ras_log_info *log;
	uint64_t socket_id;
	void *obj;

	obj = mempool_alloc_preallocated(log_ring->ras_log_mempool);
	if (!obj ||
	   (ras_log_ring_get_logged_ecc_count(ras_core) >= RAS_LOG_MEMPOOL_SIZE)) {
		ras_log_ring_delete_data(ras_core, RAS_LOG_MEM_TEMP_SIZE);
		if (!obj)
			obj = mempool_alloc_preallocated(log_ring->ras_log_mempool);
	}

	if (!obj) {
		RAS_DEV_ERR(ras_core->dev, "ERROR: Failed to alloc ras log buffer!\n");
		return;
	}

	log = (struct ras_log_info *)obj;

	memset(log, 0, sizeof(*log));
	log->timestamp =
		batch_tag ? batch_tag->timestamp : ras_core_get_utc_second_timestamp(ras_core);
	log->event = event;

	if (data)
		memcpy(&log->aca_reg, data, sizeof(log->aca_reg));

	if (event == RAS_LOG_EVENT_RMA) {
		memcpy(&log->aca_reg, ras_rma_aca_reg, sizeof(log->aca_reg));
		ras_core_get_device_system_info(ras_core, &dev_info);
		socket_id = dev_info.socket_id;
		log->aca_reg.regs[ACA_REG_IDX__IPID] |= ((socket_id / 4) & 0x01);
		log->aca_reg.regs[ACA_REG_IDX__IPID] |= (((socket_id % 4) & 0x3) << 44);
	}

	ras_log_ring_add_data(ras_core, log, batch_tag);
}

static struct ras_log_info *ras_log_ring_lookup_data(struct ras_core_context *ras_core,
					uint64_t idx)
{
	struct ras_log_ring *log_ring = &ras_core->ras_log_ring;
	unsigned long flags = 0;
	void *data;

	spin_lock_irqsave(&log_ring->spin_lock, flags);
	data = radix_tree_lookup(&log_ring->ras_log_root, idx);
	spin_unlock_irqrestore(&log_ring->spin_lock, flags);

	return (struct ras_log_info *)data;
}

int ras_log_ring_get_batch_records(struct ras_core_context *ras_core, uint64_t batch_id,
		struct ras_log_info **log_arr, uint32_t arr_num)
{
	struct ras_log_ring *log_ring = &ras_core->ras_log_ring;
	uint32_t i, idx, count = 0;
	void *data;

	if ((batch_id >= log_ring->mono_upward_batch_id) ||
		(batch_id < log_ring->last_del_batch_id))
		return -EINVAL;

	for (i = 0; i < MAX_RECORD_PER_BATCH; i++) {
		idx = BATCH_IDX_TO_TREE_IDX(batch_id, i);
		data = ras_log_ring_lookup_data(ras_core, idx);
		if (data) {
			log_arr[count++] = data;
			if (count >= arr_num)
				break;
		}
	}

	return count;
}

int ras_log_ring_get_batch_overview(struct ras_core_context *ras_core,
		struct ras_log_batch_overview *overview)
{
	struct ras_log_ring *log_ring = &ras_core->ras_log_ring;

	overview->logged_batch_count =
		log_ring->mono_upward_batch_id - log_ring->last_del_batch_id;
	overview->last_batch_id = log_ring->mono_upward_batch_id;
	overview->first_batch_id = log_ring->last_del_batch_id;

	return 0;
}
