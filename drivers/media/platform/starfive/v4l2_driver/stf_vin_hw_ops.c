// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#include "stfcamss.h"
#include <linux/of_graph.h>
#include <media/v4l2-async.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <linux/clk-provider.h>
#include <soc/starfive/jh7110_pmu.h>

static void vin_intr_clear(void __iomem *sysctrl_base)
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

	vin_dev->hw_ops->isr_change_buffer(&vin_dev->line[VIN_LINE_WR]);
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
	u32 int_status;
	int isp_id = irq == vin->isp0_irq ? 0 : 1;

	if (isp_id == 0)
		ispbase = vin->isp_isp0_base;
	else
		ispbase = vin->isp_isp1_base;

	int_status = reg_read(ispbase, ISP_REG_ISP_CTRL_0);

	if (int_status & BIT(24)) {
		if ((int_status & BIT(20)))
			vin_dev->hw_ops->isr_buffer_done(
				&vin_dev->line[VIN_LINE_ISP0 + isp_id], &params);

#ifndef ISP_USE_CSI_AND_SC_DONE_INTERRUPT
		if (int_status & BIT(25))
			vin_dev->hw_ops->isr_buffer_done(
				&vin_dev->line[VIN_LINE_ISP0_RAW + isp_id], &params);

		/* clear interrupt */
		reg_write(ispbase, ISP_REG_ISP_CTRL_0, (int_status & ~EN_INT_ALL)
				| EN_INT_ISP_DONE | EN_INT_CSI_DONE | EN_INT_SC_DONE);
#else
		/* clear interrupt */
		reg_write(ispbase, ISP_REG_ISP_CTRL_0,
			(int_status & ~EN_INT_ALL) | EN_INT_ISP_DONE);
#endif
	} else
		st_debug(ST_VIN, "%s, Unknown interrupt!!!\n", __func__);

	return IRQ_HANDLED;
}

static irqreturn_t stf_vin_isp_csi_irq_handler(int irq, void *priv)
{
	struct stf_vin2_dev *vin_dev = priv;
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase;
	u32 int_status;
	int isp_id = irq == vin->isp0_csi_irq ? 0 : 1;

	if (isp_id == 0)
		ispbase = vin->isp_isp0_base;
	else
		ispbase = vin->isp_isp1_base;

	int_status = reg_read(ispbase, ISP_REG_ISP_CTRL_0);

	if (int_status & BIT(25)) {
		/* clear interrupt */
		reg_write(ispbase, ISP_REG_ISP_CTRL_0,
			(int_status & ~EN_INT_ALL) | EN_INT_CSI_DONE);
	} else
		st_debug(ST_VIN, "%s, Unknown interrupt!!!\n", __func__);

	return IRQ_HANDLED;
}

static irqreturn_t stf_vin_isp_scd_irq_handler(int irq, void *priv)
{
	struct stf_vin2_dev *vin_dev = priv;
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase;
	u32 int_status;
	int isp_id = irq == vin->isp0_scd_irq ? 0 : 1;

	if (isp_id == 0)
		ispbase = vin->isp_isp0_base;
	else
		ispbase = vin->isp_isp1_base;

	int_status = reg_read(ispbase, ISP_REG_ISP_CTRL_0);

	if (int_status & BIT(26)) {
		/* clear interrupt */
		reg_write(ispbase, ISP_REG_ISP_CTRL_0, (int_status & ~EN_INT_ALL) | EN_INT_SC_DONE);
	} else
		st_debug(ST_VIN, "%s, Unknown interrupt!!!\n", __func__);

	return IRQ_HANDLED;
}

static irqreturn_t stf_vin_isp_irq_csiline_handler(int irq, void *priv)
{
	struct stf_vin2_dev *vin_dev = priv;
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	struct stf_isp_dev *isp_dev;
	void __iomem *ispbase;
	u32 int_status, value;
	int isp_id = irq == vin->isp0_irq_csiline ? 0 : 1;

	if (isp_id == 0)
		ispbase = vin->isp_isp0_base;
	else
		ispbase = vin->isp_isp1_base;
	isp_dev = &vin_dev->stfcamss->isp_dev[isp_id];

	int_status = reg_read(ispbase, ISP_REG_ISP_CTRL_0);
	if (int_status & BIT(27)) {
		if (!atomic_read(&isp_dev->shadow_count)) {
			if ((int_status & BIT(20)))
				vin_dev->hw_ops->isr_change_buffer(
					&vin_dev->line[VIN_LINE_ISP0 + isp_id]);

			value = reg_read(ispbase, ISP_REG_CSI_MODULE_CFG);
			if ((value & BIT(19)))
				vin_dev->hw_ops->isr_change_buffer(
					&vin_dev->line[VIN_LINE_ISP0_RAW + isp_id]);

			/* shadow update */
			reg_set_bit(ispbase, ISP_REG_CSIINTS_ADDR, 0x30000, 0x30000);
			reg_set_bit(ispbase, ISP_REG_IESHD_ADDR, BIT(1) | BIT(0), 0x3);
		} else {
			st_err_ratelimited(ST_VIN,
				"isp%d shadow_lock locked. skip this frame\n", isp_id);
		}

		/* clear interrupt */
		reg_write(ispbase, ISP_REG_ISP_CTRL_0,
			(int_status & ~EN_INT_ALL) | EN_INT_LINE_INT);
	} else
		st_debug(ST_VIN, "%s, Unknown interrupt!!!\n", __func__);

	return IRQ_HANDLED;
}

static int stf_vin_top_clk_init(struct stf_vin2_dev *vin_dev)
{
	struct stfcamss *stfcamss = vin_dev->stfcamss;

	starfive_power_domain_set(POWER_DOMAIN_ISP, 1);

	if (!__clk_is_enabled(stfcamss->sys_clk[STFCLK_NOC_BUS_CLK_ISP_AXI].clk))
		clk_prepare_enable(stfcamss->sys_clk[STFCLK_NOC_BUS_CLK_ISP_AXI].clk);
	else
		st_warn(ST_VIN, "noc_bus_clk_isp_axi already enable\n");

	clk_prepare_enable(stfcamss->sys_clk[STFCLK_ISPCORE_2X].clk);
	clk_prepare_enable(stfcamss->sys_clk[STFCLK_ISP_AXI].clk);
	reset_control_deassert(stfcamss->sys_rst[STFRST_ISP_TOP_N].rstc);
	reset_control_deassert(stfcamss->sys_rst[STFRST_ISP_TOP_AXI].rstc);

	return 0;
}

static int stf_vin_top_clk_deinit(struct stf_vin2_dev *vin_dev)
{
	struct stfcamss *stfcamss = vin_dev->stfcamss;

	reset_control_assert(stfcamss->sys_rst[STFRST_ISP_TOP_AXI].rstc);
	reset_control_assert(stfcamss->sys_rst[STFRST_ISP_TOP_N].rstc);
	clk_disable_unprepare(stfcamss->sys_clk[STFCLK_ISP_AXI].clk);
	clk_disable_unprepare(stfcamss->sys_clk[STFCLK_ISPCORE_2X].clk);

	starfive_power_domain_set(POWER_DOMAIN_ISP, 0);

	return 0;
}

static int stf_vin_clk_enable(struct stf_vin2_dev *vin_dev)
{
	struct stfcamss *stfcamss = vin_dev->stfcamss;

	clk_prepare_enable(stfcamss->sys_clk[STFCLK_PCLK].clk);
	clk_set_rate(stfcamss->sys_clk[STFCLK_APB_FUNC].clk, 51200000);
	clk_set_rate(stfcamss->sys_clk[STFCLK_SYS_CLK].clk, 307200000);

	reset_control_deassert(stfcamss->sys_rst[STFRST_PCLK].rstc);
	reset_control_deassert(stfcamss->sys_rst[STFRST_SYS_CLK].rstc);

	return 0;
}


static int stf_vin_clk_disable(struct stf_vin2_dev *vin_dev)
{
	struct stfcamss *stfcamss = vin_dev->stfcamss;

	reset_control_assert(stfcamss->sys_rst[STFRST_PCLK].rstc);
	reset_control_assert(stfcamss->sys_rst[STFRST_SYS_CLK].rstc);

	clk_disable_unprepare(stfcamss->sys_clk[STFCLK_PCLK].clk);

	return 0;
}

static int stf_vin_config_set(struct stf_vin2_dev *vin_dev)
{
	return 0;
}

static int stf_vin_wr_stream_set(struct stf_vin2_dev *vin_dev, int on)
{
	struct stfcamss *stfcamss = vin_dev->stfcamss;
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;

	print_reg(ST_VIN, vin->sysctrl_base, SYSCONSAIF_SYSCFG_20);
	if (on) {
		reset_control_deassert(stfcamss->sys_rst[STFRST_AXIWR].rstc);
		reg_set(vin->sysctrl_base, SYSCONSAIF_SYSCFG_20, U0_VIN_CNFG_AXIWR0_EN);
	} else {
		reg_clear(vin->sysctrl_base, SYSCONSAIF_SYSCFG_20, U0_VIN_CNFG_AXIWR0_EN);
		reset_control_assert(stfcamss->sys_rst[STFRST_AXIWR].rstc);
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
#ifdef UNUSED_CODE
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
	// reg_set_bit(ispbase, ISP_REG_ISP_CTRL_0, BIT(0), 1);
}

void stf_vin_isp_set_raw_addr(struct stf_vin2_dev *vin_dev, int isp_id,
				dma_addr_t raw_addr)
{
#ifdef UNUSED_CODE
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
}

struct vin_hw_ops vin_ops = {
	.vin_top_clk_init      = stf_vin_top_clk_init,
	.vin_top_clk_deinit    = stf_vin_top_clk_deinit,
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
	.vin_isp_csi_irq_handler   = stf_vin_isp_csi_irq_handler,
	.vin_isp_scd_irq_handler   = stf_vin_isp_scd_irq_handler,
	.vin_isp_irq_csiline_handler   = stf_vin_isp_irq_csiline_handler,
};
