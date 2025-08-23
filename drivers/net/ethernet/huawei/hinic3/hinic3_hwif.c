// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/io.h>

#include "hinic3_common.h"
#include "hinic3_csr.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"

/* config BAR4/5 4MB, DB & DWQE both 2MB */
#define HINIC3_DB_DWQE_SIZE    0x00400000

/* db/dwqe page size: 4K */
#define HINIC3_DB_PAGE_SIZE    0x00001000
#define HINIC3_DWQE_OFFSET     0x00000800
#define HINIC3_DB_MAX_AREAS    (HINIC3_DB_DWQE_SIZE / HINIC3_DB_PAGE_SIZE)

#define HINIC3_GET_REG_ADDR(reg)  ((reg) & (HINIC3_REGS_FLAG_MASK))

static void __iomem *hinic3_reg_addr(struct hinic3_hwif *hwif, u32 reg)
{
	return hwif->cfg_regs_base + HINIC3_GET_REG_ADDR(reg);
}

u32 hinic3_hwif_read_reg(struct hinic3_hwif *hwif, u32 reg)
{
	void __iomem *addr = hinic3_reg_addr(hwif, reg);

	return ioread32be(addr);
}

void hinic3_hwif_write_reg(struct hinic3_hwif *hwif, u32 reg, u32 val)
{
	void __iomem *addr = hinic3_reg_addr(hwif, reg);

	iowrite32be(val, addr);
}

static int get_db_idx(struct hinic3_hwif *hwif, u32 *idx)
{
	struct hinic3_db_area *db_area = &hwif->db_area;
	u32 pg_idx;

	spin_lock(&db_area->idx_lock);
	pg_idx = find_first_zero_bit(db_area->db_bitmap_array,
				     db_area->db_max_areas);
	if (pg_idx == db_area->db_max_areas) {
		spin_unlock(&db_area->idx_lock);
		return -ENOMEM;
	}
	set_bit(pg_idx, db_area->db_bitmap_array);
	spin_unlock(&db_area->idx_lock);

	*idx = pg_idx;

	return 0;
}

static void free_db_idx(struct hinic3_hwif *hwif, u32 idx)
{
	struct hinic3_db_area *db_area = &hwif->db_area;

	spin_lock(&db_area->idx_lock);
	clear_bit(idx, db_area->db_bitmap_array);
	spin_unlock(&db_area->idx_lock);
}

void hinic3_free_db_addr(struct hinic3_hwdev *hwdev, const u8 __iomem *db_base)
{
	struct hinic3_hwif *hwif;
	uintptr_t distance;
	u32 idx;

	hwif = hwdev->hwif;
	distance = db_base - hwif->db_base;
	idx = distance / HINIC3_DB_PAGE_SIZE;

	free_db_idx(hwif, idx);
}

int hinic3_alloc_db_addr(struct hinic3_hwdev *hwdev, void __iomem **db_base,
			 void __iomem **dwqe_base)
{
	struct hinic3_hwif *hwif;
	u8 __iomem *addr;
	u32 idx;
	int err;

	hwif = hwdev->hwif;

	err = get_db_idx(hwif, &idx);
	if (err)
		return err;

	addr = hwif->db_base + idx * HINIC3_DB_PAGE_SIZE;
	*db_base = addr;

	if (dwqe_base)
		*dwqe_base = addr + HINIC3_DWQE_OFFSET;

	return 0;
}

void hinic3_set_msix_state(struct hinic3_hwdev *hwdev, u16 msix_idx,
			   enum hinic3_msix_state flag)
{
	struct hinic3_hwif *hwif;
	u8 int_msk = 1;
	u32 mask_bits;
	u32 addr;

	hwif = hwdev->hwif;

	if (flag)
		mask_bits = HINIC3_MSI_CLR_INDIR_SET(int_msk, INT_MSK_SET);
	else
		mask_bits = HINIC3_MSI_CLR_INDIR_SET(int_msk, INT_MSK_CLR);
	mask_bits = mask_bits |
		    HINIC3_MSI_CLR_INDIR_SET(msix_idx, SIMPLE_INDIR_IDX);

	addr = HINIC3_CSR_FUNC_MSI_CLR_WR_ADDR;
	hinic3_hwif_write_reg(hwif, addr, mask_bits);
}

void hinic3_msix_intr_clear_resend_bit(struct hinic3_hwdev *hwdev, u16 msix_idx,
				       u8 clear_resend_en)
{
	struct hinic3_hwif *hwif;
	u32 msix_ctrl, addr;

	hwif = hwdev->hwif;

	msix_ctrl = HINIC3_MSI_CLR_INDIR_SET(msix_idx, SIMPLE_INDIR_IDX) |
		    HINIC3_MSI_CLR_INDIR_SET(clear_resend_en, RESEND_TIMER_CLR);

	addr = HINIC3_CSR_FUNC_MSI_CLR_WR_ADDR;
	hinic3_hwif_write_reg(hwif, addr, msix_ctrl);
}

void hinic3_set_msix_auto_mask_state(struct hinic3_hwdev *hwdev, u16 msix_idx,
				     enum hinic3_msix_auto_mask flag)
{
	struct hinic3_hwif *hwif;
	u32 mask_bits;
	u32 addr;

	hwif = hwdev->hwif;

	if (flag)
		mask_bits = HINIC3_MSI_CLR_INDIR_SET(1, AUTO_MSK_SET);
	else
		mask_bits = HINIC3_MSI_CLR_INDIR_SET(1, AUTO_MSK_CLR);

	mask_bits = mask_bits |
		    HINIC3_MSI_CLR_INDIR_SET(msix_idx, SIMPLE_INDIR_IDX);

	addr = HINIC3_CSR_FUNC_MSI_CLR_WR_ADDR;
	hinic3_hwif_write_reg(hwif, addr, mask_bits);
}

u16 hinic3_global_func_id(struct hinic3_hwdev *hwdev)
{
	return hwdev->hwif->attr.func_global_idx;
}
