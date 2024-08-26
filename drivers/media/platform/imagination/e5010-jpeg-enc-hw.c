// SPDX-License-Identifier: GPL-2.0
/*
 * Imagination E5010 JPEG Encoder driver.
 *
 * Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Author: David Huang <d-huang@ti.com>
 * Author: Devarsh Thakkar <devarsht@ti.com>
 */

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/dev_printk.h>
#include "e5010-jpeg-enc-hw.h"

static void write_reg_field(void __iomem *base, unsigned int offset, u32 mask,
			    unsigned int shift, u32 value)
{
	u32 reg;

	value <<= shift;
	if (mask != 0xffffffff) {
		reg = readl(base + offset);
		value = (value & mask) | (reg & ~mask);
	}
	writel(value, (base + offset));
}

static int write_reg_field_not_busy(void __iomem *jasper_base, void __iomem *wr_base,
				    unsigned int offset, u32 mask, unsigned int shift,
				    u32 value)
{
	int ret;
	u32 val;

	ret = readl_poll_timeout_atomic(jasper_base + JASPER_STATUS_OFFSET, val,
					(val & JASPER_STATUS_CR_JASPER_BUSY_MASK) == 0,
					2000, 50000);
	if (ret)
		return ret;

	write_reg_field(wr_base, offset, mask, shift, value);

	return 0;
}

void e5010_reset(struct device *dev, void __iomem *core_base, void __iomem *mmu_base)
{
	int ret = 0;
	u32 val;

	write_reg_field(core_base, JASPER_RESET_OFFSET,
			JASPER_RESET_CR_CORE_RESET_MASK,
			JASPER_RESET_CR_CORE_RESET_SHIFT, 1);

	write_reg_field(mmu_base, MMU_MMU_CONTROL1_OFFSET,
			MMU_MMU_CONTROL1_MMU_SOFT_RESET_MASK,
			MMU_MMU_CONTROL1_MMU_SOFT_RESET_SHIFT, 1);

	ret = readl_poll_timeout_atomic(mmu_base + MMU_MMU_CONTROL1_OFFSET, val,
					(val & MMU_MMU_CONTROL1_MMU_SOFT_RESET_MASK) == 0,
					2000, 50000);
	if (ret)
		dev_warn(dev, "MMU soft reset timed out, forcing system soft reset\n");

	write_reg_field(core_base, JASPER_RESET_OFFSET,
			JASPER_RESET_CR_SYS_RESET_MASK,
			JASPER_RESET_CR_SYS_RESET_SHIFT, 1);
}

void e5010_hw_bypass_mmu(void __iomem *mmu_base, u32 enable)
{
	/* Bypass MMU */
	write_reg_field(mmu_base,
			MMU_MMU_ADDRESS_CONTROL_OFFSET,
			MMU_MMU_ADDRESS_CONTROL_MMU_BYPASS_MASK,
			MMU_MMU_ADDRESS_CONTROL_MMU_BYPASS_SHIFT,
			enable);
}

int e5010_hw_enable_output_address_error_irq(void __iomem *core_base, u32 enable)
{
	return write_reg_field_not_busy(core_base, core_base,
				       JASPER_INTERRUPT_MASK_OFFSET,
				       JASPER_INTERRUPT_MASK_CR_OUTPUT_ADDRESS_ERROR_ENABLE_MASK,
				       JASPER_INTERRUPT_MASK_CR_OUTPUT_ADDRESS_ERROR_ENABLE_SHIFT,
				       enable);
}

bool e5010_hw_pic_done_irq(void __iomem *core_base)
{
	u32 reg;

	reg = readl(core_base + JASPER_INTERRUPT_STATUS_OFFSET);
	return reg & JASPER_INTERRUPT_STATUS_CR_PICTURE_DONE_IRQ_MASK;
}

bool e5010_hw_output_address_irq(void __iomem *core_base)
{
	u32 reg;

	reg = readl(core_base + JASPER_INTERRUPT_STATUS_OFFSET);
	return reg & JASPER_INTERRUPT_STATUS_CR_OUTPUT_ADDRESS_ERROR_IRQ_MASK;
}

int e5010_hw_enable_picture_done_irq(void __iomem *core_base, u32 enable)
{
	return write_reg_field_not_busy(core_base, core_base,
				       JASPER_INTERRUPT_MASK_OFFSET,
				       JASPER_INTERRUPT_MASK_CR_PICTURE_DONE_ENABLE_MASK,
				       JASPER_INTERRUPT_MASK_CR_PICTURE_DONE_ENABLE_SHIFT,
				       enable);
}

int e5010_hw_enable_auto_clock_gating(void __iomem *core_base, u32 enable)
{
	return write_reg_field_not_busy(core_base, core_base,
				       JASPER_CLK_CONTROL_OFFSET,
				       JASPER_CLK_CONTROL_CR_JASPER_AUTO_CLKG_ENABLE_MASK,
				       JASPER_CLK_CONTROL_CR_JASPER_AUTO_CLKG_ENABLE_SHIFT,
				       enable);
}

int e5010_hw_enable_manual_clock_gating(void __iomem *core_base, u32 enable)
{
	return write_reg_field_not_busy(core_base, core_base,
				       JASPER_CLK_CONTROL_OFFSET,
				       JASPER_CLK_CONTROL_CR_JASPER_MAN_CLKG_ENABLE_MASK,
				       JASPER_CLK_CONTROL_CR_JASPER_MAN_CLKG_ENABLE_SHIFT, 0);
}

int e5010_hw_enable_crc_check(void __iomem *core_base, u32 enable)
{
	return write_reg_field_not_busy(core_base, core_base,
				       JASPER_CRC_CTRL_OFFSET,
				       JASPER_CRC_CTRL_JASPER_CRC_ENABLE_MASK,
				       JASPER_CRC_CTRL_JASPER_CRC_ENABLE_SHIFT, enable);
}

int e5010_hw_set_input_source_to_memory(void __iomem *core_base, u32 set)
{
	return write_reg_field_not_busy(core_base, core_base,
				       JASPER_INPUT_CTRL0_OFFSET,
				       JASPER_INPUT_CTRL0_CR_INPUT_SOURCE_MASK,
				       JASPER_INPUT_CTRL0_CR_INPUT_SOURCE_SHIFT, set);
}

int e5010_hw_set_input_luma_addr(void __iomem *core_base, u32 val)
{
	return  write_reg_field_not_busy(core_base, core_base,
				       INPUT_LUMA_BASE_OFFSET,
				       INPUT_LUMA_BASE_CR_INPUT_LUMA_BASE_MASK, 0, val);
}

int e5010_hw_set_input_chroma_addr(void __iomem *core_base, u32 val)
{
	return write_reg_field_not_busy(core_base, core_base,
				       INPUT_CHROMA_BASE_OFFSET,
				       INPUT_CHROMA_BASE_CR_INPUT_CHROMA_BASE_MASK, 0, val);
}

int e5010_hw_set_output_base_addr(void __iomem *core_base, u32 val)
{
	return write_reg_field_not_busy(core_base, core_base,
				       JASPER_OUTPUT_BASE_OFFSET,
				       JASPER_OUTPUT_BASE_CR_OUTPUT_BASE_MASK,
				       JASPER_OUTPUT_BASE_CR_OUTPUT_BASE_SHIFT, val);
}

int e5010_hw_set_horizontal_size(void __iomem *core_base, u32 val)
{
	return write_reg_field_not_busy(core_base, core_base,
				       JASPER_IMAGE_SIZE_OFFSET,
				       JASPER_IMAGE_SIZE_CR_IMAGE_HORIZONTAL_SIZE_MASK,
				       JASPER_IMAGE_SIZE_CR_IMAGE_HORIZONTAL_SIZE_SHIFT,
				       val);
}

int e5010_hw_set_vertical_size(void __iomem *core_base, u32 val)
{
	return write_reg_field_not_busy(core_base, core_base,
				       JASPER_IMAGE_SIZE_OFFSET,
				       JASPER_IMAGE_SIZE_CR_IMAGE_VERTICAL_SIZE_MASK,
				       JASPER_IMAGE_SIZE_CR_IMAGE_VERTICAL_SIZE_SHIFT,
				       val);
}

int e5010_hw_set_luma_stride(void __iomem *core_base, u32 bytesperline)
{
	u32 val = bytesperline / 64;

	return write_reg_field_not_busy(core_base, core_base,
				       JASPER_INPUT_CTRL1_OFFSET,
				       JASPER_INPUT_CTRL1_CR_INPUT_LUMA_STRIDE_MASK,
				       JASPER_INPUT_CTRL1_CR_INPUT_LUMA_STRIDE_SHIFT,
				       val);
}

int e5010_hw_set_chroma_stride(void __iomem *core_base, u32 bytesperline)
{
	u32 val = bytesperline / 64;

	return write_reg_field_not_busy(core_base, core_base,
				       JASPER_INPUT_CTRL1_OFFSET,
				       JASPER_INPUT_CTRL1_CR_INPUT_CHROMA_STRIDE_MASK,
				       JASPER_INPUT_CTRL1_CR_INPUT_CHROMA_STRIDE_SHIFT,
				       val);
}

int e5010_hw_set_input_subsampling(void __iomem *core_base, u32 val)
{
	return write_reg_field_not_busy(core_base, core_base,
				       JASPER_INPUT_CTRL0_OFFSET,
				       JASPER_INPUT_CTRL0_CR_INPUT_SUBSAMPLING_MASK,
				       JASPER_INPUT_CTRL0_CR_INPUT_SUBSAMPLING_SHIFT,
				       val);
}

int e5010_hw_set_chroma_order(void __iomem *core_base, u32 val)
{
	return write_reg_field_not_busy(core_base, core_base,
				       JASPER_INPUT_CTRL0_OFFSET,
				       JASPER_INPUT_CTRL0_CR_INPUT_CHROMA_ORDER_MASK,
				       JASPER_INPUT_CTRL0_CR_INPUT_CHROMA_ORDER_SHIFT,
				       val);
}

void e5010_hw_set_output_max_size(void __iomem *core_base, u32 val)
{
	write_reg_field(core_base, JASPER_OUTPUT_MAX_SIZE_OFFSET,
			JASPER_OUTPUT_MAX_SIZE_CR_OUTPUT_MAX_SIZE_MASK,
			JASPER_OUTPUT_MAX_SIZE_CR_OUTPUT_MAX_SIZE_SHIFT,
			val);
}

int e5010_hw_set_qpvalue(void __iomem *core_base, u32 offset, u32 val)
{
	return write_reg_field_not_busy(core_base, core_base, offset, 0xffffffff, 0, val);
}

void e5010_hw_clear_output_error(void __iomem *core_base, u32 clear)
{
	/* Make sure interrupts are clear */
	write_reg_field(core_base, JASPER_INTERRUPT_CLEAR_OFFSET,
			JASPER_INTERRUPT_CLEAR_CR_OUTPUT_ERROR_CLEAR_MASK,
			JASPER_INTERRUPT_CLEAR_CR_OUTPUT_ERROR_CLEAR_SHIFT, clear);
}

void e5010_hw_clear_picture_done(void __iomem *core_base, u32 clear)
{
	write_reg_field(core_base,
			JASPER_INTERRUPT_CLEAR_OFFSET,
			JASPER_INTERRUPT_CLEAR_CR_PICTURE_DONE_CLEAR_MASK,
			JASPER_INTERRUPT_CLEAR_CR_PICTURE_DONE_CLEAR_SHIFT, clear);
}

int e5010_hw_get_output_size(void __iomem *core_base)
{
	return readl(core_base + JASPER_OUTPUT_SIZE_OFFSET);
}

void e5010_hw_encode_start(void __iomem *core_base, u32 start)
{
	write_reg_field(core_base, JASPER_CORE_CTRL_OFFSET,
			JASPER_CORE_CTRL_CR_JASPER_ENCODE_START_MASK,
			JASPER_CORE_CTRL_CR_JASPER_ENCODE_START_SHIFT, start);
}
