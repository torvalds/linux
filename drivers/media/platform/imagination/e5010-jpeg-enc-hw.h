/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Imagination E5010 JPEG Encoder driver.
 *
 * Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Author: David Huang <d-huang@ti.com>
 * Author: Devarsh Thakkar <devarsht@ti.com>
 */

#ifndef _E5010_JPEG_ENC_HW_H
#define _E5010_JPEG_ENC_HW_H

#include "e5010-core-regs.h"
#include "e5010-mmu-regs.h"

int e5010_hw_enable_output_address_error_irq(void __iomem *core_offset, u32 enable);
int e5010_hw_enable_picture_done_irq(void __iomem *core_offset, u32 enable);
int e5010_hw_enable_auto_clock_gating(void __iomem *core_offset, u32 enable);
int e5010_hw_enable_manual_clock_gating(void __iomem *core_offset, u32 enable);
int e5010_hw_enable_crc_check(void __iomem *core_offset, u32 enable);
int e5010_hw_set_input_source_to_memory(void __iomem *core_offset, u32 set);
int e5010_hw_set_input_luma_addr(void __iomem *core_offset, u32 val);
int e5010_hw_set_input_chroma_addr(void __iomem *core_offset, u32 val);
int e5010_hw_set_output_base_addr(void __iomem *core_offset, u32 val);
int e5010_hw_get_output_size(void __iomem *core_offset);
int e5010_hw_set_horizontal_size(void __iomem *core_offset, u32 val);
int e5010_hw_set_vertical_size(void __iomem *core_offset, u32 val);
int e5010_hw_set_luma_stride(void __iomem *core_offset, u32 bytesperline);
int e5010_hw_set_chroma_stride(void __iomem *core_offset, u32 bytesperline);
int e5010_hw_set_input_subsampling(void __iomem *core_offset, u32 val);
int e5010_hw_set_chroma_order(void __iomem *core_offset, u32 val);
int e5010_hw_set_qpvalue(void __iomem *core_offset, u32 offset, u32 value);
void e5010_reset(struct device *dev, void __iomem *core_offset, void __iomem *mmu_offset);
void e5010_hw_set_output_max_size(void __iomem *core_offset, u32 val);
void e5010_hw_clear_picture_done(void __iomem *core_offset, u32 clear);
void e5010_hw_encode_start(void __iomem *core_offset, u32 start);
void e5010_hw_clear_output_error(void __iomem *core_offset, u32 clear);
void e5010_hw_bypass_mmu(void __iomem *mmu_base, u32 enable);
bool e5010_hw_pic_done_irq(void __iomem *core_base);
bool e5010_hw_output_address_irq(void __iomem *core_base);
#endif
