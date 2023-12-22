// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2023 Intel Corporation. */
#include <linux/export.h>
#include <linux/kernel.h>

#include "adf_gen4_tl.h"
#include "adf_telemetry.h"
#include "adf_tl_debugfs.h"

#define ADF_GEN4_TL_DEV_REG_OFF(reg) ADF_TL_DEV_REG_OFF(reg, gen4)

#define ADF_GEN4_TL_SL_UTIL_COUNTER(_name)	\
	ADF_TL_COUNTER("util_" #_name,		\
			ADF_TL_SIMPLE_COUNT,	\
			ADF_TL_SLICE_REG_OFF(_name, reg_tm_slice_util, gen4))

#define ADF_GEN4_TL_SL_EXEC_COUNTER(_name)	\
	ADF_TL_COUNTER("exec_" #_name,		\
			ADF_TL_SIMPLE_COUNT,	\
			ADF_TL_SLICE_REG_OFF(_name, reg_tm_slice_exec_cnt, gen4))

/* Device level counters. */
static const struct adf_tl_dbg_counter dev_counters[] = {
	/* PCIe partial transactions. */
	ADF_TL_COUNTER(PCI_TRANS_CNT_NAME, ADF_TL_SIMPLE_COUNT,
		       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_pci_trans_cnt)),
	/* Max read latency[ns]. */
	ADF_TL_COUNTER(MAX_RD_LAT_NAME, ADF_TL_COUNTER_NS,
		       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_rd_lat_max)),
	/* Read latency average[ns]. */
	ADF_TL_COUNTER_LATENCY(RD_LAT_ACC_NAME, ADF_TL_COUNTER_NS_AVG,
			       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_rd_lat_acc),
			       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_rd_cmpl_cnt)),
	/* Max get to put latency[ns]. */
	ADF_TL_COUNTER(MAX_LAT_NAME, ADF_TL_COUNTER_NS,
		       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_gp_lat_max)),
	/* Get to put latency average[ns]. */
	ADF_TL_COUNTER_LATENCY(LAT_ACC_NAME, ADF_TL_COUNTER_NS_AVG,
			       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_gp_lat_acc),
			       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_ae_put_cnt)),
	/* PCIe write bandwidth[Mbps]. */
	ADF_TL_COUNTER(BW_IN_NAME, ADF_TL_COUNTER_MBPS,
		       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_bw_in)),
	/* PCIe read bandwidth[Mbps]. */
	ADF_TL_COUNTER(BW_OUT_NAME, ADF_TL_COUNTER_MBPS,
		       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_bw_out)),
	/* Page request latency average[ns]. */
	ADF_TL_COUNTER_LATENCY(PAGE_REQ_LAT_NAME, ADF_TL_COUNTER_NS_AVG,
			       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_at_page_req_lat_acc),
			       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_at_page_req_cnt)),
	/* Page translation latency average[ns]. */
	ADF_TL_COUNTER_LATENCY(AT_TRANS_LAT_NAME, ADF_TL_COUNTER_NS_AVG,
			       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_at_trans_lat_acc),
			       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_at_trans_lat_cnt)),
	/* Maximum uTLB used. */
	ADF_TL_COUNTER(AT_MAX_UTLB_USED_NAME, ADF_TL_SIMPLE_COUNT,
		       ADF_GEN4_TL_DEV_REG_OFF(reg_tl_at_max_tlb_used)),
};

/* Slice utilization counters. */
static const struct adf_tl_dbg_counter sl_util_counters[ADF_TL_SL_CNT_COUNT] = {
	/* Compression slice utilization. */
	ADF_GEN4_TL_SL_UTIL_COUNTER(cpr),
	/* Translator slice utilization. */
	ADF_GEN4_TL_SL_UTIL_COUNTER(xlt),
	/* Decompression slice utilization. */
	ADF_GEN4_TL_SL_UTIL_COUNTER(dcpr),
	/* PKE utilization. */
	ADF_GEN4_TL_SL_UTIL_COUNTER(pke),
	/* Wireless Authentication slice utilization. */
	ADF_GEN4_TL_SL_UTIL_COUNTER(wat),
	/* Wireless Cipher slice utilization. */
	ADF_GEN4_TL_SL_UTIL_COUNTER(wcp),
	/* UCS slice utilization. */
	ADF_GEN4_TL_SL_UTIL_COUNTER(ucs),
	/* Cipher slice utilization. */
	ADF_GEN4_TL_SL_UTIL_COUNTER(cph),
	/* Authentication slice utilization. */
	ADF_GEN4_TL_SL_UTIL_COUNTER(ath),
};

/* Slice execution counters. */
static const struct adf_tl_dbg_counter sl_exec_counters[ADF_TL_SL_CNT_COUNT] = {
	/* Compression slice execution count. */
	ADF_GEN4_TL_SL_EXEC_COUNTER(cpr),
	/* Translator slice execution count. */
	ADF_GEN4_TL_SL_EXEC_COUNTER(xlt),
	/* Decompression slice execution count. */
	ADF_GEN4_TL_SL_EXEC_COUNTER(dcpr),
	/* PKE execution count. */
	ADF_GEN4_TL_SL_EXEC_COUNTER(pke),
	/* Wireless Authentication slice execution count. */
	ADF_GEN4_TL_SL_EXEC_COUNTER(wat),
	/* Wireless Cipher slice execution count. */
	ADF_GEN4_TL_SL_EXEC_COUNTER(wcp),
	/* UCS slice execution count. */
	ADF_GEN4_TL_SL_EXEC_COUNTER(ucs),
	/* Cipher slice execution count. */
	ADF_GEN4_TL_SL_EXEC_COUNTER(cph),
	/* Authentication slice execution count. */
	ADF_GEN4_TL_SL_EXEC_COUNTER(ath),
};

void adf_gen4_init_tl_data(struct adf_tl_hw_data *tl_data)
{
	tl_data->layout_sz = ADF_GEN4_TL_LAYOUT_SZ;
	tl_data->slice_reg_sz = ADF_GEN4_TL_SLICE_REG_SZ;
	tl_data->num_hbuff = ADF_GEN4_TL_NUM_HIST_BUFFS;
	tl_data->msg_cnt_off = ADF_GEN4_TL_MSG_CNT_OFF;
	tl_data->cpp_ns_per_cycle = ADF_GEN4_CPP_NS_PER_CYCLE;
	tl_data->bw_units_to_bytes = ADF_GEN4_TL_BW_HW_UNITS_TO_BYTES;

	tl_data->dev_counters = dev_counters;
	tl_data->num_dev_counters = ARRAY_SIZE(dev_counters);
	tl_data->sl_util_counters = sl_util_counters;
	tl_data->sl_exec_counters = sl_exec_counters;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_tl_data);
