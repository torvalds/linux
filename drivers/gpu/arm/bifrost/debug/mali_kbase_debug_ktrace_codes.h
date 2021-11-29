/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2011-2015, 2018-2021 ARM Limited. All rights reserved.
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
 * ***** IMPORTANT: THIS IS NOT A NORMAL HEADER FILE         *****
 * *****            DO NOT INCLUDE DIRECTLY                  *****
 * *****            THE LACK OF HEADER GUARDS IS INTENTIONAL *****
 */

/*
 * The purpose of this header file is just to contain a list of trace code
 * identifiers
 *
 * When updating this file, also remember to update
 * mali_kbase_debug_linux_ktrace.h
 *
 * Each identifier is wrapped in a macro, so that its string form and enum form
 * can be created
 *
 * Each macro is separated with a comma, to allow insertion into an array
 * initializer or enum definition block.
 *
 * This allows automatic creation of an enum and a corresponding array of
 * strings
 *
 * Before #including, the includer MUST #define KBASE_KTRACE_CODE_MAKE_CODE.
 * After #including, the includer MUST #under KBASE_KTRACE_CODE_MAKE_CODE.
 *
 * e.g.:
 * #define KBASE_KTRACE_CODE( X ) KBASE_KTRACE_CODE_ ## X
 * typedef enum
 * {
 * #define KBASE_KTRACE_CODE_MAKE_CODE( X ) KBASE_KTRACE_CODE( X )
 * #include "mali_kbase_debug_ktrace_codes.h"
 * #undef  KBASE_KTRACE_CODE_MAKE_CODE
 * } kbase_ktrace_code;
 *
 * IMPORTANT: THIS FILE MUST NOT BE USED FOR ANY OTHER PURPOSE OTHER THAN THE ABOVE
 *
 *
 * The use of the macro here is:
 * - KBASE_KTRACE_CODE_MAKE_CODE( X )
 *
 * Which produces:
 * - For an enum, KBASE_KTRACE_CODE_X
 * - For a string, "X"
 *
 *
 * For example:
 * - KBASE_KTRACE_CODE_MAKE_CODE( JM_JOB_COMPLETE ) expands to:
 *  - KBASE_KTRACE_CODE_JM_JOB_COMPLETE for the enum
 *  - "JM_JOB_COMPLETE" for the string
 * - To use it to trace an event, do:
 *  - KBASE_KTRACE_ADD( kbdev, JM_JOB_COMPLETE, subcode, kctx, uatom, val );
 */

#if 0 /* Dummy section to avoid breaking formatting */
int dummy_array[] = {
#endif

	/*
	 * Core events
	 */
	/* no info_val */
	KBASE_KTRACE_CODE_MAKE_CODE(CORE_CTX_DESTROY),
	/* no info_val */
	KBASE_KTRACE_CODE_MAKE_CODE(CORE_CTX_HWINSTR_TERM),
	/* info_val == GPU_IRQ_STATUS register */
	KBASE_KTRACE_CODE_MAKE_CODE(CORE_GPU_IRQ),
	/* info_val == bits cleared */
	KBASE_KTRACE_CODE_MAKE_CODE(CORE_GPU_IRQ_CLEAR),
	/* info_val == GPU_IRQ_STATUS register */
	KBASE_KTRACE_CODE_MAKE_CODE(CORE_GPU_IRQ_DONE),
	KBASE_KTRACE_CODE_MAKE_CODE(CORE_GPU_SOFT_RESET),
	KBASE_KTRACE_CODE_MAKE_CODE(CORE_GPU_HARD_RESET),
	KBASE_KTRACE_CODE_MAKE_CODE(CORE_GPU_PRFCNT_CLEAR),
	/* info_val == dump address */
	KBASE_KTRACE_CODE_MAKE_CODE(CORE_GPU_PRFCNT_SAMPLE),
	KBASE_KTRACE_CODE_MAKE_CODE(CORE_GPU_CLEAN_INV_CACHES),

	/*
	 * Power Management Events
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(PM_JOB_SUBMIT_AFTER_POWERING_UP),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_JOB_SUBMIT_AFTER_POWERED_UP),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_PWRON),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_PWRON_TILER),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_PWRON_L2),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_PWROFF),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_PWROFF_TILER),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_PWROFF_L2),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CORES_POWERED),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CORES_POWERED_TILER),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CORES_POWERED_L2),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CORES_CHANGE_DESIRED),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CORES_CHANGE_DESIRED_TILER),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CORES_CHANGE_AVAILABLE),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CORES_CHANGE_AVAILABLE_TILER),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CORES_CHANGE_AVAILABLE_L2),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CORES_AVAILABLE),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CORES_AVAILABLE_TILER),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_DESIRED_REACHED),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_DESIRED_REACHED_TILER),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_RELEASE_CHANGE_SHADER_NEEDED),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_RELEASE_CHANGE_TILER_NEEDED),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_REQUEST_CHANGE_SHADER_NEEDED),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_REQUEST_CHANGE_TILER_NEEDED),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_WAKE_WAITERS),
	/* info_val == kbdev->pm.active_count*/
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CONTEXT_ACTIVE),
	/* info_val == kbdev->pm.active_count*/
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CONTEXT_IDLE),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_GPU_ON),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_GPU_OFF),
	/* info_val == policy number, or -1 for "Already changing" */
	KBASE_KTRACE_CODE_MAKE_CODE(PM_SET_POLICY),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CA_SET_POLICY),
	/* info_val == policy number */
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CURRENT_POLICY_INIT),
	/* info_val == policy number */
	KBASE_KTRACE_CODE_MAKE_CODE(PM_CURRENT_POLICY_TERM),

	KBASE_KTRACE_CODE_MAKE_CODE(PM_POWEROFF_WAIT_WQ),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_RUNTIME_SUSPEND_CALLBACK),
	KBASE_KTRACE_CODE_MAKE_CODE(PM_RUNTIME_RESUME_CALLBACK),

	/*
	 * Context Scheduler events
	 */
	/* info_val == kctx->refcount */
	KBASE_KTRACE_CODE_MAKE_CODE(SCHED_RETAIN_CTX_NOLOCK),
	/* info_val == kctx->refcount */
	KBASE_KTRACE_CODE_MAKE_CODE(SCHED_RELEASE_CTX),
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	/*
	 * Arbitration events
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(ARB_GPU_LOST),
	KBASE_KTRACE_CODE_MAKE_CODE(ARB_VM_STATE),
	KBASE_KTRACE_CODE_MAKE_CODE(ARB_VM_EVT),
#endif

#if MALI_USE_CSF
#include "debug/backend/mali_kbase_debug_ktrace_codes_csf.h"
#else
#include "debug/backend/mali_kbase_debug_ktrace_codes_jm.h"
#endif
	/*
	 * Unused code just to make it easier to not have a comma at the end.
	 * All other codes MUST come before this
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(DUMMY)

#if 0 /* Dummy section to avoid breaking formatting */
};
#endif

/* ***** THE LACK OF HEADER GUARDS IS INTENTIONAL ***** */
