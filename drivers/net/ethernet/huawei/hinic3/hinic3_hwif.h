/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_HWIF_H_
#define _HINIC3_HWIF_H_

#include <linux/build_bug.h>
#include <linux/spinlock_types.h>

struct hinic3_hwdev;

enum hinic3_func_type {
	HINIC3_FUNC_TYPE_VF = 1,
};

struct hinic3_db_area {
	unsigned long *db_bitmap_array;
	u32           db_max_areas;
	/* protect doorbell area alloc and free */
	spinlock_t    idx_lock;
};

struct hinic3_func_attr {
	enum hinic3_func_type func_type;
	u16                   func_global_idx;
	u16                   global_vf_id_of_pf;
	u16                   num_irqs;
	u16                   num_sq;
	u8                    port_to_port_idx;
	u8                    pci_intf_idx;
	u8                    ppf_idx;
	u8                    num_aeqs;
	u8                    num_ceqs;
	u8                    msix_flex_en;
};

static_assert(sizeof(struct hinic3_func_attr) == 20);

struct hinic3_hwif {
	u8 __iomem              *cfg_regs_base;
	u64                     db_base_phy;
	u64                     db_dwqe_len;
	u8 __iomem              *db_base;
	struct hinic3_db_area   db_area;
	struct hinic3_func_attr attr;
};

enum hinic3_msix_state {
	HINIC3_MSIX_ENABLE,
	HINIC3_MSIX_DISABLE,
};

void hinic3_set_msix_state(struct hinic3_hwdev *hwdev, u16 msix_idx,
			   enum hinic3_msix_state flag);

u16 hinic3_global_func_id(struct hinic3_hwdev *hwdev);

#endif
