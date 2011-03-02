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
 * @ihost: container of the timer list
 *
 * This method returns an error code indicating sucess or failure. The user
 * should check for possible memory allocation error return otherwise, a zero
 * indicates success.
 */
int isci_timer_list_construct(struct isci_host *ihost)
{
	struct isci_timer *itimer;
	int i, err = 0;

	INIT_LIST_HEAD(&ihost->timers);
	for (i = 0; i < SCI_MAX_TIMER_COUNT; i++) {
		itimer = devm_kzalloc(&ihost->pdev->dev, sizeof(*itimer), GFP_KERNEL);

		if (!itimer) {
			err = -ENOMEM;
			break;
		}
		init_timer(&itimer->timer);
		itimer->used = 0;
		itimer->stopped = 1;
		list_add(&itimer->node, &ihost->timers);
	}

	return err;
}

/**
 * isci_timer_list_destroy() - This method destroys the SCI Timer List object
 *    used by the SCI Module class. The destruction  process involves freeing
 *    memory allocated for isci_timer objects on the SCI Timer List object's
 *    timers list_head member. If any isci_timer objects are mark as "in use",
 *    they are not freed and the function returns an error code of -EBUSY.
 * @ihost: container of the list to be destroyed
 */
void isci_timer_list_destroy(struct isci_host *ihost)
{
	struct isci_timer *timer;
	LIST_HEAD(list);

	spin_lock_irq(&ihost->scic_lock);
	list_splice_init(&ihost->timers, &list);
	spin_unlock_irq(&ihost->scic_lock);

	list_for_each_entry(timer, &list, node)
		del_timer_sync(&timer->timer);
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

	struct isci_timer *timer = (struct isci_timer *)data;
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
		timer->timer_callback(timer->cb_param);
	}

	spin_unlock_irqrestore(&isci_host->scic_lock, flags);
}


struct isci_timer *isci_timer_create(struct isci_host *ihost, void *cb_param,
				     void (*timer_callback)(void *))
{
	struct timer_list *timer;
	struct isci_timer *isci_timer;
	struct list_head *list = &ihost->timers;

	WARN_ONCE(!spin_is_locked(&ihost->scic_lock),
		  "%s: unlocked!\n", __func__);

	if (WARN_ONCE(list_empty(list), "%s: timer pool empty\n", __func__))
		return NULL;

	isci_timer = list_entry(list->next, struct isci_timer, node);

	isci_timer->used = 1;
	isci_timer->stopped = 1;
	/* FIXME: what!? we recycle the timer, rather than take it off
	 * the free list?
	 */
	list_move_tail(&isci_timer->node, list);

	timer = &isci_timer->timer;
	timer->data = (unsigned long)isci_timer;
	timer->function = timer_function;
	isci_timer->cb_param = cb_param;
	isci_timer->timer_callback = timer_callback;
	isci_timer->isci_host = ihost;

	dev_dbg(&ihost->pdev->dev,
		"%s: isci_timer = %p\n", __func__, isci_timer);

	return isci_timer;
}

/* isci_del_timer() - This method frees the isci_timer, marking it "free to
 *    use", then places its back at the head of the timers list for the SCI
 *    Timer List object specified.
 */
void isci_del_timer(struct isci_host *ihost, struct isci_timer *isci_timer)
{
	struct list_head *list = &ihost->timers;

	WARN_ONCE(!spin_is_locked(&ihost->scic_lock),
		  "%s unlocked!\n", __func__);

	dev_dbg(&isci_timer->isci_host->pdev->dev,
		"%s: isci_timer = %p\n", __func__, isci_timer);

	isci_timer->used = 0;
	list_move(&isci_timer->node, list);
	del_timer(&isci_timer->timer);
	isci_timer->stopped = 1;
}

/**
 * isci_timer_start() - This method starts the specified isci_timer, with the
 *    specified timeout value.
 * @isci_timer: This parameter specifies the timer to be started.
 * @timeout: This parameter specifies the timeout, in milliseconds, after which
 *    the associated callback function will be called.
 *
 */
void isci_timer_start(struct isci_timer *isci_timer, unsigned long tmo)
{
	struct timer_list *timer = &isci_timer->timer;

	dev_dbg(&isci_timer->isci_host->pdev->dev,
		"%s: isci_timer = %p\n", __func__, isci_timer);

	isci_timer->stopped = 0;
	mod_timer(timer, jiffies + msecs_to_jiffies(tmo));
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

	isci_timer->stopped = 1;
	del_timer(&isci_timer->timer);
}
