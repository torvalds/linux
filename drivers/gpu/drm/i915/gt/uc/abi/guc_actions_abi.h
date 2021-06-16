/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2021 Intel Corporation
 */

#ifndef _ABI_GUC_ACTIONS_ABI_H
#define _ABI_GUC_ACTIONS_ABI_H

/**
 * DOC: HOST2GUC_REGISTER_CTB
 *
 * This message is used as part of the `CTB based communication`_ setup.
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
 *  |   |  15:0 | ACTION = _`GUC_ACTION_HOST2GUC_REGISTER_CTB` = 0x4505        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:12 | RESERVED = MBZ                                               |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  11:8 | **TYPE** - type for the `CT Buffer`_                         |
 *  |   |       |                                                              |
 *  |   |       |   - _`GUC_CTB_TYPE_HOST2GUC` = 0                             |
 *  |   |       |   - _`GUC_CTB_TYPE_GUC2HOST` = 1                             |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |   7:0 | **SIZE** - size of the `CT Buffer`_ in 4K units minus 1      |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **DESC_ADDR** - GGTT address of the `CTB Descriptor`_        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 3 |  31:0 | **BUFF_ADDF** - GGTT address of the `CT Buffer`_             |
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
#define GUC_ACTION_HOST2GUC_REGISTER_CTB		0x4505

#define HOST2GUC_REGISTER_CTB_REQUEST_MSG_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 3u)
#define HOST2GUC_REGISTER_CTB_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define HOST2GUC_REGISTER_CTB_REQUEST_MSG_1_MBZ		(0xfffff << 12)
#define HOST2GUC_REGISTER_CTB_REQUEST_MSG_1_TYPE	(0xf << 8)
#define   GUC_CTB_TYPE_HOST2GUC				0u
#define   GUC_CTB_TYPE_GUC2HOST				1u
#define HOST2GUC_REGISTER_CTB_REQUEST_MSG_1_SIZE	(0xff << 0)
#define HOST2GUC_REGISTER_CTB_REQUEST_MSG_2_DESC_ADDR	GUC_HXG_REQUEST_MSG_n_DATAn
#define HOST2GUC_REGISTER_CTB_REQUEST_MSG_3_BUFF_ADDR	GUC_HXG_REQUEST_MSG_n_DATAn

#define HOST2GUC_REGISTER_CTB_RESPONSE_MSG_LEN		GUC_HXG_RESPONSE_MSG_MIN_LEN
#define HOST2GUC_REGISTER_CTB_RESPONSE_MSG_0_MBZ	GUC_HXG_RESPONSE_MSG_0_DATA0

/**
 * DOC: HOST2GUC_DEREGISTER_CTB
 *
 * This message is used as part of the `CTB based communication`_ teardown.
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
 *  |   |  15:0 | ACTION = _`GUC_ACTION_HOST2GUC_DEREGISTER_CTB` = 0x4506      |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 | 31:12 | RESERVED = MBZ                                               |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  11:8 | **TYPE** - type of the `CT Buffer`_                          |
 *  |   |       |                                                              |
 *  |   |       | see `GUC_ACTION_HOST2GUC_REGISTER_CTB`_                      |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |   7:0 | RESERVED = MBZ                                               |
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
#define GUC_ACTION_HOST2GUC_DEREGISTER_CTB		0x4506

#define HOST2GUC_DEREGISTER_CTB_REQUEST_MSG_LEN		(GUC_HXG_REQUEST_MSG_MIN_LEN + 1u)
#define HOST2GUC_DEREGISTER_CTB_REQUEST_MSG_0_MBZ	GUC_HXG_REQUEST_MSG_0_DATA0
#define HOST2GUC_DEREGISTER_CTB_REQUEST_MSG_1_MBZ	(0xfffff << 12)
#define HOST2GUC_DEREGISTER_CTB_REQUEST_MSG_1_TYPE	(0xf << 8)
#define HOST2GUC_DEREGISTER_CTB_REQUEST_MSG_1_MBZ2	(0xff << 0)

#define HOST2GUC_DEREGISTER_CTB_RESPONSE_MSG_LEN	GUC_HXG_RESPONSE_MSG_MIN_LEN
#define HOST2GUC_DEREGISTER_CTB_RESPONSE_MSG_0_MBZ	GUC_HXG_RESPONSE_MSG_0_DATA0

/* legacy definitions */

enum intel_guc_action {
	INTEL_GUC_ACTION_DEFAULT = 0x0,
	INTEL_GUC_ACTION_REQUEST_PREEMPTION = 0x2,
	INTEL_GUC_ACTION_REQUEST_ENGINE_RESET = 0x3,
	INTEL_GUC_ACTION_ALLOCATE_DOORBELL = 0x10,
	INTEL_GUC_ACTION_DEALLOCATE_DOORBELL = 0x20,
	INTEL_GUC_ACTION_LOG_BUFFER_FILE_FLUSH_COMPLETE = 0x30,
	INTEL_GUC_ACTION_UK_LOG_ENABLE_LOGGING = 0x40,
	INTEL_GUC_ACTION_FORCE_LOG_BUFFER_FLUSH = 0x302,
	INTEL_GUC_ACTION_ENTER_S_STATE = 0x501,
	INTEL_GUC_ACTION_EXIT_S_STATE = 0x502,
	INTEL_GUC_ACTION_SLPC_REQUEST = 0x3003,
	INTEL_GUC_ACTION_AUTHENTICATE_HUC = 0x4000,
	INTEL_GUC_ACTION_REGISTER_COMMAND_TRANSPORT_BUFFER = 0x4505,
	INTEL_GUC_ACTION_DEREGISTER_COMMAND_TRANSPORT_BUFFER = 0x4506,
	INTEL_GUC_ACTION_LIMIT
};

enum intel_guc_preempt_options {
	INTEL_GUC_PREEMPT_OPTION_DROP_WORK_Q = 0x4,
	INTEL_GUC_PREEMPT_OPTION_DROP_SUBMIT_Q = 0x8,
};

enum intel_guc_report_status {
	INTEL_GUC_REPORT_STATUS_UNKNOWN = 0x0,
	INTEL_GUC_REPORT_STATUS_ACKED = 0x1,
	INTEL_GUC_REPORT_STATUS_ERROR = 0x2,
	INTEL_GUC_REPORT_STATUS_COMPLETE = 0x4,
};

enum intel_guc_sleep_state_status {
	INTEL_GUC_SLEEP_STATE_SUCCESS = 0x1,
	INTEL_GUC_SLEEP_STATE_PREEMPT_TO_IDLE_FAILED = 0x2,
	INTEL_GUC_SLEEP_STATE_ENGINE_RESET_FAILED = 0x3
#define INTEL_GUC_SLEEP_STATE_INVALID_MASK 0x80000000
};

#define GUC_LOG_CONTROL_LOGGING_ENABLED	(1 << 0)
#define GUC_LOG_CONTROL_VERBOSITY_SHIFT	4
#define GUC_LOG_CONTROL_VERBOSITY_MASK	(0xF << GUC_LOG_CONTROL_VERBOSITY_SHIFT)
#define GUC_LOG_CONTROL_DEFAULT_LOGGING	(1 << 8)

#endif /* _ABI_GUC_ACTIONS_ABI_H */
