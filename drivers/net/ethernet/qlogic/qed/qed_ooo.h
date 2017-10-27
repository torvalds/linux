/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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

#ifndef _QED_OOO_H
#define _QED_OOO_H
#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>
#include "qed.h"

#define QED_MAX_NUM_ISLES	256
#define QED_MAX_NUM_OOO_HISTORY_ENTRIES	512

#define QED_OOO_LEFT_BUF	0
#define QED_OOO_RIGHT_BUF	1

struct qed_ooo_buffer {
	struct list_head list_entry;
	void *rx_buffer_virt_addr;
	dma_addr_t rx_buffer_phys_addr;
	u32 rx_buffer_size;
	u16 packet_length;
	u16 parse_flags;
	u16 vlan;
	u8 placement_offset;
};

struct qed_ooo_isle {
	struct list_head list_entry;
	struct list_head buffers_list;
};

struct qed_ooo_archipelago {
	struct list_head isles_list;
};

struct qed_ooo_history {
	struct ooo_opaque *p_cqes;
	u32 head_idx;
	u32 num_of_cqes;
};

struct qed_ooo_info {
	struct list_head free_buffers_list;
	struct list_head ready_buffers_list;
	struct list_head free_isles_list;
	struct qed_ooo_archipelago *p_archipelagos_mem;
	struct qed_ooo_isle *p_isles_mem;
	struct qed_ooo_history ooo_history;
	u32 cur_isles_number;
	u32 max_isles_number;
	u32 gen_isles_number;
	u16 max_num_archipelagos;
	u16 cid_base;
};

#if IS_ENABLED(CONFIG_QED_ISCSI)
void qed_ooo_save_history_entry(struct qed_hwfn *p_hwfn,
				struct qed_ooo_info *p_ooo_info,
				struct ooo_opaque *p_cqe);

int qed_ooo_alloc(struct qed_hwfn *p_hwfn);

void qed_ooo_setup(struct qed_hwfn *p_hwfn);

void qed_ooo_free(struct qed_hwfn *p_hwfn);

void qed_ooo_release_connection_isles(struct qed_hwfn *p_hwfn,
				      struct qed_ooo_info *p_ooo_info,
				      u32 cid);

void qed_ooo_release_all_isles(struct qed_hwfn *p_hwfn,
			       struct qed_ooo_info *p_ooo_info);

void qed_ooo_put_free_buffer(struct qed_hwfn *p_hwfn,
			     struct qed_ooo_info *p_ooo_info,
			     struct qed_ooo_buffer *p_buffer);

struct qed_ooo_buffer *
qed_ooo_get_free_buffer(struct qed_hwfn *p_hwfn,
			struct qed_ooo_info *p_ooo_info);

void qed_ooo_put_ready_buffer(struct qed_hwfn *p_hwfn,
			      struct qed_ooo_info *p_ooo_info,
			      struct qed_ooo_buffer *p_buffer, u8 on_tail);

struct qed_ooo_buffer *
qed_ooo_get_ready_buffer(struct qed_hwfn *p_hwfn,
			 struct qed_ooo_info *p_ooo_info);

void qed_ooo_delete_isles(struct qed_hwfn *p_hwfn,
			  struct qed_ooo_info *p_ooo_info,
			  u32 cid, u8 drop_isle, u8 drop_size);

void qed_ooo_add_new_isle(struct qed_hwfn *p_hwfn,
			  struct qed_ooo_info *p_ooo_info,
			  u32 cid,
			  u8 ooo_isle, struct qed_ooo_buffer *p_buffer);

void qed_ooo_add_new_buffer(struct qed_hwfn *p_hwfn,
			    struct qed_ooo_info *p_ooo_info,
			    u32 cid,
			    u8 ooo_isle,
			    struct qed_ooo_buffer *p_buffer, u8 buffer_side);

void qed_ooo_join_isles(struct qed_hwfn *p_hwfn,
			struct qed_ooo_info *p_ooo_info, u32 cid,
			u8 left_isle);
#else /* IS_ENABLED(CONFIG_QED_ISCSI) */
static inline void qed_ooo_save_history_entry(struct qed_hwfn *p_hwfn,
					      struct qed_ooo_info *p_ooo_info,
					      struct ooo_opaque *p_cqe) {}

static inline int qed_ooo_alloc(struct qed_hwfn *p_hwfn)
{
	return -EINVAL;
}

static inline void qed_ooo_setup(struct qed_hwfn *p_hwfn) {}

static inline void qed_ooo_free(struct qed_hwfn *p_hwfn) {}

static inline void
qed_ooo_release_connection_isles(struct qed_hwfn *p_hwfn,
				 struct qed_ooo_info *p_ooo_info,
				 u32 cid) {}

static inline void qed_ooo_release_all_isles(struct qed_hwfn *p_hwfn,
					     struct qed_ooo_info *p_ooo_info)
					     {}

static inline void qed_ooo_put_free_buffer(struct qed_hwfn *p_hwfn,
					   struct qed_ooo_info *p_ooo_info,
					   struct qed_ooo_buffer *p_buffer) {}

static inline struct qed_ooo_buffer *
qed_ooo_get_free_buffer(struct qed_hwfn *p_hwfn,
			struct qed_ooo_info *p_ooo_info) { return NULL; }

static inline void qed_ooo_put_ready_buffer(struct qed_hwfn *p_hwfn,
					    struct qed_ooo_info *p_ooo_info,
					    struct qed_ooo_buffer *p_buffer,
					    u8 on_tail) {}

static inline struct qed_ooo_buffer *
qed_ooo_get_ready_buffer(struct qed_hwfn *p_hwfn,
			 struct qed_ooo_info *p_ooo_info) { return NULL; }

static inline void qed_ooo_delete_isles(struct qed_hwfn *p_hwfn,
					struct qed_ooo_info *p_ooo_info,
					u32 cid, u8 drop_isle, u8 drop_size) {}

static inline void qed_ooo_add_new_isle(struct qed_hwfn *p_hwfn,
					struct qed_ooo_info *p_ooo_info,
					u32 cid, u8 ooo_isle,
					struct qed_ooo_buffer *p_buffer) {}

static inline void qed_ooo_add_new_buffer(struct qed_hwfn *p_hwfn,
					  struct qed_ooo_info *p_ooo_info,
					  u32 cid, u8 ooo_isle,
					  struct qed_ooo_buffer *p_buffer,
					  u8 buffer_side) {}

static inline void qed_ooo_join_isles(struct qed_hwfn *p_hwfn,
				      struct qed_ooo_info *p_ooo_info, u32 cid,
				      u8 left_isle) {}
#endif /* IS_ENABLED(CONFIG_QED_ISCSI) */

#endif
