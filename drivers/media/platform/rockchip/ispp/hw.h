/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_HW_H
#define _RKISPP_HW_H

#include "common.h"
#include "fec.h"
#include "../isp/isp_ispp.h"

#define ISPP_MAX_BUS_CLK 4

struct ispp_clk_info {
	u32 clk_rate;
	u32 refer_data;
};

struct ispp_match_data {
	int clks_num;
	const char * const *clks;
	int clk_rate_tbl_num;
	const struct ispp_clk_info *clk_rate_tbl;
	enum rkispp_ver ispp_ver;
	struct irqs_data *irqs;
	int num_irqs;
};

struct rkispp_hw_dev {
	struct device *dev;
	void __iomem *base_addr;
	const struct ispp_match_data *match_data;
	const struct ispp_clk_info *clk_rate_tbl;
	struct reset_control *reset;
	struct clk *clks[ISPP_MAX_BUS_CLK];
	struct rkispp_device *ispp[DEV_MAX];
	struct rkispp_isp_buf_pool pool[RKISPP_BUF_POOL_MAX];
	struct rkispp_dummy_buffer dummy_buf;
	struct rkispp_fec_dev fec_dev;
	struct max_input max_in;
	struct list_head list;
	int clk_rate_tbl_num;
	int clks_num;
	int dev_num;
	int cur_dev_id;
	unsigned long core_clk_min;
	unsigned long core_clk_max;
	enum rkispp_ver	ispp_ver;
	/* lock for irq */
	spinlock_t irq_lock;
	/* lock for multi dev */
	struct mutex dev_lock;
	spinlock_t buf_lock;
	atomic_t refcnt;
	const struct vb2_mem_ops *mem_ops;
	struct rkisp_ispp_reg *reg_buf;
	u32 first_frame_dma;
	bool is_mmu;
	bool is_idle;
	bool is_single;
	bool is_fec_ext;
	bool is_dma_contig;
	bool is_shutdown;
	bool is_first;
};

void rkispp_soft_reset(struct rkispp_hw_dev *hw_dev);
#endif
