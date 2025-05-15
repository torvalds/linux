/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2021 Intel Corporation
 */

#ifndef _ABI_GUC_ACTIONS_ABI_H
#define _ABI_GUC_ACTIONS_ABI_H

/**
 * DOC: HOST2GUC_SELF_CFG
 *
 * This message is used by Host KMD to setup of the `GuC Self Config KLVs`_.
 *
 * This message must be sent as `MMIO HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = MBZ                                                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_HOST2GUC_SELF_CFG` = 0x0508            |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:16 | **KLV_KEY** - KLV key, see `GuC Self Config KLVs`_           |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **KLV_LEN** - KLV length                                     |
 *  |   |       |                                                              |
 *  |   |       |   - 32 bit KLV = 1                                           |
 *  |   |       |   - 64 bit KLV = 2                                           |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **VALUE32** - Bits 31-0 of the KLV value                     |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **VALUE64** - Bits 63-32 of the KLV value (**KLV_LEN** = 2)  |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = **NUM** - 1 if KLV was parsed, 0 if not recognized   |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_HOST2GUC_SELF_CFG			0x0508

#define HOST2GUC_SELF_CFG_REQUEST_MSG_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 3u)
#define HOST2GUC_SELF_CFG_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define HOST2GUC_SELF_CFG_REQUEST_MSG_1_KLV_KEY		(0xffffu << 16)
#define HOST2GUC_SELF_CFG_REQUEST_MSG_1_KLV_LEN		(0xffffu << 0)
#define HOST2GUC_SELF_CFG_REQUEST_MSG_2_VALUE32		GUC_HXG_REQUEST_MSG_n_DATAn
#define HOST2GUC_SELF_CFG_REQUEST_MSG_3_VALUE64		GUC_HXG_REQUEST_MSG_n_DATAn

#define HOST2GUC_SELF_CFG_RESPONSE_MSG_LEN		GUC_HXG_RESPONSE_MSG_MIN_LEN
#define HOST2GUC_SELF_CFG_RESPONSE_MSG_0_NUM		GUC_HXG_RESPONSE_MSG_0_DATA0

/**
 * DOC: HOST2GUC_CONTROL_CTB
 *
 * This H2G action allows Vf Host to enable or disable H2G and G2H `CT Buffer`_.
 *
 * This message must be sent as `MMIO HXG Message`_.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_HOST_                                |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 = MBZ                                                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION = _`GUC_ACTION_HOST2GUC_CONTROL_CTB` = 0x4509         |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **CONTROL** - control `CTB based communication`_             |
 *  |   |       |                                                              |
 *  |   |       |   - _`GUC_CTB_CONTROL_DISABLE` = 0                           |
 *  |   |       |   - _`GUC_CTB_CONTROL_ENABLE` = 1                            |
 *  +---+-------+--------------------------------------------------------------+
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN = GUC_HXG_ORIGIN_GUC_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | DATA0 = MBZ                                                  |
 *  +---+-------+--------------------------------------------------------------+
 */
#define GUC_ACTION_HOST2GUC_CONTROL_CTB			0x4509

#define HOST2GUC_CONTROL_CTB_REQUEST_MSG_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 1u)
#define HOST2GUC_CONTROL_CTB_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define HOST2GUC_CONTROL_CTB_REQUEST_MSG_1_CONTROL	GUC_HXG_REQUEST_MSG_n_DATAn
#define   GUC_CTB_CONTROL_DISABLE			0u
#define   GUC_CTB_CONTROL_ENABLE			1u

#define HOST2GUC_CONTROL_CTB_RESPONSE_MSG_LEN		GUC_HXG_RESPONSE_MSG_MIN_LEN
#define HOST2GUC_CONTROL_CTB_RESPONSE_MSG_0_MBZ		GUC_HXG_RESPONSE_MSG_0_DATA0

/* legacy definitions */

enum xe_guc_action {
	XE_GUC_ACTION_DEFAULT = 0x0,
	XE_GUC_ACTION_REQUEST_PREEMPTION = 0x2,
	XE_GUC_ACTION_REQUEST_ENGINE_RESET = 0x3,
	XE_GUC_ACTION_ALLOCATE_DOORBELL = 0x10,
	XE_GUC_ACTION_DEALLOCATE_DOORBELL = 0x20,
	XE_GUC_ACTION_LOG_BUFFER_FILE_FLUSH_COMPLETE = 0x30,
	XE_GUC_ACTION_UK_LOG_ENABLE_LOGGING = 0x40,
	XE_GUC_ACTION_FORCE_LOG_BUFFER_FLUSH = 0x302,
	XE_GUC_ACTION_ENTER_S_STATE = 0x501,
	XE_GUC_ACTION_EXIT_S_STATE = 0x502,
	XE_GUC_ACTION_GLOBAL_SCHED_POLICY_CHANGE = 0x506,
	XE_GUC_ACTION_SCHED_CONTEXT = 0x1000,
	XE_GUC_ACTION_SCHED_CONTEXT_MODE_SET = 0x1001,
	XE_GUC_ACTION_SCHED_CONTEXT_MODE_DONE = 0x1002,
	XE_GUC_ACTION_SCHED_ENGINE_MODE_SET = 0x1003,
	XE_GUC_ACTION_SCHED_ENGINE_MODE_DONE = 0x1004,
	XE_GUC_ACTION_SET_CONTEXT_PRIORITY = 0x1005,
	XE_GUC_ACTION_SET_CONTEXT_EXECUTION_QUANTUM = 0x1006,
	XE_GUC_ACTION_SET_CONTEXT_PREEMPTION_TIMEOUT = 0x1007,
	XE_GUC_ACTION_CONTEXT_RESET_NOTIFICATION = 0x1008,
	XE_GUC_ACTION_ENGINE_FAILURE_NOTIFICATION = 0x1009,
	XE_GUC_ACTION_HOST2GUC_UPDATE_CONTEXT_POLICIES = 0x100B,
	XE_GUC_ACTION_AUTHENTICATE_HUC = 0x4000,
	XE_GUC_ACTION_GET_HWCONFIG = 0x4100,
	XE_GUC_ACTION_REGISTER_CONTEXT = 0x4502,
	XE_GUC_ACTION_DEREGISTER_CONTEXT = 0x4503,
	XE_GUC_ACTION_REGISTER_COMMAND_TRANSPORT_BUFFER = 0x4505,
	XE_GUC_ACTION_DEREGISTER_COMMAND_TRANSPORT_BUFFER = 0x4506,
	XE_GUC_ACTION_REGISTER_G2G = 0x4507,
	XE_GUC_ACTION_DEREGISTER_G2G = 0x4508,
	XE_GUC_ACTION_DEREGISTER_CONTEXT_DONE = 0x4600,
	XE_GUC_ACTION_REGISTER_CONTEXT_MULTI_LRC = 0x4601,
	XE_GUC_ACTION_CLIENT_SOFT_RESET = 0x5507,
	XE_GUC_ACTION_SET_ENG_UTIL_BUFF = 0x550A,
	XE_GUC_ACTION_SET_DEVICE_ENGINE_ACTIVITY_BUFFER = 0x550C,
	XE_GUC_ACTION_NOTIFY_MEMORY_CAT_ERROR = 0x6000,
	XE_GUC_ACTION_REPORT_PAGE_FAULT_REQ_DESC = 0x6002,
	XE_GUC_ACTION_PAGE_FAULT_RES_DESC = 0x6003,
	XE_GUC_ACTION_ACCESS_COUNTER_NOTIFY = 0x6004,
	XE_GUC_ACTION_TLB_INVALIDATION = 0x7000,
	XE_GUC_ACTION_TLB_INVALIDATION_DONE = 0x7001,
	XE_GUC_ACTION_TLB_INVALIDATION_ALL = 0x7002,
	XE_GUC_ACTION_STATE_CAPTURE_NOTIFICATION = 0x8002,
	XE_GUC_ACTION_NOTIFY_FLUSH_LOG_BUFFER_TO_FILE = 0x8003,
	XE_GUC_ACTION_NOTIFY_CRASH_DUMP_POSTED = 0x8004,
	XE_GUC_ACTION_NOTIFY_EXCEPTION = 0x8005,
	XE_GUC_ACTION_LIMIT
};

enum xe_guc_preempt_options {
	XE_GUC_PREEMPT_OPTION_DROP_WORK_Q = 0x4,
	XE_GUC_PREEMPT_OPTION_DROP_SUBMIT_Q = 0x8,
};

enum xe_guc_report_status {
	XE_GUC_REPORT_STATUS_UNKNOWN = 0x0,
	XE_GUC_REPORT_STATUS_ACKED = 0x1,
	XE_GUC_REPORT_STATUS_ERROR = 0x2,
	XE_GUC_REPORT_STATUS_COMPLETE = 0x4,
};

enum xe_guc_sleep_state_status {
	XE_GUC_SLEEP_STATE_SUCCESS = 0x1,
	XE_GUC_SLEEP_STATE_PREEMPT_TO_IDLE_FAILED = 0x2,
	XE_GUC_SLEEP_STATE_ENGINE_RESET_FAILED = 0x3
#define XE_GUC_SLEEP_STATE_INVALID_MASK 0x80000000
};

#define GUC_LOG_CONTROL_LOGGING_ENABLED	(1 << 0)
#define GUC_LOG_CONTROL_VERBOSITY_SHIFT	4
#define GUC_LOG_CONTROL_VERBOSITY_MASK	(0xF << GUC_LOG_CONTROL_VERBOSITY_SHIFT)
#define GUC_LOG_CONTROL_DEFAULT_LOGGING	(1 << 8)

enum xe_guc_state_capture_event_status {
	XE_GUC_STATE_CAPTURE_EVENT_STATUS_SUCCESS = 0x0,
	XE_GUC_STATE_CAPTURE_EVENT_STATUS_NOSPACE = 0x1,
};

#define XE_GUC_STATE_CAPTURE_EVENT_STATUS_MASK      0x000000FF
#define XE_GUC_ACTION_STATE_CAPTURE_NOTIFICATION_DATA_LEN 1

#define XE_GUC_TLB_INVAL_TYPE_SHIFT 0
#define XE_GUC_TLB_INVAL_MODE_SHIFT 8
/* Flush PPC or SMRO caches along with TLB invalidation request */
#define XE_GUC_TLB_INVAL_FLUSH_CACHE (1 << 31)

enum xe_guc_tlb_invalidation_type {
	XE_GUC_TLB_INVAL_FULL = 0x0,
	XE_GUC_TLB_INVAL_PAGE_SELECTIVE = 0x1,
	XE_GUC_TLB_INVAL_PAGE_SELECTIVE_CTX = 0x2,
	XE_GUC_TLB_INVAL_GUC = 0x3,
};

/*
 * 0: Heavy mode of Invalidation:
 * The pipeline of the engine(s) for which the invalidation is targeted to is
 * blocked, and all the in-flight transactions are guaranteed to be Globally
 * Observed before completing the TLB invalidation
 * 1: Lite mode of Invalidation:
 * TLBs of the targeted engine(s) are immediately invalidated.
 * In-flight transactions are NOT guaranteed to be Globally Observed before
 * completing TLB invalidation.
 * Light Invalidation Mode is to be used only when
 * it can be guaranteed (by SW) that the address translations remain invariant
 * for the in-flight transactions across the TLB invalidation. In other words,
 * this mode can be used when the TLB invalidation is intended to clear out the
 * stale cached translations that are no longer in use. Light Invalidation Mode
 * is much faster than the Heavy Invalidation Mode, as it does not wait for the
 * in-flight transactions to be GOd.
 */
enum xe_guc_tlb_inval_mode {
	XE_GUC_TLB_INVAL_MODE_HEAVY = 0x0,
	XE_GUC_TLB_INVAL_MODE_LITE = 0x1,
};

/*
 * GuC to GuC communication (de-)registration fields:
 */
enum xe_guc_g2g_type {
	XE_G2G_TYPE_IN = 0x0,
	XE_G2G_TYPE_OUT,
	XE_G2G_TYPE_LIMIT,
};

#define XE_G2G_REGISTER_DEVICE	REG_GENMASK(16, 16)
#define XE_G2G_REGISTER_TILE	REG_GENMASK(15, 12)
#define XE_G2G_REGISTER_TYPE	REG_GENMASK(11, 8)
#define XE_G2G_REGISTER_SIZE	REG_GENMASK(7, 0)

#define XE_G2G_DEREGISTER_DEVICE	REG_GENMASK(16, 16)
#define XE_G2G_DEREGISTER_TILE	REG_GENMASK(15, 12)
#define XE_G2G_DEREGISTER_TYPE	REG_GENMASK(11, 8)

#endif
