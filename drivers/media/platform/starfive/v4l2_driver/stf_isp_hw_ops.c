// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 *
 * linux/drivers/media/platform/starfive/stf_isp.c
 *
 * PURPOSE:	This files contains the driver of VPP.
 */
#include "stfcamss.h"
#include <linux/io.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <video/stf-vin.h>
#include <linux/delay.h>

static const struct regval_t isp_sc2235_reg_config_list[] = {
	{0x00000014, 0x00000008, 0, 0},
//	{0x00000018, 0x000011BB, 0, 0},
	{0x00000A1C, 0x00000032, 0, 0},
//	{0x0000001C, 0x00000000, 0, 0},
//	{0x00000020, 0x0437077F, 0, 0},
//	{0x00000A0C, 0x04380780, 0, 0},
	{0x00000A80, 0x88000000, 0, 0},
	{0x00000A84, 0x881fa400, 0, 0},
//	{0x00000A88, 0x00000780, 0, 0},
	{0x00000A8C, 0x00000010, 0, 0},
	{0x00000A90, 0x00000000, 0, 0},
	{0x00000A94, 0x803f4810, 0, 0},
	{0x00000A98, 0x80517990, 0, 0},
	{0x00000A9c, 0x000005c0, 0, 0},
	{0x00000AA0, 0x0c000000, 0, 0},
	{0x00000AA4, 0x0c000000, 0, 0},
	{0x00000AA8, 0x05a0032a, 0, 0},
	{0x00000AAC, 0x0418e410, 0, 0},
	{0x00000AB0, 0x0420cd10, 0, 0},
	{0x00000AB4, 0x0000021c, 0, 0},
	{0x00000AB8, 0x08000000, 0, 0},
	{0x00000ABc, 0x08000000, 0, 0},
	{0x00000AC0, 0x021c03c0, 0, 0},
	{0x00000AC4, 0x00000000, 0, 0},
	{0x00000E40, 0x0000004D, 0, 0},
	{0x00000E44, 0x00000096, 0, 0},
	{0x00000E48, 0x0000001D, 0, 0},
	{0x00000E4C, 0x000001DA, 0, 0},
	{0x00000E50, 0x000001B6, 0, 0},
	{0x00000E54, 0x00000070, 0, 0},
	{0x00000E58, 0x0000009D, 0, 0},
	{0x00000E5C, 0x0000017C, 0, 0},
	{0x00000E60, 0x000001E6, 0, 0},
	{0x00000010, 0x00000000, 0, 0},
	{0x00000A08, 0x10000022, 0xFFFFFFF, 0},
	{0x00000044, 0x00000000, 0, 0},
	{0x00000A00, 0x00120002, 0, 0},
	{0x00000A00, 0x00120000, 0, 0},
	{0x00000A50, 0x00000002, 0, 0},
	{0x00000A00, 0x00120001, 0, 0},
	{0x00000008, 0x00010000, 0, 0},
	{0x00000008, 0x00020004, 0, 0},
	{0x00000000, 0x00000001, 0, 0},
};

static const struct regval_t isp_ov13850_reg_config_list[] = {
	{0x00000014, 0x00000001, 0, 0},
//	{0x00000018, 0x000011BB, 0, 0},
	{0x00000A1C, 0x00000030, 0, 0},
//	{0x0000001C, 0x00000000, 0, 0},
//	{0x00000020, 0x0437077F, 0, 0},
//	{0x00000A0C, 0x04380780, 0, 0},
	{0x00000A80, 0x88000000, 0, 0},
	{0x00000A84, 0x881fa400, 0, 0},
//	{0x00000A88, 0x00000780, 0, 0},
	{0x00000A8C, 0x00000010, 0, 0},
	{0x00000A90, 0x00000000, 0, 0},
	{0x00000A94, 0x803f4810, 0, 0},
	{0x00000A98, 0x80517990, 0, 0},
	{0x00000A9c, 0x000005c0, 0, 0},
	{0x00000AA0, 0x0c000000, 0, 0},
	{0x00000AA4, 0x0c000000, 0, 0},
	{0x00000AA8, 0x05a0032a, 0, 0},
	{0x00000AAC, 0x0418e410, 0, 0},
	{0x00000AB0, 0x0420cd10, 0, 0},
	{0x00000AB4, 0x0000021c, 0, 0},
	{0x00000AB8, 0x08000000, 0, 0},
	{0x00000ABc, 0x08000000, 0, 0},
	{0x00000AC0, 0x021c03c0, 0, 0},
	{0x00000AC4, 0x00000000, 0, 0},
	{0x00000E40, 0x0000004D, 0, 0},
	{0x00000E44, 0x00000096, 0, 0},
	{0x00000E48, 0x0000001D, 0, 0},
	{0x00000E4C, 0x000001DA, 0, 0},
	{0x00000E50, 0x000001B6, 0, 0},
	{0x00000E54, 0x00000070, 0, 0},
	{0x00000E58, 0x0000009D, 0, 0},
	{0x00000E5C, 0x0000017C, 0, 0},
	{0x00000E60, 0x000001E6, 0, 0},
	{0x00000010, 0x00000000, 0, 0},
	{0x00000A08, 0x10000022, 0xFFFFFFF, 0},
	{0x00000044, 0x00000000, 0, 0},
	{0x00000008, 0x00010005, 0, 0},
	{0x00000A00, 0x00120002, 0, 0},
	{0x00000A00, 0x00120000, 0, 0},
	{0x00000A00, 0x00120001, 0, 0},
	{0x00000000, 0x00000001, 0, 0},
};

static const struct regval_t isp_1080p_reg_config_list[] = {
	{0x00000014, 0x0000000D, 0, 0},
	// {0x00000018, 0x000011BB, 0, 0},
	{0x00000A1C, 0x00000032, 0, 0},
	// {0x0000001C, 0x00000000, 0, 0},
	// {0x00000020, 0x0437077F, 0, 0},
	// {0x00000A0C, 0x04380780, 0, 0},
	// {0x00000A80, 0xF9000000, 0, 0},
	// {0x00000A84, 0xF91FA400, 0, 0},
	// {0x00000A88, 0x00000780, 0, 0},
	{0x00000A8C, 0x00000000, 0, 0},
	{0x00000A90, 0x00000000, 0, 0},
	{0x00000E40, 0x0000004C, 0, 0},
	{0x00000E44, 0x00000097, 0, 0},
	{0x00000E48, 0x0000001D, 0, 0},
	{0x00000E4C, 0x000001D5, 0, 0},
	{0x00000E50, 0x000001AC, 0, 0},
	{0x00000E54, 0x00000080, 0, 0},
	{0x00000E58, 0x00000080, 0, 0},
	{0x00000E5C, 0x00000194, 0, 0},
	{0x00000E60, 0x000001EC, 0, 0},
	{0x00000280, 0x00000000, 0, 0},
	{0x00000284, 0x00000000, 0, 0},
	{0x00000288, 0x00000000, 0, 0},
	{0x0000028C, 0x00000000, 0, 0},
	{0x00000290, 0x00000000, 0, 0},
	{0x00000294, 0x00000000, 0, 0},
	{0x00000298, 0x00000000, 0, 0},
	{0x0000029C, 0x00000000, 0, 0},
	{0x000002A0, 0x00000000, 0, 0},
	{0x000002A4, 0x00000000, 0, 0},
	{0x000002A8, 0x00000000, 0, 0},
	{0x000002AC, 0x00000000, 0, 0},
	{0x000002B0, 0x00000000, 0, 0},
	{0x000002B4, 0x00000000, 0, 0},
	{0x000002B8, 0x00000000, 0, 0},
	{0x000002BC, 0x00000000, 0, 0},
	{0x000002C0, 0x00F000F0, 0, 0},
	{0x000002C4, 0x00F000F0, 0, 0},
	{0x000002C8, 0x00800080, 0, 0},
	{0x000002CC, 0x00800080, 0, 0},
	{0x000002D0, 0x00800080, 0, 0},
	{0x000002D4, 0x00800080, 0, 0},
	{0x000002D8, 0x00B000B0, 0, 0},
	{0x000002DC, 0x00B000B0, 0, 0},
	{0x00000E00, 0x24000000, 0, 0},
	{0x00000E04, 0x159500A5, 0, 0},
	{0x00000E08, 0x0F9900EE, 0, 0},
	{0x00000E0C, 0x0CE40127, 0, 0},
	{0x00000E10, 0x0B410157, 0, 0},
	{0x00000E14, 0x0A210181, 0, 0},
	{0x00000E18, 0x094B01A8, 0, 0},
	{0x00000E1C, 0x08A401CC, 0, 0},
	{0x00000E20, 0x081D01EE, 0, 0},
	{0x00000E24, 0x06B20263, 0, 0},
	{0x00000E28, 0x05D802C7, 0, 0},
	{0x00000E2C, 0x05420320, 0, 0},
	{0x00000E30, 0x04D30370, 0, 0},
	{0x00000E34, 0x047C03BB, 0, 0},
	{0x00000E38, 0x043703FF, 0, 0},
	{0x00000010, 0x00000080, 0, 0},
	{0x00000A08, 0x10000032, 0xFFFFFFF, 0},
	{0x00000A00, 0x00120002, 0, 0},
	{0x00000A00, 0x00120000, 0, 0},
	{0x00000A50, 0x00000002, 0, 0},
	{0x00000A00, 0x00120001, 0, 0},
	{0x00000008, 0x00010000, 0, 0},
	{0x00000008, 0x0002000A, 0, 0},
	{0x00000000, 0x00000001, 0, 0},
};

const struct reg_table isp_1920_1080_settings[] = {
	{isp_1080p_reg_config_list,
	ARRAY_SIZE(isp_1080p_reg_config_list)},
};

const struct reg_table isp_sc2235_settings[] = {
	{isp_sc2235_reg_config_list,
	ARRAY_SIZE(isp_sc2235_reg_config_list)},
};

const struct reg_table isp_ov13850_settings[] = {
	{isp_ov13850_reg_config_list,
	ARRAY_SIZE(isp_ov13850_reg_config_list)},
};

static struct regval_t isp_format_reg_list[] = {
	{0x0000001C, 0x00000000, 0, 0},
	{0x00000020, 0x0437077F, 0, 0},
	{0x00000A0C, 0x04380780, 0, 0},
	{0x00000A88, 0x00000780, 0, 0},
	{0x00000018, 0x000011BB, 0, 0},
	{0x00000A08, 0x10000022, 0xF0000000, 0},
};

const struct reg_table  isp_format_settings[] = {
	{isp_format_reg_list,
	ARRAY_SIZE(isp_format_reg_list)},
};

static const struct reg_table *isp_settings = isp_1920_1080_settings;

static void isp_load_regs(void __iomem *ispbase, const struct reg_table *table)
{
	int j;
	u32 delay_ms, reg_addr, mask, val;

	for (j = 0; j < table->regval_num; j++) {
		delay_ms = table->regval[j].delay_ms;
		reg_addr = table->regval[j].addr;
		val = table->regval[j].val;
		mask = table->regval[j].mask;

		if (reg_addr % 4
			|| reg_addr > STF_ISP_REG_OFFSET_MAX
			|| delay_ms > STF_ISP_REG_DELAY_MAX)
			continue;

		if (mask)
			reg_set_bit(ispbase, reg_addr, mask, val);
		else
			reg_write(ispbase, reg_addr, val);
		if (delay_ms)
			usleep_range(1000 * delay_ms, 1000 * delay_ms + 100);
	}
}

static int stf_isp_clk_enable(struct stf_isp_dev *isp_dev)
{
	struct stfcamss *stfcamss = isp_dev->stfcamss;

	if (isp_dev->id == 0) {
		clk_prepare_enable(stfcamss->sys_clk[STFCLK_WRAPPER_CLK_C].clk);
		reset_control_deassert(stfcamss->sys_rst[STFRST_WRAPPER_C].rstc);
		reset_control_deassert(stfcamss->sys_rst[STFRST_WRAPPER_P].rstc);
	} else {
		st_err(ST_ISP, "please check isp id :%d\n", isp_dev->id);
	}

	return 0;
}

static int stf_isp_clk_disable(struct stf_isp_dev *isp_dev)
{
	struct stfcamss *stfcamss = isp_dev->stfcamss;

	if (isp_dev->id == 0) {
		reset_control_deassert(stfcamss->sys_rst[STFRST_WRAPPER_C].rstc);
		reset_control_deassert(stfcamss->sys_rst[STFRST_WRAPPER_P].rstc);
		clk_disable_unprepare(stfcamss->sys_clk[STFCLK_WRAPPER_CLK_C].clk);
	} else {
		st_err(ST_ISP, "please check isp id :%d\n", isp_dev->id);
	}

	return 0;
}

static int stf_isp_reset(struct stf_isp_dev *isp_dev)
{
	return 0;
}

static int stf_isp_config_set(struct stf_isp_dev *isp_dev)
{
	struct stf_vin_dev *vin = isp_dev->stfcamss->vin;
	void __iomem *ispbase;

	if (isp_dev->id == 0)
		ispbase = vin->isp_isp0_base;
	else
		ispbase = vin->isp_isp1_base;

	st_debug(ST_ISP, "%s, isp_id = %d\n", __func__, isp_dev->id);

	isp_load_regs(ispbase, isp_format_settings);
	mutex_lock(&isp_dev->setfile_lock);
	if (isp_dev->setfile.state)
		isp_load_regs(ispbase, &isp_dev->setfile.settings);
	else
		isp_load_regs(ispbase, isp_settings);
	mutex_unlock(&isp_dev->setfile_lock);

	st_debug(ST_ISP, "config 0x%x = 0x%x\n",
			isp_format_reg_list[0].addr,
			isp_format_reg_list[0].val);
	st_debug(ST_ISP, "config 0x%x = 0x%x\n",
			isp_format_reg_list[1].addr,
			isp_format_reg_list[1].val);
	st_debug(ST_ISP, "config 0x%x = 0x%x\n",
			isp_format_reg_list[2].addr,
			isp_format_reg_list[2].val);
	st_debug(ST_ISP, "config 0x%x = 0x%x\n",
			isp_format_reg_list[3].addr,
			isp_format_reg_list[3].val);

	return 0;
}

static int stf_isp_set_format(struct stf_isp_dev *isp_dev,
		struct v4l2_rect *crop, u32 mcode)
		// u32 width, u32 height)
{
	struct stf_vin_dev *vin = isp_dev->stfcamss->vin;
	void __iomem *ispbase;
	u32 val, val1;

	if (isp_dev->id == 0) {
		ispbase = vin->isp_isp0_base;
		isp_settings = isp_sc2235_settings;
		//isp_settings = isp_ov13850_settings;
	} else {
		ispbase = vin->isp_isp1_base;
		isp_settings = isp_sc2235_settings;
	}

	val = crop->left + (crop->top << 16);
	isp_format_reg_list[0].addr = ISP_REG_PIC_CAPTURE_START_CFG;
	isp_format_reg_list[0].val = val;

	val = (crop->width + crop->left - 1)
		+ ((crop->height + crop->top - 1) << 16);
	isp_format_reg_list[1].addr = ISP_REG_PIC_CAPTURE_END_CFG;
	isp_format_reg_list[1].val = val;

	val = crop->width + (crop->height << 16);
	isp_format_reg_list[2].addr = ISP_REG_PIPELINE_XY_SIZE;
	isp_format_reg_list[2].val = val;

	isp_format_reg_list[3].addr = ISP_REG_STRIDE;
	isp_format_reg_list[3].val = crop->width;

	switch (mcode) {
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		// 3 2 3 2 1 0 1 0 B Gb B Gb Gr R Gr R
		val = 0x0000EE44;
		val1 = 0x00000000;
		break;
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		// 2 3 2 3 0 1 0 1, Gb B Gb B R Gr R Gr
		val = 0x0000BB11;
		val1 = 0x20000000;
		break;
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
		// 1 0 1 0 3 2 3 2, Gr R Gr R B Gb B Gb
		val = 0x000044EE;
		val1 = 0x30000000;
		break;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		// 0 1 0 1 2 3 2 3 R Gr R Gr Gb B Gb B
		val = 0x000011BB;
		val1 = 0x10000000;
		break;
	default:
		st_err(ST_ISP, "UNKNOW format\n");
		val = 0x000011BB;
		val1 = 0x10000000;
		break;
	}
	isp_format_reg_list[4].addr = ISP_REG_RAW_FORMAT_CFG;
	isp_format_reg_list[4].val = val;

	isp_format_reg_list[5].addr = ISP_REG_ISP_CTRL_1;
	isp_format_reg_list[5].val = val1;
	isp_format_reg_list[5].mask = 0xF0000000;

	st_info(ST_ISP, "left: %d, top: %d, width = %d, height = %d, code = 0x%x\n",
		crop->left, crop->top, crop->width, crop->height, mcode);

	return 0;
}

static int stf_isp_stream_set(struct stf_isp_dev *isp_dev, int on)
{
	return 0;
}

void dump_isp_reg(void *__iomem ispbase, int id)
{
	int j;
	u32 addr, val;

	st_debug(ST_ISP, "DUMP ISP%d register:\n", id);
	for (j = 0; j < isp_format_settings->regval_num; j++) {
		addr = isp_format_settings->regval[j].addr;
		val = ioread32(ispbase + addr);
		st_debug(ST_ISP, "{0x%08x, 0x%08x}\n", addr, val);
	}

	val = ioread32(ispbase + ISP_REG_Y_PLANE_START_ADDR);
	st_debug(ST_ISP, "{0x%08x, 0x%08x}\n", ISP_REG_Y_PLANE_START_ADDR, val);
	val = ioread32(ispbase + ISP_REG_UV_PLANE_START_ADDR);
	st_debug(ST_ISP, "{0x%08x, 0x%08x}\n", ISP_REG_UV_PLANE_START_ADDR, val);
	val = ioread32(ispbase + ISP_REG_DUMP_CFG_0);
	st_debug(ST_ISP, "{0x%08x, 0x%08x}\n", ISP_REG_DUMP_CFG_0, val);
	val = ioread32(ispbase + ISP_REG_DUMP_CFG_1);
	st_debug(ST_ISP, "{0x%08x, 0x%08x}\n", ISP_REG_DUMP_CFG_1, val);

	for (j = 0; j < isp_settings->regval_num; j++) {
		addr = isp_settings->regval[j].addr;
		val = ioread32(ispbase + addr);
		st_debug(ST_ISP, "{0x%08x, 0x%08x}\n", addr, val);
	}
}

struct isp_hw_ops isp_ops = {
	.isp_clk_enable        = stf_isp_clk_enable,
	.isp_clk_disable       = stf_isp_clk_disable,
	.isp_reset             = stf_isp_reset,
	.isp_config_set        = stf_isp_config_set,
	.isp_set_format        = stf_isp_set_format,
	.isp_stream_set        = stf_isp_stream_set,
};
