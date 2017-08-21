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

#ifndef HINIC_HW_DEV_H
#define HINIC_HW_DEV_H

#include <linux/pci.h>

#include "hinic_hw_if.h"

#define HINIC_MAX_QPS   32

struct hinic_hwdev {
	struct hinic_hwif               *hwif;
	struct msix_entry               *msix_entries;
};

struct hinic_pfhwdev {
	struct hinic_hwdev              hwdev;

	/* PF Extended components should be here */
};

struct hinic_hwdev *hinic_init_hwdev(struct pci_dev *pdev);

void hinic_free_hwdev(struct hinic_hwdev *hwdev);

int hinic_hwdev_num_qps(struct hinic_hwdev *hwdev);

#endif
