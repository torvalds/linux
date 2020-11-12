/*
 *
 * (C) COPYRIGHT 2011-2015,2018-2020 ARM Limited. All rights reserved.
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

/*
 * ***** IMPORTANT: THIS IS NOT A NORMAL HEADER FILE         *****
 * *****            DO NOT INCLUDE DIRECTLY                  *****
 * *****            THE LACK OF HEADER GUARDS IS INTENTIONAL *****
 */

/*
 * The purpose of this header file is just to contain a list of trace code
 * identifiers
 *
 * When updating this file, also remember to update
 * mali_kbase_debug_linux_ktrace_jm.h
 *
 * IMPORTANT: THIS FILE MUST NOT BE USED FOR ANY OTHER PURPOSE OTHER THAN THAT
 * DESCRIBED IN mali_kbase_debug_ktrace_codes.h
 */

#if 0 /* Dummy section to avoid breaking formatting */
int dummy_array[] = {
#endif

	/*
	 * Job Slot management events
	 */
	/* info_val==irq rawstat at start */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_IRQ),
	/* info_val==jobs processed */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_IRQ_END),
	/* In the following:
	 *
	 * - ctx is set if a corresponding job found (NULL otherwise, e.g. some
	 *   soft-stop cases)
	 * - uatom==kernel-side mapped uatom address (for correlation with
	 *   user-side)
	 */
	/* info_val==exit code; gpu_addr==chain gpuaddr */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_JOB_DONE),
	/* gpu_addr==JS_HEAD_NEXT written, info_val==lower 32 bits of
	 * affinity
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_SUBMIT),
	/* gpu_addr is as follows:
	 * - If JS_STATUS active after soft-stop, val==gpu addr written to
	 *   JS_HEAD on submit
	 * - otherwise gpu_addr==0
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_SOFTSTOP),
	KBASE_KTRACE_CODE_MAKE_CODE(JM_SOFTSTOP_0),
	KBASE_KTRACE_CODE_MAKE_CODE(JM_SOFTSTOP_1),
	/* gpu_addr==JS_HEAD read */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_HARDSTOP),
	/* gpu_addr==JS_HEAD read */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_HARDSTOP_0),
	/* gpu_addr==JS_HEAD read */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_HARDSTOP_1),
	/* gpu_addr==JS_TAIL read */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_UPDATE_HEAD),
	/* gpu_addr is as follows:
	 * - If JS_STATUS active before soft-stop, val==JS_HEAD
	 * - otherwise gpu_addr==0
	 */
	/* gpu_addr==JS_HEAD read */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_CHECK_HEAD),
	KBASE_KTRACE_CODE_MAKE_CODE(JM_FLUSH_WORKQS),
	KBASE_KTRACE_CODE_MAKE_CODE(JM_FLUSH_WORKQS_DONE),
	/* info_val == is_scheduled */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_ZAP_NON_SCHEDULED),
	/* info_val == is_scheduled */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_ZAP_SCHEDULED),
	KBASE_KTRACE_CODE_MAKE_CODE(JM_ZAP_DONE),
	/* info_val == nr jobs submitted */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_SLOT_SOFT_OR_HARD_STOP),
	/* gpu_addr==JS_HEAD_NEXT last written */
	KBASE_KTRACE_CODE_MAKE_CODE(JM_SLOT_EVICT),
	KBASE_KTRACE_CODE_MAKE_CODE(JM_SUBMIT_AFTER_RESET),
	KBASE_KTRACE_CODE_MAKE_CODE(JM_BEGIN_RESET_WORKER),
	KBASE_KTRACE_CODE_MAKE_CODE(JM_END_RESET_WORKER),
	/*
	 * Job dispatch events
	 */
	/* gpu_addr==value to write into JS_HEAD */
	KBASE_KTRACE_CODE_MAKE_CODE(JD_DONE),
	/* gpu_addr==value to write into JS_HEAD */
	KBASE_KTRACE_CODE_MAKE_CODE(JD_DONE_WORKER),
	/* gpu_addr==value to write into JS_HEAD */
	KBASE_KTRACE_CODE_MAKE_CODE(JD_DONE_WORKER_END),
	/* gpu_addr==value to write into JS_HEAD */
	KBASE_KTRACE_CODE_MAKE_CODE(JD_DONE_TRY_RUN_NEXT_JOB),
	/* gpu_addr==0, info_val==0, uatom==0 */
	KBASE_KTRACE_CODE_MAKE_CODE(JD_ZAP_CONTEXT),
	/* gpu_addr==value to write into JS_HEAD */
	KBASE_KTRACE_CODE_MAKE_CODE(JD_CANCEL),
	/* gpu_addr==value to write into JS_HEAD */
	KBASE_KTRACE_CODE_MAKE_CODE(JD_CANCEL_WORKER),
	/*
	 * Scheduler Core events
	 */
	/* gpu_addr==value to write into JS_HEAD */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_ADD_JOB),
	/* gpu_addr==last value written/would be written to JS_HEAD */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_REMOVE_JOB),
	KBASE_KTRACE_CODE_MAKE_CODE(JS_TRY_SCHEDULE_HEAD_CTX),
	/* gpu_addr==value to write into JS_HEAD */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_JOB_DONE_TRY_RUN_NEXT_JOB),
	/* gpu_addr==value to write into JS_HEAD */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_JOB_DONE_RETRY_NEEDED),
	KBASE_KTRACE_CODE_MAKE_CODE(JS_AFFINITY_SUBMIT_TO_BLOCKED),
	/* info_val == lower 32 bits of affinity */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_AFFINITY_CURRENT),
	/* info_val == lower 32 bits of affinity */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_CORE_REF_REQUEST_CORES_FAILED),
	/* info_val == lower 32 bits of affinity */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_CORE_REF_REGISTER_INUSE_FAILED),
	/* info_val == lower 32 bits of rechecked affinity */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_CORE_REF_REQUEST_ON_RECHECK_FAILED),
	/* info_val == lower 32 bits of rechecked affinity */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_CORE_REF_REGISTER_ON_RECHECK_FAILED),
	/* info_val == lower 32 bits of affinity */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_CORE_REF_AFFINITY_WOULD_VIOLATE),
	/* info_val == the ctx attribute now on ctx */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_CTX_ATTR_NOW_ON_CTX),
	/* info_val == the ctx attribute now on runpool */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_CTX_ATTR_NOW_ON_RUNPOOL),
	/* info_val == the ctx attribute now off ctx */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_CTX_ATTR_NOW_OFF_CTX),
	/* info_val == the ctx attribute now off runpool */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_CTX_ATTR_NOW_OFF_RUNPOOL),
	/*
	 * Scheduler Policy events
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_POLICY_INIT_CTX),
	KBASE_KTRACE_CODE_MAKE_CODE(JS_POLICY_TERM_CTX),
	/* info_val == whether it was evicted */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_POLICY_TRY_EVICT_CTX),
	KBASE_KTRACE_CODE_MAKE_CODE(JS_POLICY_FOREACH_CTX_JOBS),
	KBASE_KTRACE_CODE_MAKE_CODE(JS_POLICY_ENQUEUE_CTX),
	KBASE_KTRACE_CODE_MAKE_CODE(JS_POLICY_DEQUEUE_HEAD_CTX),
	KBASE_KTRACE_CODE_MAKE_CODE(JS_POLICY_RUNPOOL_ADD_CTX),
	KBASE_KTRACE_CODE_MAKE_CODE(JS_POLICY_RUNPOOL_REMOVE_CTX),
	KBASE_KTRACE_CODE_MAKE_CODE(JS_POLICY_DEQUEUE_JOB),
	KBASE_KTRACE_CODE_MAKE_CODE(JS_POLICY_DEQUEUE_JOB_IRQ),
	/* gpu_addr==JS_HEAD to write if the job were run */
	KBASE_KTRACE_CODE_MAKE_CODE(JS_POLICY_ENQUEUE_JOB),
	KBASE_KTRACE_CODE_MAKE_CODE(JS_POLICY_TIMER_START),
	KBASE_KTRACE_CODE_MAKE_CODE(JS_POLICY_TIMER_END),

#if 0 /* Dummy section to avoid breaking formatting */
};
#endif

/* ***** THE LACK OF HEADER GUARDS IS INTENTIONAL ***** */
