/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_TILER_HEAP_RECLAIM_H_
#define _KBASE_CSF_TILER_HEAP_RECLAIM_H_

#include <mali_kbase.h>

/**
 * kbase_csf_tiler_heap_reclaim_sched_notify_grp_active - Notifier function for the scheduler
 *                                                        to use when a group is put on-slot.
 *
 * @group: Pointer to the group object that has been placed on-slot for running.
 *
 */
void kbase_csf_tiler_heap_reclaim_sched_notify_grp_active(struct kbase_queue_group *group);

/**
 * kbase_csf_tiler_heap_reclaim_sched_notify_grp_evict - Notifier function for the scheduler
 *               to use when a group is evicted out of the schedulder's scope, i.e no run of
 *               the group is possible afterwards.
 *
 * @group: Pointer to the group object that has been evicted.
 *
 */
void kbase_csf_tiler_heap_reclaim_sched_notify_grp_evict(struct kbase_queue_group *group);

/**
 * kbase_csf_tiler_heap_reclaim_sched_notify_grp_suspend - Notifier function for the scheduler
 *                to use when a group is suspended from running, but could resume in future.
 *
 * @group: Pointer to the group object that is in suspended state.
 *
 */
void kbase_csf_tiler_heap_reclaim_sched_notify_grp_suspend(struct kbase_queue_group *group);

/**
 * kbase_csf_tiler_heap_reclaim_ctx_init - Initializer on per context data fields for use
 *                                         with the tiler heap reclaim manager.
 *
 * @kctx: Pointer to the kbase_context.
 *
 */
void kbase_csf_tiler_heap_reclaim_ctx_init(struct kbase_context *kctx);

/**
 * kbase_csf_tiler_heap_reclaim_mgr_init - Initializer for the tiler heap reclaim manger.
 *
 * @kbdev: Pointer to the device.
 *
 */
void kbase_csf_tiler_heap_reclaim_mgr_init(struct kbase_device *kbdev);

/**
 * kbase_csf_tiler_heap_reclaim_mgr_term - Termination call for the tiler heap reclaim manger.
 *
 * @kbdev: Pointer to the device.
 *
 */
void kbase_csf_tiler_heap_reclaim_mgr_term(struct kbase_device *kbdev);

#endif
