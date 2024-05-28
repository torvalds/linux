/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_PRIORITY_TABLE_H
#define VDO_PRIORITY_TABLE_H

#include <linux/list.h>

/*
 * A priority_table is a simple implementation of a priority queue for entries with priorities that
 * are small non-negative integer values. It implements the obvious priority queue operations of
 * enqueuing an entry and dequeuing an entry with the maximum priority. It also supports removing
 * an arbitrary entry. The priority of an entry already in the table can be changed by removing it
 * and re-enqueuing it with a different priority. All operations have O(1) complexity.
 *
 * The links for the table entries must be embedded in the entries themselves. Lists are used to
 * link entries in the table and no wrapper type is declared, so an existing list entry in an
 * object can also be used to queue it in a priority_table, assuming the field is not used for
 * anything else while so queued.
 *
 * The table is implemented as an array of queues (circular lists) indexed by priority, along with
 * a hint for which queues are non-empty. Steven Skiena calls a very similar structure a "bounded
 * height priority queue", but given the resemblance to a hash table, "priority table" seems both
 * shorter and more apt, if somewhat novel.
 */

struct priority_table;

int __must_check vdo_make_priority_table(unsigned int max_priority,
					 struct priority_table **table_ptr);

void vdo_free_priority_table(struct priority_table *table);

void vdo_priority_table_enqueue(struct priority_table *table, unsigned int priority,
				struct list_head *entry);

void vdo_reset_priority_table(struct priority_table *table);

struct list_head * __must_check vdo_priority_table_dequeue(struct priority_table *table);

void vdo_priority_table_remove(struct priority_table *table, struct list_head *entry);

bool __must_check vdo_is_priority_table_empty(struct priority_table *table);

#endif /* VDO_PRIORITY_TABLE_H */
