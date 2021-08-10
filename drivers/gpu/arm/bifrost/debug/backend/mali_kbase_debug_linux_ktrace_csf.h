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

/*
 * NOTE: This must **only** be included through mali_linux_trace.h,
 * otherwise it will fail to setup tracepoints correctly
 */

#if !defined(_KBASE_DEBUG_LINUX_KTRACE_CSF_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _KBASE_DEBUG_LINUX_KTRACE_CSF_H_

/*
 * Generic CSF events - using the common DEFINE_MALI_ADD_EVENT
 */
DEFINE_MALI_ADD_EVENT(EVICT_CTX_SLOTS);
DEFINE_MALI_ADD_EVENT(FIRMWARE_BOOT);
DEFINE_MALI_ADD_EVENT(FIRMWARE_REBOOT);
DEFINE_MALI_ADD_EVENT(SCHEDULER_TOCK);
DEFINE_MALI_ADD_EVENT(SCHEDULER_TOCK_END);
DEFINE_MALI_ADD_EVENT(SCHEDULER_TICK);
DEFINE_MALI_ADD_EVENT(SCHEDULER_TICK_END);
DEFINE_MALI_ADD_EVENT(SCHEDULER_RESET);
DEFINE_MALI_ADD_EVENT(SCHEDULER_WAIT_PROTM_QUIT);
DEFINE_MALI_ADD_EVENT(SCHEDULER_WAIT_PROTM_QUIT_DONE);
DEFINE_MALI_ADD_EVENT(SYNC_UPDATE_EVENT);
DEFINE_MALI_ADD_EVENT(SYNC_UPDATE_EVENT_NOTIFY_GPU);
DEFINE_MALI_ADD_EVENT(CSF_INTERRUPT);
DEFINE_MALI_ADD_EVENT(CSF_INTERRUPT_END);
DEFINE_MALI_ADD_EVENT(CSG_INTERRUPT_PROCESS);
DEFINE_MALI_ADD_EVENT(GLB_REQ_ACQ);
DEFINE_MALI_ADD_EVENT(SCHEDULER_CAN_IDLE);
DEFINE_MALI_ADD_EVENT(SCHEDULER_ADVANCE_TICK);
DEFINE_MALI_ADD_EVENT(SCHEDULER_NOADVANCE_TICK);
DEFINE_MALI_ADD_EVENT(SCHEDULER_INSERT_RUNNABLE);
DEFINE_MALI_ADD_EVENT(SCHEDULER_REMOVE_RUNNABLE);
DEFINE_MALI_ADD_EVENT(SCHEDULER_ROTATE_RUNNABLE);
DEFINE_MALI_ADD_EVENT(SCHEDULER_HEAD_RUNNABLE);
DEFINE_MALI_ADD_EVENT(IDLE_WORKER_BEGIN);
DEFINE_MALI_ADD_EVENT(IDLE_WORKER_END);
DEFINE_MALI_ADD_EVENT(GROUP_SYNC_UPDATE_WORKER_BEGIN);
DEFINE_MALI_ADD_EVENT(GROUP_SYNC_UPDATE_WORKER_END);
DEFINE_MALI_ADD_EVENT(SLOTS_STATUS_UPDATE_ACK);

DECLARE_EVENT_CLASS(mali_csf_grp_q_template,
	TP_PROTO(struct kbase_device *kbdev, struct kbase_queue_group *group,
			struct kbase_queue *queue, u64 info_val),
	TP_ARGS(kbdev, group, queue, info_val),
	TP_STRUCT__entry(
		__field(u64, info_val)
		__field(pid_t, kctx_tgid)
		__field(u32, kctx_id)
		__field(u8, group_handle)
		__field(s8, csg_nr)
		__field(u8, slot_prio)
		__field(s8, csi_index)
	),
	TP_fast_assign(
		{
			struct kbase_context *kctx = NULL;

			__entry->info_val = info_val;
			/* Note: if required in future, we could record some
			 * flags in __entry about whether the group/queue parts
			 * are valid, and add that to the trace message e.g.
			 * by using __print_flags()/__print_symbolic()
			 */
			if (queue) {
				/* Note: kctx overridden by group->kctx later if group is valid */
				kctx = queue->kctx;
				__entry->csi_index = queue->csi_index;
			} else {
				__entry->csi_index = -1;
			}

			if (group) {
				kctx = group->kctx;
				__entry->group_handle = group->handle;
				__entry->csg_nr = group->csg_nr;
				if (group->csg_nr >= 0)
					__entry->slot_prio = kbdev->csf.scheduler.csg_slots[group->csg_nr].priority;
				else
					__entry->slot_prio = 0u;
			} else {
				__entry->group_handle = 0u;
				__entry->csg_nr = -1;
				__entry->slot_prio = 0u;
			}
			__entry->kctx_id = (kctx) ? kctx->id : 0u;
			__entry->kctx_tgid = (kctx) ? kctx->tgid : 0;
		}

	),
	TP_printk("kctx=%d_%u group=%u slot=%d prio=%u csi=%d info=0x%llx",
			__entry->kctx_tgid, __entry->kctx_id,
			__entry->group_handle, __entry->csg_nr,
			__entry->slot_prio, __entry->csi_index,
			__entry->info_val)
);

/*
 * Group events
 */
#define DEFINE_MALI_CSF_GRP_EVENT(name) \
	DEFINE_EVENT_PRINT(mali_csf_grp_q_template, mali_##name, \
	TP_PROTO(struct kbase_device *kbdev, struct kbase_queue_group *group, \
			struct kbase_queue *queue, u64 info_val), \
	TP_ARGS(kbdev, group, queue, info_val), \
	TP_printk("kctx=%d_%u group=%u slot=%d prio=%u info=0x%llx", \
		__entry->kctx_tgid, __entry->kctx_id, __entry->group_handle, \
		__entry->csg_nr, __entry->slot_prio, __entry->info_val))

DEFINE_MALI_CSF_GRP_EVENT(CSG_SLOT_START);
DEFINE_MALI_CSF_GRP_EVENT(CSG_SLOT_STOP);
DEFINE_MALI_CSF_GRP_EVENT(CSG_SLOT_STARTED);
DEFINE_MALI_CSF_GRP_EVENT(CSG_SLOT_STOPPED);
DEFINE_MALI_CSF_GRP_EVENT(CSG_SLOT_CLEANED);
DEFINE_MALI_CSF_GRP_EVENT(CSG_SLOT_STATUS_UPDATE);
DEFINE_MALI_CSF_GRP_EVENT(CSG_SLOT_IDLE_SET);
DEFINE_MALI_CSF_GRP_EVENT(CSG_SLOT_IDLE_CLEAR);
DEFINE_MALI_CSF_GRP_EVENT(CSG_PRIO_UPDATE);
DEFINE_MALI_CSF_GRP_EVENT(CSG_SYNC_UPDATE_INTERRUPT);
DEFINE_MALI_CSF_GRP_EVENT(CSG_IDLE_INTERRUPT);
DEFINE_MALI_CSF_GRP_EVENT(CSG_PROGRESS_TIMER_INTERRUPT);
DEFINE_MALI_CSF_GRP_EVENT(CSG_INTERRUPT_PROCESS_END);
DEFINE_MALI_CSF_GRP_EVENT(GROUP_SYNC_UPDATE_DONE);
DEFINE_MALI_CSF_GRP_EVENT(GROUP_DESCHEDULE);
DEFINE_MALI_CSF_GRP_EVENT(GROUP_SCHEDULE);
DEFINE_MALI_CSF_GRP_EVENT(GROUP_EVICT_SCHED);
DEFINE_MALI_CSF_GRP_EVENT(GROUP_INSERT_RUNNABLE);
DEFINE_MALI_CSF_GRP_EVENT(GROUP_REMOVE_RUNNABLE);
DEFINE_MALI_CSF_GRP_EVENT(GROUP_ROTATE_RUNNABLE);
DEFINE_MALI_CSF_GRP_EVENT(GROUP_HEAD_RUNNABLE);
DEFINE_MALI_CSF_GRP_EVENT(GROUP_INSERT_IDLE_WAIT);
DEFINE_MALI_CSF_GRP_EVENT(GROUP_REMOVE_IDLE_WAIT);
DEFINE_MALI_CSF_GRP_EVENT(GROUP_HEAD_IDLE_WAIT);
DEFINE_MALI_CSF_GRP_EVENT(SCHEDULER_CHECK_PROTM_ENTER);
DEFINE_MALI_CSF_GRP_EVENT(SCHEDULER_ENTER_PROTM);
DEFINE_MALI_CSF_GRP_EVENT(SCHEDULER_EXIT_PROTM);
DEFINE_MALI_CSF_GRP_EVENT(SCHEDULER_TOP_GRP);
DEFINE_MALI_CSF_GRP_EVENT(SCHEDULER_NONIDLE_OFFSLOT_INC);
DEFINE_MALI_CSF_GRP_EVENT(SCHEDULER_NONIDLE_OFFSLOT_DEC);
DEFINE_MALI_CSF_GRP_EVENT(PROTM_EVENT_WORKER_BEGIN);
DEFINE_MALI_CSF_GRP_EVENT(PROTM_EVENT_WORKER_END);

#undef DEFINE_MALI_CSF_GRP_EVENT

/*
 * Group + Queue events
 */
#define DEFINE_MALI_CSF_GRP_Q_EVENT(name)  \
	DEFINE_EVENT(mali_csf_grp_q_template, mali_##name, \
	TP_PROTO(struct kbase_device *kbdev, struct kbase_queue_group *group, \
			struct kbase_queue *queue, u64 info_val), \
	TP_ARGS(kbdev, group, queue, info_val))

DEFINE_MALI_CSF_GRP_Q_EVENT(CSI_START);
DEFINE_MALI_CSF_GRP_Q_EVENT(CSI_STOP);
DEFINE_MALI_CSF_GRP_Q_EVENT(CSI_STOP_REQUESTED);
DEFINE_MALI_CSF_GRP_Q_EVENT(CSI_IGNORED_INTERRUPTS_GROUP_SUSPEND);
DEFINE_MALI_CSF_GRP_Q_EVENT(CSI_FAULT_INTERRUPT);
DEFINE_MALI_CSF_GRP_Q_EVENT(CSI_TILER_OOM_INTERRUPT);
DEFINE_MALI_CSF_GRP_Q_EVENT(CSI_PROTM_PEND_INTERRUPT);
DEFINE_MALI_CSF_GRP_Q_EVENT(CSI_PROTM_ACK);
DEFINE_MALI_CSF_GRP_Q_EVENT(QUEUE_START);
DEFINE_MALI_CSF_GRP_Q_EVENT(QUEUE_STOP);
DEFINE_MALI_CSF_GRP_Q_EVENT(QUEUE_SYNC_UPDATE);
DEFINE_MALI_CSF_GRP_Q_EVENT(QUEUE_SYNC_UPDATE_EVALUATED);
DEFINE_MALI_CSF_GRP_Q_EVENT(QUEUE_SYNC_STATUS_WAIT);
DEFINE_MALI_CSF_GRP_Q_EVENT(QUEUE_SYNC_CURRENT_VAL);
DEFINE_MALI_CSF_GRP_Q_EVENT(QUEUE_SYNC_TEST_VAL);
DEFINE_MALI_CSF_GRP_Q_EVENT(QUEUE_SYNC_BLOCKED_REASON);
DEFINE_MALI_CSF_GRP_Q_EVENT(PROTM_PENDING_SET);
DEFINE_MALI_CSF_GRP_Q_EVENT(PROTM_PENDING_CLEAR);

#undef DEFINE_MALI_CSF_GRP_Q_EVENT

/*
 * KCPU queue events
 */
DECLARE_EVENT_CLASS(mali_csf_kcpu_queue_template,
	TP_PROTO(struct kbase_kcpu_command_queue *queue,
		 u64 info_val1, u64 info_val2),
	TP_ARGS(queue, info_val1, info_val2),
	TP_STRUCT__entry(
		__field(u64, info_val1)
		__field(u64, info_val2)
		__field(pid_t, kctx_tgid)
		__field(u32, kctx_id)
		__field(u8, id)
	),
	TP_fast_assign(
		{
			__entry->info_val1 = info_val1;
			__entry->info_val2 = info_val2;
			__entry->kctx_id = queue->kctx->id;
			__entry->kctx_tgid = queue->kctx->tgid;
			__entry->id = queue->id;
		}

	),
	TP_printk("kctx=%d_%u id=%u info_val1=0x%llx info_val2=0x%llx",
			__entry->kctx_tgid, __entry->kctx_id, __entry->id,
			__entry->info_val1, __entry->info_val2)
);

#define DEFINE_MALI_CSF_KCPU_EVENT(name)  \
	DEFINE_EVENT(mali_csf_kcpu_queue_template, mali_##name, \
	TP_PROTO(struct kbase_kcpu_command_queue *queue, \
		 u64 info_val1, u64 info_val2), \
	TP_ARGS(queue, info_val1, info_val2))

DEFINE_MALI_CSF_KCPU_EVENT(KCPU_QUEUE_NEW);
DEFINE_MALI_CSF_KCPU_EVENT(KCPU_QUEUE_DESTROY);
DEFINE_MALI_CSF_KCPU_EVENT(CQS_SET);
DEFINE_MALI_CSF_KCPU_EVENT(CQS_WAIT_START);
DEFINE_MALI_CSF_KCPU_EVENT(CQS_WAIT_END);
DEFINE_MALI_CSF_KCPU_EVENT(FENCE_SIGNAL);
DEFINE_MALI_CSF_KCPU_EVENT(FENCE_WAIT_START);
DEFINE_MALI_CSF_KCPU_EVENT(FENCE_WAIT_END);

#undef DEFINE_MALI_CSF_KCPU_EVENT

#endif /* !defined(_KBASE_DEBUG_LINUX_KTRACE_CSF_H_) || defined(TRACE_HEADER_MULTI_READ) */
