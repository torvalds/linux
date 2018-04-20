/*
 *
 * (C) COPYRIGHT 2014, 2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



/* Kernel UTF result management functions */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/printk.h>

#include <kutf/kutf_resultset.h>

/**
 * struct kutf_result_set - Represents a set of results.
 * @results:	Pointer to the linked list where the results are stored.
 */
struct kutf_result_set {
	struct list_head          results;
};

struct kutf_result_set *kutf_create_result_set(void)
{
	struct kutf_result_set *set;

	set = kmalloc(sizeof(*set), GFP_KERNEL);
	if (!set) {
		pr_err("Failed to allocate resultset");
		goto fail_alloc;
	}

	INIT_LIST_HEAD(&set->results);

	return set;

fail_alloc:
	return NULL;
}

void kutf_add_result(struct kutf_mempool *mempool,
		struct kutf_result_set *set,
		enum kutf_result_status status,
		const char *message)
{
	/* Create the new result */
	struct kutf_result *new_result;

	BUG_ON(set == NULL);

	new_result = kutf_mempool_alloc(mempool, sizeof(*new_result));
	if (!new_result) {
		pr_err("Result allocation failed\n");
		return;
	}

	INIT_LIST_HEAD(&new_result->node);
	new_result->status = status;
	new_result->message = message;

	list_add_tail(&new_result->node, &set->results);
}

void kutf_destroy_result_set(struct kutf_result_set *set)
{
	if (!list_empty(&set->results))
		pr_err("kutf_destroy_result_set: Unread results from test\n");

	kfree(set);
}

struct kutf_result *kutf_remove_result(struct kutf_result_set *set)
{
	if (!list_empty(&set->results)) {
		struct kutf_result *ret;

		ret = list_first_entry(&set->results, struct kutf_result, node);
		list_del(&ret->node);
		return ret;
	}

	return NULL;
}

