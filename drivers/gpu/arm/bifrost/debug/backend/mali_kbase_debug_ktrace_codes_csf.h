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
 * ***** IMPORTANT: THIS IS NOT A NORMAL HEADER FILE         *****
 * *****            DO NOT INCLUDE DIRECTLY                  *****
 * *****            THE LACK OF HEADER GUARDS IS INTENTIONAL *****
 */

/*
 * The purpose of this header file is just to contain a list of trace code
 * identifiers
 *
 * When updating this file, also remember to update
 * mali_kbase_debug_linux_ktrace_csf.h
 *
 * IMPORTANT: THIS FILE MUST NOT BE USED FOR ANY OTHER PURPOSE OTHER THAN THAT
 * DESCRIBED IN mali_kbase_debug_ktrace_codes.h
 */

#if 0 /* Dummy section to avoid breaking formatting */
int dummy_array[] = {
#endif
	/*
	 * Generic CSF events
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(EVICT_CTX_SLOTS),
	/* info_val[0:7]   == fw version_minor
	 * info_val[15:8]  == fw version_major
	 * info_val[63:32] == fw version_hash
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(FIRMWARE_BOOT),
	KBASE_KTRACE_CODE_MAKE_CODE(FIRMWARE_REBOOT),
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_TOCK),
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_TOCK_END),
	/* info_val == total number of runnable groups across all kctxs */
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_TICK),
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_TICK_END),
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_RESET),
	/* info_val = timeout in ms */
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_WAIT_PROTM_QUIT),
	/* info_val = remaining ms timeout, or 0 if timedout */
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_WAIT_PROTM_QUIT_DONE),
	KBASE_KTRACE_CODE_MAKE_CODE(SYNC_UPDATE_EVENT),
	KBASE_KTRACE_CODE_MAKE_CODE(SYNC_UPDATE_EVENT_NOTIFY_GPU),

	/* info_val = JOB_IRQ_STATUS */
	KBASE_KTRACE_CODE_MAKE_CODE(CSF_INTERRUPT),
	/* info_val = JOB_IRQ_STATUS */
	KBASE_KTRACE_CODE_MAKE_CODE(CSF_INTERRUPT_END),
	/* info_val = JOB_IRQ_STATUS */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_INTERRUPT_PROCESS),
	/* info_val = GLB_REQ ^ GLB_ACQ */
	KBASE_KTRACE_CODE_MAKE_CODE(GLB_REQ_ACQ),
	/* info_val[31:0] = num non idle offslot groups
	 * info_val[32] = scheduler can suspend on idle
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_CAN_IDLE),
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_ADVANCE_TICK),
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_NOADVANCE_TICK),
	/* kctx is added to the back of the list */
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_INSERT_RUNNABLE),
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_REMOVE_RUNNABLE),
	/* kctx is moved to the back of the list */
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_ROTATE_RUNNABLE),
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_HEAD_RUNNABLE),

	KBASE_KTRACE_CODE_MAKE_CODE(IDLE_WORKER_BEGIN),
	/* 4-bit encoding of boolean values (ease of reading as hex values)
	 *
	 * info_val[3:0] = was reset active/failed to be prevented
	 * info_val[7:4] = whether scheduler was both idle and suspendable
	 * info_val[11:8] = whether all groups were suspended
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(IDLE_WORKER_END),
	KBASE_KTRACE_CODE_MAKE_CODE(GROUP_SYNC_UPDATE_WORKER_BEGIN),
	KBASE_KTRACE_CODE_MAKE_CODE(GROUP_SYNC_UPDATE_WORKER_END),

	/* info_val = bitmask of slots that gave an ACK for STATUS_UPDATE */
	KBASE_KTRACE_CODE_MAKE_CODE(SLOTS_STATUS_UPDATE_ACK),

	/*
	 * Group events
	 */
	/* info_val[2:0] == CSG_REQ state issued
	 * info_val[19:16] == as_nr
	 * info_val[63:32] == endpoint config (max number of endpoints allowed)
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_SLOT_START),
	/* info_val == CSG_REQ state issued */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_SLOT_STOP),
	/* info_val == CSG_ACK state */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_SLOT_STARTED),
	/* info_val == CSG_ACK state */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_SLOT_STOPPED),
	/* info_val == slot cleaned */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_SLOT_CLEANED),
	/* info_val = slot requesting STATUS_UPDATE */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_SLOT_STATUS_UPDATE),
	/* info_val = scheduler's new csg_slots_idle_mask[0]
	 * group->csg_nr indicates which bit was set
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_SLOT_IDLE_SET),
	/* info_val = scheduler's new csg_slots_idle_mask[0]
	 * group->csg_nr indicates which bit was cleared
	 *
	 * in case of no group, multiple bits may have been updated
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_SLOT_IDLE_CLEAR),
	/* info_val == previous priority */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_PRIO_UPDATE),
	/* info_val == CSG_REQ ^ CSG_ACK */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_SYNC_UPDATE_INTERRUPT),
	/* info_val == CSG_REQ ^ CSG_ACK */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_IDLE_INTERRUPT),
	/* info_val == CSG_REQ ^ CSG_ACK */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_PROGRESS_TIMER_INTERRUPT),
	/* info_val[31:0] == CSG_REQ ^ CSG_ACQ
	 * info_val[63:32] == CSG_IRQ_REQ ^ CSG_IRQ_ACK
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(CSG_INTERRUPT_PROCESS_END),
	KBASE_KTRACE_CODE_MAKE_CODE(GROUP_SYNC_UPDATE_DONE),
	/* info_val == run state of the group */
	KBASE_KTRACE_CODE_MAKE_CODE(GROUP_DESCHEDULE),
	/* info_val == run state of the group */
	KBASE_KTRACE_CODE_MAKE_CODE(GROUP_SCHEDULE),
	/* info_val[31:0] == new run state of the evicted group
	 * info_val[63:32] == number of runnable groups
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(GROUP_EVICT_SCHED),

	/* info_val == new num_runnable_grps
	 * group is added to the back of the list for its priority level
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(GROUP_INSERT_RUNNABLE),
	/* info_val == new num_runnable_grps
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(GROUP_REMOVE_RUNNABLE),
	/* info_val == num_runnable_grps
	 * group is moved to the back of the list for its priority level
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(GROUP_ROTATE_RUNNABLE),
	KBASE_KTRACE_CODE_MAKE_CODE(GROUP_HEAD_RUNNABLE),
	/* info_val == new num_idle_wait_grps
	 * group is added to the back of the list
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(GROUP_INSERT_IDLE_WAIT),
	/* info_val == new num_idle_wait_grps
	 * group is added to the back of the list
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(GROUP_REMOVE_IDLE_WAIT),
	KBASE_KTRACE_CODE_MAKE_CODE(GROUP_HEAD_IDLE_WAIT),

	/* info_val == is scheduler running with protected mode tasks */
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_CHECK_PROTM_ENTER),
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_ENTER_PROTM),
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_EXIT_PROTM),
	/* info_val[31:0] == number of GPU address space slots in use
	 * info_val[63:32] == number of runnable groups
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_TOP_GRP),
	/* info_val == new count of off-slot non-idle groups
	 * no group indicates it was set rather than incremented
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_NONIDLE_OFFSLOT_INC),
	/* info_val == new count of off-slot non-idle groups */
	KBASE_KTRACE_CODE_MAKE_CODE(SCHEDULER_NONIDLE_OFFSLOT_DEC),

	KBASE_KTRACE_CODE_MAKE_CODE(PROTM_EVENT_WORKER_BEGIN),
	KBASE_KTRACE_CODE_MAKE_CODE(PROTM_EVENT_WORKER_END),

	/*
	 * Group + Queue events
	 */
	/* info_val == queue->enabled */
	KBASE_KTRACE_CODE_MAKE_CODE(CSI_START),
	/* info_val == queue->enabled before stop */
	KBASE_KTRACE_CODE_MAKE_CODE(CSI_STOP),
	KBASE_KTRACE_CODE_MAKE_CODE(CSI_STOP_REQUESTED),
	/* info_val == CS_REQ ^ CS_ACK that were not processed due to the group
	 * being suspended
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(CSI_IGNORED_INTERRUPTS_GROUP_SUSPEND),
	/* info_val == CS_REQ ^ CS_ACK */
	KBASE_KTRACE_CODE_MAKE_CODE(CSI_FAULT_INTERRUPT),
	/* info_val == CS_REQ ^ CS_ACK */
	KBASE_KTRACE_CODE_MAKE_CODE(CSI_TILER_OOM_INTERRUPT),
	/* info_val == CS_REQ ^ CS_ACK */
	KBASE_KTRACE_CODE_MAKE_CODE(CSI_PROTM_PEND_INTERRUPT),
	/* info_val == CS_ACK_PROTM_PEND ^ CS_REQ_PROTM_PEND */
	KBASE_KTRACE_CODE_MAKE_CODE(CSI_PROTM_ACK),
	/* info_val == group->run_State (for group the queue is bound to) */
	KBASE_KTRACE_CODE_MAKE_CODE(QUEUE_START),
	KBASE_KTRACE_CODE_MAKE_CODE(QUEUE_STOP),
	/* info_val == contents of CS_STATUS_WAIT_SYNC_POINTER */
	KBASE_KTRACE_CODE_MAKE_CODE(QUEUE_SYNC_UPDATE),
	/* info_val == bool for result of the evaluation */
	KBASE_KTRACE_CODE_MAKE_CODE(QUEUE_SYNC_UPDATE_EVALUATED),
	/* info_val == contents of CS_STATUS_WAIT */
	KBASE_KTRACE_CODE_MAKE_CODE(QUEUE_SYNC_STATUS_WAIT),
	/* info_val == current sync value pointed to by queue->sync_ptr */
	KBASE_KTRACE_CODE_MAKE_CODE(QUEUE_SYNC_CURRENT_VAL),
	/* info_val == current value of CS_STATUS_WAIT_SYNC_VALUE */
	KBASE_KTRACE_CODE_MAKE_CODE(QUEUE_SYNC_TEST_VAL),
	/* info_val == current value of CS_STATUS_BLOCKED_REASON */
	KBASE_KTRACE_CODE_MAKE_CODE(QUEUE_SYNC_BLOCKED_REASON),
	/* info_val = group's new protm_pending_bitmap[0]
	 * queue->csi_index indicates which bit was set
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(PROTM_PENDING_SET),
	/* info_val = group's new protm_pending_bitmap[0]
	 * queue->csi_index indicates which bit was cleared
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(PROTM_PENDING_CLEAR),

	/*
	 * KCPU queue events
	 */
	/* KTrace info_val == KCPU queue fence context
	 * KCPU extra_info_val == N/A.
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(KCPU_QUEUE_NEW),
	/* KTrace info_val == Number of pending commands in KCPU queue when
	 * it is destroyed.
	 * KCPU extra_info_val == Number of CQS wait operations present in
	 * the KCPU queue when it is destroyed.
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(KCPU_QUEUE_DESTROY),
	/* KTrace info_val == CQS event memory address
	 * KCPU extra_info_val == Upper 32 bits of event memory, i.e. contents
	 * of error field.
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(CQS_SET),
	/* KTrace info_val == Number of CQS objects to be waited upon
	 * KCPU extra_info_val == N/A.
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(CQS_WAIT_START),
	/* KTrace info_val == CQS event memory address
	 * KCPU extra_info_val == 1 if CQS was signaled with an error and queue
	 * inherited the error, otherwise 0.
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(CQS_WAIT_END),
	/* KTrace info_val == Fence context
	 * KCPU extra_info_val == Fence seqno.
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(FENCE_SIGNAL),
	/* KTrace info_val == Fence context
	 * KCPU extra_info_val == Fence seqno.
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(FENCE_WAIT_START),
	/* KTrace info_val == Fence context
	 * KCPU extra_info_val == Fence seqno.
	 */
	KBASE_KTRACE_CODE_MAKE_CODE(FENCE_WAIT_END),

#if 0 /* Dummy section to avoid breaking formatting */
};
#endif

/* ***** THE LACK OF HEADER GUARDS IS INTENTIONAL ***** */
