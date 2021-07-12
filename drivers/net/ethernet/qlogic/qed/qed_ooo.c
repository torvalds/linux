// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "qed.h"
#include "qed_iscsi.h"
#include "qed_ll2.h"
#include "qed_ooo.h"
#include "qed_cxt.h"
#include "qed_nvmetcp.h"
static struct qed_ooo_archipelago
*qed_ooo_seek_archipelago(struct qed_hwfn *p_hwfn,
			  struct qed_ooo_info
			  *p_ooo_info,
			  u32 cid)
{
	u32 idx = (cid & 0xffff) - p_ooo_info->cid_base;
	struct qed_ooo_archipelago *p_archipelago;

	if (idx >= p_ooo_info->max_num_archipelagos)
		return NULL;

	p_archipelago = &p_ooo_info->p_archipelagos_mem[idx];

	if (list_empty(&p_archipelago->isles_list))
		return NULL;

	return p_archipelago;
}

static struct qed_ooo_isle *qed_ooo_seek_isle(struct qed_hwfn *p_hwfn,
					      struct qed_ooo_info *p_ooo_info,
					      u32 cid, u8 isle)
{
	struct qed_ooo_archipelago *p_archipelago = NULL;
	struct qed_ooo_isle *p_isle = NULL;
	u8 the_num_of_isle = 1;

	p_archipelago = qed_ooo_seek_archipelago(p_hwfn, p_ooo_info, cid);
	if (!p_archipelago) {
		DP_NOTICE(p_hwfn,
			  "Connection %d is not found in OOO list\n", cid);
		return NULL;
	}

	list_for_each_entry(p_isle, &p_archipelago->isles_list, list_entry) {
		if (the_num_of_isle == isle)
			return p_isle;
		the_num_of_isle++;
	}

	return NULL;
}

void qed_ooo_save_history_entry(struct qed_hwfn *p_hwfn,
				struct qed_ooo_info *p_ooo_info,
				struct ooo_opaque *p_cqe)
{
	struct qed_ooo_history *p_history = &p_ooo_info->ooo_history;

	if (p_history->head_idx == p_history->num_of_cqes)
		p_history->head_idx = 0;
	p_history->p_cqes[p_history->head_idx] = *p_cqe;
	p_history->head_idx++;
}

int qed_ooo_alloc(struct qed_hwfn *p_hwfn)
{
	u16 max_num_archipelagos = 0, cid_base;
	struct qed_ooo_info *p_ooo_info;
	enum protocol_type proto;
	u16 max_num_isles = 0;
	u32 i;

	switch (p_hwfn->hw_info.personality) {
	case QED_PCI_ISCSI:
	case QED_PCI_NVMETCP:
		proto = PROTOCOLID_TCP_ULP;
		break;
	case QED_PCI_ETH_RDMA:
	case QED_PCI_ETH_IWARP:
		proto = PROTOCOLID_IWARP;
		break;
	default:
		DP_NOTICE(p_hwfn,
			  "Failed to allocate qed_ooo_info: unknown personality\n");
		return -EINVAL;
	}

	max_num_archipelagos = (u16)qed_cxt_get_proto_cid_count(p_hwfn, proto,
								NULL);
	max_num_isles = QED_MAX_NUM_ISLES + max_num_archipelagos;
	cid_base = (u16)qed_cxt_get_proto_cid_start(p_hwfn, proto);

	if (!max_num_archipelagos) {
		DP_NOTICE(p_hwfn,
			  "Failed to allocate qed_ooo_info: unknown amount of connections\n");
		return -EINVAL;
	}

	p_ooo_info = kzalloc(sizeof(*p_ooo_info), GFP_KERNEL);
	if (!p_ooo_info)
		return -ENOMEM;

	p_ooo_info->cid_base = cid_base;
	p_ooo_info->max_num_archipelagos = max_num_archipelagos;

	INIT_LIST_HEAD(&p_ooo_info->free_buffers_list);
	INIT_LIST_HEAD(&p_ooo_info->ready_buffers_list);
	INIT_LIST_HEAD(&p_ooo_info->free_isles_list);

	p_ooo_info->p_isles_mem = kcalloc(max_num_isles,
					  sizeof(struct qed_ooo_isle),
					  GFP_KERNEL);
	if (!p_ooo_info->p_isles_mem)
		goto no_isles_mem;

	for (i = 0; i < max_num_isles; i++) {
		INIT_LIST_HEAD(&p_ooo_info->p_isles_mem[i].buffers_list);
		list_add_tail(&p_ooo_info->p_isles_mem[i].list_entry,
			      &p_ooo_info->free_isles_list);
	}

	p_ooo_info->p_archipelagos_mem =
				kcalloc(max_num_archipelagos,
					sizeof(struct qed_ooo_archipelago),
					GFP_KERNEL);
	if (!p_ooo_info->p_archipelagos_mem)
		goto no_archipelagos_mem;

	for (i = 0; i < max_num_archipelagos; i++)
		INIT_LIST_HEAD(&p_ooo_info->p_archipelagos_mem[i].isles_list);

	p_ooo_info->ooo_history.p_cqes =
				kcalloc(QED_MAX_NUM_OOO_HISTORY_ENTRIES,
					sizeof(struct ooo_opaque),
					GFP_KERNEL);
	if (!p_ooo_info->ooo_history.p_cqes)
		goto no_history_mem;

	p_ooo_info->ooo_history.num_of_cqes = QED_MAX_NUM_OOO_HISTORY_ENTRIES;

	p_hwfn->p_ooo_info = p_ooo_info;
	return 0;

no_history_mem:
	kfree(p_ooo_info->p_archipelagos_mem);
no_archipelagos_mem:
	kfree(p_ooo_info->p_isles_mem);
no_isles_mem:
	kfree(p_ooo_info);
	return -ENOMEM;
}

void qed_ooo_release_connection_isles(struct qed_hwfn *p_hwfn,
				      struct qed_ooo_info *p_ooo_info, u32 cid)
{
	struct qed_ooo_archipelago *p_archipelago;
	struct qed_ooo_buffer *p_buffer;
	struct qed_ooo_isle *p_isle;

	p_archipelago = qed_ooo_seek_archipelago(p_hwfn, p_ooo_info, cid);
	if (!p_archipelago)
		return;

	while (!list_empty(&p_archipelago->isles_list)) {
		p_isle = list_first_entry(&p_archipelago->isles_list,
					  struct qed_ooo_isle, list_entry);

		list_del(&p_isle->list_entry);

		while (!list_empty(&p_isle->buffers_list)) {
			p_buffer = list_first_entry(&p_isle->buffers_list,
						    struct qed_ooo_buffer,
						    list_entry);

			if (!p_buffer)
				break;

			list_move_tail(&p_buffer->list_entry,
				       &p_ooo_info->free_buffers_list);
		}
		list_add_tail(&p_isle->list_entry,
			      &p_ooo_info->free_isles_list);
	}
}

void qed_ooo_release_all_isles(struct qed_hwfn *p_hwfn,
			       struct qed_ooo_info *p_ooo_info)
{
	struct qed_ooo_archipelago *p_archipelago;
	struct qed_ooo_buffer *p_buffer;
	struct qed_ooo_isle *p_isle;
	u32 i;

	for (i = 0; i < p_ooo_info->max_num_archipelagos; i++) {
		p_archipelago = &(p_ooo_info->p_archipelagos_mem[i]);

		while (!list_empty(&p_archipelago->isles_list)) {
			p_isle = list_first_entry(&p_archipelago->isles_list,
						  struct qed_ooo_isle,
						  list_entry);

			list_del(&p_isle->list_entry);

			while (!list_empty(&p_isle->buffers_list)) {
				p_buffer =
				    list_first_entry(&p_isle->buffers_list,
						     struct qed_ooo_buffer,
						     list_entry);

				if (!p_buffer)
					break;

				list_move_tail(&p_buffer->list_entry,
					       &p_ooo_info->free_buffers_list);
			}
			list_add_tail(&p_isle->list_entry,
				      &p_ooo_info->free_isles_list);
		}
	}
	if (!list_empty(&p_ooo_info->ready_buffers_list))
		list_splice_tail_init(&p_ooo_info->ready_buffers_list,
				      &p_ooo_info->free_buffers_list);
}

void qed_ooo_setup(struct qed_hwfn *p_hwfn)
{
	qed_ooo_release_all_isles(p_hwfn, p_hwfn->p_ooo_info);
	memset(p_hwfn->p_ooo_info->ooo_history.p_cqes, 0,
	       p_hwfn->p_ooo_info->ooo_history.num_of_cqes *
	       sizeof(struct ooo_opaque));
	p_hwfn->p_ooo_info->ooo_history.head_idx = 0;
}

void qed_ooo_free(struct qed_hwfn *p_hwfn)
{
	struct qed_ooo_info *p_ooo_info  = p_hwfn->p_ooo_info;
	struct qed_ooo_buffer *p_buffer;

	if (!p_ooo_info)
		return;

	qed_ooo_release_all_isles(p_hwfn, p_ooo_info);
	while (!list_empty(&p_ooo_info->free_buffers_list)) {
		p_buffer = list_first_entry(&p_ooo_info->free_buffers_list,
					    struct qed_ooo_buffer, list_entry);

		if (!p_buffer)
			break;

		list_del(&p_buffer->list_entry);
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  p_buffer->rx_buffer_size,
				  p_buffer->rx_buffer_virt_addr,
				  p_buffer->rx_buffer_phys_addr);
		kfree(p_buffer);
	}

	kfree(p_ooo_info->p_isles_mem);
	kfree(p_ooo_info->p_archipelagos_mem);
	kfree(p_ooo_info->ooo_history.p_cqes);
	kfree(p_ooo_info);
	p_hwfn->p_ooo_info = NULL;
}

void qed_ooo_put_free_buffer(struct qed_hwfn *p_hwfn,
			     struct qed_ooo_info *p_ooo_info,
			     struct qed_ooo_buffer *p_buffer)
{
	list_add_tail(&p_buffer->list_entry, &p_ooo_info->free_buffers_list);
}

struct qed_ooo_buffer *qed_ooo_get_free_buffer(struct qed_hwfn *p_hwfn,
					       struct qed_ooo_info *p_ooo_info)
{
	struct qed_ooo_buffer *p_buffer = NULL;

	if (!list_empty(&p_ooo_info->free_buffers_list)) {
		p_buffer = list_first_entry(&p_ooo_info->free_buffers_list,
					    struct qed_ooo_buffer, list_entry);

		list_del(&p_buffer->list_entry);
	}

	return p_buffer;
}

void qed_ooo_put_ready_buffer(struct qed_hwfn *p_hwfn,
			      struct qed_ooo_info *p_ooo_info,
			      struct qed_ooo_buffer *p_buffer, u8 on_tail)
{
	if (on_tail)
		list_add_tail(&p_buffer->list_entry,
			      &p_ooo_info->ready_buffers_list);
	else
		list_add(&p_buffer->list_entry,
			 &p_ooo_info->ready_buffers_list);
}

struct qed_ooo_buffer *qed_ooo_get_ready_buffer(struct qed_hwfn *p_hwfn,
						struct qed_ooo_info *p_ooo_info)
{
	struct qed_ooo_buffer *p_buffer = NULL;

	if (!list_empty(&p_ooo_info->ready_buffers_list)) {
		p_buffer = list_first_entry(&p_ooo_info->ready_buffers_list,
					    struct qed_ooo_buffer, list_entry);

		list_del(&p_buffer->list_entry);
	}

	return p_buffer;
}

void qed_ooo_delete_isles(struct qed_hwfn *p_hwfn,
			  struct qed_ooo_info *p_ooo_info,
			  u32 cid, u8 drop_isle, u8 drop_size)
{
	struct qed_ooo_isle *p_isle = NULL;
	u8 isle_idx;

	for (isle_idx = 0; isle_idx < drop_size; isle_idx++) {
		p_isle = qed_ooo_seek_isle(p_hwfn, p_ooo_info, cid, drop_isle);
		if (!p_isle) {
			DP_NOTICE(p_hwfn,
				  "Isle %d is not found(cid %d)\n",
				  drop_isle, cid);
			return;
		}
		if (list_empty(&p_isle->buffers_list))
			DP_NOTICE(p_hwfn,
				  "Isle %d is empty(cid %d)\n", drop_isle, cid);
		else
			list_splice_tail_init(&p_isle->buffers_list,
					      &p_ooo_info->free_buffers_list);

		list_del(&p_isle->list_entry);
		p_ooo_info->cur_isles_number--;
		list_add(&p_isle->list_entry, &p_ooo_info->free_isles_list);
	}
}

void qed_ooo_add_new_isle(struct qed_hwfn *p_hwfn,
			  struct qed_ooo_info *p_ooo_info,
			  u32 cid, u8 ooo_isle,
			  struct qed_ooo_buffer *p_buffer)
{
	struct qed_ooo_archipelago *p_archipelago = NULL;
	struct qed_ooo_isle *p_prev_isle = NULL;
	struct qed_ooo_isle *p_isle = NULL;

	if (ooo_isle > 1) {
		p_prev_isle = qed_ooo_seek_isle(p_hwfn,
						p_ooo_info, cid, ooo_isle - 1);
		if (!p_prev_isle) {
			DP_NOTICE(p_hwfn,
				  "Isle %d is not found(cid %d)\n",
				  ooo_isle - 1, cid);
			return;
		}
	}
	p_archipelago = qed_ooo_seek_archipelago(p_hwfn, p_ooo_info, cid);
	if (!p_archipelago && (ooo_isle != 1)) {
		DP_NOTICE(p_hwfn,
			  "Connection %d is not found in OOO list\n", cid);
		return;
	}

	if (!list_empty(&p_ooo_info->free_isles_list)) {
		p_isle = list_first_entry(&p_ooo_info->free_isles_list,
					  struct qed_ooo_isle, list_entry);

		list_del(&p_isle->list_entry);
		if (!list_empty(&p_isle->buffers_list)) {
			DP_NOTICE(p_hwfn, "Free isle is not empty\n");
			INIT_LIST_HEAD(&p_isle->buffers_list);
		}
	} else {
		DP_NOTICE(p_hwfn, "No more free isles\n");
		return;
	}

	if (!p_archipelago) {
		u32 idx = (cid & 0xffff) - p_ooo_info->cid_base;

		p_archipelago = &p_ooo_info->p_archipelagos_mem[idx];
	}

	list_add(&p_buffer->list_entry, &p_isle->buffers_list);
	p_ooo_info->cur_isles_number++;
	p_ooo_info->gen_isles_number++;

	if (p_ooo_info->cur_isles_number > p_ooo_info->max_isles_number)
		p_ooo_info->max_isles_number = p_ooo_info->cur_isles_number;

	if (!p_prev_isle)
		list_add(&p_isle->list_entry, &p_archipelago->isles_list);
	else
		list_add(&p_isle->list_entry, &p_prev_isle->list_entry);
}

void qed_ooo_add_new_buffer(struct qed_hwfn *p_hwfn,
			    struct qed_ooo_info *p_ooo_info,
			    u32 cid,
			    u8 ooo_isle,
			    struct qed_ooo_buffer *p_buffer, u8 buffer_side)
{
	struct qed_ooo_isle *p_isle = NULL;

	p_isle = qed_ooo_seek_isle(p_hwfn, p_ooo_info, cid, ooo_isle);
	if (!p_isle) {
		DP_NOTICE(p_hwfn,
			  "Isle %d is not found(cid %d)\n", ooo_isle, cid);
		return;
	}

	if (buffer_side == QED_OOO_LEFT_BUF)
		list_add(&p_buffer->list_entry, &p_isle->buffers_list);
	else
		list_add_tail(&p_buffer->list_entry, &p_isle->buffers_list);
}

void qed_ooo_join_isles(struct qed_hwfn *p_hwfn,
			struct qed_ooo_info *p_ooo_info, u32 cid, u8 left_isle)
{
	struct qed_ooo_isle *p_right_isle = NULL;
	struct qed_ooo_isle *p_left_isle = NULL;

	p_right_isle = qed_ooo_seek_isle(p_hwfn, p_ooo_info, cid,
					 left_isle + 1);
	if (!p_right_isle) {
		DP_NOTICE(p_hwfn,
			  "Right isle %d is not found(cid %d)\n",
			  left_isle + 1, cid);
		return;
	}

	list_del(&p_right_isle->list_entry);
	p_ooo_info->cur_isles_number--;
	if (left_isle) {
		p_left_isle = qed_ooo_seek_isle(p_hwfn, p_ooo_info, cid,
						left_isle);
		if (!p_left_isle) {
			DP_NOTICE(p_hwfn,
				  "Left isle %d is not found(cid %d)\n",
				  left_isle, cid);
			return;
		}
		list_splice_tail_init(&p_right_isle->buffers_list,
				      &p_left_isle->buffers_list);
	} else {
		list_splice_tail_init(&p_right_isle->buffers_list,
				      &p_ooo_info->ready_buffers_list);
	}
	list_add_tail(&p_right_isle->list_entry, &p_ooo_info->free_isles_list);
}
