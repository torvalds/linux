// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - Google LLC
 * Author: Keir Fraser <keirf@google.com>
 */

#include <linux/list.h>
#include <linux/bug.h>

static inline __must_check bool nvhe_check_data_corruption(bool v)
{
	return v;
}

#define NVHE_CHECK_DATA_CORRUPTION(condition)				 \
	nvhe_check_data_corruption(({					 \
		bool corruption = unlikely(condition);			 \
		if (corruption) {					 \
			if (IS_ENABLED(CONFIG_BUG_ON_DATA_CORRUPTION)) { \
				BUG_ON(1);				 \
			} else						 \
				WARN_ON(1);				 \
		}							 \
		corruption;						 \
	}))

/* The predicates checked here are taken from lib/list_debug.c. */

bool __list_add_valid(struct list_head *new, struct list_head *prev,
		      struct list_head *next)
{
	if (NVHE_CHECK_DATA_CORRUPTION(next->prev != prev) ||
	    NVHE_CHECK_DATA_CORRUPTION(prev->next != next) ||
	    NVHE_CHECK_DATA_CORRUPTION(new == prev || new == next))
		return false;

	return true;
}

bool __list_del_entry_valid(struct list_head *entry)
{
	struct list_head *prev, *next;

	prev = entry->prev;
	next = entry->next;

	if (NVHE_CHECK_DATA_CORRUPTION(next == LIST_POISON1) ||
	    NVHE_CHECK_DATA_CORRUPTION(prev == LIST_POISON2) ||
	    NVHE_CHECK_DATA_CORRUPTION(prev->next != entry) ||
	    NVHE_CHECK_DATA_CORRUPTION(next->prev != entry))
		return false;

	return true;
}
