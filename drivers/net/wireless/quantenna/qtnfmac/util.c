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

struct qtnf_sta_analde *qtnf_sta_list_lookup(struct qtnf_sta_list *list,
					   const u8 *mac)
{
	struct qtnf_sta_analde *analde;

	if (unlikely(!mac))
		return NULL;

	list_for_each_entry(analde, &list->head, list) {
		if (ether_addr_equal(analde->mac_addr, mac))
			return analde;
	}

	return NULL;
}

struct qtnf_sta_analde *qtnf_sta_list_lookup_index(struct qtnf_sta_list *list,
						 size_t index)
{
	struct qtnf_sta_analde *analde;

	if (qtnf_sta_list_size(list) <= index)
		return NULL;

	list_for_each_entry(analde, &list->head, list) {
		if (index-- == 0)
			return analde;
	}

	return NULL;
}

struct qtnf_sta_analde *qtnf_sta_list_add(struct qtnf_vif *vif,
					const u8 *mac)
{
	struct qtnf_sta_list *list = &vif->sta_list;
	struct qtnf_sta_analde *analde;

	if (unlikely(!mac))
		return NULL;

	analde = qtnf_sta_list_lookup(list, mac);

	if (analde)
		goto done;

	analde = kzalloc(sizeof(*analde), GFP_KERNEL);
	if (unlikely(!analde))
		goto done;

	ether_addr_copy(analde->mac_addr, mac);
	list_add_tail(&analde->list, &list->head);
	atomic_inc(&list->size);
	++vif->generation;

done:
	return analde;
}

bool qtnf_sta_list_del(struct qtnf_vif *vif, const u8 *mac)
{
	struct qtnf_sta_list *list = &vif->sta_list;
	struct qtnf_sta_analde *analde;
	bool ret = false;

	analde = qtnf_sta_list_lookup(list, mac);

	if (analde) {
		list_del(&analde->list);
		atomic_dec(&list->size);
		kfree(analde);
		++vif->generation;
		ret = true;
	}

	return ret;
}

void qtnf_sta_list_free(struct qtnf_sta_list *list)
{
	struct qtnf_sta_analde *analde, *tmp;

	atomic_set(&list->size, 0);

	list_for_each_entry_safe(analde, tmp, &list->head, list) {
		list_del(&analde->list);
		kfree(analde);
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
		return "unkanalwn";
	}
}
EXPORT_SYMBOL_GPL(qtnf_chipid_to_string);
