// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2020-2021 ARM Limited. All rights reserved.
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

#include <mali_kbase.h>
#include "debug/mali_kbase_debug_ktrace_internal.h"
#include "debug/backend/mali_kbase_debug_ktrace_csf.h"

#if KBASE_KTRACE_TARGET_RBUF

void kbasep_ktrace_backend_format_header(char *buffer, int sz, s32 *written)
{
	*written += MAX(snprintf(buffer + *written, MAX(sz - *written, 0),
			"group,slot,prio,csi,kcpu"), 0);
}

void kbasep_ktrace_backend_format_msg(struct kbase_ktrace_msg *trace_msg,
		char *buffer, int sz, s32 *written)
{
	const union kbase_ktrace_backend * const be_msg = &trace_msg->backend;
	/* At present, no need to check for KBASE_KTRACE_FLAG_BACKEND, as the
	 * other backend-specific flags currently imply this anyway
	 */

	/* group parts */
	if (be_msg->gpu.flags & KBASE_KTRACE_FLAG_CSF_GROUP) {
		const s8 slot = be_msg->gpu.csg_nr;
		/* group,slot, */
		*written += MAX(snprintf(buffer + *written,
				MAX(sz - *written, 0),
				"%u,%d,", be_msg->gpu.group_handle, slot), 0);

		/* prio */
		if (slot >= 0)
			*written += MAX(snprintf(buffer + *written,
					MAX(sz - *written, 0),
					"%u", be_msg->gpu.slot_prio), 0);

		/* , */
		*written += MAX(snprintf(buffer + *written,
				MAX(sz - *written, 0),
				","), 0);
	} else {
		/* No group,slot,prio fields, but ensure ending with "," */
		*written += MAX(snprintf(buffer + *written,
				MAX(sz - *written, 0),
				",,,"), 0);
	}

	/* queue parts: csi */
	if (trace_msg->backend.gpu.flags & KBASE_KTRACE_FLAG_CSF_QUEUE)
		*written += MAX(snprintf(buffer + *written,
				MAX(sz - *written, 0),
				"%d", be_msg->gpu.csi_index), 0);

	/* , */
	*written += MAX(snprintf(buffer + *written,
				MAX(sz - *written, 0),
				","), 0);

	if (be_msg->gpu.flags & KBASE_KTRACE_FLAG_CSF_KCPU) {
		/* kcpu data */
		*written += MAX(snprintf(buffer + *written,
				MAX(sz - *written, 0),
				"kcpu %d (0x%llx)",
				be_msg->kcpu.id,
				be_msg->kcpu.extra_info_val), 0);
	}

	/* Don't end with a trailing "," - this is a 'standalone' formatted
	 * msg, caller will handle the delimiters
	 */
}

void kbasep_ktrace_add_csf(struct kbase_device *kbdev,
		enum kbase_ktrace_code code, struct kbase_queue_group *group,
		struct kbase_queue *queue, kbase_ktrace_flag_t flags,
		u64 info_val)
{
	unsigned long irqflags;
	struct kbase_ktrace_msg *trace_msg;
	struct kbase_context *kctx = NULL;

	spin_lock_irqsave(&kbdev->ktrace.lock, irqflags);

	/* Reserve and update indices */
	trace_msg = kbasep_ktrace_reserve(&kbdev->ktrace);

	/* Determine the kctx */
	if (group)
		kctx = group->kctx;
	else if (queue)
		kctx = queue->kctx;

	/* Fill the common part of the message (including backend.gpu.flags) */
	kbasep_ktrace_msg_init(&kbdev->ktrace, trace_msg, code, kctx, flags,
			info_val);

	/* Indicate to the common code that backend-specific parts will be
	 * valid
	 */
	trace_msg->backend.gpu.flags |= KBASE_KTRACE_FLAG_BACKEND;

	/* Fill the CSF-specific parts of the message
	 *
	 * Generally, no need to use default initializers when queue/group not
	 * present - can usually check the flags instead.
	 */

	if (queue) {
		trace_msg->backend.gpu.flags |= KBASE_KTRACE_FLAG_CSF_QUEUE;
		trace_msg->backend.gpu.csi_index = queue->csi_index;
	}

	if (group) {
		const s8 slot = group->csg_nr;

		trace_msg->backend.gpu.flags |= KBASE_KTRACE_FLAG_CSF_GROUP;

		trace_msg->backend.gpu.csg_nr = slot;

		if (slot >= 0) {
			struct kbase_csf_csg_slot *csg_slot =
				&kbdev->csf.scheduler.csg_slots[slot];

			trace_msg->backend.gpu.slot_prio =
				csg_slot->priority;
		}
		/* slot >=0 indicates whether slot_prio valid, so no need to
		 * initialize in the case where it's invalid
		 */

		trace_msg->backend.gpu.group_handle = group->handle;
	}

	WARN_ON((trace_msg->backend.gpu.flags & ~KBASE_KTRACE_FLAG_ALL));

	/* Done */
	spin_unlock_irqrestore(&kbdev->ktrace.lock, irqflags);
}

void kbasep_ktrace_add_csf_kcpu(struct kbase_device *kbdev,
				enum kbase_ktrace_code code,
				struct kbase_kcpu_command_queue *queue,
				u64 info_val1, u64 info_val2)
{
	unsigned long irqflags;
	struct kbase_ktrace_msg *trace_msg;
	struct kbase_context *kctx = queue->kctx;

	spin_lock_irqsave(&kbdev->ktrace.lock, irqflags);

	/* Reserve and update indices */
	trace_msg = kbasep_ktrace_reserve(&kbdev->ktrace);

	/* Fill the common part of the message */
	kbasep_ktrace_msg_init(&kbdev->ktrace, trace_msg, code, kctx, 0,
		info_val1);

	/* Indicate to the common code that backend-specific parts will be
	 * valid
	 */
	trace_msg->backend.gpu.flags |= KBASE_KTRACE_FLAG_BACKEND;

	/* Fill the KCPU-specific parts of the message */
	trace_msg->backend.kcpu.id = queue->id;
	trace_msg->backend.kcpu.extra_info_val = info_val2;
	trace_msg->backend.gpu.flags |= KBASE_KTRACE_FLAG_CSF_KCPU;

	WARN_ON((trace_msg->backend.gpu.flags & ~KBASE_KTRACE_FLAG_ALL));

	/* Done */
	spin_unlock_irqrestore(&kbdev->ktrace.lock, irqflags);
}

#endif /* KBASE_KTRACE_TARGET_RBUF */
