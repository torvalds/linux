/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2023 Intel Corporation. */
#ifndef ADF_TELEMETRY_H
#define ADF_TELEMETRY_H

#include <linux/bits.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "icp_qat_fw_init_admin.h"

struct adf_accel_dev;
struct adf_tl_dbg_counter;
struct dentry;

#define ADF_TL_SL_CNT_COUNT		\
	(sizeof(struct icp_qat_fw_init_admin_slice_cnt) / sizeof(__u8))

#define TL_CAPABILITY_BIT		BIT(1)
/* Interval within device writes data to DMA region. Value in milliseconds. */
#define ADF_TL_DATA_WR_INTERVAL_MS	1000
/* Interval within timer interrupt should be handled. Value in milliseconds. */
#define ADF_TL_TIMER_INT_MS		(ADF_TL_DATA_WR_INTERVAL_MS / 2)

#define ADF_TL_RP_REGS_DISABLED		(0xff)

struct adf_tl_hw_data {
	size_t layout_sz;
	size_t slice_reg_sz;
	size_t rp_reg_sz;
	size_t msg_cnt_off;
	const struct adf_tl_dbg_counter *dev_counters;
	const struct adf_tl_dbg_counter *sl_util_counters;
	const struct adf_tl_dbg_counter *sl_exec_counters;
	const struct adf_tl_dbg_counter *rp_counters;
	u8 num_hbuff;
	u8 cpp_ns_per_cycle;
	u8 bw_units_to_bytes;
	u8 num_dev_counters;
	u8 num_rp_counters;
	u8 max_rp;
	u8 max_sl_cnt;
};

struct adf_telemetry {
	struct adf_accel_dev *accel_dev;
	atomic_t state;
	u32 hbuffs;
	int hb_num;
	u32 msg_cnt;
	dma_addr_t regs_data_p; /* bus address for DMA mapping */
	void *regs_data; /* virtual address for DMA mapping */
	/**
	 * @regs_hist_buff: array of pointers to copies of the last @hbuffs
	 * values of @regs_data
	 */
	void **regs_hist_buff;
	struct dentry *dbg_dir;
	u8 *rp_num_indexes;
	/**
	 * @regs_hist_lock: protects from race conditions between write and read
	 * to the copies referenced by @regs_hist_buff
	 */
	struct mutex regs_hist_lock;
	/**
	 * @wr_lock: protects from concurrent writes to debugfs telemetry files
	 */
	struct mutex wr_lock;
	struct delayed_work work_ctx;
	struct icp_qat_fw_init_admin_slice_cnt slice_cnt;
};

#ifdef CONFIG_DEBUG_FS
int adf_tl_init(struct adf_accel_dev *accel_dev);
int adf_tl_start(struct adf_accel_dev *accel_dev);
void adf_tl_stop(struct adf_accel_dev *accel_dev);
void adf_tl_shutdown(struct adf_accel_dev *accel_dev);
int adf_tl_run(struct adf_accel_dev *accel_dev, int state);
int adf_tl_halt(struct adf_accel_dev *accel_dev);
#else
static inline int adf_tl_init(struct adf_accel_dev *accel_dev)
{
	return 0;
}

static inline int adf_tl_start(struct adf_accel_dev *accel_dev)
{
	return 0;
}

static inline void adf_tl_stop(struct adf_accel_dev *accel_dev)
{
}

static inline void adf_tl_shutdown(struct adf_accel_dev *accel_dev)
{
}
#endif /* CONFIG_DEBUG_FS */
#endif /* ADF_TELEMETRY_H */
