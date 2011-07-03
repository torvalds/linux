/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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
 */

#include "isci.h"
#include "timers.h"


/**
 * isci_timer_list_construct() - This method contrucst the SCI Timer List
 *    object used by the SCI Module class. The construction process involves
 *    creating isci_timer objects and adding them to the SCI Timer List
 *    object's list member. The number of isci_timer objects is determined by
 *    the timer_list_size parameter.
 * @isci_timer_list: This parameter points to the SCI Timer List object being
 *    constructed. The calling routine is responsible for allocating the memory
 *    for isci_timer_list and initializing the timer list_head member of
 *    isci_timer_list.
 * @timer_list_size: This parameter specifies the number of isci_timer objects
 *    contained by the SCI Timer List. which this timer is to be associated.
 *
 * This method returns an error code indicating sucess or failure. The user
 * should check for possible memory allocation error return otherwise, a zero
 * indicates success.
 */
int isci_timer_list_construct(
	struct isci_timer_list *isci_timer_list,
	int timer_list_size)
{
	struct isci_timer *isci_timer;
	int i;
	int err = 0;


	for (i = 0; i < timer_list_size; i++) {

		isci_timer = kzalloc(sizeof(*isci_timer), GFP_KERNEL);

		if (!isci_timer) {

			err = -ENOMEM;
			break;
		}
		isci_timer->used = 0;
		isci_timer->stopped = 1;
		isci_timer->parent = isci_timer_list;
		list_add(&isci_timer->node, &isci_timer_list->timers);
	}

	return 0;

}


/**
 * isci_timer_list_destroy() - This method destroys the SCI Timer List object
 *    used by the SCI Module class. The destruction  process involves freeing
 *    memory allocated for isci_timer objects on the SCI Timer List object's
 *    timers list_head member. If any isci_timer objects are mark as "in use",
 *    they are not freed and the function returns an error code of -EBUSY.
 * @isci_timer_list: This parameter points to the SCI Timer List object being
 *    destroyed.
 *
 * This method returns an error code indicating sucess or failure. The user
 * should check for possible -EBUSY error return, in the event of one or more
 * isci_timers still "in use", otherwise, a zero indicates success.
 */
int isci_timer_list_destroy(
	struct isci_timer_list *isci_timer_list)
{
	struct isci_timer *timer, *tmp;

	list_for_each_entry_safe(timer, tmp, &isci_timer_list->timers, node) {
		isci_timer_free(isci_timer_list, timer);
		list_del(&timer->node);
		kfree(timer);
	}
	return 0;
}



static void isci_timer_restart(struct isci_timer *isci_timer)
{
	struct timer_list *timer =
		&isci_timer->timer;
	unsigned long timeout;

	dev_dbg(&isci_timer->isci_host->pdev->dev,
		"%s: isci_timer = %p\n", __func__, isci_timer);

	isci_timer->restart = 0;
	isci_timer->stopped = 0;
	timeout = isci_timer->timeout_value;
	timeout = (timeout * HZ) / 1000;
	timeout = timeout ? timeout : 1;
	mod_timer(timer, jiffies + timeout);
}

/**
 * This method pulls an isci_timer object off of the list for the SCI Timer
 *    List object specified, marks the isci_timer as "in use" and initializes
 *    it with user callback function and cookie data. The timer is not start at
 *    this time, just reserved for the user.
 * @isci_timer_list: This parameter points to the SCI Timer List from which the
 *    timer is reserved.
 * @cookie: This parameter specifies a piece of information that the user must
 *    retain.  This cookie is to be supplied by the user anytime a timeout
 *    occurs for the created timer.
 * @timer_callback: This parameter specifies the callback method to be invoked
 *    whenever the timer expires.
 *
 * This method returns a pointer to an isci_timer object reserved from the SCI
 * Timer List.  The pointer will be utilized for all further interactions
 * relating to this timer.
 */

static void timer_function(unsigned long data)
{

	struct isci_timer *timer     = (struct isci_timer *)data;
	struct isci_host *isci_host = timer->isci_host;
	unsigned long flags;

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_timer = %p\n", __func__, timer);

	if (isci_stopped == isci_host_get_state(isci_host)) {
		timer->stopped = 1;
		return;
	}

	spin_lock_irqsave(&isci_host->scic_lock, flags);

	if (!timer->stopped) {
		timer->stopped = 1;
		timer->timer_callback(timer->cookie);

		if (timer->restart)
			isci_timer_restart(timer);
	}

	spin_unlock_irqrestore(&isci_host->scic_lock, flags);
}


struct isci_timer *isci_timer_create(
	struct isci_timer_list *isci_timer_list,
	struct isci_host *isci_host,
	void *cookie,
	void (*timer_callback)(void *))
{

	struct timer_list *timer;
	struct isci_timer *isci_timer;
	struct list_head *timer_list =
		&isci_timer_list->timers;
	unsigned long flags;

	spin_lock_irqsave(&isci_host->scic_lock, flags);

	if (list_empty(timer_list)) {
		spin_unlock_irqrestore(&isci_host->scic_lock, flags);
		return NULL;
	}

	isci_timer = list_entry(timer_list->next, struct isci_timer, node);

	if (isci_timer->used) {
		spin_unlock_irqrestore(&isci_host->scic_lock, flags);
		return NULL;
	}
	isci_timer->used = 1;
	isci_timer->stopped = 1;
	list_move_tail(&isci_timer->node, timer_list);

	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	timer = &isci_timer->timer;
	timer->data = (unsigned long)isci_timer;
	timer->function = timer_function;
	isci_timer->cookie = cookie;
	isci_timer->timer_callback = timer_callback;
	isci_timer->isci_host = isci_host;

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_timer = %p\n", __func__, isci_timer);

	return isci_timer;
}

/**
 * isci_timer_free() - This method frees the isci_timer, marking it "free to
 *    use", then places its back at the head of the timers list for the SCI
 *    Timer List object specified.
 * @isci_timer_list: This parameter points to the SCI Timer List from which the
 *    timer is reserved.
 * @isci_timer: This parameter specifies the timer to be freed.
 *
 */
void isci_timer_free(
	struct isci_timer_list *isci_timer_list,
	struct isci_timer *isci_timer)
{
	struct list_head *timer_list = &isci_timer_list->timers;

	dev_dbg(&isci_timer->isci_host->pdev->dev,
		"%s: isci_timer = %p\n", __func__, isci_timer);

	if (list_empty(timer_list))
		return;

	isci_timer->used = 0;
	list_move(&isci_timer->node, timer_list);

	if (!isci_timer->stopped) {
		del_timer(&isci_timer->timer);
		isci_timer->stopped = 1;
	}
}

/**
 * isci_timer_start() - This method starts the specified isci_timer, with the
 *    specified timeout value.
 * @isci_timer: This parameter specifies the timer to be started.
 * @timeout: This parameter specifies the timeout, in milliseconds, after which
 *    the associated callback function will be called.
 *
 */
void isci_timer_start(
	struct isci_timer *isci_timer,
	unsigned long timeout)
{
	struct timer_list *timer = &isci_timer->timer;

	dev_dbg(&isci_timer->isci_host->pdev->dev,
		"%s: isci_timer = %p\n", __func__, isci_timer);

	isci_timer->timeout_value = timeout;
	init_timer(timer);
	timeout = (timeout * HZ) / 1000;
	timeout = timeout ? timeout : 1;

	timer->expires = jiffies + timeout;
	timer->data = (unsigned long)isci_timer;
	timer->function = timer_function;
	isci_timer->stopped = 0;
	isci_timer->restart = 0;
	add_timer(timer);
}

/**
 * isci_timer_stop() - This method stops the supplied isci_timer.
 * @isci_timer: This parameter specifies the isci_timer to be stopped.
 *
 */
void isci_timer_stop(struct isci_timer *isci_timer)
{
	dev_dbg(&isci_timer->isci_host->pdev->dev,
		"%s: isci_timer = %p\n", __func__, isci_timer);

	if (isci_timer->stopped)
		return;

	isci_timer->stopped = 1;

	del_timer(&isci_timer->timer);
}
