// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - CSI-2 Receiver
 *
 * Copyright (C) 2019 Collabora, Ltd.
 * Copyright (C) 2022 Ideas on Board
 *
 * Based on Rockchip ISP1 driver by Rockchip Electronics Co., Ltd.
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include <linux/device.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>

#include <media/v4l2-ctrls.h>

#include "rkisp1-common.h"
#include "rkisp1-csi.h"

static int rkisp1_config_mipi(struct rkisp1_csi *csi)
{
	struct rkisp1_device *rkisp1 = csi->rkisp1;
	const struct rkisp1_mbus_info *sink_fmt = rkisp1->isp.sink_fmt;
	unsigned int lanes = rkisp1->active_sensor->lanes;
	u32 mipi_ctrl;

	if (lanes < 1 || lanes > 4)
		return -EINVAL;

	mipi_ctrl = RKISP1_CIF_MIPI_CTRL_NUM_LANES(lanes - 1) |
		    RKISP1_CIF_MIPI_CTRL_SHUTDOWNLANES(0xf) |
		    RKISP1_CIF_MIPI_CTRL_ERR_SOT_SYNC_HS_SKIP |
		    RKISP1_CIF_MIPI_CTRL_CLOCKLANE_ENA;

	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_CTRL, mipi_ctrl);

	/* V12 could also use a newer csi2-host, but we don't want that yet */
	if (rkisp1->info->isp_ver == RKISP1_V12)
		rkisp1_write(rkisp1, RKISP1_CIF_ISP_CSI0_CTRL0, 0);

	/* Configure Data Type and Virtual Channel */
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_IMG_DATA_SEL,
		     RKISP1_CIF_MIPI_DATA_SEL_DT(sink_fmt->mipi_dt) |
		     RKISP1_CIF_MIPI_DATA_SEL_VC(0));

	/* Clear MIPI interrupts */
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_ICR, ~0);

	/*
	 * Disable RKISP1_CIF_MIPI_ERR_DPHY interrupt here temporary for
	 * isp bus may be dead when switch isp.
	 */
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_IMSC,
		     RKISP1_CIF_MIPI_FRAME_END | RKISP1_CIF_MIPI_ERR_CSI |
		     RKISP1_CIF_MIPI_ERR_DPHY |
		     RKISP1_CIF_MIPI_SYNC_FIFO_OVFLW(0x03) |
		     RKISP1_CIF_MIPI_ADD_DATA_OVFLW);

	dev_dbg(rkisp1->dev, "\n  MIPI_CTRL 0x%08x\n"
		"  MIPI_IMG_DATA_SEL 0x%08x\n"
		"  MIPI_STATUS 0x%08x\n"
		"  MIPI_IMSC 0x%08x\n",
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMG_DATA_SEL),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_STATUS),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMSC));

	return 0;
}

static void rkisp1_mipi_start(struct rkisp1_csi *csi)
{
	struct rkisp1_device *rkisp1 = csi->rkisp1;
	u32 val;

	val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL);
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_CTRL,
		     val | RKISP1_CIF_MIPI_CTRL_OUTPUT_ENA);
}

static void rkisp1_mipi_stop(struct rkisp1_csi *csi)
{
	struct rkisp1_device *rkisp1 = csi->rkisp1;
	u32 val;

	/* Mask and clear interrupts. */
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_IMSC, 0);
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_ICR, ~0);

	val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL);
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_CTRL,
		     val & (~RKISP1_CIF_MIPI_CTRL_OUTPUT_ENA));
}

int rkisp1_mipi_csi2_start(struct rkisp1_csi *csi,
			   struct rkisp1_sensor_async *sensor)
{
	struct rkisp1_device *rkisp1 = csi->rkisp1;
	union phy_configure_opts opts;
	struct phy_configure_opts_mipi_dphy *cfg = &opts.mipi_dphy;
	s64 pixel_clock;
	int ret;

	ret = rkisp1_config_mipi(csi);
	if (ret)
		return ret;

	pixel_clock = v4l2_ctrl_g_ctrl_int64(sensor->pixel_rate_ctrl);
	if (!pixel_clock) {
		dev_err(rkisp1->dev, "Invalid pixel rate value\n");
		return -EINVAL;
	}

	phy_mipi_dphy_get_default_config(pixel_clock,
					 rkisp1->isp.sink_fmt->bus_width,
					 sensor->lanes, cfg);
	phy_set_mode(csi->dphy, PHY_MODE_MIPI_DPHY);
	phy_configure(csi->dphy, &opts);
	phy_power_on(csi->dphy);

	rkisp1_mipi_start(csi);

	return 0;
}

void rkisp1_mipi_csi2_stop(struct rkisp1_csi *csi)
{
	rkisp1_mipi_stop(csi);

	phy_power_off(csi->dphy);
}

irqreturn_t rkisp1_mipi_isr(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp1_device *rkisp1 = dev_get_drvdata(dev);
	u32 val, status;

	status = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_MIS);
	if (!status)
		return IRQ_NONE;

	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_ICR, status);

	/*
	 * Disable DPHY errctrl interrupt, because this dphy
	 * erctrl signal is asserted until the next changes
	 * of line state. This time is may be too long and cpu
	 * is hold in this interrupt.
	 */
	if (status & RKISP1_CIF_MIPI_ERR_CTRL(0x0f)) {
		val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMSC);
		rkisp1_write(rkisp1, RKISP1_CIF_MIPI_IMSC,
			     val & ~RKISP1_CIF_MIPI_ERR_CTRL(0x0f));
		rkisp1->csi.is_dphy_errctrl_disabled = true;
	}

	/*
	 * Enable DPHY errctrl interrupt again, if mipi have receive
	 * the whole frame without any error.
	 */
	if (status == RKISP1_CIF_MIPI_FRAME_END) {
		/*
		 * Enable DPHY errctrl interrupt again, if mipi have receive
		 * the whole frame without any error.
		 */
		if (rkisp1->csi.is_dphy_errctrl_disabled) {
			val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMSC);
			val |= RKISP1_CIF_MIPI_ERR_CTRL(0x0f);
			rkisp1_write(rkisp1, RKISP1_CIF_MIPI_IMSC, val);
			rkisp1->csi.is_dphy_errctrl_disabled = false;
		}
	} else {
		rkisp1->debug.mipi_error++;
	}

	return IRQ_HANDLED;
}

int rkisp1_csi_init(struct rkisp1_device *rkisp1)
{
	struct rkisp1_csi *csi = &rkisp1->csi;

	csi->rkisp1 = rkisp1;

	csi->dphy = devm_phy_get(rkisp1->dev, "dphy");
	if (IS_ERR(csi->dphy))
		return dev_err_probe(rkisp1->dev, PTR_ERR(csi->dphy),
				     "Couldn't get the MIPI D-PHY\n");

	phy_init(csi->dphy);

	return 0;
}

void rkisp1_csi_cleanup(struct rkisp1_device *rkisp1)
{
	struct rkisp1_csi *csi = &rkisp1->csi;

	phy_exit(csi->dphy);
}
