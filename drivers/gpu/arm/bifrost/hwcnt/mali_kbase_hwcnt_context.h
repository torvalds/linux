/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2018, 2020-2022 ARM Limited. All rights reserved.
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

/*
 * Hardware counter context API.
 */

#ifndef _KBASE_HWCNT_CONTEXT_H_
#define _KBASE_HWCNT_CONTEXT_H_

#include <linux/types.h>
#include <linux/workqueue.h>

struct kbase_hwcnt_backend_interface;
struct kbase_hwcnt_context;

/**
 * kbase_hwcnt_context_init() - Initialise a hardware counter context.
 * @iface:    Non-NULL pointer to a hardware counter backend interface.
 * @out_hctx: Non-NULL pointer to where the pointer to the created context will
 *            be stored on success.
 *
 * On creation, the disable count of the context will be 0.
 * A hardware counter accumulator can be acquired using a created context.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_context_init(const struct kbase_hwcnt_backend_interface *iface,
			     struct kbase_hwcnt_context **out_hctx);

/**
 * kbase_hwcnt_context_term() - Terminate a hardware counter context.
 * @hctx: Pointer to context to be terminated.
 */
void kbase_hwcnt_context_term(struct kbase_hwcnt_context *hctx);

/**
 * kbase_hwcnt_context_metadata() - Get the hardware counter metadata used by
 *                                  the context, so related counter data
 *                                  structures can be created.
 * @hctx: Non-NULL pointer to the hardware counter context.
 *
 * Return: Non-NULL pointer to metadata, or NULL on error.
 */
const struct kbase_hwcnt_metadata *kbase_hwcnt_context_metadata(struct kbase_hwcnt_context *hctx);

/**
 * kbase_hwcnt_context_disable() - Increment the disable count of the context.
 * @hctx: Non-NULL pointer to the hardware counter context.
 *
 * If a call to this function increments the disable count from 0 to 1, and
 * an accumulator has been acquired, then a counter dump will be performed
 * before counters are disabled via the backend interface.
 *
 * Subsequent dumps via the accumulator while counters are disabled will first
 * return the accumulated dump, then will return dumps with zeroed counters.
 *
 * After this function call returns, it is guaranteed that counters will not be
 * enabled via the backend interface.
 */
void kbase_hwcnt_context_disable(struct kbase_hwcnt_context *hctx);

/**
 * kbase_hwcnt_context_disable_atomic() - Increment the disable count of the
 *                                        context if possible in an atomic
 *                                        context.
 * @hctx: Non-NULL pointer to the hardware counter context.
 *
 * This function will only succeed if hardware counters are effectively already
 * disabled, i.e. there is no accumulator, the disable count is already
 * non-zero, or the accumulator has no counters set.
 *
 * After this function call returns true, it is guaranteed that counters will
 * not be enabled via the backend interface.
 *
 * Return: True if the disable count was incremented, else False.
 */
bool kbase_hwcnt_context_disable_atomic(struct kbase_hwcnt_context *hctx);

/**
 * kbase_hwcnt_context_enable() - Decrement the disable count of the context.
 * @hctx: Non-NULL pointer to the hardware counter context.
 *
 * If a call to this function decrements the disable count from 1 to 0, and
 * an accumulator has been acquired, then counters will be re-enabled via the
 * backend interface.
 *
 * If an accumulator has been acquired and enabling counters fails for some
 * reason, the accumulator will be placed into an error state.
 *
 * It is only valid to call this function one time for each prior returned call
 * to kbase_hwcnt_context_disable.
 *
 * The spinlock documented in the backend interface that was passed in to
 * kbase_hwcnt_context_init() must be held before calling this function.
 */
void kbase_hwcnt_context_enable(struct kbase_hwcnt_context *hctx);

/**
 * kbase_hwcnt_context_queue_work() - Queue hardware counter related async
 *                                    work on a workqueue specialized for
 *                                    hardware counters.
 * @hctx: Non-NULL pointer to the hardware counter context.
 * @work: Non-NULL pointer to work to queue.
 *
 * Return: false if work was already on a queue, true otherwise.
 *
 * Performance counter related work is high priority, short running, and
 * generally CPU locality is unimportant. There is no standard workqueue that
 * can service this flavor of work.
 *
 * Rather than have each user of counters define their own workqueue, we have
 * a centralized one in here that anybody using this hardware counter API
 * should use.
 *
 * Before the context is destroyed, all work submitted must have been completed.
 * Given that the work enqueued via this function is likely to be hardware
 * counter related and will therefore use the context object, this is likely
 * to be behavior that will occur naturally.
 *
 * Historical note: prior to this centralized workqueue, the system_highpri_wq
 * was used. This was generally fine, except when a particularly long running,
 * higher priority thread ended up scheduled on the enqueuing CPU core. Given
 * that hardware counters requires tight integration with power management,
 * this meant progress through the power management states could be stalled
 * for however long that higher priority thread took.
 */
bool kbase_hwcnt_context_queue_work(struct kbase_hwcnt_context *hctx, struct work_struct *work);

#endif /* _KBASE_HWCNT_CONTEXT_H_ */
