/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2023 Intel Corporation. */
#ifndef ADF_TL_DEBUGFS_H
#define ADF_TL_DEBUGFS_H

#include <linux/types.h>

struct adf_accel_dev;

#define MAX_COUNT_NAME_SIZE	32
#define SNAPSHOT_CNT_MSG	"sample_cnt"
#define RP_NUM_INDEX		"rp_num"
#define PCI_TRANS_CNT_NAME	"pci_trans_cnt"
#define MAX_RD_LAT_NAME		"max_rd_lat"
#define RD_LAT_ACC_NAME		"rd_lat_acc_avg"
#define MAX_LAT_NAME		"max_gp_lat"
#define LAT_ACC_NAME		"gp_lat_acc_avg"
#define BW_IN_NAME		"bw_in"
#define BW_OUT_NAME		"bw_out"
#define RE_ACC_NAME		"re_acc_avg"
#define PAGE_REQ_LAT_NAME	"at_page_req_lat_avg"
#define AT_TRANS_LAT_NAME	"at_trans_lat_avg"
#define AT_MAX_UTLB_USED_NAME	"at_max_tlb_used"
#define AT_GLOB_DTLB_HIT_NAME	"at_glob_devtlb_hit"
#define AT_GLOB_DTLB_MISS_NAME	"at_glob_devtlb_miss"
#define AT_PAYLD_DTLB_HIT_NAME	"tl_at_payld_devtlb_hit"
#define AT_PAYLD_DTLB_MISS_NAME	"tl_at_payld_devtlb_miss"
#define RP_SERVICE_TYPE		"service_type"

#define ADF_TL_DBG_RP_ALPHA_INDEX(index) ((index) + 'A')
#define ADF_TL_DBG_RP_INDEX_ALPHA(alpha) ((alpha) - 'A')

#define ADF_TL_RP_REGS_FNAME		"rp_%c_data"
#define ADF_TL_RP_REGS_FNAME_SIZE		16

#define ADF_TL_DATA_REG_OFF(reg, qat_gen)	\
	offsetof(struct adf_##qat_gen##_tl_layout, reg)

#define ADF_TL_DEV_REG_OFF(reg, qat_gen)			\
	(ADF_TL_DATA_REG_OFF(tl_device_data_regs, qat_gen) +	\
	offsetof(struct adf_##qat_gen##_tl_device_data_regs, reg))

#define ADF_TL_SLICE_REG_OFF(slice, reg, qat_gen)		\
	(ADF_TL_DEV_REG_OFF(slice##_slices[0], qat_gen) +	\
	offsetof(struct adf_##qat_gen##_tl_slice_data_regs, reg))

#define ADF_TL_CMDQ_REG_OFF(slice, reg, qat_gen)        \
	(ADF_TL_DEV_REG_OFF(slice##_cmdq[0], qat_gen) + \
	 offsetof(struct adf_##qat_gen##_tl_cmdq_data_regs, reg))

#define ADF_TL_RP_REG_OFF(reg, qat_gen)					\
	(ADF_TL_DATA_REG_OFF(tl_ring_pairs_data_regs[0], qat_gen) +	\
	offsetof(struct adf_##qat_gen##_tl_ring_pair_data_regs, reg))

/**
 * enum adf_tl_counter_type - telemetry counter types
 * @ADF_TL_COUNTER_UNSUPPORTED: unsupported counter
 * @ADF_TL_SIMPLE_COUNT: simple counter
 * @ADF_TL_COUNTER_NS: latency counter, value in ns
 * @ADF_TL_COUNTER_NS_AVG: accumulated average latency counter, value in ns
 * @ADF_TL_COUNTER_MBPS: bandwidth, value in MBps
 */
enum adf_tl_counter_type {
	ADF_TL_COUNTER_UNSUPPORTED,
	ADF_TL_SIMPLE_COUNT,
	ADF_TL_COUNTER_NS,
	ADF_TL_COUNTER_NS_AVG,
	ADF_TL_COUNTER_MBPS,
};

/**
 * struct adf_tl_dbg_counter - telemetry counter definition
 * @name: name of the counter as printed in the report
 * @adf_tl_counter_type: type of the counter
 * @offset1: offset of 1st register
 * @offset2: offset of 2nd optional register
 */
struct adf_tl_dbg_counter {
	const char *name;
	enum adf_tl_counter_type type;
	size_t offset1;
	size_t offset2;
};

#define ADF_TL_COUNTER(_name, _type, _offset)	\
{	.name =		_name,			\
	.type =		_type,			\
	.offset1 =	_offset			\
}

#define ADF_TL_COUNTER_LATENCY(_name, _type, _offset1, _offset2)	\
{	.name =		_name,						\
	.type =		_type,						\
	.offset1 =	_offset1,					\
	.offset2 =	_offset2					\
}

/* Telemetry counter aggregated values. */
struct adf_tl_dbg_aggr_values {
	u64 curr;
	u64 min;
	u64 max;
	u64 avg;
};

/**
 * adf_tl_dbgfs_add() - Add telemetry's debug fs entries.
 * @accel_dev: Pointer to acceleration device.
 *
 * Creates telemetry's debug fs folder and attributes in QAT debug fs root.
 */
void adf_tl_dbgfs_add(struct adf_accel_dev *accel_dev);

/**
 * adf_tl_dbgfs_rm() - Remove telemetry's debug fs entries.
 * @accel_dev: Pointer to acceleration device.
 *
 * Removes telemetry's debug fs folder and attributes from QAT debug fs root.
 */
void adf_tl_dbgfs_rm(struct adf_accel_dev *accel_dev);

#endif /* ADF_TL_DEBUGFS_H */
