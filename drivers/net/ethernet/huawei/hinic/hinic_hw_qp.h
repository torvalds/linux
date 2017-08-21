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

struct hinic_sq {
	/* should be implemented */
};

struct hinic_rq {
	/* should be implemented */
};

struct hinic_qp {
	struct hinic_sq         sq;
	struct hinic_rq         rq;
};

#endif
