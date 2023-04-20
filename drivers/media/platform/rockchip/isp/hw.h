/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_HW_H
#define _RKISP_HW_H

#include "bridge.h"

#define RKISP_MAX_BUS_CLK 10
#define RKISP_MAX_RETRY_CNT 5

struct isp_clk_info {
	u32 clk_rate;
	u32 refer_data;
};

struct isp_match_data {
	const char * const *clks;
	int num_clks;
	enum rkisp_isp_ver isp_ver;
	const struct isp_clk_info  *clk_rate_tbl;
	int num_clk_rate_tbl;
	struct isp_irqs_data *irqs;
	int num_irqs;
	bool unite;
};

struct rkisp_monitor {
	struct rkisp_hw_dev *dev;
	struct work_struct work;
	struct completion cmpl;
	int (*reset_handle)(struct rkisp_device *dev);
	u32 state;
	u8 retry;
	bool is_en;
};

struct rkisp_sram {
	dma_addr_t dma_addr;
	u32 size;
};

struct rkisp_size_info {
	u32 w;
	u32 h;
	u32 size;
	u32 fps;
	bool is_on;
};

struct rkisp_hw_dev {
	const struct isp_match_data *match_data;
	struct platform_device *pdev;
	struct device *dev;
	struct regmap *grf;
	void __iomem *base_addr;
	void __iomem *base_next_addr;
	struct clk *clks[RKISP_MAX_BUS_CLK];
	int num_clks;
	const struct isp_clk_info *clk_rate_tbl;
	int num_clk_rate_tbl;
	struct reset_control *reset;
	int mipi_irq;
	enum rkisp_isp_ver isp_ver;
	struct rkisp_device *isp[DEV_MAX];
	struct rkisp_size_info isp_size[DEV_MAX];
	int dev_num;
	int dev_link_num;
	int cur_dev_id;
	int pre_dev_id;
	int mipi_dev_id;
	struct max_input max_in;
	/* lock for multi dev */
	struct mutex dev_lock;
	spinlock_t rdbk_lock;
	atomic_t refcnt;

	struct rkisp_sram sram;

	/* share buf for multi dev */
	spinlock_t buf_lock;
	struct rkisp_bridge_buf bufs[BRIDGE_BUF_MAX];
	struct rkisp_ispp_buf *cur_buf;
	struct rkisp_ispp_buf *nxt_buf;
	struct list_head list;
	struct list_head rpt_list;
	struct rkisp_dummy_buffer dummy_buf;
	const struct vb2_mem_ops *mem_ops;
	struct rkisp_monitor monitor;
	u64 iq_feature;
	int buf_init_cnt;
	u32 unite;
	bool is_feature_on;
	bool is_dma_contig;
	bool is_dma_sg_ops;
	bool is_mmu;
	bool is_idle;
	bool is_single;
	bool is_mi_update;
	bool is_thunderboot;
	bool is_buf_init;
	bool is_shutdown;
	bool is_multi_overflow;
	bool is_runing;
	bool is_frm_buf;
	bool is_dvfs;
	bool is_assigned_clk;
};

int rkisp_register_irq(struct rkisp_hw_dev *dev);
void rkisp_soft_reset(struct rkisp_hw_dev *dev, bool is_secure);
void rkisp_hw_enum_isp_size(struct rkisp_hw_dev *hw_dev);
#endif
