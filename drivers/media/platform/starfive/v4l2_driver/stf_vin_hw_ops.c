// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#include "stfcamss.h"
#include <linux/of_graph.h>
#include <media/v4l2-async.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

static void vin_intr_clear(void __iomem * sysctrl_base)
{
	reg_set_bit(sysctrl_base, SYSCONSAIF_SYSCFG_28, 
		U0_VIN_CNFG_AXIWR0_INTR_CLEAN, 
		0x1);
	reg_set_bit(sysctrl_base, SYSCONSAIF_SYSCFG_28, 
		U0_VIN_CNFG_AXIWR0_INTR_CLEAN, 
		0x0);
}

static irqreturn_t stf_vin_wr_irq_handler(int irq, void *priv)
{
	static struct vin_params params;
	struct stf_vin2_dev *vin_dev = priv;
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;

	vin_dev->hw_ops->isr_buffer_done(&vin_dev->line[VIN_LINE_WR], &params);
	vin_intr_clear(vin->sysctrl_base);

	return IRQ_HANDLED;
}

static irqreturn_t stf_vin_isp_irq_handler(int irq, void *priv)
{
	static struct vin_params params;
	struct stf_vin2_dev *vin_dev = priv;
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase;
	u32 int_status, value;
	int isp_id = irq == vin->isp0_irq ? 0 : 1;

	if (isp_id == 0)
		ispbase = vin->isp_isp0_base;
	else
		ispbase = vin->isp_isp1_base;

	int_status = reg_read(ispbase, ISP_REG_ISP_CTRL_0);

	// if (int_status & BIT(24))
	vin_dev->hw_ops->isr_buffer_done(
		&vin_dev->line[VIN_LINE_ISP0 + isp_id], &params);

	value = reg_read(ispbase, ISP_REG_CIS_MODULE_CFG);
	if ((value & BIT(19)) && (int_status & BIT(25)))
		vin_dev->hw_ops->isr_buffer_done(
			&vin_dev->line[VIN_LINE_ISP0_RAW + isp_id], &params);

	/* clear interrupt */
	reg_write(ispbase, ISP_REG_ISP_CTRL_0, int_status);

	return IRQ_HANDLED;
}


static int stf_vin_clk_init(struct stf_vin2_dev *vin_dev)
{
	return 0;
}

static int stf_vin_clk_enable(struct stf_vin2_dev *vin_dev)
{
	struct stfcamss *stfcamss = vin_dev->stfcamss;

	reset_control_deassert(stfcamss->sys_rst[STFRST_WRAPPER_C].rstc);
	reset_control_deassert(stfcamss->sys_rst[STFRST_WRAPPER_P].rstc);
	reset_control_deassert(stfcamss->sys_rst[STFRST_PCLK].rstc);
	reset_control_deassert(stfcamss->sys_rst[STFRST_SYS_CLK].rstc);
	reset_control_deassert(stfcamss->sys_rst[STFRST_AXIRD].rstc);
	reset_control_deassert(stfcamss->sys_rst[STFRST_AXIWR].rstc);

	clk_prepare_enable(stfcamss->sys_clk[STFCLK_PCLK].clk);
	clk_prepare_enable(stfcamss->sys_clk[STFCLK_WRAPPER_CLK_C].clk);

#ifdef HWBOARD_FPGA
	clk_set_rate(stfcamss->sys_clk[STFCLK_APB_FUNC].clk, 51200000);
	clk_set_rate(stfcamss->sys_clk[STFCLK_SYS_CLK].clk, 307200000);
#else
	reg_set_bit(vin->clkgen_base, CLK_DOM4_APB_FUNC, CLK_MUX_SEL, 0x8);
	reg_set_bit(vin->clkgen_base, CLK_U0_VIN_SYS_CLK, CLK_MUX_SEL, 0x2);
#endif

	clk_set_phase(stfcamss->sys_clk[STFCLK_DVP_INV].clk, 0);
	clk_set_parent(stfcamss->sys_clk[STFCLK_WRAPPER_CLK_C].clk,
		stfcamss->sys_clk[STFCLK_DVP_INV].clk);
	clk_set_parent(stfcamss->sys_clk[STFCLK_AXIWR].clk,
		stfcamss->sys_clk[STFCLK_DVP_INV].clk);

	return 0;
}


static int stf_vin_clk_disable(struct stf_vin2_dev *vin_dev)
{
	struct stfcamss *stfcamss = vin_dev->stfcamss;

	reset_control_assert(stfcamss->sys_rst[STFRST_PCLK].rstc);
	reset_control_assert(stfcamss->sys_rst[STFRST_SYS_CLK].rstc);
	reset_control_assert(stfcamss->sys_rst[STFRST_AXIRD].rstc);
	reset_control_assert(stfcamss->sys_rst[STFRST_AXIWR].rstc);

	clk_disable_unprepare(stfcamss->sys_clk[STFCLK_PCLK].clk);

	return 0;
}

static int stf_vin_config_set(struct stf_vin2_dev *vin_dev)
{
	return 0;
}

static int stf_vin_wr_stream_set(struct stf_vin2_dev *vin_dev, int on)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	struct stfcamss *stfcamss = vin_dev->stfcamss;

	print_reg(ST_VIN, vin->sysctrl_base, SYSCONSAIF_SYSCFG_20);
	if (on) {
		reg_set(vin->sysctrl_base, SYSCONSAIF_SYSCFG_20, U0_VIN_CNFG_AXIWR0_EN);	  
	} else {
		reset_control_assert(stfcamss->sys_rst[STFRST_AXIWR].rstc);
		usleep_range(500, 1000);
		reg_clear(vin->sysctrl_base, SYSCONSAIF_SYSCFG_20, U0_VIN_CNFG_AXIWR0_EN);
	}
	print_reg(ST_VIN, vin->sysctrl_base, SYSCONSAIF_SYSCFG_20);

	return 0;
}

static void stf_vin_wr_irq_enable(struct stf_vin2_dev *vin_dev,
		int enable)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	unsigned int value = 0;

	if (enable) {
		value = ~(0x1 << 1);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_28, 
			U0_VIN_CNFG_AXIWR0_MASK, 
			value);
	} else {
		/* clear vin interrupt */
		value = 0x1 << 1;
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_28, 
			U0_VIN_CNFG_AXIWR0_INTR_CLEAN, 
			0x1);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_28, 
			U0_VIN_CNFG_AXIWR0_INTR_CLEAN, 
			0x0);
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_28, 
			U0_VIN_CNFG_AXIWR0_MASK, 
			value);
	}
}

static void stf_vin_wr_rd_set_addr(struct stf_vin2_dev *vin_dev,
		dma_addr_t wr_addr, dma_addr_t rd_addr)
{
#if 0
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;

	/* set the start address*/
	reg_write(vin->sysctrl_base,
			SYSCTRL_VIN_WR_START_ADDR, (long)wr_addr);
	reg_write(vin->sysctrl_base,
			SYSCTRL_VIN_RD_END_ADDR, (long)rd_addr);
#endif
}

void stf_vin_wr_set_ping_addr(struct stf_vin2_dev *vin_dev,
		dma_addr_t addr)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;

	/* set the start address */
	reg_write(vin->sysctrl_base,  SYSCONSAIF_SYSCFG_24, (long)addr);
}

void stf_vin_wr_set_pong_addr(struct stf_vin2_dev *vin_dev, dma_addr_t addr)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;

	/* set the start address */
	reg_write(vin->sysctrl_base, SYSCONSAIF_SYSCFG_32, (long)addr);
}

void stf_vin_isp_set_yuv_addr(struct stf_vin2_dev *vin_dev, int isp_id,
				dma_addr_t y_addr, dma_addr_t uv_addr)
{

	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase =
		isp_id ? vin->isp_isp1_base : vin->isp_isp0_base;

	reg_write(ispbase, ISP_REG_Y_PLANE_START_ADDR, y_addr);
	reg_write(ispbase, ISP_REG_UV_PLANE_START_ADDR, uv_addr);
	// shadow update
	//reg_set_bit(ispbase, ISP_REG_IESHD_ADDR, BIT(1) | BIT(0), 0x3);    //fw no configure  2021 1110
}

void stf_vin_isp_set_raw_addr(struct stf_vin2_dev *vin_dev, int isp_id,
				dma_addr_t raw_addr)
{

#if 0
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase =
		isp_id ? vin->isp_isp1_base : vin->isp_isp0_base;

	reg_write(ispbase, ISP_REG_DUMP_CFG_0, raw_addr);
	reg_set_bit(ispbase, ISP_REG_CSIINTS_ADDR, 0x3FFFF, 0x3000a);
#endif
}

void dump_vin_reg(void *__iomem regbase)
{
	st_debug(ST_VIN, "DUMP VIN register:\n");
	print_reg(ST_VIN, regbase, 0x00);
	print_reg(ST_VIN, regbase, 0x04);
	print_reg(ST_VIN, regbase, 0x08);
	print_reg(ST_VIN, regbase, 0x0c);
	print_reg(ST_VIN, regbase, 0x10);
	print_reg(ST_VIN, regbase, 0x14);
	print_reg(ST_VIN, regbase, 0x18);
	print_reg(ST_VIN, regbase, 0x1c);
	print_reg(ST_VIN, regbase, 0x20);
	print_reg(ST_VIN, regbase, 0x24);
	print_reg(ST_VIN, regbase, 0x28);
	print_reg(ST_VIN, regbase, 0x2c);
	print_reg(ST_VIN, regbase, 0x30);
	print_reg(ST_VIN, regbase, 0x34);
	print_reg(ST_VIN, regbase, 0x38);
	print_reg(ST_VIN, regbase, 0x3c);
	print_reg(ST_VIN, regbase, 0x40);
	print_reg(ST_VIN, regbase, 0x44);
	print_reg(ST_VIN, regbase, 0x48);
	print_reg(ST_VIN, regbase, 0x4c);
	print_reg(ST_VIN, regbase, 0x50);
	print_reg(ST_VIN, regbase, 0x54);
	print_reg(ST_VIN, regbase, 0x58);
	print_reg(ST_VIN, regbase, 0x5c);
}

struct vin_hw_ops vin_ops = {
	.vin_clk_init          = stf_vin_clk_init,
	.vin_clk_enable        = stf_vin_clk_enable,
	.vin_clk_disable       = stf_vin_clk_disable,
	.vin_config_set        = stf_vin_config_set,
	.vin_wr_stream_set     = stf_vin_wr_stream_set,
	.vin_wr_irq_enable     = stf_vin_wr_irq_enable,
	.wr_rd_set_addr        = stf_vin_wr_rd_set_addr,
	.vin_wr_set_ping_addr  = stf_vin_wr_set_ping_addr,
	.vin_wr_set_pong_addr  = stf_vin_wr_set_pong_addr,
	.vin_isp_set_yuv_addr  = stf_vin_isp_set_yuv_addr,
	.vin_isp_set_raw_addr  = stf_vin_isp_set_raw_addr,
	.vin_wr_irq_handler    = stf_vin_wr_irq_handler,
	.vin_isp_irq_handler   = stf_vin_isp_irq_handler,

};
