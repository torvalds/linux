/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_HW_H
#define _RKISPP_HW_H

#include "../isp/isp_ispp.h"

#define ISPP_MAX_BUS_CLK 4

struct rkispp_hw_dev {
	struct device *dev;
	void __iomem *base_addr;
	struct clk *clks[ISPP_MAX_BUS_CLK];
	struct rkispp_device *ispp[DEV_MAX];
	struct rkispp_isp_buf_pool pool[RKISPP_BUF_POOL_MAX];
	struct max_input max_in;
	struct list_head list;
	int clks_num;
	int dev_num;
	int cur_dev_id;
	enum rkispp_ver	ispp_ver;
	/* lock for irq */
	spinlock_t irq_lock;
	/* lock for multi dev */
	struct mutex dev_lock;
	spinlock_t buf_lock;
	atomic_t refcnt;
	atomic_t power_cnt;
	bool is_idle;
	bool is_single;
};
#endif
