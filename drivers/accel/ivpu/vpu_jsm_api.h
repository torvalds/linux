/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

/**
 * @file
 * @brief JSM shared definitions
 *
 * @ingroup Jsm
 * @brief JSM shared definitions
 * @{
 */
#ifndef VPU_JSM_API_H
#define VPU_JSM_API_H

/*
 * Major version changes that break backward compatibility
 */
#define VPU_JSM_API_VER_MAJOR 3

/*
 * Minor version changes when API backward compatibility is preserved.
 */
#define VPU_JSM_API_VER_MINOR 0

/*
 * API header changed (field names, documentation, formatting) but API itself has not been changed
 */
#define VPU_JSM_API_VER_PATCH 1

/*
 * Index in the API version table
 */
#define VPU_JSM_API_VER_INDEX 4

/*
 * Number of Priority Bands for Hardware Scheduling
 * Bands: RealTime, Focus, Normal, Idle
 */
#define VPU_HWS_NUM_PRIORITY_BANDS 4

/* Max number of impacted contexts that can be dealt with the engine reset command */
#define VPU_MAX_ENGINE_RESET_IMPACTED_CONTEXTS 3

/** Pack the API structures for now, once alignment issues are fixed this can be removed */
#pragma pack(push, 1)

/*
 * Engine indexes.
 */
#define VPU_ENGINE_COMPUTE 0
#define VPU_ENGINE_COPY	   1
#define VPU_ENGINE_NB	   2

/*
 * VPU status values.
 */
#define VPU_JSM_STATUS_SUCCESS				 0x0U
#define VPU_JSM_STATUS_PARSING_ERR			 0x1U
#define VPU_JSM_STATUS_PROCESSING_ERR			 0x2U
#define VPU_JSM_STATUS_PREEMPTED			 0x3U
#define VPU_JSM_STATUS_ABORTED				 0x4U
#define VPU_JSM_STATUS_USER_CTX_VIOL_ERR		 0x5U
#define VPU_JSM_STATUS_GLOBAL_CTX_VIOL_ERR		 0x6U
#define VPU_JSM_STATUS_MVNCI_WRONG_INPUT_FORMAT		 0x7U
#define VPU_JSM_STATUS_MVNCI_UNSUPPORTED_NETWORK_ELEMENT 0x8U
#define VPU_JSM_STATUS_MVNCI_INVALID_HANDLE		 0x9U
#define VPU_JSM_STATUS_MVNCI_OUT_OF_RESOURCES		 0xAU
#define VPU_JSM_STATUS_MVNCI_NOT_IMPLEMENTED		 0xBU
#define VPU_JSM_STATUS_MVNCI_INTERNAL_ERROR		 0xCU
/* Job status returned when the job was preempted mid-inference */
#define VPU_JSM_STATUS_PREEMPTED_MID_INFERENCE		 0xDU

/*
 * Host <-> VPU IPC channels.
 * ASYNC commands use a high priority channel, other messages use low-priority ones.
 */
#define VPU_IPC_CHAN_ASYNC_CMD 0
#define VPU_IPC_CHAN_GEN_CMD   10
#define VPU_IPC_CHAN_JOB_RET   11

/*
 * Job flags bit masks.
 */
#define VPU_JOB_FLAGS_NULL_SUBMISSION_MASK 0x00000001

/*
 * Sizes of the reserved areas in jobs, in bytes.
 */
#define VPU_JOB_RESERVED_BYTES	     16
/*
 * Sizes of the reserved areas in job queues, in bytes.
 */
#define VPU_JOB_QUEUE_RESERVED_BYTES 52

/*
 * Max length (including trailing NULL char) of trace entity name (e.g., the
 * name of a logging destination or a loggable HW component).
 */
#define VPU_TRACE_ENTITY_NAME_MAX_LEN 32

/*
 * Max length (including trailing NULL char) of a dyndbg command.
 *
 * NOTE: 96 is used so that the size of 'struct vpu_ipc_msg' in the JSM API is
 * 128 bytes (multiple of 64 bytes, the cache line size).
 */
#define VPU_DYNDBG_CMD_MAX_LEN 96

/*
 * Job format.
 */
struct vpu_job_queue_entry {
	u64 batch_buf_addr; /**< Address of VPU commands batch buffer */
	u32 job_id;	  /**< Job ID */
	u32 flags; /**< Flags bit field, see VPU_JOB_FLAGS_* above */
	u64 root_page_table_addr; /**< Address of root page table to use for this job */
	u64 root_page_table_update_counter; /**< Page tables update events counter */
	u64 preemption_buffer_address; /**< Address of the preemption buffer to use for this job */
	u64 preemption_buffer_size; /**< Size of the preemption buffer to use for this job */
	u8 reserved_0[VPU_JOB_RESERVED_BYTES];
};

/*
 * Job queue control registers.
 */
struct vpu_job_queue_header {
	u32 engine_idx;
	u32 head;
	u32 tail;
	u8 reserved_0[VPU_JOB_QUEUE_RESERVED_BYTES];
};

/*
 * Job queue format.
 */
struct vpu_job_queue {
	struct vpu_job_queue_header header;
	struct vpu_job_queue_entry job[];
};

/**
 * Logging entity types.
 *
 * This enum defines the different types of entities involved in logging.
 */
enum vpu_trace_entity_type {
	/** Logging destination (entity where logs can be stored / printed). */
	VPU_TRACE_ENTITY_TYPE_DESTINATION = 1,
	/** Loggable HW component (HW entity that can be logged). */
	VPU_TRACE_ENTITY_TYPE_HW_COMPONENT = 2,
};

/*
 * Host <-> VPU IPC messages types.
 */
enum vpu_ipc_msg_type {
	VPU_JSM_MSG_UNKNOWN = 0xFFFFFFFF,
	/* IPC Host -> Device, Async commands */
	VPU_JSM_MSG_ASYNC_CMD = 0x1100,
	VPU_JSM_MSG_ENGINE_RESET = VPU_JSM_MSG_ASYNC_CMD,
	VPU_JSM_MSG_ENGINE_PREEMPT = 0x1101,
	VPU_JSM_MSG_REGISTER_DB = 0x1102,
	VPU_JSM_MSG_UNREGISTER_DB = 0x1103,
	VPU_JSM_MSG_QUERY_ENGINE_HB = 0x1104,
	VPU_JSM_MSG_GET_POWER_LEVEL_COUNT = 0x1105,
	VPU_JSM_MSG_GET_POWER_LEVEL = 0x1106,
	VPU_JSM_MSG_SET_POWER_LEVEL = 0x1107,
	/* @deprecated */
	VPU_JSM_MSG_METRIC_STREAMER_OPEN = 0x1108,
	/* @deprecated */
	VPU_JSM_MSG_METRIC_STREAMER_CLOSE = 0x1109,
	/** Configure logging (used to modify configuration passed in boot params). */
	VPU_JSM_MSG_TRACE_SET_CONFIG = 0x110a,
	/** Return current logging configuration. */
	VPU_JSM_MSG_TRACE_GET_CONFIG = 0x110b,
	/**
	 * Get masks of destinations and HW components supported by the firmware
	 * (may vary between HW generations and FW compile
	 * time configurations)
	 */
	VPU_JSM_MSG_TRACE_GET_CAPABILITY = 0x110c,
	/** Get the name of a destination or HW component. */
	VPU_JSM_MSG_TRACE_GET_NAME = 0x110d,
	/**
	 * Release resource associated with host ssid . All jobs that belong to the host_ssid
	 * aborted and removed from internal scheduling queues. All doorbells assigned
	 * to the host_ssid are unregistered and any internal FW resources belonging to
	 * the host_ssid are released.
	 */
	VPU_JSM_MSG_SSID_RELEASE = 0x110e,
	/**
	 * Start collecting metric data.
	 * @see vpu_jsm_metric_streamer_start
	 */
	VPU_JSM_MSG_METRIC_STREAMER_START = 0x110f,
	/**
	 * Stop collecting metric data. This command will return success if it is called
	 * for a metric stream that has already been stopped or was never started.
	 * @see vpu_jsm_metric_streamer_stop
	 */
	VPU_JSM_MSG_METRIC_STREAMER_STOP = 0x1110,
	/**
	 * Update current and next buffer for metric data collection. This command can
	 * also be used to request information about the number of collected samples
	 * and the amount of data written to the buffer.
	 * @see vpu_jsm_metric_streamer_update
	 */
	VPU_JSM_MSG_METRIC_STREAMER_UPDATE = 0x1111,
	/**
	 * Request description of selected metric groups and metric counters within
	 * each group. The VPU will write the description of groups and counters to
	 * the buffer specified in the command structure.
	 * @see vpu_jsm_metric_streamer_start
	 */
	VPU_JSM_MSG_METRIC_STREAMER_INFO = 0x1112,
	/** Control command: Priority band setup */
	VPU_JSM_MSG_SET_PRIORITY_BAND_SETUP = 0x1113,
	/** Control command: Create command queue */
	VPU_JSM_MSG_CREATE_CMD_QUEUE = 0x1114,
	/** Control command: Destroy command queue */
	VPU_JSM_MSG_DESTROY_CMD_QUEUE = 0x1115,
	/** Control command: Set context scheduling properties */
	VPU_JSM_MSG_SET_CONTEXT_SCHED_PROPERTIES = 0x1116,
	/*
	 * Register a doorbell to notify VPU of new work. The doorbell may later be
	 * deallocated or reassigned to another context.
	 */
	VPU_JSM_MSG_HWS_REGISTER_DB = 0x1117,
	/* IPC Host -> Device, General commands */
	VPU_JSM_MSG_GENERAL_CMD = 0x1200,
	VPU_JSM_MSG_BLOB_DEINIT = VPU_JSM_MSG_GENERAL_CMD,
	/**
	 * Control dyndbg behavior by executing a dyndbg command; equivalent to
	 * Linux command: `echo '<dyndbg_cmd>' > <debugfs>/dynamic_debug/control`.
	 */
	VPU_JSM_MSG_DYNDBG_CONTROL = 0x1201,
	/* IPC Device -> Host, Job completion */
	VPU_JSM_MSG_JOB_DONE = 0x2100,
	/* IPC Device -> Host, Async command completion */
	VPU_JSM_MSG_ASYNC_CMD_DONE = 0x2200,
	VPU_JSM_MSG_ENGINE_RESET_DONE = VPU_JSM_MSG_ASYNC_CMD_DONE,
	VPU_JSM_MSG_ENGINE_PREEMPT_DONE = 0x2201,
	VPU_JSM_MSG_REGISTER_DB_DONE = 0x2202,
	VPU_JSM_MSG_UNREGISTER_DB_DONE = 0x2203,
	VPU_JSM_MSG_QUERY_ENGINE_HB_DONE = 0x2204,
	VPU_JSM_MSG_GET_POWER_LEVEL_COUNT_DONE = 0x2205,
	VPU_JSM_MSG_GET_POWER_LEVEL_DONE = 0x2206,
	VPU_JSM_MSG_SET_POWER_LEVEL_DONE = 0x2207,
	/* @deprecated */
	VPU_JSM_MSG_METRIC_STREAMER_OPEN_DONE = 0x2208,
	/* @deprecated */
	VPU_JSM_MSG_METRIC_STREAMER_CLOSE_DONE = 0x2209,
	/** Response to VPU_JSM_MSG_TRACE_SET_CONFIG. */
	VPU_JSM_MSG_TRACE_SET_CONFIG_RSP = 0x220a,
	/** Response to VPU_JSM_MSG_TRACE_GET_CONFIG. */
	VPU_JSM_MSG_TRACE_GET_CONFIG_RSP = 0x220b,
	/** Response to VPU_JSM_MSG_TRACE_GET_CAPABILITY. */
	VPU_JSM_MSG_TRACE_GET_CAPABILITY_RSP = 0x220c,
	/** Response to VPU_JSM_MSG_TRACE_GET_NAME. */
	VPU_JSM_MSG_TRACE_GET_NAME_RSP = 0x220d,
	/** Response to VPU_JSM_MSG_SSID_RELEASE. */
	VPU_JSM_MSG_SSID_RELEASE_DONE = 0x220e,
	/**
	 * Response to VPU_JSM_MSG_METRIC_STREAMER_START.
	 * VPU will return an error result if metric collection cannot be started,
	 * e.g. when the specified metric mask is invalid.
	 * @see vpu_jsm_metric_streamer_done
	 */
	VPU_JSM_MSG_METRIC_STREAMER_START_DONE = 0x220f,
	/**
	 * Response to VPU_JSM_MSG_METRIC_STREAMER_STOP.
	 * Returns information about collected metric data.
	 * @see vpu_jsm_metric_streamer_done
	 */
	VPU_JSM_MSG_METRIC_STREAMER_STOP_DONE = 0x2210,
	/**
	 * Response to VPU_JSM_MSG_METRIC_STREAMER_UPDATE.
	 * Returns information about collected metric data.
	 * @see vpu_jsm_metric_streamer_done
	 */
	VPU_JSM_MSG_METRIC_STREAMER_UPDATE_DONE = 0x2211,
	/**
	 * Response to VPU_JSM_MSG_METRIC_STREAMER_INFO.
	 * Returns a description of the metric groups and metric counters.
	 * @see vpu_jsm_metric_streamer_done
	 */
	VPU_JSM_MSG_METRIC_STREAMER_INFO_DONE = 0x2212,
	/**
	 * Asynchronous event sent from the VPU to the host either when the current
	 * metric buffer is full or when the VPU has collected a multiple of
	 * @notify_sample_count samples as indicated through the start command
	 * (VPU_JSM_MSG_METRIC_STREAMER_START). Returns information about collected
	 * metric data.
	 * @see vpu_jsm_metric_streamer_done
	 */
	VPU_JSM_MSG_METRIC_STREAMER_NOTIFICATION = 0x2213,
	/** Response to control command: Priority band setup */
	VPU_JSM_MSG_SET_PRIORITY_BAND_SETUP_RSP = 0x2214,
	/** Response to control command: Create command queue */
	VPU_JSM_MSG_CREATE_CMD_QUEUE_RSP = 0x2215,
	/** Response to control command: Destroy command queue */
	VPU_JSM_MSG_DESTROY_CMD_QUEUE_RSP = 0x2216,
	/** Response to control command: Set context scheduling properties */
	VPU_JSM_MSG_SET_CONTEXT_SCHED_PROPERTIES_RSP = 0x2217,
	/* IPC Device -> Host, General command completion */
	VPU_JSM_MSG_GENERAL_CMD_DONE = 0x2300,
	VPU_JSM_MSG_BLOB_DEINIT_DONE = VPU_JSM_MSG_GENERAL_CMD_DONE,
	/** Response to VPU_JSM_MSG_DYNDBG_CONTROL. */
	VPU_JSM_MSG_DYNDBG_CONTROL_RSP = 0x2301,
};

enum vpu_ipc_msg_status { VPU_JSM_MSG_FREE, VPU_JSM_MSG_ALLOCATED };

/*
 * Host <-> LRT IPC message payload definitions
 */
struct vpu_ipc_msg_payload_engine_reset {
	/* Engine to be reset. */
	u32 engine_idx;
	/* Reserved */
	u32 reserved_0;
};

struct vpu_ipc_msg_payload_engine_preempt {
	/* Engine to be preempted. */
	u32 engine_idx;
	/* ID of the preemption request. */
	u32 preempt_id;
};

/*
 * @brief Register doorbell command structure.
 * This structure supports doorbell registration for only OS scheduling.
 * @see VPU_JSM_MSG_REGISTER_DB
 */
struct vpu_ipc_msg_payload_register_db {
	/* Index of the doorbell to register. */
	u32 db_idx;
	/* Reserved */
	u32 reserved_0;
	/* Virtual address in Global GTT pointing to the start of job queue. */
	u64 jobq_base;
	/* Size of the job queue in bytes. */
	u32 jobq_size;
	/* Host sub-stream ID for the context assigned to the doorbell. */
	u32 host_ssid;
};

/**
 * @brief Unregister doorbell command structure.
 * Request structure to unregister a doorbell for both HW and OS scheduling.
 * @see VPU_JSM_MSG_UNREGISTER_DB
 */
struct vpu_ipc_msg_payload_unregister_db {
	/* Index of the doorbell to unregister. */
	u32 db_idx;
	/* Reserved */
	u32 reserved_0;
};

struct vpu_ipc_msg_payload_query_engine_hb {
	/* Engine to return heartbeat value. */
	u32 engine_idx;
	/* Reserved */
	u32 reserved_0;
};

struct vpu_ipc_msg_payload_power_level {
	/**
	 * Requested power level. The power level value is in the
	 * range [0, power_level_count-1] where power_level_count
	 * is the number of available power levels as returned by
	 * the get power level count command. A power level of 0
	 * corresponds to the maximum possible power level, while
	 * power_level_count-1 corresponds to the minimum possible
	 * power level. Values outside of this range are not
	 * considered to be valid.
	 */
	u32 power_level;
	/* Reserved */
	u32 reserved_0;
};

struct vpu_ipc_msg_payload_ssid_release {
	/* Host sub-stream ID for the context to be released. */
	u32 host_ssid;
	/* Reserved */
	u32 reserved_0;
};

/**
 * @brief Metric streamer start command structure.
 * This structure is also used with VPU_JSM_MSG_METRIC_STREAMER_INFO to request metric
 * groups and metric counters description from the firmware.
 * @see VPU_JSM_MSG_METRIC_STREAMER_START
 * @see VPU_JSM_MSG_METRIC_STREAMER_INFO
 */
struct vpu_jsm_metric_streamer_start {
	/**
	 * Bitmask to select the desired metric groups.
	 * A metric group can belong only to one metric streamer instance at a time.
	 * Since each metric streamer instance has a unique set of metric groups, it
	 * can also identify a metric streamer instance if more than one instance was
	 * started. If the VPU device does not support multiple metric streamer instances,
	 * then VPU_JSM_MSG_METRIC_STREAMER_START will return an error even if the second
	 * instance has different groups to the first.
	 */
	u64 metric_group_mask;
	/** Sampling rate in nanoseconds. */
	u64 sampling_rate;
	/**
	 * If > 0 the VPU will send a VPU_JSM_MSG_METRIC_STREAMER_NOTIFICATION message
	 * after every @notify_sample_count samples is collected or dropped by the VPU.
	 * If set to UINT_MAX the VPU will only generate a notification when the metric
	 * buffer is full. If set to 0 the VPU will never generate a notification.
	 */
	u32 notify_sample_count;
	u32 reserved_0;
	/**
	 * Address and size of the buffer where the VPU will write metric data. The
	 * VPU writes all counters from enabled metric groups one after another. If
	 * there is no space left to write data at the next sample period the VPU
	 * will switch to the next buffer (@see next_buffer_addr) and will optionally
	 * send a notification to the host driver if @notify_sample_count is non-zero.
	 * If @next_buffer_addr is NULL the VPU will stop collecting metric data.
	 */
	u64 buffer_addr;
	u64 buffer_size;
	/**
	 * Address and size of the next buffer to write metric data to after the initial
	 * buffer is full. If the address is NULL the VPU will stop collecting metric
	 * data.
	 */
	u64 next_buffer_addr;
	u64 next_buffer_size;
};

/**
 * @brief Metric streamer stop command structure.
 * @see VPU_JSM_MSG_METRIC_STREAMER_STOP
 */
struct vpu_jsm_metric_streamer_stop {
	/** Bitmask to select the desired metric groups. */
	u64 metric_group_mask;
};

/**
 * Provide VPU FW with buffers to write metric data.
 * @see VPU_JSM_MSG_METRIC_STREAMER_UPDATE
 */
struct vpu_jsm_metric_streamer_update {
	/** Metric group mask that identifies metric streamer instance. */
	u64 metric_group_mask;
	/**
	 * Address and size of the buffer where the VPU will write metric data. If
	 * the buffer address is 0 or same as the currently used buffer the VPU will
	 * continue writing metric data to the current buffer. In this case the
	 * buffer size is ignored and the size of the current buffer is unchanged.
	 * If the address is non-zero and differs from the current buffer address the
	 * VPU will immediately switch data collection to the new buffer.
	 */
	u64 buffer_addr;
	u64 buffer_size;
	/**
	 * Address and size of the next buffer to write metric data after the initial
	 * buffer is full. If the address is NULL the VPU will stop collecting metric
	 * data but will continue to record dropped samples.
	 *
	 * Note that there is a hazard possible if both buffer_addr and the next_buffer_addr
	 * are non-zero in same update request. It is the host's responsibility to ensure
	 * that both addresses make sense even if the VPU just switched to writing samples
	 * from the current to the next buffer.
	 */
	u64 next_buffer_addr;
	u64 next_buffer_size;
};

struct vpu_ipc_msg_payload_blob_deinit {
	/* 64-bit unique ID for the blob to be de-initialized. */
	u64 blob_id;
};

struct vpu_ipc_msg_payload_job_done {
	/* Engine to which the job was submitted. */
	u32 engine_idx;
	/* Index of the doorbell to which the job was submitted */
	u32 db_idx;
	/* ID of the completed job */
	u32 job_id;
	/* Status of the completed job */
	u32 job_status;
	/* Host SSID */
	u32 host_ssid;
	/* Zero Padding */
	u32 reserved_0;
	/* Command queue id */
	u64 cmdq_id;
};

struct vpu_jsm_engine_reset_context {
	/* Host SSID */
	u32 host_ssid;
	/* Zero Padding */
	u32 reserved_0;
	/* Command queue id */
	u64 cmdq_id;
	/* Flags: 0: cause of hang; 1: collateral damage of reset */
	u64 flags;
};

struct vpu_ipc_msg_payload_engine_reset_done {
	/* Engine ordinal */
	u32 engine_idx;
	/* Number of impacted contexts */
	u32 num_impacted_contexts;
	/* Array of impacted command queue ids and their flags */
	struct vpu_jsm_engine_reset_context
		impacted_contexts[VPU_MAX_ENGINE_RESET_IMPACTED_CONTEXTS];
};

struct vpu_ipc_msg_payload_engine_preempt_done {
	/* Engine preempted. */
	u32 engine_idx;
	/* ID of the preemption request. */
	u32 preempt_id;
};

/**
 * Response structure for register doorbell command for both OS
 * and HW scheduling.
 * @see VPU_JSM_MSG_REGISTER_DB
 * @see VPU_JSM_MSG_HWS_REGISTER_DB
 */
struct vpu_ipc_msg_payload_register_db_done {
	/* Index of the registered doorbell. */
	u32 db_idx;
	/* Reserved */
	u32 reserved_0;
};

/**
 * Response structure for unregister doorbell command for both OS
 * and HW scheduling.
 * @see VPU_JSM_MSG_UNREGISTER_DB
 */
struct vpu_ipc_msg_payload_unregister_db_done {
	/* Index of the unregistered doorbell. */
	u32 db_idx;
	/* Reserved */
	u32 reserved_0;
};

struct vpu_ipc_msg_payload_query_engine_hb_done {
	/* Engine returning heartbeat value. */
	u32 engine_idx;
	/* Reserved */
	u32 reserved_0;
	/* Heartbeat value. */
	u64 heartbeat;
};

struct vpu_ipc_msg_payload_get_power_level_count_done {
	/**
	 * Number of supported power levels. The maximum possible
	 * value of power_level_count is 16 but this may vary across
	 * implementations.
	 */
	u32 power_level_count;
	/* Reserved */
	u32 reserved_0;
	/**
	 * Power consumption limit for each supported power level in
	 * [0-100%] range relative to power level 0.
	 */
	u8 power_limit[16];
};

struct vpu_ipc_msg_payload_blob_deinit_done {
	/* 64-bit unique ID for the blob de-initialized. */
	u64 blob_id;
};

/* HWS priority band setup request / response */
struct vpu_ipc_msg_payload_hws_priority_band_setup {
	/*
	 * Grace period in 100ns units when preempting another priority band for
	 * this priority band
	 */
	u32 grace_period[VPU_HWS_NUM_PRIORITY_BANDS];
	/*
	 * Default quantum in 100ns units for scheduling across processes
	 * within a priority band
	 */
	u64 process_quantum[VPU_HWS_NUM_PRIORITY_BANDS];
	/*
	 * Default grace period in 100ns units for processes that preempt each
	 * other within a priority band
	 */
	u64 process_grace_period[VPU_HWS_NUM_PRIORITY_BANDS];
	/*
	 * For normal priority band, specifies the target VPU percentage
	 * in situations when it's starved by the focus band.
	 */
	u32 normal_band_percentage;
	/* Reserved */
	u32 reserved_0;
};

/* HWS create command queue request */
struct vpu_ipc_msg_payload_hws_create_cmdq {
	/* Process id */
	u64 process_id;
	/* Host SSID */
	u32 host_ssid;
	/* Zero Padding */
	u32 reserved;
	/* Command queue id */
	u64 cmdq_id;
	/* Command queue base */
	u64 cmdq_base;
	/* Command queue size */
	u32 cmdq_size;
	/* Reserved */
	u32 reserved_0;
};

/* HWS create command queue response */
struct vpu_ipc_msg_payload_hws_create_cmdq_rsp {
	/* Process id */
	u64 process_id;
	/* Host SSID */
	u32 host_ssid;
	/* Zero Padding */
	u32 reserved;
	/* Command queue id */
	u64 cmdq_id;
};

/* HWS destroy command queue request / response */
struct vpu_ipc_msg_payload_hws_destroy_cmdq {
	/* Host SSID */
	u32 host_ssid;
	/* Zero Padding */
	u32 reserved;
	/* Command queue id */
	u64 cmdq_id;
};

/* HWS set context scheduling properties request / response */
struct vpu_ipc_msg_payload_hws_set_context_sched_properties {
	/* Host SSID */
	u32 host_ssid;
	/* Zero Padding */
	u32 reserved_0;
	/* Command queue id */
	u64 cmdq_id;
	/* Priority band to assign to work of this context */
	u32 priority_band;
	/* Inside realtime band assigns a further priority */
	u32 realtime_priority_level;
	/* Priority relative to other contexts in the same process */
	u32 in_process_priority;
	/* Zero padding / Reserved */
	u32 reserved_1;
	/* Context quantum relative to other contexts of same priority in the same process */
	u64 context_quantum;
	/* Grace period when preempting context of the same priority within the same process */
	u64 grace_period_same_priority;
	/* Grace period when preempting context of a lower priority within the same process */
	u64 grace_period_lower_priority;
};

/*
 * @brief Register doorbell command structure.
 * This structure supports doorbell registration for both HW and OS scheduling.
 * Note: Queue base and size are added here so that the same structure can be used for
 * OS scheduling and HW scheduling. For OS scheduling, cmdq_id will be ignored
 * and cmdq_base and cmdq_size will be used. For HW scheduling, cmdq_base and cmdq_size will be
 * ignored and cmdq_id is used.
 * @see VPU_JSM_MSG_HWS_REGISTER_DB
 */
struct vpu_jsm_hws_register_db {
	/* Index of the doorbell to register. */
	u32 db_id;
	/* Host sub-stream ID for the context assigned to the doorbell. */
	u32 host_ssid;
	/* ID of the command queue associated with the doorbell. */
	u64 cmdq_id;
	/* Virtual address pointing to the start of command queue. */
	u64 cmdq_base;
	/* Size of the command queue in bytes. */
	u64 cmdq_size;
};

/**
 * Payload for VPU_JSM_MSG_TRACE_SET_CONFIG[_RSP] and
 * VPU_JSM_MSG_TRACE_GET_CONFIG_RSP messages.
 *
 * The payload is interpreted differently depending on the type of message:
 *
 * - For VPU_JSM_MSG_TRACE_SET_CONFIG, the payload specifies the desired
 *   logging configuration to be set.
 *
 * - For VPU_JSM_MSG_TRACE_SET_CONFIG_RSP, the payload reports the logging
 *   configuration that was set after a VPU_JSM_MSG_TRACE_SET_CONFIG request.
 *   The host can compare this payload with the one it sent in the
 *   VPU_JSM_MSG_TRACE_SET_CONFIG request to check whether or not the
 *   configuration was set as desired.
 *
 * - VPU_JSM_MSG_TRACE_GET_CONFIG_RSP, the payload reports the current logging
 *   configuration.
 */
struct vpu_ipc_msg_payload_trace_config {
	/**
	 * Logging level (currently set or to be set); see 'mvLog_t' enum for
	 * acceptable values. The specified logging level applies to all
	 * destinations and HW components
	 */
	u32 trace_level;
	/**
	 * Bitmask of logging destinations (currently enabled or to be enabled);
	 * bitwise OR of values defined in logging_destination enum.
	 */
	u32 trace_destination_mask;
	/**
	 * Bitmask of loggable HW components (currently enabled or to be enabled);
	 * bitwise OR of values defined in loggable_hw_component enum.
	 */
	u64 trace_hw_component_mask;
	u64 reserved_0; /**< Reserved for future extensions. */
};

/**
 * Payload for VPU_JSM_MSG_TRACE_GET_CAPABILITY_RSP messages.
 */
struct vpu_ipc_msg_payload_trace_capability_rsp {
	u32 trace_destination_mask; /**< Bitmask of supported logging destinations. */
	u32 reserved_0;
	u64 trace_hw_component_mask; /**< Bitmask of supported loggable HW components. */
	u64 reserved_1; /**< Reserved for future extensions. */
};

/**
 * Payload for VPU_JSM_MSG_TRACE_GET_NAME requests.
 */
struct vpu_ipc_msg_payload_trace_get_name {
	/**
	 * The type of the entity to query name for; see logging_entity_type for
	 * possible values.
	 */
	u32 entity_type;
	u32 reserved_0;
	/**
	 * The ID of the entity to query name for; possible values depends on the
	 * entity type.
	 */
	u64 entity_id;
};

/**
 * Payload for VPU_JSM_MSG_TRACE_GET_NAME_RSP responses.
 */
struct vpu_ipc_msg_payload_trace_get_name_rsp {
	/**
	 * The type of the entity whose name was queried; see logging_entity_type
	 * for possible values.
	 */
	u32 entity_type;
	u32 reserved_0;
	/**
	 * The ID of the entity whose name was queried; possible values depends on
	 * the entity type.
	 */
	u64 entity_id;
	/** Reserved for future extensions. */
	u64 reserved_1;
	/** The name of the entity. */
	char entity_name[VPU_TRACE_ENTITY_NAME_MAX_LEN];
};

/**
 * Data sent from the VPU to the host in all metric streamer response messages
 * and in asynchronous notification.
 * @see VPU_JSM_MSG_METRIC_STREAMER_START_DONE
 * @see VPU_JSM_MSG_METRIC_STREAMER_STOP_DONE
 * @see VPU_JSM_MSG_METRIC_STREAMER_UPDATE_DONE
 * @see VPU_JSM_MSG_METRIC_STREAMER_INFO_DONE
 * @see VPU_JSM_MSG_METRIC_STREAMER_NOTIFICATION
 */
struct vpu_jsm_metric_streamer_done {
	/** Metric group mask that identifies metric streamer instance. */
	u64 metric_group_mask;
	/**
	 * Size in bytes of single sample - total size of all enabled counters.
	 * Some VPU implementations may align sample_size to more than 8 bytes.
	 */
	u32 sample_size;
	u32 reserved_0;
	/**
	 * Number of samples collected since the metric streamer was started.
	 * This will be 0 if the metric streamer was not started.
	 */
	u32 samples_collected;
	/**
	 * Number of samples dropped since the metric streamer was started. This
	 * is incremented every time the metric streamer is not able to write
	 * collected samples because the current buffer is full and there is no
	 * next buffer to switch to.
	 */
	u32 samples_dropped;
	/** Address of the buffer that contains the latest metric data. */
	u64 buffer_addr;
	/**
	 * Number of bytes written into the metric data buffer. In response to the
	 * VPU_JSM_MSG_METRIC_STREAMER_INFO request this field contains the size of
	 * all group and counter descriptors. The size is updated even if the buffer
	 * in the request was NULL or too small to hold descriptors of all counters
	 */
	u64 bytes_written;
};

/**
 * Metric group description placed in the metric buffer after successful completion
 * of the VPU_JSM_MSG_METRIC_STREAMER_INFO command. This is followed by one or more
 * @vpu_jsm_metric_counter_descriptor records.
 * @see VPU_JSM_MSG_METRIC_STREAMER_INFO
 */
struct vpu_jsm_metric_group_descriptor {
	/**
	 * Offset to the next metric group (8-byte aligned). If this offset is 0 this
	 * is the last descriptor. The value of metric_info_size must be greater than
	 * or equal to sizeof(struct vpu_jsm_metric_group_descriptor) + name_string_size
	 * + description_string_size and must be 8-byte aligned.
	 */
	u32 next_metric_group_info_offset;
	/**
	 * Offset to the first metric counter description record (8-byte aligned).
	 * @see vpu_jsm_metric_counter_descriptor
	 */
	u32 next_metric_counter_info_offset;
	/** Index of the group. This corresponds to bit index in metric_group_mask. */
	u32 group_id;
	/** Number of counters in the metric group. */
	u32 num_counters;
	/** Data size for all counters, must be a multiple of 8 bytes.*/
	u32 metric_group_data_size;
	/**
	 * Metric group domain number. Cannot use multiple, simultaneous metric groups
	 * from the same domain.
	 */
	u32 domain;
	/**
	 * Counter name string size. The string must include a null termination character.
	 * The FW may use a fixed size name or send a different name for each counter.
	 * If the VPU uses fixed size strings, all characters from the end of the name
	 * to the of the fixed size character array must be zeroed.
	 */
	u32 name_string_size;
	/** Counter description string size, @see name_string_size */
	u32 description_string_size;
	u64 reserved_0;
	/**
	 * Right after this structure, the VPU writes name and description of
	 * the metric group.
	 */
};

/**
 * Metric counter description, placed in the buffer after vpu_jsm_metric_group_descriptor.
 * @see VPU_JSM_MSG_METRIC_STREAMER_INFO
 */
struct vpu_jsm_metric_counter_descriptor {
	/**
	 * Offset to the next counter in a group (8-byte aligned). If this offset is
	 * 0 this is the last counter in the group.
	 */
	u32 next_metric_counter_info_offset;
	/**
	 * Offset to the counter data from the start of samples in this metric group.
	 * Note that metric_data_offset % metric_data_size must be 0.
	 */
	u32 metric_data_offset;
	/** Size of the metric counter data in bytes. */
	u32 metric_data_size;
	/** Metric type, see Level Zero API for definitions. */
	u32 tier;
	/** Metric type, see set_metric_type_t for definitions. */
	u32 metric_type;
	/** Metric type, see set_value_type_t for definitions. */
	u32 metric_value_type;
	/**
	 * Counter name string size. The string must include a null termination character.
	 * The FW may use a fixed size name or send a different name for each counter.
	 * If the VPU uses fixed size strings, all characters from the end of the name
	 * to the of the fixed size character array must be zeroed.
	 */
	u32 name_string_size;
	/** Counter description string size, @see name_string_size */
	u32 description_string_size;
	/** Counter component name string size, @see name_string_size */
	u32 component_string_size;
	/** Counter string size, @see name_string_size */
	u32 units_string_size;
	u64 reserved_0;
	/**
	 * Right after this structure, the VPU writes name, description
	 * component and unit strings.
	 */
};

/**
 * Payload for VPU_JSM_MSG_DYNDBG_CONTROL requests.
 *
 * VPU_JSM_MSG_DYNDBG_CONTROL are used to control the VPU FW Dynamic Debug
 * feature, which allows developers to selectively enable / disable MVLOG_DEBUG
 * messages. This is equivalent to the Dynamic Debug functionality provided by
 * Linux
 * (https://www.kernel.org/doc/html/latest/admin-guide/dynamic-debug-howto.html)
 * The host can control Dynamic Debug behavior by sending dyndbg commands, which
 * have the same syntax as Linux
 * dyndbg commands.
 *
 * NOTE: in order for MVLOG_DEBUG messages to be actually printed, the host
 * still has to set the logging level to MVLOG_DEBUG, using the
 * VPU_JSM_MSG_TRACE_SET_CONFIG command.
 *
 * The host can see the current dynamic debug configuration by executing a
 * special 'show' command. The dyndbg configuration will be printed to the
 * configured logging destination using MVLOG_INFO logging level.
 */
struct vpu_ipc_msg_payload_dyndbg_control {
	/**
	 * Dyndbg command (same format as Linux dyndbg); must be a NULL-terminated
	 * string.
	 */
	char dyndbg_cmd[VPU_DYNDBG_CMD_MAX_LEN];
};

/*
 * Payloads union, used to define complete message format.
 */
union vpu_ipc_msg_payload {
	struct vpu_ipc_msg_payload_engine_reset engine_reset;
	struct vpu_ipc_msg_payload_engine_preempt engine_preempt;
	struct vpu_ipc_msg_payload_register_db register_db;
	struct vpu_ipc_msg_payload_unregister_db unregister_db;
	struct vpu_ipc_msg_payload_query_engine_hb query_engine_hb;
	struct vpu_ipc_msg_payload_power_level power_level;
	struct vpu_jsm_metric_streamer_start metric_streamer_start;
	struct vpu_jsm_metric_streamer_stop metric_streamer_stop;
	struct vpu_jsm_metric_streamer_update metric_streamer_update;
	struct vpu_ipc_msg_payload_blob_deinit blob_deinit;
	struct vpu_ipc_msg_payload_ssid_release ssid_release;
	struct vpu_jsm_hws_register_db hws_register_db;
	struct vpu_ipc_msg_payload_job_done job_done;
	struct vpu_ipc_msg_payload_engine_reset_done engine_reset_done;
	struct vpu_ipc_msg_payload_engine_preempt_done engine_preempt_done;
	struct vpu_ipc_msg_payload_register_db_done register_db_done;
	struct vpu_ipc_msg_payload_unregister_db_done unregister_db_done;
	struct vpu_ipc_msg_payload_query_engine_hb_done query_engine_hb_done;
	struct vpu_ipc_msg_payload_get_power_level_count_done get_power_level_count_done;
	struct vpu_jsm_metric_streamer_done metric_streamer_done;
	struct vpu_ipc_msg_payload_blob_deinit_done blob_deinit_done;
	struct vpu_ipc_msg_payload_trace_config trace_config;
	struct vpu_ipc_msg_payload_trace_capability_rsp trace_capability;
	struct vpu_ipc_msg_payload_trace_get_name trace_get_name;
	struct vpu_ipc_msg_payload_trace_get_name_rsp trace_get_name_rsp;
	struct vpu_ipc_msg_payload_dyndbg_control dyndbg_control;
	struct vpu_ipc_msg_payload_hws_priority_band_setup hws_priority_band_setup;
	struct vpu_ipc_msg_payload_hws_create_cmdq hws_create_cmdq;
	struct vpu_ipc_msg_payload_hws_create_cmdq_rsp hws_create_cmdq_rsp;
	struct vpu_ipc_msg_payload_hws_destroy_cmdq hws_destroy_cmdq;
	struct vpu_ipc_msg_payload_hws_set_context_sched_properties
		hws_set_context_sched_properties;
};

/*
 * Host <-> LRT IPC message base structure.
 *
 * NOTE: All instances of this object must be aligned on a 64B boundary
 * to allow proper handling of VPU cache operations.
 */
struct vpu_jsm_msg {
	/* Reserved */
	u64 reserved_0;
	/* Message type, see vpu_ipc_msg_type enum. */
	u32 type;
	/* Buffer status, see vpu_ipc_msg_status enum. */
	u32 status;
	/*
	 * Request ID, provided by the host in a request message and passed
	 * back by VPU in the response message.
	 */
	u32 request_id;
	/* Request return code set by the VPU, see VPU_JSM_STATUS_* defines. */
	u32 result;
	u64 reserved_1;
	/* Message payload depending on message type, see vpu_ipc_msg_payload union. */
	union vpu_ipc_msg_payload payload;
};

#pragma pack(pop)

#endif

///@}
