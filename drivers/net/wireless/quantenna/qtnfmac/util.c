/*
 * Copyright (c) 2015-2016 Quantenna Communications, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "util.h"
#include "qtn_hw_ids.h"

void qtnf_sta_list_init(struct qtnf_sta_list *list)
{
	if (unlikely(!list))
		return;

	INIT_LIST_HEAD(&list->head);
	atomic_set(&list->size, 0);
}

struct qtnf_sta_node *qtnf_sta_list_lookup(struct qtnf_sta_list *list,
					   const u8 *mac)
{
	struct qtnf_sta_node *node;

	if (unlikely(!mac))
		return NULL;

	list_for_each_entry(node, &list->head, list) {
		if (ether_addr_equal(node->mac_addr, mac))
			return node;
	}

	return NULL;
}

struct qtnf_sta_node *qtnf_sta_list_lookup_index(struct qtnf_sta_list *list,
						 size_t index)
{
	struct qtnf_sta_node *node;

	if (qtnf_sta_list_size(list) <= index)
		return NULL;

	list_for_each_entry(node, &list->head, list) {
		if (index-- == 0)
			return node;
	}

	return NULL;
}

struct qtnf_sta_node *qtnf_sta_list_add(struct qtnf_vif *vif,
					const u8 *mac)
{
	struct qtnf_sta_list *list = &vif->sta_list;
	struct qtnf_sta_node *node;

	if (unlikely(!mac))
		return NULL;

	node = qtnf_sta_list_lookup(list, mac);

	if (node)
		goto done;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (unlikely(!node))
		goto done;

	ether_addr_copy(node->mac_addr, mac);
	list_add_tail(&node->list, &list->head);
	atomic_inc(&list->size);
	++vif->generation;

done:
	return node;
}

bool qtnf_sta_list_del(struct qtnf_vif *vif, const u8 *mac)
{
	struct qtnf_sta_list *list = &vif->sta_list;
	struct qtnf_sta_node *node;
	bool ret = false;

	node = qtnf_sta_list_lookup(list, mac);

	if (node) {
		list_del(&node->list);
		atomic_dec(&list->size);
		kfree(node);
		++vif->generation;
		ret = true;
	}

	return ret;
}

void qtnf_sta_list_free(struct qtnf_sta_list *list)
{
	struct qtnf_sta_node *node, *tmp;

	atomic_set(&list->size, 0);

	list_for_each_entry_safe(node, tmp, &list->head, list) {
		list_del(&node->list);
		kfree(node);
	}

	INIT_LIST_HEAD(&list->head);
}

const char *qtnf_chipid_to_string(unsigned long chip_id)
{
	switch (chip_id) {
	case QTN_CHIP_ID_TOPAZ:
		return "Topaz";
	case QTN_CHIP_ID_PEARL:
		return "Pearl revA";
	case QTN_CHIP_ID_PEARL_B:
		return "Pearl revB";
	case QTN_CHIP_ID_PEARL_C:
		return "Pearl revC";
	default:
		return "unknown";
	}
}
EXPORT_SYMBOL_GPL(qtnf_chipid_to_string);
