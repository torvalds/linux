/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2017 The Linux Foundation. All rights reserved. */

#ifndef _A6XX_HFI_H_
#define _A6XX_HFI_H_

struct a6xx_hfi_queue_table_header {
	u32 version;
	u32 size;		/* Size of the queue table in dwords */
	u32 qhdr0_offset;	/* Offset of the first queue header */
	u32 qhdr_size;		/* Size of the queue headers */
	u32 num_queues;		/* Number of total queues */
	u32 active_queues;	/* Number of active queues */
};

struct a6xx_hfi_queue_header {
	u32 status;
	u32 iova;
	u32 type;
	u32 size;
	u32 msg_size;
	u32 dropped;
	u32 rx_watermark;
	u32 tx_watermark;
	u32 rx_request;
	u32 tx_request;
	u32 read_index;
	u32 write_index;
};

struct a6xx_hfi_queue {
	struct a6xx_hfi_queue_header *header;
	spinlock_t lock;
	u32 *data;
	atomic_t seqnum;

	/*
	 * Tracking for the start index of the last N messages in the
	 * queue, for the benefit of devcore dump / crashdec (since
	 * parsing in the reverse direction to decode the last N
	 * messages is difficult to do and would rely on heuristics
	 * which are not guaranteed to be correct)
	 */
#define HFI_HISTORY_SZ 8
	s32 history[HFI_HISTORY_SZ];
	u8  history_idx;
};

/* This is the outgoing queue to the GMU */
#define HFI_COMMAND_QUEUE 0

/* THis is the incoming response queue from the GMU */
#define HFI_RESPONSE_QUEUE 1

#define HFI_HEADER_ID(msg) ((msg) & 0xff)
#define HFI_HEADER_SIZE(msg) (((msg) >> 8) & 0xff)
#define HFI_HEADER_SEQNUM(msg) (((msg) >> 20) & 0xfff)

/* FIXME: Do we need this or can we use ARRAY_SIZE? */
#define HFI_RESPONSE_PAYLOAD_SIZE 16

/* HFI message types */

#define HFI_MSG_CMD 0
#define HFI_MSG_ACK 1
#define HFI_MSG_ACK_V1 2

#define HFI_F2H_MSG_ACK 126

struct a6xx_hfi_msg_response {
	u32 header;
	u32 ret_header;
	u32 error;
	u32 payload[HFI_RESPONSE_PAYLOAD_SIZE];
};

#define HFI_F2H_MSG_ERROR 100

struct a6xx_hfi_msg_error {
	u32 header;
	u32 code;
	u32 payload[2];
};

#define HFI_H2F_MSG_INIT 0

struct a6xx_hfi_msg_gmu_init_cmd {
	u32 header;
	u32 seg_id;
	u32 dbg_buffer_addr;
	u32 dbg_buffer_size;
	u32 boot_state;
};

#define HFI_H2F_MSG_FW_VERSION 1

struct a6xx_hfi_msg_fw_version {
	u32 header;
	u32 supported_version;
};

#define HFI_H2F_MSG_PERF_TABLE 4

struct perf_level {
	u32 vote;
	u32 freq;
};

struct perf_gx_level {
	u32 vote;
	u32 acd;
	u32 freq;
};

struct a6xx_hfi_msg_perf_table_v1 {
	u32 header;
	u32 num_gpu_levels;
	u32 num_gmu_levels;

	struct perf_level gx_votes[16];
	struct perf_level cx_votes[4];
};

struct a6xx_hfi_msg_perf_table {
	u32 header;
	u32 num_gpu_levels;
	u32 num_gmu_levels;

	struct perf_gx_level gx_votes[16];
	struct perf_level cx_votes[4];
};

#define HFI_H2F_MSG_BW_TABLE 3

struct a6xx_hfi_msg_bw_table {
	u32 header;
	u32 bw_level_num;
	u32 cnoc_cmds_num;
	u32 ddr_cmds_num;
	u32 cnoc_wait_bitmask;
	u32 ddr_wait_bitmask;
	u32 cnoc_cmds_addrs[6];
	u32 cnoc_cmds_data[2][6];
	u32 ddr_cmds_addrs[8];
	u32 ddr_cmds_data[16][8];
};

#define HFI_H2F_MSG_TEST 5

struct a6xx_hfi_msg_test {
	u32 header;
};

#define HFI_H2F_MSG_START 10

struct a6xx_hfi_msg_start {
	u32 header;
};

#define HFI_H2F_MSG_CORE_FW_START 14

struct a6xx_hfi_msg_core_fw_start {
	u32 header;
	u32 handle;
};

#define HFI_H2F_MSG_GX_BW_PERF_VOTE 30

struct a6xx_hfi_gx_bw_perf_vote_cmd {
	u32 header;
	u32 ack_type;
	u32 freq;
	u32 bw;
};

#define AB_VOTE_MASK		GENMASK(31, 16)
#define MAX_AB_VOTE		(FIELD_MAX(AB_VOTE_MASK) - 1)
#define AB_VOTE(vote)		FIELD_PREP(AB_VOTE_MASK, (vote))
#define AB_VOTE_ENABLE		BIT(8)

#define HFI_H2F_MSG_PREPARE_SLUMBER 33

struct a6xx_hfi_prep_slumber_cmd {
	u32 header;
	u32 bw;
	u32 freq;
};

#endif
