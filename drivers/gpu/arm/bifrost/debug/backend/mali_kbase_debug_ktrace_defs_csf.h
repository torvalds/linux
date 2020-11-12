/*
 *
 * (C) COPYRIGHT 2020 ARM Limited. All rights reserved.
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

#ifndef _KBASE_DEBUG_KTRACE_DEFS_CSF_H_
#define _KBASE_DEBUG_KTRACE_DEFS_CSF_H_

#if KBASE_KTRACE_TARGET_RBUF
/**
 * DOC: KTrace version history, CSF variant
 *
 * 1.0:
 * First version, with version information in the header.
 *
 * 1.1:
 * kctx field is no longer a pointer, and is now an ID of the format %d_%u as
 * used by kctx directories in mali debugfs entries: (tgid creating the kctx),
 * (unique kctx id)
 *
 * ftrace backend now outputs kctx field (as %d_%u format).
 *
 * Add fields group, slot, prio, csi into backend-specific part.
 */
#define KBASE_KTRACE_VERSION_MAJOR 1
#define KBASE_KTRACE_VERSION_MINOR 1

/* indicates if the trace message has valid queue-group related info. */
#define KBASE_KTRACE_FLAG_CSF_GROUP     (((kbase_ktrace_flag_t)1) << 0)

/* indicates if the trace message has valid queue related info. */
#define KBASE_KTRACE_FLAG_CSF_QUEUE     (((kbase_ktrace_flag_t)1) << 1)

/* Collect all the flags together for debug checking */
#define KBASE_KTRACE_FLAG_BACKEND_ALL \
		(KBASE_KTRACE_FLAG_CSF_GROUP | KBASE_KTRACE_FLAG_CSF_QUEUE)


/**
 * struct kbase_ktrace_backend - backend specific part of a trace message
 *
 * @code:         Identifies the event, refer to enum kbase_ktrace_code.
 * @flags:        indicates information about the trace message itself. Used
 *                during dumping of the message.
 * @group_handle: Handle identifying the associated queue group. Only valid
 *                when @flags contains KBASE_KTRACE_FLAG_CSF_GROUP.
 * @csg_nr:       Number/index of the associated queue group's command stream
 *                group to which it is mapped, or negative if none associated.
 *                Only valid when @flags contains KBASE_KTRACE_FLAG_CSF_GROUP.
 * @slot_prio:    The priority of the slot for the associated group, if it was
 *                scheduled. Hence, only valid when @csg_nr >=0 and @flags
 *                contains KBASE_KTRACE_FLAG_CSF_GROUP.
 * @csi_index:    ID of the associated queue's Command Stream HW interface.
 *                Only valid when @flags contains KBASE_KTRACE_FLAG_CSF_QUEUE.
 */
struct kbase_ktrace_backend {
	/* Place 64 and 32-bit members together */
	/* Pack smaller members together */
	kbase_ktrace_code_t code;
	kbase_ktrace_flag_t flags;
	u8 group_handle;
	s8 csg_nr;
	u8 slot_prio;
	s8 csi_index;
};

#endif /* KBASE_KTRACE_TARGET_RBUF */
#endif /* _KBASE_DEBUG_KTRACE_DEFS_CSF_H_ */
