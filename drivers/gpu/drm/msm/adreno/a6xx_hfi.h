/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2017 The Linux Foundation. All rights reserved. */

#ifndef _A6XX_HFI_H_
#define _A6XX_HFI_H_

#define HFI_MAX_QUEUES 3

struct a6xx_hfi_queue_table_header {
	u32 version;
	u32 size;		/* Size of the queue table in dwords */
	u32 qhdr0_offset;	/* Offset of the first queue header */
	u32 qhdr_size;		/* Size of the queue headers */
	u32 num_queues;		/* Number of total queues */
	u32 active_queues;	/* Number of active queues */
} __packed;

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
} __packed;

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
} __packed;

#define HFI_F2H_MSG_ERROR 100

struct a6xx_hfi_msg_error {
	u32 header;
	u32 code;
	u32 payload[2];
} __packed;

#define HFI_H2F_MSG_INIT 0

struct a6xx_hfi_msg_gmu_init_cmd {
	u32 header;
	u32 seg_id;
	u32 dbg_buffer_addr;
	u32 dbg_buffer_size;
	u32 boot_state;
} __packed;

#define HFI_H2F_MSG_FW_VERSION 1

struct a6xx_hfi_msg_fw_version {
	u32 header;
	u32 supported_version;
} __packed;

#define HFI_H2F_MSG_PERF_TABLE 4

struct perf_level {
	u32 vote;
	u32 freq;
} __packed;

struct perf_gx_level {
	u32 vote;
	u32 acd;
	u32 freq;
} __packed;

struct a6xx_hfi_msg_perf_table_v1 {
	u32 header;
	u32 num_gpu_levels;
	u32 num_gmu_levels;

	struct perf_level gx_votes[16];
	struct perf_level cx_votes[4];
} __packed;

struct a6xx_hfi_msg_perf_table {
	u32 header;
	u32 num_gpu_levels;
	u32 num_gmu_levels;

	struct perf_gx_level gx_votes[16];
	struct perf_level cx_votes[4];
} __packed;

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
} __packed;

#define HFI_H2F_MSG_TEST 5

struct a6xx_hfi_msg_test {
	u32 header;
} __packed;

#define HFI_H2F_MSG_ACD 7
#define MAX_ACD_STRIDE 2

struct a6xx_hfi_acd_table {
	u32 header;
	u32 version;
	u32 enable_by_level;
	u32 stride;
	u32 num_levels;
	u32 data[16 * MAX_ACD_STRIDE];
} __packed;

#define CLX_DATA(irated, num_phases, clx_path, extd_intf) \
	((extd_intf << 29) |				  \
	 (clx_path << 28) |				  \
	 (num_phases << 22) |				  \
	 (irated << 16))

struct a6xx_hfi_clx_domain_v2 {
	/**
	 * @data: BITS[0:15]  Migration time
	 *        BITS[16:21] Current rating
	 *        BITS[22:27] Phases for domain
	 *        BITS[28:28] Path notification
	 *        BITS[29:31] Extra features
	 */
	u32 data;
	/** @clxt: CLX time in microseconds */
	u32 clxt;
	/** @clxh: CLH time in microseconds */
	u32 clxh;
	/** @urg_mode: Urgent HW throttle mode of operation */
	u32 urg_mode;
	/** @lkg_en: Enable leakage current estimate */
	u32 lkg_en;
	/** curr_budget: Current Budget */
	u32 curr_budget;
} __packed;

#define HFI_H2F_MSG_CLX_TBL 8

#define MAX_CLX_DOMAINS 2
struct a6xx_hfi_clx_table_v2_cmd {
	u32 hdr;
	u32 version;
	struct a6xx_hfi_clx_domain_v2 domain[MAX_CLX_DOMAINS];
} __packed;

#define HFI_H2F_MSG_START 10

struct a6xx_hfi_msg_start {
	u32 header;
} __packed;

#define HFI_H2F_FEATURE_CTRL 11

struct a6xx_hfi_msg_feature_ctrl {
	u32 header;
	u32 feature;
#define HFI_FEATURE_DCVS		0
#define HFI_FEATURE_HWSCHED		1
#define HFI_FEATURE_PREEMPTION		2
#define HFI_FEATURE_CLOCKS_ON		3
#define HFI_FEATURE_BUS_ON		4
#define HFI_FEATURE_RAIL_ON		5
#define HFI_FEATURE_HWCG		6
#define HFI_FEATURE_LM			7
#define HFI_FEATURE_THROTTLE		8
#define HFI_FEATURE_IFPC		9
#define HFI_FEATURE_NAP			10
#define HFI_FEATURE_BCL			11
#define HFI_FEATURE_ACD			12
#define HFI_FEATURE_DIDT		13
#define HFI_FEATURE_DEPRECATED		14
#define HFI_FEATURE_CB			15
#define HFI_FEATURE_KPROF		16
#define HFI_FEATURE_BAIL_OUT_TIMER	17
#define HFI_FEATURE_GMU_STATS		18
#define HFI_FEATURE_DBQ			19
#define HFI_FEATURE_MINBW		20
#define HFI_FEATURE_CLX			21
#define HFI_FEATURE_LSR			23
#define HFI_FEATURE_LPAC		24
#define HFI_FEATURE_HW_FENCE		25
#define HFI_FEATURE_PERF_NORETAIN	26
#define HFI_FEATURE_DMS			27
#define HFI_FEATURE_THERMAL		28
#define HFI_FEATURE_AQE			29
#define HFI_FEATURE_TDCVS		30
#define HFI_FEATURE_DCE			31
#define HFI_FEATURE_IFF_PCLX		32
#define HFI_FEATURE_SOFT_RESET		0x10000001
#define HFI_FEATURE_DCVS_PROFILE	0x10000002
#define HFI_FEATURE_FAST_CTX_DESTROY	0x10000003
	u32 enable;
	u32 data;
} __packed;

#define HFI_H2F_MSG_CORE_FW_START 14

struct a6xx_hfi_msg_core_fw_start {
	u32 header;
	u32 handle;
} __packed;

#define HFI_H2F_MSG_TABLE 15

struct a6xx_hfi_table_entry {
	u32 count;
	u32 stride;
	u32 data[];
} __packed;

struct a6xx_hfi_table {
	u32 header;
	u32 version;
	u32 type;
#define HFI_TABLE_BW_VOTE	0
#define HFI_TABLE_GPU_PERF	1
#define HFI_TABLE_DIDT		2
#define HFI_TABLE_ACD		3
#define HFI_TABLE_CLX_V1	4 /* Unused */
#define HFI_TABLE_CLX_V2	5
#define HFI_TABLE_THERM		6
#define HFI_TABLE_DCVS		7
#define HFI_TABLE_SYS_TIME	8
#define HFI_TABLE_GMU_DCVS	9
#define HFI_TABLE_LIMITS_MIT	10
	struct a6xx_hfi_table_entry entry[];
} __packed;

#define HFI_H2F_MSG_GX_BW_PERF_VOTE 30

struct a6xx_hfi_gx_bw_perf_vote_cmd {
	u32 header;
	u32 ack_type;
	u32 freq;
	u32 bw;
} __packed;

#define AB_VOTE_MASK		GENMASK(31, 16)
#define MAX_AB_VOTE		(FIELD_MAX(AB_VOTE_MASK) - 1)
#define AB_VOTE(vote)		FIELD_PREP(AB_VOTE_MASK, (vote))
#define AB_VOTE_ENABLE		BIT(8)

#define HFI_H2F_MSG_PREPARE_SLUMBER 33

struct a6xx_hfi_prep_slumber_cmd {
	u32 header;
	u32 bw;
	u32 freq;
} __packed;

struct a6xx_hfi_limits_cfg {
	u32 enable;
	u32 msg_path;
	u32 lkg_en;
	/*
	 * BIT[0]: 0 = (static) throttle to fixed sid level
	 *         1 = (dynamic) throttle to sid level calculated by HW
	 * BIT[1]: 0 = Mx
	 *         1 = Bx
	 */
	u32 mode;
	u32 sid;
	/* Mitigation time in microseconds */
	u32 mit_time;
	/* Max current in mA during mitigation */
	u32 curr_limit;
} __packed;

struct a6xx_hfi_limits_tbl {
	u8 feature_id;
#define GMU_MIT_IFF  0
#define GMU_MIT_PCLX 1
	u8 domain;
#define GMU_GX_DOMAIN 0
#define GMU_MX_DOMAIN 1
	u16 feature_rev;
	struct a6xx_hfi_limits_cfg cfg;
} __packed;

#endif
