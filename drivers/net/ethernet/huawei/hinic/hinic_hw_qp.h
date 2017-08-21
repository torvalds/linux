/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#ifndef HINIC_HW_QP_H
#define HINIC_HW_QP_H

#include <linux/types.h>
#include <linux/sizes.h>

#define HINIC_SQ_WQEBB_SIZE                     64
#define HINIC_RQ_WQEBB_SIZE                     32

#define HINIC_SQ_PAGE_SIZE                      SZ_4K
#define HINIC_RQ_PAGE_SIZE                      SZ_4K

#define HINIC_SQ_DEPTH                          SZ_4K
#define HINIC_RQ_DEPTH                          SZ_4K

struct hinic_sq {
	/* should be implemented */
};

struct hinic_rq {
	/* should be implemented */
};

struct hinic_qp {
	struct hinic_sq         sq;
	struct hinic_rq         rq;

	u16     q_id;
};

#endif
