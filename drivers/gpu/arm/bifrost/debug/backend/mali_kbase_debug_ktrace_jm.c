// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2020-2022 ARM Limited. All rights reserved.
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
#include "debug/backend/mali_kbase_debug_ktrace_jm.h"

#if KBASE_KTRACE_TARGET_RBUF

void kbasep_ktrace_backend_format_header(char *buffer, int sz, s32 *written)
{
	*written += MAX(snprintf(buffer + *written, MAX(sz - *written, 0),
			"katom,gpu_addr,jobslot,refcount"), 0);
}

void kbasep_ktrace_backend_format_msg(struct kbase_ktrace_msg *trace_msg,
		char *buffer, int sz, s32 *written)
{
	/* katom */
	if (trace_msg->backend.gpu.flags & KBASE_KTRACE_FLAG_JM_ATOM)
		*written += MAX(snprintf(buffer + *written,
				MAX(sz - *written, 0),
				"atom %d (ud: 0x%llx 0x%llx)",
				trace_msg->backend.gpu.atom_number,
				trace_msg->backend.gpu.atom_udata[0],
				trace_msg->backend.gpu.atom_udata[1]), 0);

	/* gpu_addr */
	if (trace_msg->backend.gpu.flags & KBASE_KTRACE_FLAG_BACKEND)
		*written += MAX(snprintf(buffer + *written,
				MAX(sz - *written, 0),
				",%.8llx,", trace_msg->backend.gpu.gpu_addr),
				0);
	else
		*written += MAX(snprintf(buffer + *written,
				MAX(sz - *written, 0),
				",,"), 0);

	/* jobslot */
	if (trace_msg->backend.gpu.flags & KBASE_KTRACE_FLAG_JM_JOBSLOT)
		*written += MAX(snprintf(buffer + *written,
				MAX(sz - *written, 0),
				"%d", trace_msg->backend.gpu.jobslot), 0);

	*written += MAX(snprintf(buffer + *written, MAX(sz - *written, 0),
				","), 0);

	/* refcount */
	if (trace_msg->backend.gpu.flags & KBASE_KTRACE_FLAG_JM_REFCOUNT)
		*written += MAX(snprintf(buffer + *written,
				MAX(sz - *written, 0),
				"%d", trace_msg->backend.gpu.refcount), 0);
}

void kbasep_ktrace_add_jm(struct kbase_device *kbdev,
			  enum kbase_ktrace_code code,
			  struct kbase_context *kctx,
			  const struct kbase_jd_atom *katom, u64 gpu_addr,
			  kbase_ktrace_flag_t flags, int refcount, int jobslot,
			  u64 info_val)
{
	unsigned long irqflags;
	struct kbase_ktrace_msg *trace_msg;

	if (unlikely(!kbasep_ktrace_initialized(&kbdev->ktrace)))
		return;

	spin_lock_irqsave(&kbdev->ktrace.lock, irqflags);

	/* Reserve and update indices */
	trace_msg = kbasep_ktrace_reserve(&kbdev->ktrace);

	/* Fill the common part of the message (including backend.gpu.flags) */
	kbasep_ktrace_msg_init(&kbdev->ktrace, trace_msg, code, kctx, flags,
			info_val);

	/* Indicate to the common code that backend-specific parts will be
	 * valid
	 */
	trace_msg->backend.gpu.flags |= KBASE_KTRACE_FLAG_BACKEND;

	/* Fill the JM-specific parts of the message */
	if (katom) {
		trace_msg->backend.gpu.flags |= KBASE_KTRACE_FLAG_JM_ATOM;

		trace_msg->backend.gpu.atom_number =
			kbase_jd_atom_id(katom->kctx, katom);
		trace_msg->backend.gpu.atom_udata[0] = katom->udata.blob[0];
		trace_msg->backend.gpu.atom_udata[1] = katom->udata.blob[1];
	}

	trace_msg->backend.gpu.gpu_addr = gpu_addr;
	trace_msg->backend.gpu.jobslot = jobslot;
	/* Clamp refcount */
	trace_msg->backend.gpu.refcount = MIN((unsigned int)refcount, 0xFF);

	WARN_ON((trace_msg->backend.gpu.flags & ~KBASE_KTRACE_FLAG_ALL));

	/* Done */
	spin_unlock_irqrestore(&kbdev->ktrace.lock, irqflags);
}

#endif /* KBASE_KTRACE_TARGET_RBUF */
