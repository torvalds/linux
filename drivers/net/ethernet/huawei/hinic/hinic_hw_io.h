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

#ifndef HINIC_HW_IO_H
#define HINIC_HW_IO_H

#include <linux/types.h>
#include <linux/pci.h>

#include "hinic_hw_if.h"
#include "hinic_hw_qp.h"

struct hinic_func_to_io {
	struct hinic_hwif       *hwif;

	struct hinic_qp         *qps;
	u16                     max_qps;
};

int hinic_io_create_qps(struct hinic_func_to_io *func_to_io,
			u16 base_qpn, int num_qps,
			struct msix_entry *sq_msix_entries,
			struct msix_entry *rq_msix_entries);

void hinic_io_destroy_qps(struct hinic_func_to_io *func_to_io,
			  int num_qps);

int hinic_io_init(struct hinic_func_to_io *func_to_io,
		  struct hinic_hwif *hwif, u16 max_qps, int num_ceqs,
		  struct msix_entry *ceq_msix_entries);

void hinic_io_free(struct hinic_func_to_io *func_to_io);

#endif
