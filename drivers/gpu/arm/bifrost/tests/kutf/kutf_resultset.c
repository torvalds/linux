/*
 *
 * (C) COPYRIGHT 2014, 2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

/* Kernel UTF result management functions */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/err.h>

#include <kutf/kutf_suite.h>
#include <kutf/kutf_resultset.h>

/* Lock to protect all result structures */
static DEFINE_SPINLOCK(kutf_result_lock);

struct kutf_result_set *kutf_create_result_set(void)
{
	struct kutf_result_set *set;

	set = kmalloc(sizeof(*set), GFP_KERNEL);
	if (!set) {
		pr_err("Failed to allocate resultset");
		goto fail_alloc;
	}

	INIT_LIST_HEAD(&set->results);
	init_waitqueue_head(&set->waitq);
	set->flags = 0;

	return set;

fail_alloc:
	return NULL;
}

int kutf_add_result(struct kutf_context *context,
		enum kutf_result_status status,
		const char *message)
{
	struct kutf_mempool *mempool = &context->fixture_pool;
	struct kutf_result_set *set = context->result_set;
	/* Create the new result */
	struct kutf_result *new_result;

	BUG_ON(set == NULL);

	new_result = kutf_mempool_alloc(mempool, sizeof(*new_result));
	if (!new_result) {
		pr_err("Result allocation failed\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&new_result->node);
	new_result->status = status;
	new_result->message = message;

	spin_lock(&kutf_result_lock);

	list_add_tail(&new_result->node, &set->results);

	spin_unlock(&kutf_result_lock);

	wake_up(&set->waitq);

	return 0;
}

void kutf_destroy_result_set(struct kutf_result_set *set)
{
	if (!list_empty(&set->results))
		pr_err("kutf_destroy_result_set: Unread results from test\n");

	kfree(set);
}

static bool kutf_has_result(struct kutf_result_set *set)
{
	bool has_result;

	spin_lock(&kutf_result_lock);
	if (set->flags & KUTF_RESULT_SET_WAITING_FOR_INPUT)
		/* Pretend there are results if waiting for input */
		has_result = true;
	else
		has_result = !list_empty(&set->results);
	spin_unlock(&kutf_result_lock);

	return has_result;
}

struct kutf_result *kutf_remove_result(struct kutf_result_set *set)
{
	struct kutf_result *result = NULL;
	int ret;

	do {
		ret = wait_event_interruptible(set->waitq,
				kutf_has_result(set));

		if (ret)
			return ERR_PTR(ret);

		spin_lock(&kutf_result_lock);

		if (!list_empty(&set->results)) {
			result = list_first_entry(&set->results,
					struct kutf_result,
					node);
			list_del(&result->node);
		} else if (set->flags & KUTF_RESULT_SET_WAITING_FOR_INPUT) {
			/* Return a fake result */
			static struct kutf_result waiting = {
				.status = KUTF_RESULT_USERDATA_WAIT
			};
			result = &waiting;
		}
		/* If result == NULL then there was a race with the event
		 * being removed between the check in kutf_has_result and
		 * the lock being obtained. In this case we retry
		 */

		spin_unlock(&kutf_result_lock);
	} while (result == NULL);

	return result;
}

void kutf_set_waiting_for_input(struct kutf_result_set *set)
{
	spin_lock(&kutf_result_lock);
	set->flags |= KUTF_RESULT_SET_WAITING_FOR_INPUT;
	spin_unlock(&kutf_result_lock);

	wake_up(&set->waitq);
}

void kutf_clear_waiting_for_input(struct kutf_result_set *set)
{
	spin_lock(&kutf_result_lock);
	set->flags &= ~KUTF_RESULT_SET_WAITING_FOR_INPUT;
	spin_unlock(&kutf_result_lock);
}
