// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014, 2017, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/* Kernel UTF memory management functions */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/export.h>

#include <kutf/kutf_mem.h>


/**
 * struct kutf_alloc_entry - Structure representing an allocation.
 * @node:	List node for use with kutf_mempool.
 * @data:	Data area of the allocation
 */
struct kutf_alloc_entry {
	struct list_head node;
	u8 data[0];
};

int kutf_mempool_init(struct kutf_mempool *pool)
{
	if (!pool) {
		pr_err("NULL pointer passed to %s\n", __func__);
		return -1;
	}

	INIT_LIST_HEAD(&pool->head);
	mutex_init(&pool->lock);

	return 0;
}
EXPORT_SYMBOL(kutf_mempool_init);

void kutf_mempool_destroy(struct kutf_mempool *pool)
{
	struct list_head *remove;
	struct list_head *tmp;

	if (!pool) {
		pr_err("NULL pointer passed to %s\n", __func__);
		return;
	}

	mutex_lock(&pool->lock);
	list_for_each_safe(remove, tmp, &pool->head) {
		struct kutf_alloc_entry *remove_alloc;

		remove_alloc = list_entry(remove, struct kutf_alloc_entry, node);
		list_del(&remove_alloc->node);
		kfree(remove_alloc);
	}
	mutex_unlock(&pool->lock);

}
EXPORT_SYMBOL(kutf_mempool_destroy);

void *kutf_mempool_alloc(struct kutf_mempool *pool, size_t size)
{
	struct kutf_alloc_entry *ret;

	if (!pool) {
		pr_err("NULL pointer passed to %s\n", __func__);
		goto fail_pool;
	}

	mutex_lock(&pool->lock);

	ret = kmalloc(sizeof(*ret) + size, GFP_KERNEL);
	if (!ret) {
		pr_err("Failed to allocate memory\n");
		goto fail_alloc;
	}

	INIT_LIST_HEAD(&ret->node);
	list_add(&ret->node, &pool->head);

	mutex_unlock(&pool->lock);

	return &ret->data[0];

fail_alloc:
	mutex_unlock(&pool->lock);
fail_pool:
	return NULL;
}
EXPORT_SYMBOL(kutf_mempool_alloc);
