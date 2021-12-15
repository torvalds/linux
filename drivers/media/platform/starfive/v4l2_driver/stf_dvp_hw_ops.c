// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#include "stfcamss.h"

static int stf_dvp_clk_init(struct stf_dvp_dev *dvp_dev)
{
	return 0;
}

#define U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR                        0x13040000
void reg_phy_write(uint32_t addr,uint32_t reg,uint32_t val)
{
    uint32_t tmp;

   iowrite32(val, ioremap(addr + reg, 4));
}

void stf_dvp_io_pad_config(struct stf_vin_dev *vin)
{
	/*
	 * pin: 21 ~ 35
	 * offset: 0x144 ~ 0x164
	 * SCFG_funcshare_pad_ctrl
	 */
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x0174U, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a0U, 0x10);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b4U, 0x200000);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x0178U, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a0U, 0x90);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b4U, 0x240000);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x017cU, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a0U, 0x490);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b4U, 0x248000);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x0180U, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a0U, 0x02a0);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b0U, 0x800);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x0184U, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a0U, 0x12490);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b0U, 0x100800);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x0188U, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a0U, 0x92490);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b0U, 0x900800);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x018cU, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a0U, 0x492490);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b0U, 0x4900800);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x0190U, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a0U, 0x2492490);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b0U, 0x24900800);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x0194U, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a0U, 0x12492490);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b4U, 0x248001);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x0198U, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a4U, 0x2);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b4U, 0x248009);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x019cU, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a4U, 0x12);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b4U, 0x248049);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x01a0U, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a4U, 0x92);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b4U, 0x248249);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x01a4U, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a4U, 0x492);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b4U, 0x249249);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x01a8U, 0x1);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a4U, 0x2492);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b0U, 0x24904800);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x01acU, 0x11);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02a4U, 0x12492);
	reg_phy_write(U0_SYS_IOMUX__SAIF_BD_APBS__BASE_ADDR, 0x02b0U, 0x24924800);

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

	stf_dvp_io_pad_config(vin);

	if (flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
		polarities |= BIT(1);
	if (flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH)
		polarities |= BIT(3);
	print_reg(ST_DVP, vin->sysctrl_base, SYSCONSAIF_SYSCFG_36);
	reg_set_bit(vin->sysctrl_base,	SYSCONSAIF_SYSCFG_36,
		BIT(1) 
		| BIT(3),
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
	print_reg(ST_DVP, vin->sysctrl_base, SYSCTRL_VIN_RW_CTRL);
	reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_28,
			UO_VIN_CNFG_AXIWR0_PIXEL_HEIGH_BIT_SEL, data_shift << 15);
	print_reg(ST_DVP, vin->sysctrl_base, SYSCTRL_VIN_RW_CTRL);

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
		value = 2;
	else if (cnfg_axiwr_pix_ct == 8)
		value = 0;
	else
		return 0;

	print_reg(ST_DVP, vin->sysctrl_base, SYSCTRL_VIN_RW_CTRL);
	reg_set_bit(vin->sysctrl_base,
			SYSCTRL_VIN_RW_CTRL, BIT(1) | BIT(0), value);
	print_reg(ST_DVP, vin->sysctrl_base, SYSCTRL_VIN_RW_CTRL);

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
		reg_write(vin->sysctrl_base, SYSCTRL_VIN_WR_PIX_TOTAL, val);
		print_reg(ST_DVP, vin->sysctrl_base, SYSCTRL_VIN_WR_PIX_TOTAL);

	}

	return 0;
}

static int stf_dvp_stream_set(struct stf_dvp_dev *dvp_dev, int on)
{
	struct stf_vin_dev *vin = dvp_dev->stfcamss->vin;

	switch (dvp_dev->s_type) {
	case SENSOR_VIN:
#if 0
		/*View segment register (2.7.4.1.ispv2 ) is vin RD WR related (data stream not
		 through ISP, directly to DDR, or take data from DDR) 2021 1110*/
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_0, U0_VIN_CNFG_AXIRD_AXI_CNT_END, 0x0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_4, U0_VIN_CNFG_AXIRD_END_ADDR, 0x0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_8,	U0_VIN_CNFG_AXIRD_LINE_CNT_END, 0x0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_8,	U0_VIN_CNFG_AXIRD_LINE_CNT_START, 0x0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_12, U0_VIN_CNFG_AXIRD_PIX_CNT_END 
			| U0_VIN_CNFG_AXIRD_PIX_CNT_START 
			| U0_VIN_CNFG_AXIRD_PIX_CT, 0x0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_16, U0_VIN_CNFG_AXIRD_START_ADDR, 0x0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_20, U0_VIN_CNFG_AXIWR0_CHANNEL_SEL
			| U0_VIN_CNFG_AXIWR0_EN, 0x0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_24, U0_VIN_CNFG_AXIWR0_END_ADDR, 0x0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_28, U0_VIN_CNFG_AXIWR0_PIXEL_HITH_BIT_SEL 
			| U0_VIN_CNFG_AXIWR0_PIX_CNT_CNT_END
			| U0_VIN_CNFG_AXIWR0_PIX_CNT_CT
			| SYSCONSAIF_SYSCFG_28, 0x0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_32, U0_VIN_CNFG_AXIWR0_START_ADDR, 0x0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_8,	U0_VIN_CNFG_AXIRD_INTR_MASK, 0x0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_0, U0_VIN_CNFG_AXI_DVP_EN, 0x0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_36, U0_VIN_CNFG_COLOR_BAR_EN, 0x0);
#endif 
		reg_set_bit(vin->sysctrl_base,
				SYSCTRL_VIN_AXI_CTRL, BIT(0), on);
		reg_set_bit(vin->clkgen_base,
				CLK_VIN_AXI_WR_CTRL,
				BIT(25) | BIT(24), 2 << 24);
		break;
	case SENSOR_ISP0:
		reg_set_bit(vin->sysctrl_base,	SYSCONSAIF_SYSCFG_36,
			BIT(U0_VIN_CNFG_ISP_DVP_EN0), 
			!!on<<U0_VIN_CNFG_ISP_DVP_EN0);	
		reg_set_bit(vin->sysctrl_base,	SYSCONSAIF_SYSCFG_36, 
			BIT(U0_VIN_CNFG_DVP_SWAP_EN), 0x0);	  
		reg_set_bit(vin->sysctrl_base,	SYSCONSAIF_SYSCFG_36, 
			BIT(U0_VIN_CNFG_GEN_EN_AXIRD), 0x0);
		break;
	case SENSOR_ISP1:
		st_err(ST_DVP, "please check dvp_dev s_type:%d\n", dvp_dev->s_type);
		break;
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
