/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _GUC_ACTIONS_SLPC_ABI_H_
#define _GUC_ACTIONS_SLPC_ABI_H_

#include <linux/types.h>

/**
 * DOC: SLPC SHARED DATA STRUCTURE
 *
 *  +----+------+--------------------------------------------------------------+
 *  | CL | Bytes| Description                                                  |
 *  +====+======+==============================================================+
 *  | 1  | 0-3  | SHARED DATA SIZE                                             |
 *  |    +------+--------------------------------------------------------------+
 *  |    | 4-7  | GLOBAL STATE                                                 |
 *  |    +------+--------------------------------------------------------------+
 *  |    | 8-11 | DISPLAY DATA ADDRESS                                         |
 *  |    +------+--------------------------------------------------------------+
 *  |    | 12:63| PADDING                                                      |
 *  +----+------+--------------------------------------------------------------+
 *  |    | 0:63 | PADDING(PLATFORM INFO)                                       |
 *  +----+------+--------------------------------------------------------------+
 *  | 3  | 0-3  | TASK STATE DATA                                              |
 *  +    +------+--------------------------------------------------------------+
 *  |    | 4:63 | PADDING                                                      |
 *  +----+------+--------------------------------------------------------------+
 *  |4-21|0:1087| OVERRIDE PARAMS AND BIT FIELDS                               |
 *  +----+------+--------------------------------------------------------------+
 *  |    |      | PADDING + EXTRA RESERVED PAGE                                |
 *  +----+------+--------------------------------------------------------------+
 */

/*
 * SLPC exposes certain parameters for global configuration by the host.
 * These are referred to as override parameters, because in most cases
 * the host will not need to modify the default values used by SLPC.
 * SLPC remembers the default values which allows the host to easily restore
 * them by simply unsetting the override. The host can set or unset override
 * parameters during SLPC (re-)initialization using the SLPC Reset event.
 * The host can also set or unset override parameters on the fly using the
 * Parameter Set and Parameter Unset events
 */

#define SLPC_MAX_OVERRIDE_PARAMETERS		256
#define SLPC_OVERRIDE_BITFIELD_SIZE \
		(SLPC_MAX_OVERRIDE_PARAMETERS / 32)

#define SLPC_PAGE_SIZE_BYTES			4096
#define SLPC_CACHELINE_SIZE_BYTES		64
#define SLPC_SHARED_DATA_SIZE_BYTE_HEADER	SLPC_CACHELINE_SIZE_BYTES
#define SLPC_SHARED_DATA_SIZE_BYTE_PLATFORM_INFO	SLPC_CACHELINE_SIZE_BYTES
#define SLPC_SHARED_DATA_SIZE_BYTE_TASK_STATE	SLPC_CACHELINE_SIZE_BYTES
#define SLPC_SHARED_DATA_MODE_DEFN_TABLE_SIZE	SLPC_PAGE_SIZE_BYTES
#define SLPC_SHARED_DATA_SIZE_BYTE_MAX		(2 * SLPC_PAGE_SIZE_BYTES)

/*
 * Cacheline size aligned (Total size needed for
 * SLPM_KMD_MAX_OVERRIDE_PARAMETERS=256 is 1088 bytes)
 */
#define SLPC_OVERRIDE_PARAMS_TOTAL_BYTES	(((((SLPC_MAX_OVERRIDE_PARAMETERS * 4) \
						+ ((SLPC_MAX_OVERRIDE_PARAMETERS / 32) * 4)) \
		+ (SLPC_CACHELINE_SIZE_BYTES - 1)) / SLPC_CACHELINE_SIZE_BYTES) * \
					SLPC_CACHELINE_SIZE_BYTES)

#define SLPC_SHARED_DATA_SIZE_BYTE_OTHER	(SLPC_SHARED_DATA_SIZE_BYTE_MAX - \
					(SLPC_SHARED_DATA_SIZE_BYTE_HEADER \
					+ SLPC_SHARED_DATA_SIZE_BYTE_PLATFORM_INFO \
					+ SLPC_SHARED_DATA_SIZE_BYTE_TASK_STATE \
					+ SLPC_OVERRIDE_PARAMS_TOTAL_BYTES \
					+ SLPC_SHARED_DATA_MODE_DEFN_TABLE_SIZE))

enum slpc_task_enable {
	SLPC_PARAM_TASK_DEFAULT = 0,
	SLPC_PARAM_TASK_ENABLED,
	SLPC_PARAM_TASK_DISABLED,
	SLPC_PARAM_TASK_UNKNOWN
};

enum slpc_global_state {
	SLPC_GLOBAL_STATE_NOT_RUNNING = 0,
	SLPC_GLOBAL_STATE_INITIALIZING = 1,
	SLPC_GLOBAL_STATE_RESETTING = 2,
	SLPC_GLOBAL_STATE_RUNNING = 3,
	SLPC_GLOBAL_STATE_SHUTTING_DOWN = 4,
	SLPC_GLOBAL_STATE_ERROR = 5
};

enum slpc_param_id {
	SLPC_PARAM_TASK_ENABLE_GTPERF = 0,
	SLPC_PARAM_TASK_DISABLE_GTPERF = 1,
	SLPC_PARAM_TASK_ENABLE_BALANCER = 2,
	SLPC_PARAM_TASK_DISABLE_BALANCER = 3,
	SLPC_PARAM_TASK_ENABLE_DCC = 4,
	SLPC_PARAM_TASK_DISABLE_DCC = 5,
	SLPC_PARAM_GLOBAL_MIN_GT_UNSLICE_FREQ_MHZ = 6,
	SLPC_PARAM_GLOBAL_MAX_GT_UNSLICE_FREQ_MHZ = 7,
	SLPC_PARAM_GLOBAL_MIN_GT_SLICE_FREQ_MHZ = 8,
	SLPC_PARAM_GLOBAL_MAX_GT_SLICE_FREQ_MHZ = 9,
	SLPC_PARAM_GTPERF_THRESHOLD_MAX_FPS = 10,
	SLPC_PARAM_GLOBAL_DISABLE_GT_FREQ_MANAGEMENT = 11,
	SLPC_PARAM_GTPERF_ENABLE_FRAMERATE_STALLING = 12,
	SLPC_PARAM_GLOBAL_DISABLE_RC6_MODE_CHANGE = 13,
	SLPC_PARAM_GLOBAL_OC_UNSLICE_FREQ_MHZ = 14,
	SLPC_PARAM_GLOBAL_OC_SLICE_FREQ_MHZ = 15,
	SLPC_PARAM_GLOBAL_ENABLE_IA_GT_BALANCING = 16,
	SLPC_PARAM_GLOBAL_ENABLE_ADAPTIVE_BURST_TURBO = 17,
	SLPC_PARAM_GLOBAL_ENABLE_EVAL_MODE = 18,
	SLPC_PARAM_GLOBAL_ENABLE_BALANCER_IN_NON_GAMING_MODE = 19,
	SLPC_PARAM_GLOBAL_RT_MODE_TURBO_FREQ_DELTA_MHZ = 20,
	SLPC_PARAM_PWRGATE_RC_MODE = 21,
	SLPC_PARAM_EDR_MODE_COMPUTE_TIMEOUT_MS = 22,
	SLPC_PARAM_EDR_QOS_FREQ_MHZ = 23,
	SLPC_PARAM_MEDIA_FF_RATIO_MODE = 24,
	SLPC_PARAM_ENABLE_IA_FREQ_LIMITING = 25,
	SLPC_PARAM_STRATEGIES = 26,
	SLPC_PARAM_POWER_PROFILE = 27,
	SLPC_PARAM_IGNORE_EFFICIENT_FREQUENCY = 28,
	SLPC_MAX_PARAM = 32,
};

enum slpc_media_ratio_mode {
	SLPC_MEDIA_RATIO_MODE_DYNAMIC_CONTROL = 0,
	SLPC_MEDIA_RATIO_MODE_FIXED_ONE_TO_ONE = 1,
	SLPC_MEDIA_RATIO_MODE_FIXED_ONE_TO_TWO = 2,
};

enum slpc_event_id {
	SLPC_EVENT_RESET = 0,
	SLPC_EVENT_SHUTDOWN = 1,
	SLPC_EVENT_PLATFORM_INFO_CHANGE = 2,
	SLPC_EVENT_DISPLAY_MODE_CHANGE = 3,
	SLPC_EVENT_FLIP_COMPLETE = 4,
	SLPC_EVENT_QUERY_TASK_STATE = 5,
	SLPC_EVENT_PARAMETER_SET = 6,
	SLPC_EVENT_PARAMETER_UNSET = 7,
};

struct slpc_task_state_data {
	union {
		u32 task_status_padding;
		struct {
			u32 status;
#define SLPC_GTPERF_TASK_ENABLED	REG_BIT(0)
#define SLPC_DCC_TASK_ENABLED		REG_BIT(11)
#define SLPC_IN_DCC			REG_BIT(12)
#define SLPC_BALANCER_ENABLED		REG_BIT(15)
#define SLPC_IBC_TASK_ENABLED		REG_BIT(16)
#define SLPC_BALANCER_IA_LMT_ENABLED	REG_BIT(17)
#define SLPC_BALANCER_IA_LMT_ACTIVE	REG_BIT(18)
		};
	};
	union {
		u32 freq_padding;
		struct {
#define SLPC_MAX_UNSLICE_FREQ_MASK	REG_GENMASK(7, 0)
#define SLPC_MIN_UNSLICE_FREQ_MASK	REG_GENMASK(15, 8)
#define SLPC_MAX_SLICE_FREQ_MASK	REG_GENMASK(23, 16)
#define SLPC_MIN_SLICE_FREQ_MASK	REG_GENMASK(31, 24)
			u32 freq;
		};
	};
} __packed;

struct slpc_shared_data_header {
	/* Total size in bytes of this shared buffer. */
	u32 size;
	u32 global_state;
	u32 display_data_addr;
} __packed;

struct slpc_override_params {
	u32 bits[SLPC_OVERRIDE_BITFIELD_SIZE];
	u32 values[SLPC_MAX_OVERRIDE_PARAMETERS];
} __packed;

struct slpc_shared_data {
	struct slpc_shared_data_header header;
	u8 shared_data_header_pad[SLPC_SHARED_DATA_SIZE_BYTE_HEADER -
				sizeof(struct slpc_shared_data_header)];

	u8 platform_info_pad[SLPC_SHARED_DATA_SIZE_BYTE_PLATFORM_INFO];

	struct slpc_task_state_data task_state_data;
	u8 task_state_data_pad[SLPC_SHARED_DATA_SIZE_BYTE_TASK_STATE -
				sizeof(struct slpc_task_state_data)];

	struct slpc_override_params override_params;
	u8 override_params_pad[SLPC_OVERRIDE_PARAMS_TOTAL_BYTES -
				sizeof(struct slpc_override_params)];

	u8 shared_data_pad[SLPC_SHARED_DATA_SIZE_BYTE_OTHER];

	/* PAGE 2 (4096 bytes), mode based parameter will be removed soon */
	u8 reserved_mode_definition[4096];
} __packed;

/**
 * DOC: SLPC H2G MESSAGE FORMAT
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
 *  |   |  15:0 | ACTION = _`GUC_ACTION_HOST2GUC_PC_SLPM_REQUEST` = 0x3003     |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:8 | **EVENT_ID**                                                 |
 *  +   +-------+--------------------------------------------------------------+
 *  |   |   7:0 | **EVENT_ARGC** - number of data arguments                    |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **EVENT_DATA1**                                              |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|  31:0 | ...                                                          |
 *  +---+-------+--------------------------------------------------------------+
 *  |2+n|  31:0 | **EVENT_DATAn**                                              |
 *  +---+-------+--------------------------------------------------------------+
 */

#define GUC_ACTION_HOST2GUC_PC_SLPC_REQUEST		0x3003

#define HOST2GUC_PC_SLPC_REQUEST_MSG_MIN_LEN \
				(GUC_HXG_REQUEST_MSG_MIN_LEN + 1u)
#define HOST2GUC_PC_SLPC_EVENT_MAX_INPUT_ARGS		9
#define HOST2GUC_PC_SLPC_REQUEST_MSG_MAX_LEN \
		(HOST2GUC_PC_SLPC_REQUEST_REQUEST_MSG_MIN_LEN + \
			HOST2GUC_PC_SLPC_EVENT_MAX_INPUT_ARGS)
#define HOST2GUC_PC_SLPC_REQUEST_MSG_0_MBZ		GUC_HXG_REQUEST_MSG_0_DATA0
#define HOST2GUC_PC_SLPC_REQUEST_MSG_1_EVENT_ID		(0xff << 8)
#define HOST2GUC_PC_SLPC_REQUEST_MSG_1_EVENT_ARGC	(0xff << 0)
#define HOST2GUC_PC_SLPC_REQUEST_MSG_N_EVENT_DATA_N	GUC_HXG_REQUEST_MSG_n_DATAn

#endif
