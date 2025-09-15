// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Intel Corporation. */
#include <linux/export.h>

#include "adf_gen6_tl.h"
#include "adf_telemetry.h"
#include "adf_tl_debugfs.h"
#include "icp_qat_fw_init_admin.h"

#define ADF_GEN6_TL_DEV_REG_OFF(reg) ADF_TL_DEV_REG_OFF(reg, gen6)

#define ADF_GEN6_TL_RP_REG_OFF(reg) ADF_TL_RP_REG_OFF(reg, gen6)

#define ADF_GEN6_TL_SL_UTIL_COUNTER(_name)			\
	ADF_TL_COUNTER("util_" #_name, ADF_TL_SIMPLE_COUNT,	\
			ADF_TL_SLICE_REG_OFF(_name, reg_tm_slice_util, gen6))

#define ADF_GEN6_TL_SL_EXEC_COUNTER(_name)			\
	ADF_TL_COUNTER("exec_" #_name, ADF_TL_SIMPLE_COUNT,	\
			ADF_TL_SLICE_REG_OFF(_name, reg_tm_slice_exec_cnt, gen6))

#define SLICE_IDX(sl) offsetof(struct icp_qat_fw_init_admin_slice_cnt, sl##_cnt)

/* Device level counters. */
static const struct adf_tl_dbg_counter dev_counters[] = {
	/* PCIe partial transactions. */
	ADF_TL_COUNTER(PCI_TRANS_CNT_NAME, ADF_TL_SIMPLE_COUNT,
		       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_prt_trans_cnt)),
	/* Max read latency[ns]. */
	ADF_TL_COUNTER(MAX_RD_LAT_NAME, ADF_TL_COUNTER_NS,
		       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_rd_lat_max)),
	/* Read latency average[ns]. */
	ADF_TL_COUNTER_LATENCY(RD_LAT_ACC_NAME, ADF_TL_COUNTER_NS_AVG,
			       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_rd_lat_acc),
			       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_rd_cmpl_cnt)),
	/* Max "get to put" latency[ns]. */
	ADF_TL_COUNTER(MAX_LAT_NAME, ADF_TL_COUNTER_NS,
		       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_gp_lat_max)),
	/* "Get to put" latency average[ns]. */
	ADF_TL_COUNTER_LATENCY(LAT_ACC_NAME, ADF_TL_COUNTER_NS_AVG,
			       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_gp_lat_acc),
			       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_ae_put_cnt)),
	/* PCIe write bandwidth[Mbps]. */
	ADF_TL_COUNTER(BW_IN_NAME, ADF_TL_COUNTER_MBPS,
		       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_bw_in)),
	/* PCIe read bandwidth[Mbps]. */
	ADF_TL_COUNTER(BW_OUT_NAME, ADF_TL_COUNTER_MBPS,
		       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_bw_out)),
	/* Page request latency average[ns]. */
	ADF_TL_COUNTER_LATENCY(PAGE_REQ_LAT_NAME, ADF_TL_COUNTER_NS_AVG,
			       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_at_page_req_lat_acc),
			       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_at_page_req_cnt)),
	/* Page translation latency average[ns]. */
	ADF_TL_COUNTER_LATENCY(AT_TRANS_LAT_NAME, ADF_TL_COUNTER_NS_AVG,
			       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_at_trans_lat_acc),
			       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_at_trans_lat_cnt)),
	/* Maximum uTLB used. */
	ADF_TL_COUNTER(AT_MAX_UTLB_USED_NAME, ADF_TL_SIMPLE_COUNT,
		       ADF_GEN6_TL_DEV_REG_OFF(reg_tl_at_max_utlb_used)),
};

/* Accelerator utilization counters */
static const struct adf_tl_dbg_counter sl_util_counters[ADF_TL_SL_CNT_COUNT] = {
	/* Compression accelerator utilization. */
	[SLICE_IDX(cpr)] = ADF_GEN6_TL_SL_UTIL_COUNTER(cnv),
	/* Decompression accelerator utilization. */
	[SLICE_IDX(dcpr)] = ADF_GEN6_TL_SL_UTIL_COUNTER(dcprz),
	/* PKE accelerator utilization. */
	[SLICE_IDX(pke)] = ADF_GEN6_TL_SL_UTIL_COUNTER(pke),
	/* Wireless Authentication accelerator utilization. */
	[SLICE_IDX(wat)] = ADF_GEN6_TL_SL_UTIL_COUNTER(wat),
	/* Wireless Cipher accelerator utilization. */
	[SLICE_IDX(wcp)] = ADF_GEN6_TL_SL_UTIL_COUNTER(wcp),
	/* UCS accelerator utilization. */
	[SLICE_IDX(ucs)] = ADF_GEN6_TL_SL_UTIL_COUNTER(ucs),
	/* Authentication accelerator utilization. */
	[SLICE_IDX(ath)] = ADF_GEN6_TL_SL_UTIL_COUNTER(ath),
};

/* Accelerator execution counters */
static const struct adf_tl_dbg_counter sl_exec_counters[ADF_TL_SL_CNT_COUNT] = {
	/* Compression accelerator execution count. */
	[SLICE_IDX(cpr)] = ADF_GEN6_TL_SL_EXEC_COUNTER(cnv),
	/* Decompression accelerator execution count. */
	[SLICE_IDX(dcpr)] = ADF_GEN6_TL_SL_EXEC_COUNTER(dcprz),
	/* PKE execution count. */
	[SLICE_IDX(pke)] = ADF_GEN6_TL_SL_EXEC_COUNTER(pke),
	/* Wireless Authentication accelerator execution count. */
	[SLICE_IDX(wat)] = ADF_GEN6_TL_SL_EXEC_COUNTER(wat),
	/* Wireless Cipher accelerator execution count. */
	[SLICE_IDX(wcp)] = ADF_GEN6_TL_SL_EXEC_COUNTER(wcp),
	/* UCS accelerator execution count. */
	[SLICE_IDX(ucs)] = ADF_GEN6_TL_SL_EXEC_COUNTER(ucs),
	/* Authentication accelerator execution count. */
	[SLICE_IDX(ath)] = ADF_GEN6_TL_SL_EXEC_COUNTER(ath),
};

/* Ring pair counters. */
static const struct adf_tl_dbg_counter rp_counters[] = {
	/* PCIe partial transactions. */
	ADF_TL_COUNTER(PCI_TRANS_CNT_NAME, ADF_TL_SIMPLE_COUNT,
		       ADF_GEN6_TL_RP_REG_OFF(reg_tl_prt_trans_cnt)),
	/* "Get to put" latency average[ns]. */
	ADF_TL_COUNTER_LATENCY(LAT_ACC_NAME, ADF_TL_COUNTER_NS_AVG,
			       ADF_GEN6_TL_RP_REG_OFF(reg_tl_gp_lat_acc),
			       ADF_GEN6_TL_RP_REG_OFF(reg_tl_ae_put_cnt)),
	/* PCIe write bandwidth[Mbps]. */
	ADF_TL_COUNTER(BW_IN_NAME, ADF_TL_COUNTER_MBPS,
		       ADF_GEN6_TL_RP_REG_OFF(reg_tl_bw_in)),
	/* PCIe read bandwidth[Mbps]. */
	ADF_TL_COUNTER(BW_OUT_NAME, ADF_TL_COUNTER_MBPS,
		       ADF_GEN6_TL_RP_REG_OFF(reg_tl_bw_out)),
	/* Message descriptor DevTLB hit rate. */
	ADF_TL_COUNTER(AT_GLOB_DTLB_HIT_NAME, ADF_TL_SIMPLE_COUNT,
		       ADF_GEN6_TL_RP_REG_OFF(reg_tl_at_glob_devtlb_hit)),
	/* Message descriptor DevTLB miss rate. */
	ADF_TL_COUNTER(AT_GLOB_DTLB_MISS_NAME, ADF_TL_SIMPLE_COUNT,
		       ADF_GEN6_TL_RP_REG_OFF(reg_tl_at_glob_devtlb_miss)),
	/* Payload DevTLB hit rate. */
	ADF_TL_COUNTER(AT_PAYLD_DTLB_HIT_NAME, ADF_TL_SIMPLE_COUNT,
		       ADF_GEN6_TL_RP_REG_OFF(reg_tl_at_payld_devtlb_hit)),
	/* Payload DevTLB miss rate. */
	ADF_TL_COUNTER(AT_PAYLD_DTLB_MISS_NAME, ADF_TL_SIMPLE_COUNT,
		       ADF_GEN6_TL_RP_REG_OFF(reg_tl_at_payld_devtlb_miss)),
};

void adf_gen6_init_tl_data(struct adf_tl_hw_data *tl_data)
{
	tl_data->layout_sz = ADF_GEN6_TL_LAYOUT_SZ;
	tl_data->slice_reg_sz = ADF_GEN6_TL_SLICE_REG_SZ;
	tl_data->rp_reg_sz = ADF_GEN6_TL_RP_REG_SZ;
	tl_data->num_hbuff = ADF_GEN6_TL_NUM_HIST_BUFFS;
	tl_data->max_rp = ADF_GEN6_TL_MAX_RP_NUM;
	tl_data->msg_cnt_off = ADF_GEN6_TL_MSG_CNT_OFF;
	tl_data->cpp_ns_per_cycle = ADF_GEN6_CPP_NS_PER_CYCLE;
	tl_data->bw_units_to_bytes = ADF_GEN6_TL_BW_HW_UNITS_TO_BYTES;

	tl_data->dev_counters = dev_counters;
	tl_data->num_dev_counters = ARRAY_SIZE(dev_counters);
	tl_data->sl_util_counters = sl_util_counters;
	tl_data->sl_exec_counters = sl_exec_counters;
	tl_data->rp_counters = rp_counters;
	tl_data->num_rp_counters = ARRAY_SIZE(rp_counters);
	tl_data->max_sl_cnt = ADF_GEN6_TL_MAX_SLICES_PER_TYPE;
}
EXPORT_SYMBOL_GPL(adf_gen6_init_tl_data);
