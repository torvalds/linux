/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
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

#if !defined(_KBASE_DEBUG_LINUX_KTRACE_JM_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _KBASE_DEBUG_LINUX_KTRACE_JM_H_

DECLARE_EVENT_CLASS(mali_jm_slot_template,
	TP_PROTO(struct kbase_context *kctx, int jobslot, u64 info_val),
	TP_ARGS(kctx, jobslot, info_val),
	TP_STRUCT__entry(
		__field(pid_t, kctx_tgid)
		__field(u32, kctx_id)
		__field(unsigned int, jobslot)
		__field(u64, info_val)
	),
	TP_fast_assign(
		__entry->kctx_id = (kctx) ? kctx->id : 0u;
		__entry->kctx_tgid = (kctx) ? kctx->tgid : 0;
		__entry->jobslot = jobslot;
		__entry->info_val = info_val;
	),
	TP_printk("kctx=%d_%u jobslot=%u info=0x%llx", __entry->kctx_tgid,
			__entry->kctx_id, __entry->jobslot, __entry->info_val)
);

#define DEFINE_MALI_JM_SLOT_EVENT(name) \
DEFINE_EVENT(mali_jm_slot_template, mali_##name, \
	TP_PROTO(struct kbase_context *kctx, int jobslot, u64 info_val), \
	TP_ARGS(kctx, jobslot, info_val))
DEFINE_MALI_JM_SLOT_EVENT(JM_SUBMIT);
DEFINE_MALI_JM_SLOT_EVENT(JM_JOB_DONE);
DEFINE_MALI_JM_SLOT_EVENT(JM_UPDATE_HEAD);
DEFINE_MALI_JM_SLOT_EVENT(JM_CHECK_HEAD);
DEFINE_MALI_JM_SLOT_EVENT(JM_SOFTSTOP);
DEFINE_MALI_JM_SLOT_EVENT(JM_SOFTSTOP_0);
DEFINE_MALI_JM_SLOT_EVENT(JM_SOFTSTOP_1);
DEFINE_MALI_JM_SLOT_EVENT(JM_HARDSTOP);
DEFINE_MALI_JM_SLOT_EVENT(JM_HARDSTOP_0);
DEFINE_MALI_JM_SLOT_EVENT(JM_HARDSTOP_1);
DEFINE_MALI_JM_SLOT_EVENT(JM_SLOT_SOFT_OR_HARD_STOP);
DEFINE_MALI_JM_SLOT_EVENT(JM_SLOT_EVICT);
DEFINE_MALI_JM_SLOT_EVENT(JM_BEGIN_RESET_WORKER);
DEFINE_MALI_JM_SLOT_EVENT(JM_END_RESET_WORKER);
DEFINE_MALI_JM_SLOT_EVENT(JS_CORE_REF_REGISTER_ON_RECHECK_FAILED);
DEFINE_MALI_JM_SLOT_EVENT(JS_AFFINITY_SUBMIT_TO_BLOCKED);
DEFINE_MALI_JM_SLOT_EVENT(JS_AFFINITY_CURRENT);
DEFINE_MALI_JM_SLOT_EVENT(JD_DONE_TRY_RUN_NEXT_JOB);
DEFINE_MALI_JM_SLOT_EVENT(JS_CORE_REF_REQUEST_CORES_FAILED);
DEFINE_MALI_JM_SLOT_EVENT(JS_CORE_REF_REGISTER_INUSE_FAILED);
DEFINE_MALI_JM_SLOT_EVENT(JS_CORE_REF_REQUEST_ON_RECHECK_FAILED);
DEFINE_MALI_JM_SLOT_EVENT(JS_CORE_REF_AFFINITY_WOULD_VIOLATE);
DEFINE_MALI_JM_SLOT_EVENT(JS_JOB_DONE_TRY_RUN_NEXT_JOB);
DEFINE_MALI_JM_SLOT_EVENT(JS_JOB_DONE_RETRY_NEEDED);
DEFINE_MALI_JM_SLOT_EVENT(JS_POLICY_DEQUEUE_JOB);
DEFINE_MALI_JM_SLOT_EVENT(JS_POLICY_DEQUEUE_JOB_IRQ);
#undef DEFINE_MALI_JM_SLOT_EVENT

DECLARE_EVENT_CLASS(mali_jm_refcount_template,
	TP_PROTO(struct kbase_context *kctx, int refcount, u64 info_val),
	TP_ARGS(kctx, refcount, info_val),
	TP_STRUCT__entry(
		__field(pid_t, kctx_tgid)
		__field(u32, kctx_id)
		__field(unsigned int, refcount)
		__field(u64, info_val)
	),
	TP_fast_assign(
		__entry->kctx_id = (kctx) ? kctx->id : 0u;
		__entry->kctx_tgid = (kctx) ? kctx->tgid : 0;
		__entry->refcount = refcount;
		__entry->info_val = info_val;
	),
	TP_printk("kctx=%d_%u refcount=%u info=0x%llx", __entry->kctx_tgid,
			__entry->kctx_id, __entry->refcount, __entry->info_val)
);

#define DEFINE_MALI_JM_REFCOUNT_EVENT(name) \
DEFINE_EVENT(mali_jm_refcount_template, mali_##name, \
	TP_PROTO(struct kbase_context *kctx, int refcount, u64 info_val), \
	TP_ARGS(kctx, refcount, info_val))
DEFINE_MALI_JM_REFCOUNT_EVENT(JS_ADD_JOB);
DEFINE_MALI_JM_REFCOUNT_EVENT(JS_REMOVE_JOB);
DEFINE_MALI_JM_REFCOUNT_EVENT(JS_TRY_SCHEDULE_HEAD_CTX);
DEFINE_MALI_JM_REFCOUNT_EVENT(JS_POLICY_INIT_CTX);
DEFINE_MALI_JM_REFCOUNT_EVENT(JS_POLICY_TERM_CTX);
DEFINE_MALI_JM_REFCOUNT_EVENT(JS_POLICY_ENQUEUE_CTX);
DEFINE_MALI_JM_REFCOUNT_EVENT(JS_POLICY_DEQUEUE_HEAD_CTX);
DEFINE_MALI_JM_REFCOUNT_EVENT(JS_POLICY_TRY_EVICT_CTX);
DEFINE_MALI_JM_REFCOUNT_EVENT(JS_POLICY_RUNPOOL_ADD_CTX);
DEFINE_MALI_JM_REFCOUNT_EVENT(JS_POLICY_RUNPOOL_REMOVE_CTX);
DEFINE_MALI_JM_REFCOUNT_EVENT(JS_POLICY_FOREACH_CTX_JOBS);
#undef DEFINE_MALI_JM_REFCOUNT_EVENT

DECLARE_EVENT_CLASS(mali_jm_add_template,
	TP_PROTO(struct kbase_context *kctx, u64 gpu_addr, u64 info_val),
	TP_ARGS(kctx, gpu_addr, info_val),
	TP_STRUCT__entry(
		__field(pid_t, kctx_tgid)
		__field(u32, kctx_id)
		__field(u64, gpu_addr)
		__field(u64, info_val)
	),
	TP_fast_assign(
		__entry->kctx_id = (kctx) ? kctx->id : 0u;
		__entry->kctx_tgid = (kctx) ? kctx->tgid : 0;
		__entry->gpu_addr = gpu_addr;
		__entry->info_val = info_val;
	),
	TP_printk("kctx=%d_%u gpu_addr=0x%llx info=0x%llx", __entry->kctx_tgid,
			__entry->kctx_id, __entry->gpu_addr, __entry->info_val)
);

#define DEFINE_MALI_JM_ADD_EVENT(name) \
DEFINE_EVENT(mali_jm_add_template, mali_##name, \
	TP_PROTO(struct kbase_context *kctx, u64 gpu_addr, u64 info_val), \
	TP_ARGS(kctx, gpu_addr, info_val))
DEFINE_MALI_JM_ADD_EVENT(JD_DONE_WORKER);
DEFINE_MALI_JM_ADD_EVENT(JD_DONE_WORKER_END);
DEFINE_MALI_JM_ADD_EVENT(JD_CANCEL_WORKER);
DEFINE_MALI_JM_ADD_EVENT(JD_DONE);
DEFINE_MALI_JM_ADD_EVENT(JD_CANCEL);
DEFINE_MALI_JM_ADD_EVENT(JD_ZAP_CONTEXT);
DEFINE_MALI_JM_ADD_EVENT(JM_IRQ);
DEFINE_MALI_JM_ADD_EVENT(JM_IRQ_END);
DEFINE_MALI_JM_ADD_EVENT(JM_FLUSH_WORKQS);
DEFINE_MALI_JM_ADD_EVENT(JM_FLUSH_WORKQS_DONE);
DEFINE_MALI_JM_ADD_EVENT(JM_ZAP_NON_SCHEDULED);
DEFINE_MALI_JM_ADD_EVENT(JM_ZAP_SCHEDULED);
DEFINE_MALI_JM_ADD_EVENT(JM_ZAP_DONE);
DEFINE_MALI_JM_ADD_EVENT(JM_SUBMIT_AFTER_RESET);
DEFINE_MALI_JM_ADD_EVENT(JM_JOB_COMPLETE);
DEFINE_MALI_JM_ADD_EVENT(JS_CTX_ATTR_NOW_ON_RUNPOOL);
DEFINE_MALI_JM_ADD_EVENT(JS_CTX_ATTR_NOW_OFF_RUNPOOL);
DEFINE_MALI_JM_ADD_EVENT(JS_CTX_ATTR_NOW_ON_CTX);
DEFINE_MALI_JM_ADD_EVENT(JS_CTX_ATTR_NOW_OFF_CTX);
DEFINE_MALI_JM_ADD_EVENT(JS_POLICY_TIMER_END);
DEFINE_MALI_JM_ADD_EVENT(JS_POLICY_TIMER_START);
DEFINE_MALI_JM_ADD_EVENT(JS_POLICY_ENQUEUE_JOB);
#undef DEFINE_MALI_JM_ADD_EVENT

#endif /*  !defined(_KBASE_DEBUG_LINUX_KTRACE_JM_H_)  || defined(TRACE_HEADER_MULTI_READ)*/
