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
#include <linux/pm_runtime.h>

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
	struct dummy_buffer *dummy_buffer =
			&vin_dev->dummy_buffer[STF_DUMMY_VIN];

	if (atomic_dec_if_positive(&dummy_buffer->frame_skip) < 0) {
		vin_dev->hw_ops->isr_change_buffer(&vin_dev->line[VIN_LINE_WR]);
		vin_dev->hw_ops->isr_buffer_done(&vin_dev->line[VIN_LINE_WR], &params);
	}

	vin_intr_clear(vin->sysctrl_base);

	return IRQ_HANDLED;
}

static  void __iomem *stf_vin_get_ispbase(struct stf_vin_dev *vin)
{
	void __iomem *base = vin->isp_base;

	return base;
}

static irqreturn_t stf_vin_isp_irq_handler(int irq, void *priv)
{
	static struct vin_params params;
	struct stf_vin2_dev *vin_dev = priv;
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase;
	u32 int_status, value;

	ispbase = stf_vin_get_ispbase(vin);

	int_status = reg_read(ispbase, ISP_REG_ISP_CTRL_0);

	if (int_status & BIT(24)) {
		if ((int_status & BIT(11)))
			vin_dev->hw_ops->isr_buffer_done(
				&vin_dev->line[VIN_LINE_ISP_SS0], &params);

		if ((int_status & BIT(12)))
			vin_dev->hw_ops->isr_buffer_done(
				&vin_dev->line[VIN_LINE_ISP_SS1], &params);

		if ((int_status & BIT(20)))
			vin_dev->hw_ops->isr_buffer_done(
				&vin_dev->line[VIN_LINE_ISP], &params);

		value = reg_read(ispbase, ISP_REG_ITIDPSR);
		if ((value & BIT(17)))
			vin_dev->hw_ops->isr_buffer_done(
				&vin_dev->line[VIN_LINE_ISP_ITIW], &params);
		if ((value & BIT(16)))
			vin_dev->hw_ops->isr_buffer_done(
				&vin_dev->line[VIN_LINE_ISP_ITIR], &params);

#ifndef ISP_USE_CSI_AND_SC_DONE_INTERRUPT
		if (int_status & BIT(25))
			vin_dev->hw_ops->isr_buffer_done(
				&vin_dev->line[VIN_LINE_ISP_RAW], &params);

		if (int_status & BIT(26))
			vin_dev->hw_ops->isr_buffer_done(
				&vin_dev->line[VIN_LINE_ISP_SCD_Y], &params);

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
	static struct vin_params params;
	struct stf_vin2_dev *vin_dev = priv;
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase;
	u32 int_status;

	ispbase = stf_vin_get_ispbase(vin);

	int_status = reg_read(ispbase, ISP_REG_ISP_CTRL_0);

	if (int_status & BIT(25)) {
		vin_dev->hw_ops->isr_buffer_done(
			&vin_dev->line[VIN_LINE_ISP_RAW], &params);

		/* clear interrupt */
		reg_write(ispbase, ISP_REG_ISP_CTRL_0,
			(int_status & ~EN_INT_ALL) | EN_INT_CSI_DONE);
	} else
		st_debug(ST_VIN, "%s, Unknown interrupt!!!\n", __func__);

	return IRQ_HANDLED;
}

static irqreturn_t stf_vin_isp_scd_irq_handler(int irq, void *priv)
{
	static struct vin_params params;
	struct stf_vin2_dev *vin_dev = priv;
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase;
	u32 int_status;

	ispbase = stf_vin_get_ispbase(vin);

	int_status = reg_read(ispbase, ISP_REG_ISP_CTRL_0);

	if (int_status & BIT(26)) {
		vin_dev->hw_ops->isr_buffer_done(
			&vin_dev->line[VIN_LINE_ISP_SCD_Y], &params);

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

	ispbase = stf_vin_get_ispbase(vin);

	isp_dev = vin_dev->stfcamss->isp_dev;

	int_status = reg_read(ispbase, ISP_REG_ISP_CTRL_0);
	if (int_status & BIT(27)) {
		struct dummy_buffer *dummy_buffer =
			&vin_dev->dummy_buffer[STF_DUMMY_ISP];

		if (!atomic_read(&isp_dev->shadow_count)) {
			if (atomic_dec_if_positive(&dummy_buffer->frame_skip) < 0) {
				if ((int_status & BIT(11)))
					vin_dev->hw_ops->isr_change_buffer(
						&vin_dev->line[VIN_LINE_ISP_SS0]);
				if ((int_status & BIT(12)))
					vin_dev->hw_ops->isr_change_buffer(
						&vin_dev->line[VIN_LINE_ISP_SS1]);
				if ((int_status & BIT(20)))
					vin_dev->hw_ops->isr_change_buffer(
						&vin_dev->line[VIN_LINE_ISP]);

				value = reg_read(ispbase, ISP_REG_ITIDPSR);
				if ((value & BIT(17)))
					vin_dev->hw_ops->isr_change_buffer(
						&vin_dev->line[VIN_LINE_ISP_ITIW]);
				if ((value & BIT(16)))
					vin_dev->hw_ops->isr_change_buffer(
						&vin_dev->line[VIN_LINE_ISP_ITIR]);

				value = reg_read(ispbase, ISP_REG_CSI_MODULE_CFG);
				if ((value & BIT(19)))
					vin_dev->hw_ops->isr_change_buffer(
						&vin_dev->line[VIN_LINE_ISP_RAW]);
				if ((value & BIT(17)))
					vin_dev->hw_ops->isr_change_buffer(
						&vin_dev->line[VIN_LINE_ISP_SCD_Y]);
			}

			// shadow update
			reg_set_bit(ispbase, ISP_REG_CSIINTS_ADDR, 0x30000, 0x30000);
			reg_set_bit(ispbase, ISP_REG_IESHD_ADDR, BIT(1) | BIT(0), 0x3);
		} else {
			st_err_ratelimited(ST_VIN,
				"isp shadow_lock locked. skip this frame\n");
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
	int ret;

	pm_runtime_enable(stfcamss->dev);
	ret = pm_runtime_get_sync(stfcamss->dev);
	if (ret < 0) {
		dev_err(stfcamss->dev,
			"vin_clk_init: failed to get pm runtime: %d\n", ret);
		return ret;
    }

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

	pm_runtime_put_sync(stfcamss->dev);
	pm_runtime_disable(stfcamss->dev);

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

void stf_vin_isp_set_yuv_addr(struct stf_vin2_dev *vin_dev,
				dma_addr_t y_addr, dma_addr_t uv_addr)
{

	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase = stf_vin_get_ispbase(vin);

	reg_write(ispbase, ISP_REG_Y_PLANE_START_ADDR, y_addr);
	reg_write(ispbase, ISP_REG_UV_PLANE_START_ADDR, uv_addr);
	// reg_set_bit(ispbase, ISP_REG_ISP_CTRL_0, BIT(0), 1);
}

void stf_vin_isp_set_raw_addr(struct stf_vin2_dev *vin_dev,
				dma_addr_t raw_addr)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase = stf_vin_get_ispbase(vin);

	reg_write(ispbase, ISP_REG_DUMP_CFG_0, raw_addr);
}

void stf_vin_isp_set_ss0_addr(struct stf_vin2_dev *vin_dev,
				dma_addr_t y_addr, dma_addr_t uv_addr)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase = stf_vin_get_ispbase(vin);

	reg_write(ispbase, ISP_REG_SS0AY, y_addr);
	reg_write(ispbase, ISP_REG_SS0AUV, uv_addr);
}

void stf_vin_isp_set_ss1_addr(struct stf_vin2_dev *vin_dev,
				dma_addr_t y_addr, dma_addr_t uv_addr)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase = stf_vin_get_ispbase(vin);

	reg_write(ispbase, ISP_REG_SS1AY, y_addr);
	reg_write(ispbase, ISP_REG_SS1AUV, uv_addr);
}

void stf_vin_isp_set_itiw_addr(struct stf_vin2_dev *vin_dev,
				dma_addr_t y_addr, dma_addr_t uv_addr)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase = stf_vin_get_ispbase(vin);

	reg_write(ispbase, ISP_REG_ITIDWYSAR, y_addr);
	reg_write(ispbase, ISP_REG_ITIDWUSAR, uv_addr);
}

void stf_vin_isp_set_itir_addr(struct stf_vin2_dev *vin_dev,
				dma_addr_t y_addr, dma_addr_t uv_addr)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase = stf_vin_get_ispbase(vin);

	reg_write(ispbase, ISP_REG_ITIDRYSAR, y_addr);
	reg_write(ispbase, ISP_REG_ITIDRUSAR, uv_addr);
}

int stf_vin_isp_get_scd_type(struct stf_vin2_dev *vin_dev)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase = stf_vin_get_ispbase(vin);

	return (reg_read(ispbase, ISP_REG_SC_CFG_1) & (0x3 << 30)) >> 30;
}

void stf_vin_isp_set_scd_addr(struct stf_vin2_dev *vin_dev,
				dma_addr_t yhist_addr, dma_addr_t scd_addr, int scd_type)
{
	struct stf_vin_dev *vin = vin_dev->stfcamss->vin;
	void __iomem *ispbase = stf_vin_get_ispbase(vin);

	reg_set_bit(ispbase, ISP_REG_SC_CFG_1, 0x3 << 30, scd_type << 30);
	reg_write(ispbase, ISP_REG_SCD_CFG_0, scd_addr);
	reg_write(ispbase, ISP_REG_YHIST_CFG_4, yhist_addr);
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
	.vin_isp_set_ss0_addr  = stf_vin_isp_set_ss0_addr,
	.vin_isp_set_ss1_addr  = stf_vin_isp_set_ss1_addr,
	.vin_isp_set_itiw_addr  = stf_vin_isp_set_itiw_addr,
	.vin_isp_set_itir_addr  = stf_vin_isp_set_itir_addr,
	.vin_isp_set_scd_addr  = stf_vin_isp_set_scd_addr,
	.vin_isp_get_scd_type  = stf_vin_isp_get_scd_type,
	.vin_wr_irq_handler    = stf_vin_wr_irq_handler,
	.vin_isp_irq_handler   = stf_vin_isp_irq_handler,
	.vin_isp_csi_irq_handler   = stf_vin_isp_csi_irq_handler,
	.vin_isp_scd_irq_handler   = stf_vin_isp_scd_irq_handler,
	.vin_isp_irq_csiline_handler   = stf_vin_isp_irq_csiline_handler,
};
