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

#ifndef _KBASE_DEBUG_KTRACE_DEFS_H_
#define _KBASE_DEBUG_KTRACE_DEFS_H_

/* Enable SW tracing when set */
#if defined(CONFIG_MALI_BIFROST_ENABLE_TRACE) || defined(CONFIG_MALI_BIFROST_SYSTEM_TRACE)
#define KBASE_KTRACE_ENABLE 1
#endif

#ifndef KBASE_KTRACE_ENABLE
#ifdef CONFIG_MALI_BIFROST_DEBUG
#define KBASE_KTRACE_ENABLE 1
#else /* CONFIG_MALI_BIFROST_DEBUG */
#define KBASE_KTRACE_ENABLE 0
#endif /* CONFIG_MALI_BIFROST_DEBUG */
#endif /* KBASE_KTRACE_ENABLE */

/* Select targets for recording of trace:
 *
 */
#if KBASE_KTRACE_ENABLE

#ifdef CONFIG_MALI_BIFROST_SYSTEM_TRACE
#define KBASE_KTRACE_TARGET_FTRACE 1
#else /* CONFIG_MALI_BIFROST_SYSTEM_TRACE */
#define KBASE_KTRACE_TARGET_FTRACE 0
#endif /* CONFIG_MALI_BIFROST_SYSTEM_TRACE */

#ifdef CONFIG_MALI_BIFROST_ENABLE_TRACE
#define KBASE_KTRACE_TARGET_RBUF 1
#else /* CONFIG_MALI_BIFROST_ENABLE_TRACE*/
#define KBASE_KTRACE_TARGET_RBUF 0
#endif /* CONFIG_MALI_BIFROST_ENABLE_TRACE */

#else /* KBASE_KTRACE_ENABLE */
#define KBASE_KTRACE_TARGET_FTRACE 0
#define KBASE_KTRACE_TARGET_RBUF 0
#endif /* KBASE_KTRACE_ENABLE */

/*
 * NOTE: KBASE_KTRACE_VERSION_MAJOR, KBASE_KTRACE_VERSION_MINOR are kept in
 * the backend, since updates can be made to one backend in a way that doesn't
 * affect the other.
 *
 * However, modifying the common part could require both backend versions to be
 * updated.
 */

#if KBASE_KTRACE_TARGET_RBUF
typedef u8 kbase_ktrace_flag_t;
typedef u8 kbase_ktrace_code_t;

/*
 * struct kbase_ktrace_backend - backend specific part of a trace message
 *
 * At the very least, this must contain a kbase_ktrace_code_t 'code' member and
 * a kbase_ktrace_flag_t 'flags' member
 */
struct kbase_ktrace_backend;

#include "debug/backend/mali_kbase_debug_ktrace_defs_jm.h"

/* Indicates if the trace message has backend related info.
 *
 * If not set, consider the &kbase_ktrace_backend part of a &kbase_ktrace_msg
 * as uninitialized, apart from the mandatory parts:
 * - code
 * - flags
 */
#define KBASE_KTRACE_FLAG_BACKEND     (((kbase_ktrace_flag_t)1) << 7)

#define KBASE_KTRACE_SHIFT 8 /* 256 entries */
#define KBASE_KTRACE_SIZE (1 << KBASE_KTRACE_SHIFT)
#define KBASE_KTRACE_MASK ((1 << KBASE_KTRACE_SHIFT)-1)

#define KBASE_KTRACE_CODE(X) KBASE_KTRACE_CODE_ ## X

/* Note: compiletime_assert() about this against kbase_ktrace_code_t is in
 * kbase_ktrace_init()
 */
enum kbase_ktrace_code {
	/*
	 * IMPORTANT: USE OF SPECIAL #INCLUDE OF NON-STANDARD HEADER FILE
	 * THIS MUST BE USED AT THE START OF THE ENUM
	 */
#define KBASE_KTRACE_CODE_MAKE_CODE(X) KBASE_KTRACE_CODE(X)
#include <debug/mali_kbase_debug_ktrace_codes.h>
#undef  KBASE_KTRACE_CODE_MAKE_CODE
	/* Comma on its own, to extend the list */
	,
	/* Must be the last in the enum */
	KBASE_KTRACE_CODE_COUNT
};

/**
 * struct kbase_ktrace - object representing a trace message added to trace
 *                      buffer trace_rbuf in &kbase_device
 * @timestamp: CPU timestamp at which the trace message was added.
 * @thread_id: id of the thread in the context of which trace message was
 *             added.
 * @cpu:       indicates which CPU the @thread_id was scheduled on when the
 *             trace message was added.
 * @kctx:      Pointer to the kbase context for which the trace message was
 *             added. Will be NULL for certain trace messages associated with
 *             the &kbase_device itself, such as power management events.
 *             Will point to the appropriate context corresponding to
 *             backend-specific events.
 * @info_val:  value specific to the type of event being traced. Refer to the
 *             specific code in enum kbase_ktrace_code
 * @backend:   backend-specific trace information. All backends must implement
 *             a minimum common set of members
 */
struct kbase_ktrace_msg {
	struct timespec64 timestamp;
	u32 thread_id;
	u32 cpu;
	void *kctx;
	u64 info_val;

	struct kbase_ktrace_backend backend;
};

struct kbase_ktrace {
	spinlock_t              lock;
	u16                     first_out;
	u16                     next_in;
	struct kbase_ktrace_msg *rbuf;
};

#endif /* KBASE_KTRACE_TARGET_RBUF */
#endif /* _KBASE_DEBUG_KTRACE_DEFS_H_ */
