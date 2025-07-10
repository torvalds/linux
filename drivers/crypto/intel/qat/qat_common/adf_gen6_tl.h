/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2025 Intel Corporation. */
#ifndef ADF_GEN6_TL_H
#define ADF_GEN6_TL_H

#include <linux/types.h>

struct adf_tl_hw_data;

/* Computation constants. */
#define ADF_GEN6_CPP_NS_PER_CYCLE		2
#define ADF_GEN6_TL_BW_HW_UNITS_TO_BYTES	64

/* Maximum aggregation time. Value is in milliseconds. */
#define ADF_GEN6_TL_MAX_AGGR_TIME_MS		4000
/* Number of buffers to store historic values. */
#define ADF_GEN6_TL_NUM_HIST_BUFFS \
	(ADF_GEN6_TL_MAX_AGGR_TIME_MS / ADF_TL_DATA_WR_INTERVAL_MS)

/* Max number of HW resources of one type */
#define ADF_GEN6_TL_MAX_SLICES_PER_TYPE		32
#define MAX_ATH_SL_COUNT			7
#define MAX_CNV_SL_COUNT			2
#define MAX_DCPRZ_SL_COUNT			2
#define MAX_PKE_SL_COUNT			32
#define MAX_UCS_SL_COUNT			4
#define MAX_WAT_SL_COUNT			5
#define MAX_WCP_SL_COUNT			5

#define MAX_ATH_CMDQ_COUNT			14
#define MAX_CNV_CMDQ_COUNT			6
#define MAX_DCPRZ_CMDQ_COUNT			6
#define MAX_PKE_CMDQ_COUNT			32
#define MAX_UCS_CMDQ_COUNT			12
#define MAX_WAT_CMDQ_COUNT			35
#define MAX_WCP_CMDQ_COUNT			35

/* Max number of simultaneously monitored ring pairs. */
#define ADF_GEN6_TL_MAX_RP_NUM			4

/**
 * struct adf_gen6_tl_slice_data_regs - HW slice data as populated by FW.
 * @reg_tm_slice_exec_cnt: Slice execution count.
 * @reg_tm_slice_util: Slice utilization.
 */
struct adf_gen6_tl_slice_data_regs {
	__u32 reg_tm_slice_exec_cnt;
	__u32 reg_tm_slice_util;
};

#define ADF_GEN6_TL_SLICE_REG_SZ sizeof(struct adf_gen6_tl_slice_data_regs)

/**
 * struct adf_gen6_tl_cmdq_data_regs - HW CMDQ data as populated by FW.
 * @reg_tm_cmdq_wait_cnt: CMDQ wait count.
 * @reg_tm_cmdq_exec_cnt: CMDQ execution count.
 * @reg_tm_cmdq_drain_cnt: CMDQ drain count.
 */
struct adf_gen6_tl_cmdq_data_regs {
	__u32 reg_tm_cmdq_wait_cnt;
	__u32 reg_tm_cmdq_exec_cnt;
	__u32 reg_tm_cmdq_drain_cnt;
	__u32 reserved;
};

#define ADF_GEN6_TL_CMDQ_REG_SZ sizeof(struct adf_gen6_tl_cmdq_data_regs)

/**
 * struct adf_gen6_tl_device_data_regs - This structure stores device telemetry
 * counter values as are being populated periodically by device.
 * @reg_tl_rd_lat_acc: read latency accumulator
 * @reg_tl_gp_lat_acc: "get to put" latency accumulator
 * @reg_tl_at_page_req_lat_acc: AT/DevTLB page request latency accumulator
 * @reg_tl_at_trans_lat_acc: DevTLB transaction latency accumulator
 * @reg_tl_re_acc: accumulated ring empty time
 * @reg_tl_prt_trans_cnt: PCIe partial transactions
 * @reg_tl_rd_lat_max: maximum logged read latency
 * @reg_tl_rd_cmpl_cnt: read requests completed count
 * @reg_tl_gp_lat_max: maximum logged get to put latency
 * @reg_tl_ae_put_cnt: Accelerator Engine put counts across all rings
 * @reg_tl_bw_in: PCIe write bandwidth
 * @reg_tl_bw_out: PCIe read bandwidth
 * @reg_tl_at_page_req_cnt: DevTLB page requests count
 * @reg_tl_at_trans_lat_cnt: DevTLB transaction latency samples count
 * @reg_tl_at_max_utlb_used: maximum uTLB used
 * @reg_tl_re_cnt: ring empty time samples count
 * @reserved: reserved
 * @ath_slices: array of Authentication slices utilization registers
 * @cnv_slices: array of Compression slices utilization registers
 * @dcprz_slices: array of Decompression slices utilization registers
 * @pke_slices: array of PKE slices utilization registers
 * @ucs_slices: array of UCS slices utilization registers
 * @wat_slices: array of Wireless Authentication slices utilization registers
 * @wcp_slices: array of Wireless Cipher slices utilization registers
 * @ath_cmdq: array of Authentication cmdq telemetry registers
 * @cnv_cmdq: array of Compression cmdq telemetry registers
 * @dcprz_cmdq: array of Decomopression cmdq telemetry registers
 * @pke_cmdq: array of PKE cmdq telemetry registers
 * @ucs_cmdq: array of UCS cmdq telemetry registers
 * @wat_cmdq: array of Wireless Authentication cmdq telemetry registers
 * @wcp_cmdq: array of Wireless Cipher cmdq telemetry registers
 */
struct adf_gen6_tl_device_data_regs {
	__u64 reg_tl_rd_lat_acc;
	__u64 reg_tl_gp_lat_acc;
	__u64 reg_tl_at_page_req_lat_acc;
	__u64 reg_tl_at_trans_lat_acc;
	__u64 reg_tl_re_acc;
	__u32 reg_tl_prt_trans_cnt;
	__u32 reg_tl_rd_lat_max;
	__u32 reg_tl_rd_cmpl_cnt;
	__u32 reg_tl_gp_lat_max;
	__u32 reg_tl_ae_put_cnt;
	__u32 reg_tl_bw_in;
	__u32 reg_tl_bw_out;
	__u32 reg_tl_at_page_req_cnt;
	__u32 reg_tl_at_trans_lat_cnt;
	__u32 reg_tl_at_max_utlb_used;
	__u32 reg_tl_re_cnt;
	__u32 reserved;
	struct adf_gen6_tl_slice_data_regs ath_slices[MAX_ATH_SL_COUNT];
	struct adf_gen6_tl_slice_data_regs cnv_slices[MAX_CNV_SL_COUNT];
	struct adf_gen6_tl_slice_data_regs dcprz_slices[MAX_DCPRZ_SL_COUNT];
	struct adf_gen6_tl_slice_data_regs pke_slices[MAX_PKE_SL_COUNT];
	struct adf_gen6_tl_slice_data_regs ucs_slices[MAX_UCS_SL_COUNT];
	struct adf_gen6_tl_slice_data_regs wat_slices[MAX_WAT_SL_COUNT];
	struct adf_gen6_tl_slice_data_regs wcp_slices[MAX_WCP_SL_COUNT];
	struct adf_gen6_tl_cmdq_data_regs ath_cmdq[MAX_ATH_CMDQ_COUNT];
	struct adf_gen6_tl_cmdq_data_regs cnv_cmdq[MAX_CNV_CMDQ_COUNT];
	struct adf_gen6_tl_cmdq_data_regs dcprz_cmdq[MAX_DCPRZ_CMDQ_COUNT];
	struct adf_gen6_tl_cmdq_data_regs pke_cmdq[MAX_PKE_CMDQ_COUNT];
	struct adf_gen6_tl_cmdq_data_regs ucs_cmdq[MAX_UCS_CMDQ_COUNT];
	struct adf_gen6_tl_cmdq_data_regs wat_cmdq[MAX_WAT_CMDQ_COUNT];
	struct adf_gen6_tl_cmdq_data_regs wcp_cmdq[MAX_WCP_CMDQ_COUNT];
};

/**
 * struct adf_gen6_tl_ring_pair_data_regs - This structure stores ring pair
 * telemetry counter values as they are being populated periodically by device.
 * @reg_tl_gp_lat_acc: get-put latency accumulator
 * @reg_tl_re_acc: accumulated ring empty time
 * @reg_tl_pci_trans_cnt: PCIe partial transactions
 * @reg_tl_ae_put_cnt: Accelerator Engine put counts across all rings
 * @reg_tl_bw_in: PCIe write bandwidth
 * @reg_tl_bw_out: PCIe read bandwidth
 * @reg_tl_at_glob_devtlb_hit: Message descriptor DevTLB hit rate
 * @reg_tl_at_glob_devtlb_miss: Message descriptor DevTLB miss rate
 * @reg_tl_at_payld_devtlb_hit: Payload DevTLB hit rate
 * @reg_tl_at_payld_devtlb_miss: Payload DevTLB miss rate
 * @reg_tl_re_cnt: ring empty time samples count
 * @reserved1: reserved
 */
struct adf_gen6_tl_ring_pair_data_regs {
	__u64 reg_tl_gp_lat_acc;
	__u64 reg_tl_re_acc;
	__u32 reg_tl_prt_trans_cnt;
	__u32 reg_tl_ae_put_cnt;
	__u32 reg_tl_bw_in;
	__u32 reg_tl_bw_out;
	__u32 reg_tl_at_glob_devtlb_hit;
	__u32 reg_tl_at_glob_devtlb_miss;
	__u32 reg_tl_at_payld_devtlb_hit;
	__u32 reg_tl_at_payld_devtlb_miss;
	__u32 reg_tl_re_cnt;
	__u32 reserved1;
};

#define ADF_GEN6_TL_RP_REG_SZ sizeof(struct adf_gen6_tl_ring_pair_data_regs)

/**
 * struct adf_gen6_tl_layout - This structure represents the entire telemetry
 * counters data: Device + 4 Ring Pairs as they are being populated periodically
 * by device.
 * @tl_device_data_regs: structure of device telemetry registers
 * @tl_ring_pairs_data_regs: array of ring pairs telemetry registers
 * @reg_tl_msg_cnt: telemetry message counter
 * @reserved: reserved
 */
struct adf_gen6_tl_layout {
	struct adf_gen6_tl_device_data_regs tl_device_data_regs;
	struct adf_gen6_tl_ring_pair_data_regs
		tl_ring_pairs_data_regs[ADF_GEN6_TL_MAX_RP_NUM];
	__u32 reg_tl_msg_cnt;
	__u32 reserved;
};

#define ADF_GEN6_TL_LAYOUT_SZ sizeof(struct adf_gen6_tl_layout)
#define ADF_GEN6_TL_MSG_CNT_OFF \
	offsetof(struct adf_gen6_tl_layout, reg_tl_msg_cnt)

#ifdef CONFIG_DEBUG_FS
void adf_gen6_init_tl_data(struct adf_tl_hw_data *tl_data);
#else
static inline void adf_gen6_init_tl_data(struct adf_tl_hw_data *tl_data)
{
}
#endif /* CONFIG_DEBUG_FS */
#endif /* ADF_GEN6_TL_H */
