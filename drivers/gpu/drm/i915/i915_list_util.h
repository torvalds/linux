/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __I915_LIST_UTIL_H__
#define __I915_LIST_UTIL_H__

#include <linux/list.h>
#include <asm/rwonce.h>

static inline void __list_del_many(struct list_head *head,
				   struct list_head *first)
{
	first->prev = head;
	WRITE_ONCE(head->next, first);
}

static inline int list_is_last_rcu(const struct list_head *list,
				   const struct list_head *head)
{
	return READ_ONCE(list->next) == head;
}

#endif /* __I915_LIST_UTIL_H__ */
