// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#include "stfcamss.h"

static int stf_dvp_clk_init(struct stf_dvp_dev *dvp_dev)
{
	struct stfcamss *stfcamss = dvp_dev->stfcamss;

	clk_set_phase(stfcamss->sys_clk[STFCLK_DVP_INV].clk, 0);

	return 0;
}

static int stf_dvp_config_set(struct stf_dvp_dev *dvp_dev)
{

	struct stf_vin_dev *vin = dvp_dev->stfcamss->vin;
	unsigned int flags = 0;
	unsigned char data_shift = 0;
	u32 polarities = 0;

	if (!dvp_dev->dvp)
		return -EINVAL;

	flags = dvp_dev->dvp->flags;
	data_shift = dvp_dev->dvp->data_shift;
	st_info(ST_DVP, "%s, polarities = 0x%x, flags = 0x%x\n",
			__func__, polarities, flags);

	if (flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
		polarities |= BIT(1);
	if (flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH)
		polarities |= BIT(3);
	print_reg(ST_DVP, vin->sysctrl_base, SYSCONSAIF_SYSCFG_36);
	reg_set_bit(vin->sysctrl_base,	SYSCONSAIF_SYSCFG_36,
		U0_VIN_CNFG_DVP_HS_POS
		| U0_VIN_CNFG_DVP_VS_POS,
		polarities);
	print_reg(ST_DVP, vin->sysctrl_base, SYSCONSAIF_SYSCFG_36);

	switch (data_shift) {
	case 0:
		data_shift = 0;
		break;
	case 2:
		data_shift = 1;
		break;
	case 4:
		data_shift = 2;
		break;
	case 6:
		data_shift = 3;
		break;
	default:
		data_shift = 0;
		break;
	};
	print_reg(ST_DVP, vin->sysctrl_base, SYSCONSAIF_SYSCFG_28);
	reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_28,
		UO_VIN_CNFG_AXIWR0_PIXEL_HEIGH_BIT_SEL,
		data_shift << 15);
	print_reg(ST_DVP, vin->sysctrl_base, SYSCONSAIF_SYSCFG_28);

	return 0;
}

static int set_vin_axiwr_pix_ct(struct stf_vin_dev *vin, u8 bpp)
{
	u32 value = 0;
	int cnfg_axiwr_pix_ct = 64 / bpp;

	// need check
	if (cnfg_axiwr_pix_ct == 2)
		value = 1;
	else if (cnfg_axiwr_pix_ct == 4)
		value = 1;
	else if (cnfg_axiwr_pix_ct == 8)
		value = 0;
	else
		return 0;

	print_reg(ST_DVP, vin->sysctrl_base, SYSCONSAIF_SYSCFG_28);
	reg_set_bit(vin->sysctrl_base,
		SYSCONSAIF_SYSCFG_28,
		U0_VIN_CNFG_AXIWR0_PIX_CT,
		value<<13);
	print_reg(ST_DVP, vin->sysctrl_base, SYSCONSAIF_SYSCFG_28);

	return cnfg_axiwr_pix_ct;

}

static int stf_dvp_set_format(struct stf_dvp_dev *dvp_dev,
		u32 pix_width, u8 bpp)
{
	struct stf_vin_dev *vin = dvp_dev->stfcamss->vin;
	int val, pix_ct;

	if (dvp_dev->s_type == SENSOR_VIN) {
		pix_ct = set_vin_axiwr_pix_ct(vin, bpp);
		val = (pix_width / pix_ct) - 1;
		print_reg(ST_DVP, vin->sysctrl_base, SYSCTRL_VIN_WR_PIX_TOTAL);
		reg_set_bit(vin->sysctrl_base,
			SYSCONSAIF_SYSCFG_28,
			U0_VIN_CNFG_AXIWR0_PIX_CNT_END,
			val << 2);
		print_reg(ST_DVP, vin->sysctrl_base, SYSCTRL_VIN_WR_PIX_TOTAL);

	}

	return 0;
}

static int stf_dvp_stream_set(struct stf_dvp_dev *dvp_dev, int on)
{
	struct stfcamss *stfcamss = dvp_dev->stfcamss;
	struct stf_vin_dev *vin = dvp_dev->stfcamss->vin;

	switch (dvp_dev->s_type) {
	case SENSOR_VIN:
		clk_set_parent(stfcamss->sys_clk[STFCLK_AXIWR].clk,
			stfcamss->sys_clk[STFCLK_DVP_INV].clk);

		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_36,
			U0_VIN_CNFG_ISP_DVP_EN0,
			0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_0,
			U0_VIN_CNFG_AXI_DVP_EN,
			!!on<<2);
		break;
	case SENSOR_ISP0:
		clk_set_parent(stfcamss->sys_clk[STFCLK_WRAPPER_CLK_C].clk,
			stfcamss->sys_clk[STFCLK_DVP_INV].clk);

		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_36,
			U0_VIN_CNFG_ISP_DVP_EN0,
			!!on<<5);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_0,
			U0_VIN_CNFG_AXI_DVP_EN,
			0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_36,
			U0_VIN_CNFG_DVP_SWAP_EN,
			0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_36,
			U0_VIN_CNFG_GEN_EN_AXIRD,
			0);
		break;
#ifdef CONFIG_STF_DUAL_ISP
	case SENSOR_ISP1:
		st_err(ST_DVP, "please check dvp_dev s_type:%d\n", dvp_dev->s_type);
		break;
#endif
	default:
		break;
	}

	return 0;
}

struct dvp_hw_ops dvp_ops = {
	.dvp_clk_init          = stf_dvp_clk_init,
	.dvp_config_set        = stf_dvp_config_set,
	.dvp_set_format        = stf_dvp_set_format,
	.dvp_stream_set        = stf_dvp_stream_set,
};
