/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2015 Quantenna Communications. All rights reserved. */

#ifndef QTNFMAC_UTIL_H
#define QTNFMAC_UTIL_H

#include <linux/kernel.h>
#include "core.h"

const char *qtnf_chipid_to_string(unsigned long chip_id);

void qtnf_sta_list_init(struct qtnf_sta_list *list);

struct qtnf_sta_node *qtnf_sta_list_lookup(struct qtnf_sta_list *list,
					   const u8 *mac);
struct qtnf_sta_node *qtnf_sta_list_lookup_index(struct qtnf_sta_list *list,
						 size_t index);
struct qtnf_sta_node *qtnf_sta_list_add(struct qtnf_vif *vif,
					const u8 *mac);
bool qtnf_sta_list_del(struct qtnf_vif *vif, const u8 *mac);

void qtnf_sta_list_free(struct qtnf_sta_list *list);

static inline size_t qtnf_sta_list_size(const struct qtnf_sta_list *list)
{
	return atomic_read(&list->size);
}

static inline bool qtnf_sta_list_empty(const struct qtnf_sta_list *list)
{
	return list_empty(&list->head);
}

#endif /* QTNFMAC_UTIL_H */
