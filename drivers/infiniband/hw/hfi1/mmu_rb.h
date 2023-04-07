/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright(c) 2020 Cornelis Networks, Inc.
 * Copyright(c) 2016 Intel Corporation.
 */

#ifndef _HFI1_MMU_RB_H
#define _HFI1_MMU_RB_H

#include "hfi.h"

struct mmu_rb_node {
	unsigned long addr;
	unsigned long len;
	unsigned long __last;
	struct rb_node node;
	struct mmu_rb_handler *handler;
	struct list_head list;
};

/*
 * NOTE: filter, insert, invalidate, and evict must not sleep.  Only remove is
 * allowed to sleep.
 */
struct mmu_rb_ops {
	bool (*filter)(struct mmu_rb_node *node, unsigned long addr,
		       unsigned long len);
	int (*insert)(void *ops_arg, struct mmu_rb_node *mnode);
	void (*remove)(void *ops_arg, struct mmu_rb_node *mnode);
	int (*invalidate)(void *ops_arg, struct mmu_rb_node *node);
	int (*evict)(void *ops_arg, struct mmu_rb_node *mnode,
		     void *evict_arg, bool *stop);
};

struct mmu_rb_handler {
	struct mmu_notifier mn;
	struct rb_root_cached root;
	void *ops_arg;
	spinlock_t lock;        /* protect the RB tree */
	struct mmu_rb_ops *ops;
	struct list_head lru_list;
	struct work_struct del_work;
	struct list_head del_list;
	struct workqueue_struct *wq;
};

int hfi1_mmu_rb_register(void *ops_arg,
			 struct mmu_rb_ops *ops,
			 struct workqueue_struct *wq,
			 struct mmu_rb_handler **handler);
void hfi1_mmu_rb_unregister(struct mmu_rb_handler *handler);
int hfi1_mmu_rb_insert(struct mmu_rb_handler *handler,
		       struct mmu_rb_node *mnode);
void hfi1_mmu_rb_evict(struct mmu_rb_handler *handler, void *evict_arg);
struct mmu_rb_node *hfi1_mmu_rb_get_first(struct mmu_rb_handler *handler,
					  unsigned long addr,
					  unsigned long len);

#endif /* _HFI1_MMU_RB_H */
