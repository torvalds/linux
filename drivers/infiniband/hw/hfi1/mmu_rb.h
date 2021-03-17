/*
 * Copyright(c) 2020 Cornelis Networks, Inc.
 * Copyright(c) 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
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
void hfi1_mmu_rb_remove(struct mmu_rb_handler *handler,
			struct mmu_rb_node *mnode);
bool hfi1_mmu_rb_remove_unless_exact(struct mmu_rb_handler *handler,
				     unsigned long addr, unsigned long len,
				     struct mmu_rb_node **rb_node);

#endif /* _HFI1_MMU_RB_H */
