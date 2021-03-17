/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * (C) COPYRIGHT 2014, 2018, 2020-2021 ARM Limited. All rights reserved.
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
 * NOTE: This must **only** be included through mali_linux_trace.h,
 * otherwise it will fail to setup tracepoints correctly
 */

#if !defined(_KBASE_DEBUG_LINUX_KTRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _KBASE_DEBUG_LINUX_KTRACE_H_

#if KBASE_KTRACE_TARGET_FTRACE

DECLARE_EVENT_CLASS(mali_add_template,
	TP_PROTO(struct kbase_context *kctx, u64 info_val),
	TP_ARGS(kctx, info_val),
	TP_STRUCT__entry(
		__field(pid_t, kctx_tgid)
		__field(u32, kctx_id)
		__field(u64, info_val)
	),
	TP_fast_assign(
		__entry->kctx_id = (kctx) ? kctx->id : 0u;
		__entry->kctx_tgid = (kctx) ? kctx->tgid : 0;
		__entry->info_val = info_val;
	),
	TP_printk("kctx=%d_%u info=0x%llx", __entry->kctx_tgid,
			__entry->kctx_id, __entry->info_val)
);

/* DEFINE_MALI_ADD_EVENT is available also to backends for backend-specific
 * simple trace codes
 */
#define DEFINE_MALI_ADD_EVENT(name) \
DEFINE_EVENT(mali_add_template, mali_##name, \
	TP_PROTO(struct kbase_context *kctx, u64 info_val), \
	TP_ARGS(kctx, info_val))
DEFINE_MALI_ADD_EVENT(CORE_CTX_DESTROY);
DEFINE_MALI_ADD_EVENT(CORE_CTX_HWINSTR_TERM);
DEFINE_MALI_ADD_EVENT(CORE_GPU_IRQ);
DEFINE_MALI_ADD_EVENT(CORE_GPU_IRQ_CLEAR);
DEFINE_MALI_ADD_EVENT(CORE_GPU_IRQ_DONE);
DEFINE_MALI_ADD_EVENT(CORE_GPU_SOFT_RESET);
DEFINE_MALI_ADD_EVENT(CORE_GPU_HARD_RESET);
DEFINE_MALI_ADD_EVENT(CORE_GPU_PRFCNT_SAMPLE);
DEFINE_MALI_ADD_EVENT(CORE_GPU_PRFCNT_CLEAR);
DEFINE_MALI_ADD_EVENT(CORE_GPU_CLEAN_INV_CACHES);
DEFINE_MALI_ADD_EVENT(PM_CORES_CHANGE_DESIRED);
DEFINE_MALI_ADD_EVENT(PM_JOB_SUBMIT_AFTER_POWERING_UP);
DEFINE_MALI_ADD_EVENT(PM_JOB_SUBMIT_AFTER_POWERED_UP);
DEFINE_MALI_ADD_EVENT(PM_PWRON);
DEFINE_MALI_ADD_EVENT(PM_PWRON_TILER);
DEFINE_MALI_ADD_EVENT(PM_PWRON_L2);
DEFINE_MALI_ADD_EVENT(PM_PWROFF);
DEFINE_MALI_ADD_EVENT(PM_PWROFF_TILER);
DEFINE_MALI_ADD_EVENT(PM_PWROFF_L2);
DEFINE_MALI_ADD_EVENT(PM_CORES_POWERED);
DEFINE_MALI_ADD_EVENT(PM_CORES_POWERED_TILER);
DEFINE_MALI_ADD_EVENT(PM_CORES_POWERED_L2);
DEFINE_MALI_ADD_EVENT(PM_DESIRED_REACHED);
DEFINE_MALI_ADD_EVENT(PM_DESIRED_REACHED_TILER);
DEFINE_MALI_ADD_EVENT(PM_REQUEST_CHANGE_SHADER_NEEDED);
DEFINE_MALI_ADD_EVENT(PM_REQUEST_CHANGE_TILER_NEEDED);
DEFINE_MALI_ADD_EVENT(PM_RELEASE_CHANGE_SHADER_NEEDED);
DEFINE_MALI_ADD_EVENT(PM_RELEASE_CHANGE_TILER_NEEDED);
DEFINE_MALI_ADD_EVENT(PM_CORES_AVAILABLE);
DEFINE_MALI_ADD_EVENT(PM_CORES_AVAILABLE_TILER);
DEFINE_MALI_ADD_EVENT(PM_CORES_CHANGE_AVAILABLE);
DEFINE_MALI_ADD_EVENT(PM_CORES_CHANGE_AVAILABLE_TILER);
DEFINE_MALI_ADD_EVENT(PM_CORES_CHANGE_AVAILABLE_L2);
DEFINE_MALI_ADD_EVENT(PM_GPU_ON);
DEFINE_MALI_ADD_EVENT(PM_GPU_OFF);
DEFINE_MALI_ADD_EVENT(PM_SET_POLICY);
DEFINE_MALI_ADD_EVENT(PM_CURRENT_POLICY_INIT);
DEFINE_MALI_ADD_EVENT(PM_CURRENT_POLICY_TERM);
DEFINE_MALI_ADD_EVENT(PM_CA_SET_POLICY);
DEFINE_MALI_ADD_EVENT(PM_CONTEXT_ACTIVE);
DEFINE_MALI_ADD_EVENT(PM_CONTEXT_IDLE);
DEFINE_MALI_ADD_EVENT(PM_WAKE_WAITERS);
DEFINE_MALI_ADD_EVENT(SCHED_RETAIN_CTX_NOLOCK);
DEFINE_MALI_ADD_EVENT(SCHED_RELEASE_CTX);
#ifdef CONFIG_MALI_ARBITER_SUPPORT

DEFINE_MALI_ADD_EVENT(ARB_GPU_LOST);
DEFINE_MALI_ADD_EVENT(ARB_VM_STATE);
DEFINE_MALI_ADD_EVENT(ARB_VM_EVT);

#endif
#if MALI_USE_CSF
#include "backend/mali_kbase_debug_linux_ktrace_csf.h"
#else
#include "backend/mali_kbase_debug_linux_ktrace_jm.h"
#endif

#undef DEFINE_MALI_ADD_EVENT

#endif /* KBASE_KTRACE_TARGET_FTRACE */

#endif /* !defined(_KBASE_DEBUG_LINUX_KTRACE_H_)  || defined(TRACE_HEADER_MULTI_READ) */
