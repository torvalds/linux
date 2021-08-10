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

#ifndef _KBASE_DEBUG_KTRACE_CSF_H_
#define _KBASE_DEBUG_KTRACE_CSF_H_

/*
 * KTrace target for internal ringbuffer
 */
#if KBASE_KTRACE_TARGET_RBUF
/**
 * kbasep_ktrace_add_csf - internal function to add trace about CSF
 * @kbdev:    kbase device
 * @code:     trace code
 * @group:    queue group, or NULL if no queue group
 * @queue:    queue, or NULL if no queue
 * @flags:    flags about the message
 * @info_val: generic information about @code to add to the trace
 *
 * PRIVATE: do not use directly. Use KBASE_KTRACE_ADD_CSF() instead.
 */

void kbasep_ktrace_add_csf(struct kbase_device *kbdev,
		enum kbase_ktrace_code code, struct kbase_queue_group *group,
		struct kbase_queue *queue, kbase_ktrace_flag_t flags,
		u64 info_val);

/**
 * kbasep_ktrace_add_csf_kcpu - internal function to add trace about the CSF
 *				KCPU queues.
 * @kbdev:      kbase device
 * @code:       trace code
 * @queue:      queue, or NULL if no queue
 * @info_val1:  Main infoval variable with information based on the KCPU
 *              ktrace call. Refer to mali_kbase_debug_ktrace_codes_csf.h
 *              for information on the infoval values.
 * @info_val2:  Extra infoval variable with information based on the KCPU
 *              ktrace call. Refer to mali_kbase_debug_ktrace_codes_csf.h
 *              for information on the infoval values.
 *
 * PRIVATE: do not use directly. Use KBASE_KTRACE_ADD_CSF_KCPU() instead.
 */
void kbasep_ktrace_add_csf_kcpu(struct kbase_device *kbdev,
				enum kbase_ktrace_code code,
				struct kbase_kcpu_command_queue *queue,
				u64 info_val1, u64 info_val2);

#define KBASE_KTRACE_RBUF_ADD_CSF(kbdev, code, group, queue, flags, info_val) \
	kbasep_ktrace_add_csf(kbdev, KBASE_KTRACE_CODE(code), group, queue, \
	flags, info_val)

#define KBASE_KTRACE_RBUF_ADD_CSF_KCPU(kbdev, code, queue, info_val1, \
	info_val2) kbasep_ktrace_add_csf_kcpu(kbdev, KBASE_KTRACE_CODE(code), \
	queue, info_val1, info_val2)

#else /* KBASE_KTRACE_TARGET_RBUF */

#define KBASE_KTRACE_RBUF_ADD_CSF(kbdev, code, group, queue, flags, info_val) \
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(group);\
		CSTD_UNUSED(queue);\
		CSTD_UNUSED(flags);\
		CSTD_UNUSED(info_val);\
		CSTD_NOP(0);\
	} while (0)

#define KBASE_KTRACE_RBUF_ADD_CSF_KCPU(kbdev, code, queue, info_val1, info_val2) \
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(queue);\
		CSTD_UNUSED(info_val1);\
		CSTD_UNUSED(info_val2);\
	} while (0)

#endif /* KBASE_KTRACE_TARGET_RBUF */

/*
 * KTrace target for Linux's ftrace
 *
 * Note: the header file(s) that define the trace_mali_<...> tracepoints are
 * included by the parent header file
 */
#if KBASE_KTRACE_TARGET_FTRACE

#define KBASE_KTRACE_FTRACE_ADD_CSF(kbdev, code, group, queue, info_val) \
	trace_mali_##code(kbdev, group, queue, info_val)

#define KBASE_KTRACE_FTRACE_ADD_KCPU(code, queue, info_val1, info_val2) \
	trace_mali_##code(queue, info_val1, info_val2)

#else /* KBASE_KTRACE_TARGET_FTRACE */

#define KBASE_KTRACE_FTRACE_ADD_CSF(kbdev, code, group, queue, info_val) \
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(group);\
		CSTD_UNUSED(queue);\
		CSTD_UNUSED(info_val);\
		CSTD_NOP(0);\
	} while (0)

#define KBASE_KTRACE_FTRACE_ADD_KCPU(code, queue, info_val1, info_val2) \
	do {\
		CSTD_NOP(code);\
		CSTD_UNUSED(queue);\
		CSTD_UNUSED(info_val1);\
		CSTD_UNUSED(info_val2);\
	} while (0)

#endif /* KBASE_KTRACE_TARGET_FTRACE */

/*
 * Master set of macros to route KTrace to any of the targets
 */

/**
 * KBASE_KTRACE_ADD_CSF_GRP - Add trace values about a group, with info
 * @kbdev:    kbase device
 * @code:     trace code
 * @group:    queue group, or NULL if no queue group
 * @info_val: generic information about @code to add to the trace
 *
 * Note: Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_BIFROST_DEBUG not defined). Therefore, when
 * KBASE_KTRACE_ENABLE == 0 any functions called to get the parameters supplied
 * to this macro must:
 * a) be static or static inline, and
 * b) just return 0 and have no other statements present in the body.
 */
#define KBASE_KTRACE_ADD_CSF_GRP(kbdev, code, group, info_val) \
	do { \
		/* capture values that could come from non-pure fn calls */ \
		struct kbase_queue_group *__group = group; \
		u64 __info_val = info_val; \
		KBASE_KTRACE_RBUF_ADD_CSF(kbdev, code, __group, NULL, 0u, \
				__info_val); \
		KBASE_KTRACE_FTRACE_ADD_CSF(kbdev, code, __group, NULL, \
				__info_val); \
	} while (0)

/**
 * KBASE_KTRACE_ADD_CSF_GRP_Q - Add trace values about a group, queue, with info
 * @kbdev:    kbase device
 * @code:     trace code
 * @group:    queue group, or NULL if no queue group
 * @queue:    queue, or NULL if no queue
 * @info_val: generic information about @code to add to the trace
 *
 * Note: Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_BIFROST_DEBUG not defined). Therefore, when
 * KBASE_KTRACE_ENABLE == 0 any functions called to get the parameters supplied
 * to this macro must:
 * a) be static or static inline, and
 * b) just return 0 and have no other statements present in the body.
 */
#define KBASE_KTRACE_ADD_CSF_GRP_Q(kbdev, code, group, queue, info_val) \
	do { \
		/* capture values that could come from non-pure fn calls */ \
		struct kbase_queue_group *__group = group; \
		struct kbase_queue *__queue = queue; \
		u64 __info_val = info_val; \
		KBASE_KTRACE_RBUF_ADD_CSF(kbdev, code, __group, __queue, 0u, \
				__info_val); \
		KBASE_KTRACE_FTRACE_ADD_CSF(kbdev, code, __group, \
				__queue, __info_val); \
	} while (0)


#define KBASE_KTRACE_ADD_CSF_KCPU(kbdev, code, queue, info_val1, info_val2) \
	do { \
		/* capture values that could come from non-pure fn calls */ \
		struct kbase_kcpu_command_queue *__queue = queue; \
		u64 __info_val1 = info_val1; \
		u64 __info_val2 = info_val2; \
		KBASE_KTRACE_RBUF_ADD_CSF_KCPU(kbdev, code, __queue, \
					       __info_val1, __info_val2); \
		KBASE_KTRACE_FTRACE_ADD_KCPU(code, __queue, \
					     __info_val1, __info_val2); \
	} while (0)

#endif /* _KBASE_DEBUG_KTRACE_CSF_H_ */
