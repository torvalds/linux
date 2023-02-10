// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2017, 2020-2022 ARM Limited. All rights reserved.
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

/* Kernel UTF test helpers */
#include <kutf/kutf_helpers.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/preempt.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/export.h>

static DEFINE_SPINLOCK(kutf_input_lock);

bool kutf_helper_pending_input(struct kutf_context *context)
{
	bool input_pending;

	spin_lock(&kutf_input_lock);

	input_pending = !list_empty(&context->userdata.input_head);

	spin_unlock(&kutf_input_lock);

	return input_pending;
}
EXPORT_SYMBOL(kutf_helper_pending_input);

char *kutf_helper_input_dequeue(struct kutf_context *context, size_t *str_size)
{
	struct kutf_userdata_line *line;

	spin_lock(&kutf_input_lock);

	while (list_empty(&context->userdata.input_head)) {
		int err;

		kutf_set_waiting_for_input(context->result_set);

		spin_unlock(&kutf_input_lock);

		err = wait_event_interruptible(context->userdata.input_waitq,
				kutf_helper_pending_input(context));

		if (err)
			return ERR_PTR(-EINTR);

		spin_lock(&kutf_input_lock);
	}

	line = list_first_entry(&context->userdata.input_head,
			struct kutf_userdata_line, node);
	if (line->str) {
		/*
		 * Unless it is the end-of-input marker,
		 * remove it from the list
		 */
		list_del(&line->node);
	}

	spin_unlock(&kutf_input_lock);

	if (str_size)
		*str_size = line->size;
	return line->str;
}

int kutf_helper_input_enqueue(struct kutf_context *context,
		const char __user *str, size_t size)
{
	struct kutf_userdata_line *line;

	line = kutf_mempool_alloc(&context->fixture_pool,
			sizeof(*line) + size + 1);
	if (!line)
		return -ENOMEM;
	if (str) {
		unsigned long bytes_not_copied;

		line->size = size;
		line->str = (void *)(line + 1);
		bytes_not_copied = copy_from_user(line->str, str, size);
		if (bytes_not_copied != 0)
			return -EFAULT;
		/* Zero terminate the string */
		line->str[size] = '\0';
	} else {
		/* This is used to mark the end of input */
		WARN_ON(size);
		line->size = 0;
		line->str = NULL;
	}

	spin_lock(&kutf_input_lock);

	list_add_tail(&line->node, &context->userdata.input_head);

	kutf_clear_waiting_for_input(context->result_set);

	spin_unlock(&kutf_input_lock);

	wake_up(&context->userdata.input_waitq);

	return 0;
}

void kutf_helper_input_enqueue_end_of_data(struct kutf_context *context)
{
	kutf_helper_input_enqueue(context, NULL, 0);
}

void kutf_helper_ignore_dmesg(struct device *dev)
{
	dev_info(dev, "KUTF: Start ignoring dmesg warnings\n");
}
EXPORT_SYMBOL(kutf_helper_ignore_dmesg);

void kutf_helper_stop_ignoring_dmesg(struct device *dev)
{
	dev_info(dev, "KUTF: Stop ignoring dmesg warnings\n");
}
EXPORT_SYMBOL(kutf_helper_stop_ignoring_dmesg);
