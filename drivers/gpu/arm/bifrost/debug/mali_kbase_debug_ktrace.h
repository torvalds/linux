/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
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

/*
 * DOC: Kbase's own trace, 'KTrace'
 *
 * Low overhead trace specific to kbase, aimed at:
 * - common use-cases for tracing kbase specific functionality to do with
 *   running work on the GPU
 * - easy 1-line addition of new types of trace
 *
 * KTrace can be recorded in one or more of the following targets:
 * - KBASE_KTRACE_TARGET_RBUF: low overhead ringbuffer protected by an
 *   irq-spinlock, output available via dev_dbg() and debugfs file
 * - KBASE_KTRACE_TARGET_FTRACE: ftrace based tracepoints under 'mali' events
 */

#ifndef _KBASE_DEBUG_KTRACE_H_
#define _KBASE_DEBUG_KTRACE_H_

#if KBASE_KTRACE_TARGET_FTRACE
#include "mali_linux_trace.h"
#endif

#if MALI_USE_CSF
#include "debug/backend/mali_kbase_debug_ktrace_csf.h"
#else
#include "debug/backend/mali_kbase_debug_ktrace_jm.h"
#endif

/**
 * kbase_ktrace_init - initialize kbase ktrace.
 * @kbdev: kbase device
 * Return: 0 if successful or a negative error code on failure.
 */
int kbase_ktrace_init(struct kbase_device *kbdev);

/**
 * kbase_ktrace_term - terminate kbase ktrace.
 * @kbdev: kbase device
 */
void kbase_ktrace_term(struct kbase_device *kbdev);

/**
 * kbase_ktrace_hook_wrapper - wrapper so that dumping ktrace can be done via a
 *                             callback.
 * @param: kbase device, cast to void pointer
 */
void kbase_ktrace_hook_wrapper(void *param);

#if IS_ENABLED(CONFIG_DEBUG_FS)
/**
 * kbase_ktrace_debugfs_init - initialize kbase ktrace for debugfs usage, if
 *                             the selected targets support it.
 * @kbdev: kbase device
 *
 * There is no matching 'term' call, debugfs_remove_recursive() is sufficient.
 */
void kbase_ktrace_debugfs_init(struct kbase_device *kbdev);
#endif /* CONFIG_DEBUG_FS */

/*
 * KTrace target for internal ringbuffer
 */
#if KBASE_KTRACE_TARGET_RBUF
/**
 * kbasep_ktrace_initialized - Check whether kbase ktrace is initialized
 *
 * @ktrace: ktrace of kbase device.
 *
 * Return: true if ktrace has been initialized.
 */
static inline bool kbasep_ktrace_initialized(struct kbase_ktrace *ktrace)
{
	return ktrace->rbuf != NULL;
}

/**
 * kbasep_ktrace_add - internal function to add trace to the ringbuffer.
 * @kbdev:    kbase device
 * @code:     ktrace code
 * @kctx:     kbase context, or NULL if no context
 * @flags:    flags about the message
 * @info_val: generic information about @code to add to the trace
 *
 * PRIVATE: do not use directly. Use KBASE_KTRACE_ADD() instead.
 */
void kbasep_ktrace_add(struct kbase_device *kbdev, enum kbase_ktrace_code code,
		struct kbase_context *kctx, kbase_ktrace_flag_t flags,
		u64 info_val);

/**
 * kbasep_ktrace_clear - clear the trace ringbuffer
 * @kbdev: kbase device
 *
 * PRIVATE: do not use directly. Use KBASE_KTRACE_CLEAR() instead.
 */
void kbasep_ktrace_clear(struct kbase_device *kbdev);

/**
 * kbasep_ktrace_dump - dump ktrace ringbuffer to dev_dbg(), then clear it
 * @kbdev: kbase device
 *
 * PRIVATE: do not use directly. Use KBASE_KTRACE_DUMP() instead.
 */
void kbasep_ktrace_dump(struct kbase_device *kbdev);

#define KBASE_KTRACE_RBUF_ADD(kbdev, code, kctx, info_val)     \
	kbasep_ktrace_add(kbdev, KBASE_KTRACE_CODE(code), kctx, 0, \
			info_val) \

#define KBASE_KTRACE_RBUF_CLEAR(kbdev) \
	kbasep_ktrace_clear(kbdev)

#define KBASE_KTRACE_RBUF_DUMP(kbdev) \
	kbasep_ktrace_dump(kbdev)

#else /* KBASE_KTRACE_TARGET_RBUF */

#define KBASE_KTRACE_RBUF_ADD(kbdev, code, kctx, info_val) \
	do { \
		CSTD_UNUSED(kbdev); \
		CSTD_NOP(code); \
		CSTD_UNUSED(kctx); \
		CSTD_UNUSED(info_val); \
		CSTD_NOP(0); \
	} while (0)

#define KBASE_KTRACE_RBUF_CLEAR(kbdev) \
	do { \
		CSTD_UNUSED(kbdev); \
		CSTD_NOP(0); \
	} while (0)
#define KBASE_KTRACE_RBUF_DUMP(kbdev) \
	do { \
		CSTD_UNUSED(kbdev); \
		CSTD_NOP(0); \
	} while (0)
#endif /* KBASE_KTRACE_TARGET_RBUF */

/*
 * KTrace target for Linux's ftrace
 */
#if KBASE_KTRACE_TARGET_FTRACE

#define KBASE_KTRACE_FTRACE_ADD(kbdev, code, kctx, info_val) \
	trace_mali_##code(kctx, info_val)

#else /* KBASE_KTRACE_TARGET_FTRACE */
#define KBASE_KTRACE_FTRACE_ADD(kbdev, code, kctx, info_val) \
	do { \
		CSTD_UNUSED(kbdev); \
		CSTD_NOP(code); \
		CSTD_UNUSED(kctx); \
		CSTD_UNUSED(info_val); \
		CSTD_NOP(0); \
	} while (0)
#endif /* KBASE_KTRACE_TARGET_FTRACE */

/* No 'clear' implementation for ftrace yet */
#define KBASE_KTRACE_FTRACE_CLEAR(kbdev) \
	do { \
		CSTD_UNUSED(kbdev); \
		CSTD_NOP(0); \
	} while (0)

/* No 'dump' implementation for ftrace yet */
#define KBASE_KTRACE_FTRACE_DUMP(kbdev) \
	do { \
		CSTD_UNUSED(kbdev); \
		CSTD_NOP(0); \
	} while (0)

/*
 * Master set of macros to route KTrace to any of the targets
 */

/**
 * KBASE_KTRACE_ADD - Add trace values
 * @kbdev:    kbase device
 * @code:     trace code
 * @kctx:     kbase context, or NULL if no context
 * @info_val: generic information about @code to add to the trace
 *
 * Note: Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_BIFROST_DEBUG not defined). Therefore, when
 * KBASE_KTRACE_ENABLE == 0 any functions called to get the parameters supplied
 * to this macro must:
 * a) be static or static inline, and
 * b) just return 0 and have no other statements present in the body.
 */
#define KBASE_KTRACE_ADD(kbdev, code, kctx, info_val) \
	do { \
		/* capture values that could come from non-pure function calls */ \
		u64 __info_val = info_val; \
		KBASE_KTRACE_RBUF_ADD(kbdev, code, kctx, __info_val); \
		KBASE_KTRACE_FTRACE_ADD(kbdev, code, kctx, __info_val); \
	} while (0)

/**
 * KBASE_KTRACE_CLEAR - Clear the trace, if applicable to the target(s)
 * @kbdev:    kbase device
 */
#define KBASE_KTRACE_CLEAR(kbdev) \
	do { \
		KBASE_KTRACE_RBUF_CLEAR(kbdev); \
		KBASE_KTRACE_FTRACE_CLEAR(kbdev); \
	} while (0)

/**
 * KBASE_KTRACE_DUMP - Dump the trace, if applicable to the target(s)
 * @kbdev:    kbase device
 */
#define KBASE_KTRACE_DUMP(kbdev) \
	do { \
		KBASE_KTRACE_RBUF_DUMP(kbdev); \
		KBASE_KTRACE_FTRACE_DUMP(kbdev); \
	} while (0)

#endif /* _KBASE_DEBUG_KTRACE_H_ */
