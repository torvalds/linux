/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
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

#ifndef _KBASE_DEBUG_KTRACE_INTERNAL_H_
#define _KBASE_DEBUG_KTRACE_INTERNAL_H_

#if KBASE_KTRACE_TARGET_RBUF

#define KTRACE_DUMP_MESSAGE_SIZE 256

/**
 * kbasep_ktrace_backend_format_header - format the backend part of the header
 * @buffer:    buffer to write to
 * @sz:        size of @buffer in bytes
 * @written:   pointer to storage for updating bytes written so far to @buffer
 *
 * The backend must format only the non-common backend specific parts of the
 * header. It must format them as though they were standalone. The caller will
 * handle adding any delimiters around this.
 */
void kbasep_ktrace_backend_format_header(char *buffer, int sz, s32 *written);

/**
 * kbasep_ktrace_backend_format_msg - format the backend part of the message
 * @trace_msg: ktrace message
 * @buffer:    buffer to write to
 * @sz:        size of @buffer in bytes
 * @written:   pointer to storage for updating bytes written so far to @buffer
 *
 * The backend must format only the non-common backend specific parts of the
 * message. It must format them as though they were standalone. The caller will
 * handle adding any delimiters around this.
 *
 * A caller may have the flags member of @trace_msg with
 * %KBASE_KTRACE_FLAG_BACKEND clear. The backend must handle that setting
 * appropriately.
 */
void kbasep_ktrace_backend_format_msg(struct kbase_ktrace_msg *trace_msg,
		char *buffer, int sz, s32 *written);


/**
 * kbasep_ktrace_reserve - internal function to reserve space for a ktrace
 *                         message
 * @ktrace: kbase device's ktrace
 *
 * This may also empty the oldest entry in the ringbuffer to make space.
 */
struct kbase_ktrace_msg *kbasep_ktrace_reserve(struct kbase_ktrace *ktrace);

/**
 * kbasep_ktrace_msg_init - internal function to initialize just the common
 *                          part of a ktrace message
 * @ktrace:    kbase device's ktrace
 * @trace_msg: ktrace message to initialize
 * @code:      ktrace code
 * @kctx:      kbase context, or NULL if no context
 * @flags:     flags about the message
 * @info_val:  generic information about @code to add to the trace
 *
 * The common part includes the mandatory parts of the backend part
 */
void kbasep_ktrace_msg_init(struct kbase_ktrace *ktrace,
		struct kbase_ktrace_msg *trace_msg, enum kbase_ktrace_code code,
		struct kbase_context *kctx, kbase_ktrace_flag_t flags,
		u64 info_val);

#endif /* KBASE_KTRACE_TARGET_RBUF */

#endif /* _KBASE_DEBUG_KTRACE_INTERNAL_H_ */
