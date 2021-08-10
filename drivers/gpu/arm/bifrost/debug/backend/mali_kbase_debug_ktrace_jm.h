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

#ifndef _KBASE_DEBUG_KTRACE_JM_H_
#define _KBASE_DEBUG_KTRACE_JM_H_

/*
 * KTrace target for internal ringbuffer
 */
#if KBASE_KTRACE_TARGET_RBUF
/**
 * kbasep_ktrace_add_jm - internal function to add trace about Job Management
 * @kbdev:    kbase device
 * @code:     trace code
 * @kctx:     kbase context, or NULL if no context
 * @katom:    kbase atom, or NULL if no atom
 * @gpu_addr: GPU address, usually related to @katom
 * @flags:    flags about the message
 * @refcount: reference count information to add to the trace
 * @jobslot:  jobslot information to add to the trace
 * @info_val: generic information about @code to add to the trace
 *
 * PRIVATE: do not use directly. Use KBASE_KTRACE_ADD_JM() instead.
 */
void kbasep_ktrace_add_jm(struct kbase_device *kbdev,
		enum kbase_ktrace_code code, struct kbase_context *kctx,
		struct kbase_jd_atom *katom, u64 gpu_addr,
		kbase_ktrace_flag_t flags, int refcount, int jobslot,
		u64 info_val);

#define KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, gpu_addr, flags, \
		refcount, jobslot, info_val) \
	kbasep_ktrace_add_jm(kbdev, KBASE_KTRACE_CODE(code), kctx, katom, \
			gpu_addr, flags, refcount, jobslot, info_val)

#else /* KBASE_KTRACE_TARGET_RBUF */

#define KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, gpu_addr, flags, \
		refcount, jobslot, info_val) \
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(kctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(flags);\
		CSTD_UNUSED(refcount);\
		CSTD_UNUSED(jobslot);\
		CSTD_UNUSED(info_val);\
		CSTD_NOP(0);\
	} while (0)
#endif /* KBASE_KTRACE_TARGET_RBUF */

/*
 * KTrace target for Linux's ftrace
 *
 * Note: the header file(s) that define the trace_mali_<...> tracepoints are
 * included by the parent header file
 */
#if KBASE_KTRACE_TARGET_FTRACE
#define KBASE_KTRACE_FTRACE_ADD_JM_SLOT(kbdev, code, kctx, katom, gpu_addr, \
		jobslot) \
	trace_mali_##code(kctx, jobslot, 0)

#define KBASE_KTRACE_FTRACE_ADD_JM_SLOT_INFO(kbdev, code, kctx, katom, \
		gpu_addr, jobslot, info_val) \
	trace_mali_##code(kctx, jobslot, info_val)

#define KBASE_KTRACE_FTRACE_ADD_JM_REFCOUNT(kbdev, code, kctx, katom, \
		gpu_addr, refcount) \
	trace_mali_##code(kctx, refcount, 0)

#define KBASE_KTRACE_FTRACE_ADD_JM_REFCOUNT_INFO(kbdev, code, kctx, katom, \
		gpu_addr, refcount, info_val) \
	trace_mali_##code(kctx, refcount, info_val)

#define KBASE_KTRACE_FTRACE_ADD_JM(kbdev, code, kctx, katom, gpu_addr, \
		info_val) \
	trace_mali_##code(kctx, gpu_addr, info_val)
#else /* KBASE_KTRACE_TARGET_FTRACE */
#define KBASE_KTRACE_FTRACE_ADD_JM_SLOT(kbdev, code, kctx, katom, gpu_addr, \
		jobslot) \
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(kctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(jobslot);\
		CSTD_NOP(0);\
	} while (0)

#define KBASE_KTRACE_FTRACE_ADD_JM_SLOT_INFO(kbdev, code, kctx, katom, \
		gpu_addr, jobslot, info_val) \
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(kctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(jobslot);\
		CSTD_UNUSED(info_val);\
		CSTD_NOP(0);\
	} while (0)

#define KBASE_KTRACE_FTRACE_ADD_JM_REFCOUNT(kbdev, code, kctx, katom, \
		gpu_addr, refcount) \
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(kctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(refcount);\
		CSTD_NOP(0);\
	} while (0)

#define KBASE_KTRACE_FTRACE_ADD_JM_REFCOUNT_INFO(kbdev, code, kctx, katom, \
		gpu_addr, refcount, info_val) \
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(kctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(info_val);\
		CSTD_NOP(0);\
	} while (0)

#define KBASE_KTRACE_FTRACE_ADD_JM(kbdev, code, kctx, katom, gpu_addr, \
		info_val)\
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(kctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(info_val);\
		CSTD_NOP(0);\
	} while (0)
#endif /* KBASE_KTRACE_TARGET_FTRACE */

/*
 * Master set of macros to route KTrace to any of the targets
 */

/**
 * KBASE_KTRACE_ADD_JM_SLOT - Add trace values about a job-slot
 * @kbdev:    kbase device
 * @code:     trace code
 * @kctx:     kbase context, or NULL if no context
 * @katom:    kbase atom, or NULL if no atom
 * @gpu_addr: GPU address, usually related to @katom
 * @jobslot:  jobslot information to add to the trace
 *
 * Note: Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_BIFROST_DEBUG not defined). Therefore, when
 * KBASE_KTRACE_ENABLE == 0 any functions called to get the parameters supplied
 * to this macro must:
 * a) be static or static inline, and
 * b) just return 0 and have no other statements present in the body.
 */
#define KBASE_KTRACE_ADD_JM_SLOT(kbdev, code, kctx, katom, gpu_addr, \
		jobslot) \
	do { \
		/* capture values that could come from non-pure function calls */ \
		u64 __gpu_addr = gpu_addr; \
		int __jobslot = jobslot; \
		KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, __gpu_addr, \
				KBASE_KTRACE_FLAG_JM_JOBSLOT, 0, __jobslot, \
				0); \
		KBASE_KTRACE_FTRACE_ADD_JM_SLOT(kbdev, code, kctx, katom, __gpu_addr, __jobslot); \
	} while (0)

/**
 * KBASE_KTRACE_ADD_JM_SLOT_INFO - Add trace values about a job-slot, with info
 * @kbdev:    kbase device
 * @code:     trace code
 * @kctx:     kbase context, or NULL if no context
 * @katom:    kbase atom, or NULL if no atom
 * @gpu_addr: GPU address, usually related to @katom
 * @jobslot:  jobslot information to add to the trace
 * @info_val: generic information about @code to add to the trace
 *
 * Note: Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_BIFROST_DEBUG not defined). Therefore, when
 * KBASE_KTRACE_ENABLE == 0 any functions called to get the parameters supplied
 * to this macro must:
 * a) be static or static inline, and
 * b) just return 0 and have no other statements present in the body.
 */
#define KBASE_KTRACE_ADD_JM_SLOT_INFO(kbdev, code, kctx, katom, gpu_addr, \
		jobslot, info_val) \
	do { \
		/* capture values that could come from non-pure function calls */ \
		u64 __gpu_addr = gpu_addr; \
		int __jobslot = jobslot; \
		u64 __info_val = info_val; \
		KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, __gpu_addr, \
				KBASE_KTRACE_FLAG_JM_JOBSLOT, 0, __jobslot, \
				__info_val); \
		KBASE_KTRACE_FTRACE_ADD_JM_SLOT_INFO(kbdev, code, kctx, katom, __gpu_addr, __jobslot, __info_val); \
	} while (0)

/**
 * KBASE_KTRACE_ADD_JM_REFCOUNT - Add trace values about a kctx refcount
 * @kbdev:    kbase device
 * @code:     trace code
 * @kctx:     kbase context, or NULL if no context
 * @katom:    kbase atom, or NULL if no atom
 * @gpu_addr: GPU address, usually related to @katom
 * @refcount: reference count information to add to the trace
 *
 * Note: Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_BIFROST_DEBUG not defined). Therefore, when
 * KBASE_KTRACE_ENABLE == 0 any functions called to get the parameters supplied
 * to this macro must:
 * a) be static or static inline, and
 * b) just return 0 and have no other statements present in the body.
 */
#define KBASE_KTRACE_ADD_JM_REFCOUNT(kbdev, code, kctx, katom, gpu_addr, \
		refcount) \
	do { \
		/* capture values that could come from non-pure function calls */ \
		u64 __gpu_addr = gpu_addr; \
		int __refcount = refcount; \
		KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, __gpu_addr, \
				KBASE_KTRACE_FLAG_JM_REFCOUNT, __refcount, 0, \
				0u); \
		KBASE_KTRACE_FTRACE_ADD_JM_REFCOUNT(kbdev, code, kctx, katom, __gpu_addr, __refcount); \
	} while (0)

/**
 * KBASE_KTRACE_ADD_JM_REFCOUNT_INFO - Add trace values about a kctx refcount,
 *                                     and info
 * @kbdev:    kbase device
 * @code:     trace code
 * @kctx:     kbase context, or NULL if no context
 * @katom:    kbase atom, or NULL if no atom
 * @gpu_addr: GPU address, usually related to @katom
 * @refcount: reference count information to add to the trace
 * @info_val: generic information about @code to add to the trace
 *
 * Note: Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_BIFROST_DEBUG not defined). Therefore, when
 * KBASE_KTRACE_ENABLE == 0 any functions called to get the parameters supplied
 * to this macro must:
 * a) be static or static inline, and
 * b) just return 0 and have no other statements present in the body.
 */
#define KBASE_KTRACE_ADD_JM_REFCOUNT_INFO(kbdev, code, kctx, katom, \
		gpu_addr, refcount, info_val) \
	do { \
		/* capture values that could come from non-pure function calls */ \
		u64 __gpu_addr = gpu_addr; \
		int __refcount = refcount; \
		u64 __info_val = info_val; \
		KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, __gpu_addr, \
				KBASE_KTRACE_FLAG_JM_REFCOUNT, __refcount, 0, \
				__info_val); \
		KBASE_KTRACE_FTRACE_ADD_JM_REFCOUNT(kbdev, code, kctx, katom, __gpu_addr, __refcount, __info_val); \
	} while (0)

/**
 * KBASE_KTRACE_ADD_JM - Add trace values (no slot or refcount)
 * @kbdev:    kbase device
 * @code:     trace code
 * @kctx:     kbase context, or NULL if no context
 * @katom:    kbase atom, or NULL if no atom
 * @gpu_addr: GPU address, usually related to @katom
 * @info_val: generic information about @code to add to the trace
 *
 * Note: Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_BIFROST_DEBUG not defined). Therefore, when
 * KBASE_KTRACE_ENABLE == 0 any functions called to get the parameters supplied
 * to this macro must:
 * a) be static or static inline, and
 * b) just return 0 and have no other statements present in the body.
 */
#define KBASE_KTRACE_ADD_JM(kbdev, code, kctx, katom, gpu_addr, info_val) \
	do { \
		/* capture values that could come from non-pure function calls */ \
		u64 __gpu_addr = gpu_addr; \
		u64 __info_val = info_val; \
		KBASE_KTRACE_RBUF_ADD_JM(kbdev, code, kctx, katom, __gpu_addr, \
				0u, 0, 0, __info_val); \
		KBASE_KTRACE_FTRACE_ADD_JM(kbdev, code, kctx, katom, __gpu_addr, __info_val); \
	} while (0)

#endif /* _KBASE_DEBUG_KTRACE_JM_H_ */
