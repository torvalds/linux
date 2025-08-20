// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/io.h>

#include "hinic3_common.h"
#include "hinic3_csr.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"

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

void hinic3_set_msix_state(struct hinic3_hwdev *hwdev, u16 msix_idx,
			   enum hinic3_msix_state flag)
{
	/* Completed by later submission due to LoC limit. */
}

void hinic3_msix_intr_clear_resend_bit(struct hinic3_hwdev *hwdev, u16 msix_idx,
				       u8 clear_resend_en)
{
	/* Completed by later submission due to LoC limit. */
}

u16 hinic3_global_func_id(struct hinic3_hwdev *hwdev)
{
	return hwdev->hwif->attr.func_global_idx;
}
