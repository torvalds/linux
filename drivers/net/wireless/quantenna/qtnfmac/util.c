// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

#include "util.h"
#include "qtn_hw_ids.h"

void qtnf_sta_list_init(struct qtnf_sta_list *list)
{
	if (unlikely(!list))
		return;

	INIT_LIST_HEAD(&list->head);
	atomic_set(&list->size, 0);
}

struct qtnf_sta_yesde *qtnf_sta_list_lookup(struct qtnf_sta_list *list,
					   const u8 *mac)
{
	struct qtnf_sta_yesde *yesde;

	if (unlikely(!mac))
		return NULL;

	list_for_each_entry(yesde, &list->head, list) {
		if (ether_addr_equal(yesde->mac_addr, mac))
			return yesde;
	}

	return NULL;
}

struct qtnf_sta_yesde *qtnf_sta_list_lookup_index(struct qtnf_sta_list *list,
						 size_t index)
{
	struct qtnf_sta_yesde *yesde;

	if (qtnf_sta_list_size(list) <= index)
		return NULL;

	list_for_each_entry(yesde, &list->head, list) {
		if (index-- == 0)
			return yesde;
	}

	return NULL;
}

struct qtnf_sta_yesde *qtnf_sta_list_add(struct qtnf_vif *vif,
					const u8 *mac)
{
	struct qtnf_sta_list *list = &vif->sta_list;
	struct qtnf_sta_yesde *yesde;

	if (unlikely(!mac))
		return NULL;

	yesde = qtnf_sta_list_lookup(list, mac);

	if (yesde)
		goto done;

	yesde = kzalloc(sizeof(*yesde), GFP_KERNEL);
	if (unlikely(!yesde))
		goto done;

	ether_addr_copy(yesde->mac_addr, mac);
	list_add_tail(&yesde->list, &list->head);
	atomic_inc(&list->size);
	++vif->generation;

done:
	return yesde;
}

bool qtnf_sta_list_del(struct qtnf_vif *vif, const u8 *mac)
{
	struct qtnf_sta_list *list = &vif->sta_list;
	struct qtnf_sta_yesde *yesde;
	bool ret = false;

	yesde = qtnf_sta_list_lookup(list, mac);

	if (yesde) {
		list_del(&yesde->list);
		atomic_dec(&list->size);
		kfree(yesde);
		++vif->generation;
		ret = true;
	}

	return ret;
}

void qtnf_sta_list_free(struct qtnf_sta_list *list)
{
	struct qtnf_sta_yesde *yesde, *tmp;

	atomic_set(&list->size, 0);

	list_for_each_entry_safe(yesde, tmp, &list->head, list) {
		list_del(&yesde->list);
		kfree(yesde);
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
		return "unkyeswn";
	}
}
EXPORT_SYMBOL_GPL(qtnf_chipid_to_string);
