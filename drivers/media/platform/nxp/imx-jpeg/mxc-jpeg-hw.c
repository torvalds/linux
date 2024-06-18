// SPDX-License-Identifier: GPL-2.0
/*
 * i.MX8QXP/i.MX8QM JPEG encoder/decoder v4l2 driver
 *
 * Copyright 2018-2019 NXP
 */

#include <linux/delay.h>
#include <media/videobuf2-core.h>
#include "mxc-jpeg-hw.h"

#define print_wrapper_reg(dev, base_address, reg_offset)\
		internal_print_wrapper_reg(dev, (base_address), #reg_offset,\
					   (reg_offset))
#define internal_print_wrapper_reg(dev, base_address, reg_name, reg_offset) {\
		int val;\
		val = readl((base_address) + (reg_offset));\
		dev_dbg(dev, "Wrapper reg %s = 0x%x\n", reg_name, val);\
}

void print_descriptor_info(struct device *dev, struct mxc_jpeg_desc *desc)
{
	dev_dbg(dev, " MXC JPEG NEXT_DESCPT_PTR 0x%x\n",
		desc->next_descpt_ptr);
	dev_dbg(dev, " MXC JPEG BUF_BASE0 0x%x\n", desc->buf_base0);
	dev_dbg(dev, " MXC JPEG BUF_BASE1 0x%x\n", desc->buf_base1);
	dev_dbg(dev, " MXC JPEG LINE_PITCH %d\n", desc->line_pitch);
	dev_dbg(dev, " MXC JPEG STM_BUFBASE 0x%x\n", desc->stm_bufbase);
	dev_dbg(dev, " MXC JPEG STM_BUFSIZE %d\n", desc->stm_bufsize);
	dev_dbg(dev, " MXC JPEG IMGSIZE %x (%d x %d)\n", desc->imgsize,
		desc->imgsize >> 16, desc->imgsize & 0xFFFF);
	dev_dbg(dev, " MXC JPEG STM_CTRL 0x%x\n", desc->stm_ctrl);
}

void print_cast_status(struct device *dev, void __iomem *reg,
		       unsigned int mode)
{
	dev_dbg(dev, "CAST IP status regs:\n");
	print_wrapper_reg(dev, reg, CAST_STATUS0);
	print_wrapper_reg(dev, reg, CAST_STATUS1);
	print_wrapper_reg(dev, reg, CAST_STATUS2);
	print_wrapper_reg(dev, reg, CAST_STATUS3);
	print_wrapper_reg(dev, reg, CAST_STATUS4);
	print_wrapper_reg(dev, reg, CAST_STATUS5);
	print_wrapper_reg(dev, reg, CAST_STATUS6);
	print_wrapper_reg(dev, reg, CAST_STATUS7);
	print_wrapper_reg(dev, reg, CAST_STATUS8);
	print_wrapper_reg(dev, reg, CAST_STATUS9);
	print_wrapper_reg(dev, reg, CAST_STATUS10);
	print_wrapper_reg(dev, reg, CAST_STATUS11);
	print_wrapper_reg(dev, reg, CAST_STATUS12);
	print_wrapper_reg(dev, reg, CAST_STATUS13);
	if (mode == MXC_JPEG_DECODE)
		return;
	print_wrapper_reg(dev, reg, CAST_STATUS14);
	print_wrapper_reg(dev, reg, CAST_STATUS15);
	print_wrapper_reg(dev, reg, CAST_STATUS16);
	print_wrapper_reg(dev, reg, CAST_STATUS17);
	print_wrapper_reg(dev, reg, CAST_STATUS18);
	print_wrapper_reg(dev, reg, CAST_STATUS19);
}

void print_wrapper_info(struct device *dev, void __iomem *reg)
{
	dev_dbg(dev, "Wrapper regs:\n");
	print_wrapper_reg(dev, reg, GLB_CTRL);
	print_wrapper_reg(dev, reg, COM_STATUS);
	print_wrapper_reg(dev, reg, BUF_BASE0);
	print_wrapper_reg(dev, reg, BUF_BASE1);
	print_wrapper_reg(dev, reg, LINE_PITCH);
	print_wrapper_reg(dev, reg, STM_BUFBASE);
	print_wrapper_reg(dev, reg, STM_BUFSIZE);
	print_wrapper_reg(dev, reg, IMGSIZE);
	print_wrapper_reg(dev, reg, STM_CTRL);
}

void mxc_jpeg_enable_irq(void __iomem *reg, int slot)
{
	writel(0xFFFFFFFF, reg + MXC_SLOT_OFFSET(slot, SLOT_STATUS));
	writel(0xF0C, reg + MXC_SLOT_OFFSET(slot, SLOT_IRQ_EN));
}

void mxc_jpeg_disable_irq(void __iomem *reg, int slot)
{
	writel(0x0, reg + MXC_SLOT_OFFSET(slot, SLOT_IRQ_EN));
	writel(0xFFFFFFFF, reg + MXC_SLOT_OFFSET(slot, SLOT_STATUS));
}

void mxc_jpeg_sw_reset(void __iomem *reg)
{
	/*
	 * engine soft reset, internal state machine reset
	 * this will not reset registers, however, it seems
	 * the registers may remain inconsistent with the internal state
	 * so, on purpose, at least let GLB_CTRL bits clear after this reset
	 */
	writel(GLB_CTRL_SFT_RST, reg + GLB_CTRL);
}

void mxc_jpeg_enc_mode_conf(struct device *dev, void __iomem *reg, u8 extseq)
{
	dev_dbg(dev, "CAST Encoder CONFIG...\n");
	/*
	 * "Config_Mode" enabled, "Config_Mode auto clear enabled",
	 */
	if (extseq)
		writel(0xb0, reg + CAST_MODE);
	else
		writel(0xa0, reg + CAST_MODE);

	/* all markers and segments */
	writel(0x3ff, reg + CAST_CFG_MODE);
}

void mxc_jpeg_enc_mode_go(struct device *dev, void __iomem *reg, u8 extseq)
{
	dev_dbg(dev, "CAST Encoder GO...\n");
	/*
	 * "GO" enabled, "GO bit auto clear" enabled
	 */
	if (extseq)
		writel(0x150, reg + CAST_MODE);
	else
		writel(0x140, reg + CAST_MODE);
}

void mxc_jpeg_enc_set_quality(struct device *dev, void __iomem *reg, u8 quality)
{
	dev_dbg(dev, "CAST Encoder Quality %d...\n", quality);

	/* quality factor */
	writel(quality, reg + CAST_QUALITY);
}

void mxc_jpeg_dec_mode_go(struct device *dev, void __iomem *reg)
{
	dev_dbg(dev, "CAST Decoder GO...\n");
	writel(MXC_DEC_EXIT_IDLE_MODE, reg + CAST_CTRL);
}

int mxc_jpeg_enable(void __iomem *reg)
{
	u32 regval;

	writel(GLB_CTRL_JPG_EN, reg + GLB_CTRL);
	regval = readl(reg);
	return regval;
}

void mxc_jpeg_enable_slot(void __iomem *reg, int slot)
{
	u32 regval;

	regval = readl(reg + GLB_CTRL);
	writel(GLB_CTRL_SLOT_EN(slot) | regval, reg + GLB_CTRL);
}

void mxc_jpeg_set_l_endian(void __iomem *reg, int le)
{
	u32 regval;

	regval = readl(reg + GLB_CTRL);
	regval &= ~GLB_CTRL_L_ENDIAN(1); /* clear */
	writel(GLB_CTRL_L_ENDIAN(le) | regval, reg + GLB_CTRL); /* set */
}

void mxc_jpeg_set_bufsize(struct mxc_jpeg_desc *desc,  u32 bufsize)
{
	desc->stm_bufsize = bufsize;
}

void mxc_jpeg_set_res(struct mxc_jpeg_desc *desc, u16 w, u16 h)
{
	desc->imgsize = w << 16 | h;
}

void mxc_jpeg_set_line_pitch(struct mxc_jpeg_desc *desc, u32 line_pitch)
{
	desc->line_pitch = line_pitch;
}

void mxc_jpeg_set_desc(u32 desc, void __iomem *reg, int slot)
{
	writel(desc | MXC_NXT_DESCPT_EN,
	       reg + MXC_SLOT_OFFSET(slot, SLOT_NXT_DESCPT_PTR));
}

void mxc_jpeg_clr_desc(void __iomem *reg, int slot)
{
	writel(0, reg + MXC_SLOT_OFFSET(slot, SLOT_NXT_DESCPT_PTR));
}
