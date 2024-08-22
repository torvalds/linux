/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef NPU_HOST_IPC_H
#define NPU_HOST_IPC_H

/* -------------------------------------------------------------------------
 * Defines
 * -------------------------------------------------------------------------
 */
/* Messages sent **to** NPU */
/* IPC Message Commands -- uint32_t */
/* IPC command start base */
#define NPU_IPC_CMD_BASE                0x00000000
/* ipc_cmd_load_pkt */
#define NPU_IPC_CMD_LOAD                0x00000001
/* ipc_cmd_unload_pkt */
#define NPU_IPC_CMD_UNLOAD              0x00000002
/* ipc_cmd_execute_pkt */
#define NPU_IPC_CMD_EXECUTE             0x00000003
/* ipc_cmd_set_logging_state */
#define NPU_IPC_CMD_CONFIG_LOG          0x00000004
#define NPU_IPC_CMD_CONFIG_PERFORMANCE  0x00000005
#define NPU_IPC_CMD_CONFIG_DEBUG        0x00000006
#define NPU_IPC_CMD_SHUTDOWN            0x00000007
/* ipc_cmd_loopback_packet */
#define NPU_IPC_CMD_LOOPBACK            0x00000008
/* ipc_cmd_load_packet_v2_t */
#define NPU_IPC_CMD_LOAD_V2             0x00000009
/* ipc_cmd_execute_packet_v2 */
#define NPU_IPC_CMD_EXECUTE_V2          0x0000000A
/* ipc_cmd_set_property_packet */
#define NPU_IPC_CMD_SET_PROPERTY        0x0000000B
/* ipc_cmd_get_property_packet */
#define NPU_IPC_CMD_GET_PROPERTY        0x0000000C

/* Messages sent **from** NPU */
/* IPC Message Response -- uint32_t */
/* IPC response start base */
#define NPU_IPC_MSG_BASE                0x00010000
/* ipc_msg_load_pkt */
#define NPU_IPC_MSG_LOAD_DONE           0x00010001
/* ipc_msg_header_pkt */
#define NPU_IPC_MSG_UNLOAD_DONE         0x00010002
/* ipc_msg_header_pkt */
#define NPU_IPC_MSG_EXECUTE_DONE        0x00010003
/* ipc_msg_event_notify_pkt */
#define NPU_IPC_MSG_EVENT_NOTIFY        0x00010004
/* ipc_msg_loopback_pkt */
#define NPU_IPC_MSG_LOOPBACK_DONE       0x00010005
/* ipc_msg_execute_pkt_v2 */
#define NPU_IPC_MSG_EXECUTE_V2_DONE     0x00010006
/* ipc_msg_set_property_packet */
#define NPU_IPC_MSG_SET_PROPERTY_DONE   0x00010007
/* ipc_msg_get_property_packet */
#define NPU_IPC_MSG_GET_PROPERTY_DONE   0x00010008
/* ipc_msg_general_notify_pkt */
#define NPU_IPC_MSG_GENERAL_NOTIFY      0x00010010

/* IPC Notify Message Type -- uint32_t */
#define NPU_NOTIFY_DCVS_MODE            0x00002000

/* Logging message size */
/* Number 32-bit elements for the maximum log message size */
#define NPU_LOG_MSG_MAX_SIZE	4

/* Performance */
/* Performance counters for current network layer */
/* Amount of data read from all the DMA read channels */
#define NPU_PERFORMANCE_DMA_DATA_READ           0x01
/* Amount of data written from all the DMA write channels */
#define NPU_PERFORMANCE_DMA_DATA_WRITTEN        0x02
/* Number of blocks read by DMA channels */
#define NPU_PERFORMANCE_DMA_NUM_BLOCKS_READ     0x03
/* Number of blocks written by DMA channels */
#define NPU_PERFORMANCE_DMA_NUM_BLOCKS_WRITTEN  0x04
/* Number of instructions executed by CAL */
#define NPU_PERFORMANCE_INSTRUCTIONS_CAL        0x05
/* Number of instructions executed by CUB */
#define NPU_PERFORMANCE_INSTRUCTIONS_CUB        0x06
/* Timestamp of start of network load */
#define NPU_PERFORMANCE_TIMESTAMP_LOAD_START    0x07
/* Timestamp of end of network load */
#define NPU_PERFORMANCE_TIMESTAMP_LOAD_END      0x08
/* Timestamp of start of network execute */
#define NPU_PERFORMANCE_TIMESTAMP_EXECUTE_START 0x09
/* Timestamp of end of network execute */
#define NPU_PERFORMANCE_TIMESTAMP_EXECUTE_END   0x10
/* Timestamp of CAL start */
#define NPU_PERFORMANCE_TIMESTAMP_CAL_START     0x11
/* Timestamp of CAL end */
#define NPU_PERFORMANCE_TIMESTAMP_CAL_END       0x12
/* Timestamp of CUB start */
#define NPU_PERFORMANCE_TIMESTAMP_CUB_START     0x13
/* Timestamp of CUB end */
#define NPU_PERFORMANCE_TIMESTAMP_CUB_END       0x14

/* Performance enable */
/* Select which counters you want back per layer */

/* Shutdown */
/* Immediate shutdown, discard any state, etc */
#define NPU_SHUTDOWN_IMMEDIATE                  0x01
/* Shutdown after current execution (if any) is completed */
#define NPU_SHUTDOWN_WAIT_CURRENT_EXECUTION     0x02

/* Debug stats */
#define NUM_LAYER_STATS_PER_EXE_MSG_MAX 110

/* DCVS */
#define NPU_DCVS_ACTIVITY_MAX_PERF 0x100

/* -------------------------------------------------------------------------
 * Data Structures
 * -------------------------------------------------------------------------
 */
/* Command Header - Header for all Messages **TO** NPU */
/*
 * command header packet definition for
 * messages sent from host->NPU
 */
struct ipc_cmd_header_pkt {
	uint32_t	size;
	uint32_t	cmd_type;
	uint32_t	trans_id;
	uint32_t	flags; /* TDO what flags and why */
};

/* Message Header - Header for all messages **FROM** NPU */
/*
 * message header packet definition for
 * mesasges sent from NPU->host
 */
struct ipc_msg_header_pkt {
	uint32_t		size;
	uint32_t		msg_type;
	uint32_t		status;
	uint32_t		trans_id;
	uint32_t		flags;
};

/* Execute */
/*
 * FIRMWARE
 * keep lastNetworkIDRan = uint32
 * keep wasLastNetworkChunky = BOOLEAN
 */
/*
 * ACO Buffer definition
 */
struct npu_aco_buffer {
	/*
	 * used to track if previous network is the same and already loaded,
	 * we can save a dma
	 */
	uint32_t network_id;
	/*
	 * size of header + first chunk ACO buffer -
	 * this saves a dma by dmaing both header and first chunk
	 */
	uint32_t buf_size;
	/*
	 * SMMU 32-bit mapped address that the DMA engine can read -
	 * uses lower 32 bits
	 */
	uint64_t address;
};

/*
 * ACO Buffer V2 definition
 */
struct npu_aco_buffer_v2 {
	/*
	 * used to track if previous network is the same and already loaded,
	 * we can save a dma
	 */
	uint32_t network_id;
	/*
	 * size of header + first chunk ACO buffer -
	 * this saves a dma by dmaing both header and first chunk
	 */
	uint32_t buf_size;
	/*
	 * SMMU 32-bit mapped address that the DMA engine can read -
	 * uses lower 32 bits
	 */
	uint32_t address;
	/*
	 * number of layers in the network
	 */
	uint32_t num_layers;
};

/*
 * ACO Patch Parameters
 */
struct npu_patch_tuple {
	uint32_t value;
	uint32_t chunk_id;
	uint16_t instruction_size_in_bytes;
	uint16_t variable_size_in_bits;
	uint16_t shift_value_in_bits;
	uint32_t loc_offset;
};

/*
 * ACO Patch Tuple V2
 */
struct npu_patch_tuple_v2 {
	uint32_t value;
	uint32_t chunk_id;
	uint32_t instruction_size_in_bytes;
	uint32_t variable_size_in_bits;
	uint32_t shift_value_in_bits;
	uint32_t loc_offset;
};

struct npu_patch_params {
	uint32_t num_params;
	struct npu_patch_tuple param[2];
};

/*
 * LOAD command packet definition
 */
struct ipc_cmd_load_pkt {
	struct ipc_cmd_header_pkt header;
	struct npu_aco_buffer buf_pkt;
};

/*
 * LOAD command packet V2 definition
 */
struct ipc_cmd_load_pkt_v2 {
	struct ipc_cmd_header_pkt header;
	struct npu_aco_buffer_v2 buf_pkt;
	uint32_t num_patch_params;
	struct npu_patch_tuple_v2 patch_params[];
};

/*
 * UNLOAD command packet definition
 */
struct ipc_cmd_unload_pkt {
	struct ipc_cmd_header_pkt header;
	uint32_t network_hdl;
};

/*
 * Execute packet definition
 */
struct ipc_cmd_execute_pkt {
	struct ipc_cmd_header_pkt header;
	struct npu_patch_params patch_params;
	uint32_t network_hdl;
};

struct npu_patch_params_v2 {
	uint32_t value;
	uint32_t id;
};

/*
 * Execute packet V2 definition
 */
struct ipc_cmd_execute_pkt_v2 {
	struct ipc_cmd_header_pkt header;
	uint32_t network_hdl;
	uint32_t num_patch_params;
	struct npu_patch_params_v2 patch_params[];
};

/*
 * Loopback packet definition
 */
struct ipc_cmd_loopback_pkt {
	struct ipc_cmd_header_pkt header;
	uint32_t loopbackParams;
};

/*
 * Generic property definition
 */
struct ipc_cmd_prop_pkt {
	struct ipc_cmd_header_pkt header;
	uint32_t prop_id;
	uint32_t num_params;
	uint32_t network_hdl;
	uint32_t prop_param[];
};

/*
 * Generic property response packet definition
 */
struct ipc_msg_prop_pkt {
	struct ipc_msg_header_pkt header;
	uint32_t prop_id;
	uint32_t num_params;
	uint32_t network_hdl;
	uint32_t prop_param[];
};

/*
 * Generic notify message packet definition
 */
struct ipc_msg_general_notify_pkt {
	struct ipc_msg_header_pkt header;
	uint32_t notify_id;
	uint32_t num_params;
	uint32_t network_hdl;
	uint32_t notify_param[];
};


/*
 * LOAD response packet definition
 */
struct ipc_msg_load_pkt {
	struct ipc_msg_header_pkt header;
	uint32_t network_hdl;
};

/*
 * UNLOAD response packet definition
 */
struct ipc_msg_unload_pkt {
	struct ipc_msg_header_pkt header;
	uint32_t network_hdl;
};

/*
 * Layer Stats information returned back during EXECUTE_DONE response
 */
struct ipc_layer_stats {
	/*
	 * hardware tick count per layer
	 */
	uint32_t tick_count;
};

struct ipc_execute_layer_stats {
	/*
	 * total number of layers associated with the execution
	 */
	uint32_t total_num_layers;
	/*
	 * pointer to each layer stats
	 */
	struct ipc_layer_stats
		layer_stats_list[NUM_LAYER_STATS_PER_EXE_MSG_MAX];
};

struct ipc_execute_stats {
	/*
	 * total e2e IPC tick count during EXECUTE cmd
	 */
	uint32_t e2e_ipc_tick_count;
	/*
	 * tick count on ACO loading
	 */
	uint32_t aco_load_tick_count;
	/*
	 * tick count on ACO execution
	 */
	uint32_t aco_execution_tick_count;
	/*
	 * individual layer stats
	 */
	struct ipc_execute_layer_stats exe_stats;
};

/*
 * EXECUTE response packet definition
 */
struct ipc_msg_execute_pkt {
	struct ipc_msg_header_pkt header;
	struct ipc_execute_stats stats;
	uint32_t network_hdl;
};

/*
 * EXECUTE V2 response packet definition
 */
struct ipc_msg_execute_pkt_v2 {
	struct ipc_msg_header_pkt header;
	uint32_t network_hdl;
	uint32_t stats_data[];
};

/*
 * LOOPBACK response packet definition
 */
struct ipc_msg_loopback_pkt {
	struct ipc_msg_header_pkt header;
	uint32_t loopbackParams;
};

/* Logging Related */

/*
 * ipc_log_state_t - Logging state
 */
struct ipc_log_state {
	uint32_t module_msk;
	uint32_t level_msk;
};

struct ipc_cmd_log_state_pkt {
	struct ipc_cmd_header_pkt header;
	struct ipc_log_state log_state;
};

struct ipc_msg_log_state_pkt {
	struct ipc_msg_header_pkt header;
	struct ipc_log_state log_state;
};

/*
 * Logging message
 * This is a message from the NPU that contains the
 * logging message.  The values of part1-4 are not exposed
 * the receiver has to refer to the logging implementation to
 * intrepret what these mean and how to parse
 */
struct ipc_msg_log_pkt {
	struct ipc_msg_header_pkt header;
	uint32_t log_msg[NPU_LOG_MSG_MAX_SIZE];
};

/* Performance Related */

/*
 * Set counter mask of which counters we want
 * This is a message from HOST->NPU Firmware
 */
struct ipc_cmd_set_performance_query {
	struct ipc_cmd_header_pkt header;
	uint32_t cnt_msk;
};

/*
 * Set counter mask of which counters we want
 * This is a message from HOST->NPU Firmware
 */
struct ipc_msg_performance_counters {
	struct ipc_cmd_header_pkt header;
	uint32_t layer_id;
	uint32_t num_tulpes;
	/* Array of tuples [HEADER,value] */
	uint32_t cnt_tulpes[];
};

/*
 * ipc_cmd_shutdown - Shutdown command
 */
struct ipc_cmd_shutdown_pkt {
	struct ipc_cmd_header_pkt header;
	uint32_t shutdown_flags;
};

#endif /* NPU_HOST_IPC_H */
