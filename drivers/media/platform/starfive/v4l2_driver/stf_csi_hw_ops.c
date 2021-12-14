// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/media/platform/starfive/stf_csi.c
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 *
 */
#include "stfcamss.h"

#define CSI2RX_DEVICE_CFG_REG			0x000

#define CSI2RX_SOFT_RESET_REG			0x004
#define CSI2RX_SOFT_RESET_PROTOCOL		BIT(1)
#define CSI2RX_SOFT_RESET_FRONT			BIT(0)

#define CSI2RX_DPHY_LANE_CONTROL                0x040

#define CSI2RX_STATIC_CFG_REG			0x008
#define CSI2RX_STATIC_CFG_DLANE_MAP(llane, plane)	\
		((plane) << (16 + (llane) * 4))
#define CSI2RX_STATIC_CFG_LANES_MASK		GENMASK(11, 8)

#define CSI2RX_STREAM_BASE(n)		(((n) + 1) * 0x100)

#define CSI2RX_STREAM_CTRL_REG(n)		(CSI2RX_STREAM_BASE(n) + 0x000)
#define CSI2RX_STREAM_CTRL_START		BIT(0)

#define CSI2RX_STREAM_DATA_CFG_REG(n)		(CSI2RX_STREAM_BASE(n) + 0x008)
#define CSI2RX_STREAM_DATA_CFG_EN_VC_SELECT	BIT(31)
#define CSI2RX_STREAM_DATA_CFG_VC_SELECT(n)	BIT((n) + 16)

#define CSI2RX_STREAM_CFG_REG(n)		(CSI2RX_STREAM_BASE(n) + 0x00c)
#define CSI2RX_STREAM_CFG_FIFO_MODE_LARGE_BUF	(1 << 8)

#define CSI2RX_LANES_MAX	4
#define CSI2RX_STREAMS_MAX	4

static int apb_clk_set(struct stf_vin_dev *vin, int on)
{
	static int init_flag;
	static struct mutex count_lock;
	static int count;

	if (!init_flag) {
		init_flag = 1;
		mutex_init(&count_lock);
	}
	mutex_lock(&count_lock);
	if (on) {
		if (count == 0)
			reg_set_highest_bit(vin->clkgen_base,
					CLK_CSI2RX0_APB_CTRL);
		count++;
	} else {
		if (count == 0)
			goto exit;
		if (count == 1)
			reg_clr_highest_bit(vin->clkgen_base,
					CLK_CSI2RX0_APB_CTRL);
		count--;
	}
exit:
	mutex_unlock(&count_lock);
	return 0;

}
static int stf_csi_clk_enable(struct stf_csi_dev *csi_dev)
{
	struct stf_vin_dev *vin = csi_dev->stfcamss->vin;

	// reg_set_highest_bit(vin->clkgen_base, CLK_CSI2RX0_APB_CTRL);
	apb_clk_set(vin, 1);

	if (csi_dev->id == 0) {
		reg_set_bit(vin->clkgen_base,
				CLK_MIPI_RX0_PXL_CTRL,
				0x1F, 0x3);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_0_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_1_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_2_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_3_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_SYS0_CTRL);
	} else {
		reg_set_bit(vin->clkgen_base,
				CLK_MIPI_RX1_PXL_CTRL,
				0x1F, 0x3);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_0_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_1_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_2_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_3_CTRL);
		reg_set_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_SYS1_CTRL);
	}

	return 0;
}

static int stf_csi_clk_disable(struct stf_csi_dev *csi_dev)
{
	struct stf_vin_dev *vin = csi_dev->stfcamss->vin;

	// reg_clr_highest_bit(vin->clkgen_base, CLK_CSI2RX0_APB_CTRL);
	apb_clk_set(vin, 0);

	if (csi_dev->id == 0) {
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_0_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_1_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_2_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_PXL_3_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX0_SYS0_CTRL);
	} else {
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_0_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_1_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_2_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_PXL_3_CTRL);
		reg_clr_highest_bit(vin->clkgen_base, CLK_MIPI_RX1_SYS1_CTRL);
	}

	return 0;
}

static int stf_csi_config_set(struct stf_csi_dev *csi_dev)
{
	struct stf_vin_dev *vin = csi_dev->stfcamss->vin;
	u32 mipi_channel_sel, mipi_vc = 0;

	st_info(ST_CSI, "%s, csi_id = %d\n", __func__, csi_dev->id);

	switch (csi_dev->s_type) {
	case SENSOR_VIN:
		st_err(ST_CSI, "%s, %d: need todo\n", __func__, __LINE__);
		break;
	case SENSOR_ISP0:
		reg_set_bit(vin->clkgen_base,
				CLK_ISP0_MIPI_CTRL,
				BIT(24), csi_dev->id << 24);

		reg_set_bit(vin->clkgen_base,
				CLK_C_ISP0_CTRL,
				BIT(25) | BIT(24),
				csi_dev->id << 24);

		mipi_channel_sel = csi_dev->id * 4 + mipi_vc;
		reg_set_bit(vin->sysctrl_base,
				SYSCTRL_VIN_SRC_CHAN_SEL,
				0xF, mipi_channel_sel);
		break;
	case SENSOR_ISP1:
		reg_set_bit(vin->clkgen_base,
				CLK_ISP1_MIPI_CTRL,
				BIT(24), csi_dev->id << 24);

		reg_set_bit(vin->clkgen_base,
				CLK_C_ISP1_CTRL,
				BIT(25) | BIT(24),
				csi_dev->id << 24);

		mipi_channel_sel = csi_dev->id * 4 + mipi_vc;
		reg_set_bit(vin->sysctrl_base,
				SYSCTRL_VIN_SRC_CHAN_SEL,
				0xF << 4, mipi_channel_sel << 4);
	default:
		break;
	}

	return 0;
}

static int stf_csi_set_format(struct stf_csi_dev *csi_dev,
			u32 vsize, u8 bpp, int is_raw10)
{
	struct stf_vin_dev *vin = csi_dev->stfcamss->vin;
	void *reg_base = NULL;

	if (csi_dev->id == 0)
		reg_base = vin->mipi0_base;
	else if (csi_dev->id == 1)
		reg_base = vin->mipi1_base;
	else
		return 0;

	switch (csi_dev->s_type) {
	case SENSOR_VIN:
		st_err(ST_CSI, "%s, %d: need todo\n", __func__, __LINE__);
		break;
	case SENSOR_ISP0:
		if (is_raw10)
			reg_set_bit(vin->sysctrl_base,
					SYSCTRL_VIN_SRC_DW_SEL,
					BIT(4), 1 << 4);
		break;
	case SENSOR_ISP1:
		if (is_raw10)
			reg_set_bit(vin->sysctrl_base,
					SYSCTRL_VIN_SRC_DW_SEL,
					BIT(5), 1 << 5);
	default:
		break;
	}

	return 0;
}

static void csi2rx_reset(void *reg_base)
{
	writel(CSI2RX_SOFT_RESET_PROTOCOL | CSI2RX_SOFT_RESET_FRONT,
	       reg_base + CSI2RX_SOFT_RESET_REG);

	udelay(10);

	writel(0, reg_base + CSI2RX_SOFT_RESET_REG);
}

static void csi2rx_debug_config(void *reg_base, u32 frame_lines)
{
	// data_id, ecc, crc error to irq
	union error_bypass_cfg err_bypass_cfg = {
		.data_id = 0,
		.ecc     = 0,
		.crc     = 0,
	};
	union stream_monitor_ctrl stream0_monitor_ctrl = {
		.frame_length = frame_lines,
		.frame_mon_en = 1,
		.frame_mon_vc = 0,
		.lb_en = 1,
		.lb_vc = 0,
	};
	reg_write(reg_base, ERROR_BYPASS_CFG, err_bypass_cfg.value);
	reg_write(reg_base, MONITOR_IRQS_MASK_CFG, 0xffffffff);
	reg_write(reg_base, INFO_IRQS_MASK_CFG, 0xffffffff);
	reg_write(reg_base, ERROR_IRQS_MASK_CFG, 0xffffffff);
	reg_write(reg_base, DPHY_ERR_IRQ_MASK_CFG, 0xffffffff);

	reg_write(reg_base, STREAM0_MONITOR_CTRL, stream0_monitor_ctrl.value);
	reg_write(reg_base, STREAM0_FCC_CTRL, (0x0 << 1) | 0x1);
}

static int csi2rx_start(struct stf_csi_dev *csi_dev)
{
	struct stfcamss *stfcamss = csi_dev->stfcamss;
	struct stf_vin_dev *vin = stfcamss->vin;
	struct csi2phy_cfg *csiphy =
		stfcamss->csiphy_dev[csi_dev->csiphy_id].csiphy;
	unsigned int i;
	unsigned long lanes_used = 0;
	u32 reg;
	int ret;
	void *reg_base = NULL;

	if (!csiphy) {
		st_err(ST_CSI, "csiphy%d sensor not exist use csiphy%d init.\n",
				csi_dev->csiphy_id, !csi_dev->csiphy_id);
		csiphy = stfcamss->csiphy_dev[!csi_dev->csiphy_id].csiphy;
		if (!csiphy) {
			st_err(ST_CSI, "csiphy%d sensor not exist\n",
				!csi_dev->csiphy_id);
			return -EINVAL;
		}
	}

	if (csi_dev->id == 0)
		reg_base = vin->mipi0_base;
	else if (csi_dev->id == 1)
		reg_base = vin->mipi1_base;
	else
		return 0;

	csi2rx_reset(reg_base);

	reg = csiphy->num_data_lanes << 8;
	for (i = 0; i < csiphy->num_data_lanes; i++) {
#ifndef USE_CSIDPHY_ONE_CLK_MODE
		reg |= CSI2RX_STATIC_CFG_DLANE_MAP(i, csiphy->data_lanes[i]);
		set_bit(csiphy->data_lanes[i] - 1, &lanes_used);
#else
		reg |= CSI2RX_STATIC_CFG_DLANE_MAP(i, i + 1);
		set_bit(i, &lanes_used);
#endif
	}

	/*
	 * Even the unused lanes need to be mapped. In order to avoid
	 * to map twice to the same physical lane, keep the lanes used
	 * in the previous loop, and only map unused physical lanes to
	 * the rest of our logical lanes.
	 */
	for (i = csiphy->num_data_lanes; i < CSI2RX_LANES_MAX; i++) {
		unsigned int idx = find_first_zero_bit(&lanes_used,
						       CSI2RX_LANES_MAX);

		set_bit(idx, &lanes_used);
		reg |= CSI2RX_STATIC_CFG_DLANE_MAP(i, idx + 1);
	}

	writel(reg, reg_base + CSI2RX_STATIC_CFG_REG);

	// 0x40 DPHY_LANE_CONTROL
	reg = 0;
#ifndef USE_CSIDPHY_ONE_CLK_MODE
	for (i = 0; i < csiphy->num_data_lanes; i++)
		reg |= 1 << (csiphy->data_lanes[i] - 1)
			| 1 << (csiphy->data_lanes[i] + 11);
#else
	for (i = 0; i < csiphy->num_data_lanes; i++)
		reg |= 1 << i | 1 << (i + 12); 
#endif

	reg |= 1 << 4 | 1 << 16;
	writel(reg, reg_base + CSI2RX_DPHY_LANE_CONTROL);

	// csi2rx_debug_config(reg_base, 1080);

	/*
	 * Create a static mapping between the CSI virtual channels
	 * and the output stream.
	 *
	 * This should be enhanced, but v4l2 lacks the support for
	 * changing that mapping dynamically.
	 *
	 * We also cannot enable and disable independent streams here,
	 * hence the reference counting.
	 */
	for (i = 0; i < CSI2RX_STREAMS_MAX; i++) {
		writel(CSI2RX_STREAM_CFG_FIFO_MODE_LARGE_BUF,
		       reg_base + CSI2RX_STREAM_CFG_REG(i));

		writel(CSI2RX_STREAM_DATA_CFG_EN_VC_SELECT |
		       CSI2RX_STREAM_DATA_CFG_VC_SELECT(i),
		       reg_base + CSI2RX_STREAM_DATA_CFG_REG(i));

		writel(CSI2RX_STREAM_CTRL_START,
		       reg_base + CSI2RX_STREAM_CTRL_REG(i));
	}

	return 0;
}

static void csi2rx_stop(void *reg_base)
{
	unsigned int i;

	for (i = 0; i < CSI2RX_STREAMS_MAX; i++)
		writel(0, reg_base + CSI2RX_STREAM_CTRL_REG(i));
}

static int stf_csi_stream_set(struct stf_csi_dev *csi_dev, int on)
{
	struct stf_vin_dev *vin = csi_dev->stfcamss->vin;
	void *reg_base = NULL;

	if (csi_dev->id == 0)
		reg_base = vin->mipi0_base;
	else if (csi_dev->id == 1)
		reg_base = vin->mipi1_base;
	else
		return 0;

	if (on)
		csi2rx_start(csi_dev);
	else
		csi2rx_stop(reg_base);

	return 0;
}

void dump_csi_reg(void *__iomem csibase, int id)
{
	st_info(ST_CSI, "DUMP CSI%d register:\n", id);
	print_reg(ST_CSI, csibase, 0x00);
	print_reg(ST_CSI, csibase, 0x04);
	print_reg(ST_CSI, csibase, 0x08);
	print_reg(ST_CSI, csibase, 0x10);

	print_reg(ST_CSI, csibase, 0x40);
	print_reg(ST_CSI, csibase, 0x48);
	print_reg(ST_CSI, csibase, 0x4c);
	print_reg(ST_CSI, csibase, 0x50);
}

struct csi_hw_ops csi_ops = {
	.csi_clk_enable        = stf_csi_clk_enable,
	.csi_clk_disable       = stf_csi_clk_disable,
	.csi_config_set        = stf_csi_config_set,
	.csi_set_format        = stf_csi_set_format,
	.csi_stream_set        = stf_csi_stream_set,
};
