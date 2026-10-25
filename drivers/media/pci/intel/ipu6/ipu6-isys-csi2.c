// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013--2024 Intel Corporation
 */

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/minmax.h>
#include <linux/pm_runtime.h>
#include <linux/sprintf.h>
#include <linux/string_choices.h>

#include <media/media-entity.h>
#include <media/mipi-csi2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

#include "ipu6-bus.h"
#include "ipu6-isys.h"
#include "ipu6-isys-csi2.h"
#include "ipu6-isys-subdev.h"
#include "ipu6-isys-video.h"
#include "ipu6-platform-isys-csi2-reg.h"
#include "ipu7-isys-csi2-regs.h"

static const u32 csi2_supported_codes[] = {
	MEDIA_BUS_FMT_RGB565_1X16,
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SBGGR12_1X12,
	MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SBGGR8_1X8,
	MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8,
	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_Y8_1X8,
	MEDIA_BUS_FMT_Y10_1X10,
	MEDIA_BUS_FMT_Y12_1X12,
	MEDIA_BUS_FMT_Y16_1X16,
	MEDIA_BUS_FMT_META_8,
	MEDIA_BUS_FMT_META_10,
	MEDIA_BUS_FMT_META_12,
	MEDIA_BUS_FMT_META_16,
	MEDIA_BUS_FMT_META_24,
	0
};

/*
 * Strings corresponding to CSI-2 receiver errors are here.
 * Corresponding macros are defined in the header file.
 */
static const struct ipu6_csi2_error dphy_rx_errors[] = {
	{ "Single packet header error corrected", true },
	{ "Multiple packet header errors detected", true },
	{ "Payload checksum (CRC) error", true },
	{ "Transfer FIFO overflow", false },
	{ "Reserved short packet data type detected", true },
	{ "Reserved long packet data type detected", true },
	{ "Incomplete long packet detected", false },
	{ "Frame sync error", false },
	{ "Line sync error", false },
	{ "DPHY recoverable synchronization error", true },
	{ "DPHY fatal error", false },
	{ "DPHY elastic FIFO overflow", false },
	{ "Inter-frame short packet discarded", true },
	{ "Inter-frame long packet discarded", true },
	{ "MIPI pktgen overflow", false },
	{ "MIPI pktgen data loss", false },
	{ "FIFO overflow", false },
	{ "Lane deskew", false },
	{ "SOT sync error", false },
	{ "HSIDLE detected", false }
};

s64 ipu6_isys_csi2_get_link_freq(struct ipu6_isys_csi2 *csi2)
{
	struct media_pad *src_pad;

	if (!csi2)
		return -EINVAL;

	src_pad = media_pad_remote_pad_unique(&csi2->asd.sd.entity.pads[CSI2_PAD_SINK]);
	if (IS_ERR(src_pad)) {
		dev_err(&csi2->isys->adev->auxdev.dev,
			"can't get source pad of %s (%pe)\n",
			csi2->asd.sd.name, src_pad);
		return PTR_ERR(src_pad);
	}

	return v4l2_get_link_freq(src_pad, 0, 0);
}

static int csi2_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				struct v4l2_event_subscription *sub)
{
	struct ipu6_isys_subdev *asd = to_ipu6_isys_subdev(sd);
	struct ipu6_isys_csi2 *csi2 = to_ipu6_isys_csi2(asd);
	struct device *dev = &csi2->isys->adev->auxdev.dev;

	dev_dbg(dev, "csi2 subscribe event(type %u id %u)\n",
		sub->type, sub->id);

	switch (sub->type) {
	case V4L2_EVENT_FRAME_SYNC:
		return v4l2_event_subscribe(fh, sub, 10, NULL);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_subdev_core_ops csi2_sd_core_ops = {
	.subscribe_event = csi2_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

/*
 * The input system CSI2+ receiver has several
 * parameters affecting the receiver timings. These depend
 * on the MIPI bus frequency F in Hz (sensor transmitter rate)
 * as follows:
 *	register value = (A/1e9 + B * UI) / COUNT_ACC
 * where
 *	UI = 1 / (2 * F) in seconds
 *	COUNT_ACC = counter accuracy in seconds
 *	COUNT_ACC = 0.125 ns = 1 / 8 ns, ACCINV = 8.
 *
 * A and B are coefficients from the table below,
 * depending whether the register minimum or maximum value is
 * calculated.
 *				       Minimum     Maximum
 * Clock lane			       A     B     A     B
 * reg_rx_csi_dly_cnt_termen_clane     0     0    38     0
 * reg_rx_csi_dly_cnt_settle_clane    95    -8   300   -16
 * Data lanes
 * reg_rx_csi_dly_cnt_termen_dlane0    0     0    35     4
 * reg_rx_csi_dly_cnt_settle_dlane0   85    -2   145    -6
 * reg_rx_csi_dly_cnt_termen_dlane1    0     0    35     4
 * reg_rx_csi_dly_cnt_settle_dlane1   85    -2   145    -6
 * reg_rx_csi_dly_cnt_termen_dlane2    0     0    35     4
 * reg_rx_csi_dly_cnt_settle_dlane2   85    -2   145    -6
 * reg_rx_csi_dly_cnt_termen_dlane3    0     0    35     4
 * reg_rx_csi_dly_cnt_settle_dlane3   85    -2   145    -6
 *
 * We use the minimum values of both A and B.
 */

#define DIV_SHIFT	8
#define CSI2_ACCINV	8

static u32 calc_timing(s32 a, s32 b, s64 link_freq, s32 accinv)
{
	return accinv * a + (accinv * b * (500000000 >> DIV_SHIFT)
			     / (s32)(link_freq >> DIV_SHIFT));
}

static int
ipu6_isys_csi2_calc_timing(struct ipu6_isys_csi2 *csi2,
			   struct ipu6_isys_csi2_timing *timing, s32 accinv)
{
	struct device *dev = &csi2->isys->adev->auxdev.dev;
	s64 link_freq;

	link_freq = ipu6_isys_csi2_get_link_freq(csi2);
	if (link_freq < 0)
		return link_freq;

	timing->ctermen = calc_timing(CSI2_CSI_RX_DLY_CNT_TERMEN_CLANE_A,
				      CSI2_CSI_RX_DLY_CNT_TERMEN_CLANE_B,
				      link_freq, accinv);
	timing->csettle = calc_timing(CSI2_CSI_RX_DLY_CNT_SETTLE_CLANE_A,
				      CSI2_CSI_RX_DLY_CNT_SETTLE_CLANE_B,
				      link_freq, accinv);
	timing->dtermen = calc_timing(CSI2_CSI_RX_DLY_CNT_TERMEN_DLANE_A,
				      CSI2_CSI_RX_DLY_CNT_TERMEN_DLANE_B,
				      link_freq, accinv);
	timing->dsettle = calc_timing(CSI2_CSI_RX_DLY_CNT_SETTLE_DLANE_A,
				      CSI2_CSI_RX_DLY_CNT_SETTLE_DLANE_B,
				      link_freq, accinv);

	dev_dbg(dev, "ctermen %u csettle %u dtermen %u dsettle %u\n",
		timing->ctermen, timing->csettle,
		timing->dtermen, timing->dsettle);

	return 0;
}

void ipu6_isys_register_errors(struct ipu6_isys_csi2 *csi2)
{
	u32 irq = readl(csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
			CSI_PORT_REG_BASE_IRQ_STATUS_OFFSET);
	struct ipu6_isys *isys = csi2->isys;
	u32 mask;

	mask = isys->pdata->ipdata->csi2.irq_mask;
	writel(irq & mask, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_CLEAR_OFFSET);
	csi2->receiver_errors |= irq & mask;
}

void ipu6_isys_csi2_error(struct ipu6_isys_csi2 *csi2)
{
	struct device *dev = &csi2->isys->adev->auxdev.dev;
	const struct ipu6_csi2_error *errors;
	u32 status;
	u32 i;

	/* register errors once more in case of interrupts are disabled */
	ipu6_isys_register_errors(csi2);
	status = csi2->receiver_errors;
	csi2->receiver_errors = 0;
	errors = dphy_rx_errors;

	for (i = 0; i < CSI_RX_NUM_ERRORS_IN_IRQ; i++) {
		if (status & BIT(i))
			dev_err_ratelimited(dev, "csi2-%i error: %s\n",
					    csi2->port, errors[i].error_string);
	}
}

static void ipu6_isys_csi2_setup_watermark(struct ipu6_isys_csi2 *csi2,
					   struct v4l2_subdev_state *csi2_state,
					   struct v4l2_subdev *remote_sd)
{
	struct device *dev = &csi2->isys->adev->auxdev.dev;
	struct v4l2_control hb = { .id = V4L2_CID_HBLANK, .value = 0 };
	struct v4l2_subdev_route *route;
	u32 max_stream_data_rate = 0, hblank = 0;
	s64 link_freq;
	int ret;

	if (IS_IPU7(csi2->asd.isys->adev->isp) ||
	    IS_IPU8(csi2->asd.isys->adev->isp))
		return;

	ret = v4l2_g_ctrl(remote_sd->ctrl_handler, &hb);
	if (!ret)
		hblank = max(0, hb.value);

	link_freq = ipu6_isys_csi2_get_link_freq(csi2);
	if (link_freq <= 0) {
		csi2->watermark.force_iwake_disable = true;
		dev_warn(dev, "unexpected link_freq %lld (source %s)\n",
			 link_freq, remote_sd->entity.name);
		ipu6_isys_update_watermark_setting(csi2->isys);
		return;
	}

	for_each_active_route(&csi2_state->routing, route) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_state_get_format(csi2_state, CSI2_PAD_SINK,
						   route->sink_stream);
		if (WARN_ON(!fmt))
			continue;

		u32 bpp = ipu6_isys_mbus_code_to_bpp(fmt->code);
		u64 pixel_rate = mul_u64_u32_div(link_freq, csi2->nlanes * 2,
						 bpp);
		u32 pixels_per_line = fmt->width + hblank;
		u64 line_time_ns = div_u64(pixels_per_line * NSEC_PER_SEC,
					   pixel_rate);
		u32 bytes_per_line = fmt->width * bpp / 8;
		u32 pages_per_line =
			DIV_ROUND_UP(bytes_per_line,
				     csi2->isys->pdata->ipdata->sram_gran_size);
		u32 pb_bytes_per_line =
			pages_per_line << csi2->isys->pdata->ipdata->sram_gran_shift;
		u64 stream_data_rate =
			div64_u64(pb_bytes_per_line * 1000, line_time_ns);

		dev_dbg(dev, "stream %u:%u -> %u:%u data rate %lld\n",
			route->sink_pad, route->sink_stream, route->source_pad,
			route->source_stream, stream_data_rate);

		max_stream_data_rate = max(max_stream_data_rate,
					   stream_data_rate);
	}

	csi2->watermark.stream_data_rate = max_stream_data_rate;

	ipu6_isys_update_watermark_setting(csi2->isys);
}

static void ipu6_isys_csi2_clear_watermark(struct ipu6_isys_csi2 *csi2)
{
	if (IS_IPU7(csi2->asd.isys->adev->isp) ||
	    IS_IPU8(csi2->asd.isys->adev->isp))
		return;

	csi2->watermark.force_iwake_disable = false;
	csi2->watermark.stream_data_rate = 0;
	ipu6_isys_update_watermark_setting(csi2->isys);
}

static void ipu6_isys_csi2_stream_disable(struct ipu6_isys_csi2 *csi2)
{
	struct ipu6_isys *isys = csi2->isys;
	struct ipu6_isys_csi2_config cfg;
	u32 mask = isys->pdata->ipdata->csi2.irq_mask;

	cfg.port = csi2->port;
	cfg.nlanes = csi2->nlanes;

	writel(0, csi2->base + CSI_REG_CSI_FE_ENABLE);
	writel(0, csi2->base + CSI_REG_PPI2CSI_ENABLE);
	writel(0, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_ENABLE_OFFSET);
	writel(mask, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_CLEAR_OFFSET);
	writel(0, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
	       CSI_PORT_REG_BASE_IRQ_ENABLE_OFFSET);
	writel(0xffffffff, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
	       CSI_PORT_REG_BASE_IRQ_CLEAR_OFFSET);

	isys->phy_set_power(isys, &cfg, NULL, false);

	writel(0, isys->pdata->base + CSI_REG_HUB_FW_ACCESS_PORT
	       (isys->pdata->ipdata->csi2.fw_access_port_ofs, csi2->port));
	writel(0, isys->pdata->base + CSI_REG_HUB_DRV_ACCESS_PORT(csi2->port));
}

static int ipu6_isys_csi2_stream_enable(struct ipu6_isys_csi2 *csi2)
{
	struct ipu6_isys *isys = csi2->isys;
	struct ipu6_isys_csi2_timing timing = { };
	struct ipu6_isys_csi2_config cfg;
	unsigned int nports;
	u32 mask;
	int ret;

	cfg.port = csi2->port;
	cfg.nlanes = csi2->nlanes;

	ret = ipu6_isys_csi2_calc_timing(csi2, &timing, CSI2_ACCINV);
	if (ret)
		return ret;

	mask = isys->pdata->ipdata->csi2.irq_mask;
	nports = isys->pdata->ipdata->csi2.nports;

	/* reset port reset */
	writel(0x1, csi2->base + CSI_REG_PORT_GPREG_SRST);
	usleep_range(100, 200);
	writel(0x0, csi2->base + CSI_REG_PORT_GPREG_SRST);

	/* enable port clock */
	for (unsigned int i = 0; i < nports; i++) {
		writel(1, isys->pdata->base + CSI_REG_HUB_DRV_ACCESS_PORT(i));
		writel(1, isys->pdata->base + CSI_REG_HUB_FW_ACCESS_PORT
		       (isys->pdata->ipdata->csi2.fw_access_port_ofs, i));
	}

	/* enable all error related irq */
	writel(mask,
	       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_STATUS_OFFSET);
	writel(mask,
	       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_MASK_OFFSET);
	writel(mask,
	       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_CLEAR_OFFSET);
	writel(mask,
	       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_LEVEL_NOT_PULSE_OFFSET);
	writel(mask,
	       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_ENABLE_OFFSET);

	/*
	 * Using event from firmware instead of irq to handle CSI2 sync event
	 * which can reduce system wakeups. If CSI2 sync irq enabled, we need
	 * disable the firmware CSI2 sync event to avoid duplicate handling.
	 */
	writel(0xffffffff, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
	       CSI_PORT_REG_BASE_IRQ_STATUS_OFFSET);
	writel(0, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
	       CSI_PORT_REG_BASE_IRQ_MASK_OFFSET);
	writel(0xffffffff, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
	       CSI_PORT_REG_BASE_IRQ_CLEAR_OFFSET);
	writel(0, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
	       CSI_PORT_REG_BASE_IRQ_LEVEL_NOT_PULSE_OFFSET);
	writel(0xffffffff, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
	       CSI_PORT_REG_BASE_IRQ_ENABLE_OFFSET);

	/* configure to enable FE and PPI2CSI */
	writel(0, csi2->base + CSI_REG_CSI_FE_MODE);
	writel(CSI_SENSOR_INPUT, csi2->base + CSI_REG_CSI_FE_MUX_CTRL);
	writel(CSI_CNTR_SENSOR_LINE_ID | CSI_CNTR_SENSOR_FRAME_ID,
	       csi2->base + CSI_REG_CSI_FE_SYNC_CNTR_SEL);
	writel(FIELD_PREP(PPI_INTF_CONFIG_NOF_ENABLED_DLANES_MASK,
			  csi2->nlanes - 1),
	       csi2->base + CSI_REG_PPI2CSI_CONFIG_PPI_INTF);

	writel(1, csi2->base + CSI_REG_PPI2CSI_ENABLE);
	writel(1, csi2->base + CSI_REG_CSI_FE_ENABLE);

	return isys->phy_set_power(isys, &cfg, &timing, true);
}

static void ipu7_csi2_irq_enable(struct ipu6_isys_csi2 *csi2)
{
	struct ipu6_isys *isys = csi2->isys;
	unsigned int offset, mask;

	/* enable CSI2 legacy error irq */
	offset = IPU7_IS_IO_CSI2_ERR_LEGACY_IRQ_CTL_BASE(csi2->port);
	mask = IPU7_CSI_RX_ERROR_IRQ_MASK;
	writel(mask, csi2->base + offset + IPU7_IRQ_CTL_CLEAR);
	writel(mask, csi2->base + offset + IPU7_IRQ_CTL_MASK);
	writel(mask, csi2->base + offset + IPU7_IRQ_CTL_ENABLE);

	/* enable CSI2 legacy sync irq */
	offset = IPU7_IS_IO_CSI2_SYNC_LEGACY_IRQ_CTL_BASE(csi2->port);
	mask = IPU7_CSI_RX_SYNC_IRQ_MASK;
	writel(mask, csi2->base + offset + IPU7_IRQ_CTL_CLEAR);
	writel(mask, csi2->base + offset + IPU7_IRQ_CTL_MASK);
	writel(mask, csi2->base + offset + IPU7_IRQ_CTL_ENABLE);

	if (!IS_IPU7_MTL(isys->adev->isp)) {
		mask = IPU7P5_CSI_RX_SYNC_FE_IRQ_MASK;
		writel(mask, csi2->base + offset + IPU7_IRQ1_CTL_CLEAR);
		writel(mask, csi2->base + offset + IPU7_IRQ1_CTL_MASK);
		writel(mask, csi2->base + offset + IPU7_IRQ1_CTL_ENABLE);
	}
}

static void ipu7_csi2_irq_disable(struct ipu6_isys_csi2 *csi2)
{
	struct ipu6_isys *isys = csi2->isys;
	unsigned int offset, mask;

	/* disable CSI2 legacy error irq */
	offset = IPU7_IS_IO_CSI2_ERR_LEGACY_IRQ_CTL_BASE(csi2->port);
	mask = IPU7_CSI_RX_ERROR_IRQ_MASK;
	writel(mask, csi2->base + offset + IPU7_IRQ_CTL_CLEAR);
	writel(0, csi2->base + offset + IPU7_IRQ_CTL_MASK);
	writel(0, csi2->base + offset + IPU7_IRQ_CTL_ENABLE);

	/* disable CSI2 legacy sync irq */
	offset = IPU7_IS_IO_CSI2_SYNC_LEGACY_IRQ_CTL_BASE(csi2->port);
	mask = IPU7_CSI_RX_SYNC_IRQ_MASK;
	writel(mask, csi2->base + offset + IPU7_IRQ_CTL_CLEAR);
	writel(0, csi2->base + offset + IPU7_IRQ_CTL_MASK);
	writel(0, csi2->base + offset + IPU7_IRQ_CTL_ENABLE);

	if (!IS_IPU7_MTL(isys->adev->isp)) {
		writel(mask, csi2->base + offset + IPU7_IRQ1_CTL_CLEAR);
		writel(0, csi2->base + offset + IPU7_IRQ1_CTL_MASK);
		writel(0, csi2->base + offset + IPU7_IRQ1_CTL_ENABLE);
	}
}

static void ipu7_isys_csi2_stream_disable(struct ipu6_isys_csi2 *csi2)
{
	struct ipu6_isys *isys = csi2->isys;
	void __iomem *base = isys->pdata->base;
	struct ipu6_isys_csi2_config cfg;

	cfg.port = csi2->port;
	cfg.nlanes = csi2->nlanes;

	isys->phy_set_power(isys, &cfg, NULL, false);

	writel(0x4,
	       base + IPU7_IS_IO_GPREGS_BASE + IPU7_CLK_DIV_FACTOR_APB_CLK);
	ipu7_csi2_irq_disable(csi2);
}

static int ipu7_isys_csi2_stream_enable(struct ipu6_isys_csi2 *csi2)
{
	struct ipu6_isys *isys = csi2->isys;
	void __iomem *base = isys->pdata->base;
	struct ipu6_isys_csi2_config cfg;
	unsigned int offset = IPU7_IS_IO_GPREGS_BASE;
	int ret;

	writel(0x2, base + offset + IPU7_CLK_DIV_FACTOR_APB_CLK);
	if (csi2->port == 0U && csi2->nlanes == 4U &&
	    !IS_IPU7_MTL(isys->adev->isp))
		writel(0x1, base + offset + IPU7_CSI_PORTAB_AGGREGATION);

	/* input is coming from CSI receiver (sensor) */
	offset = IPU7_IS_IO_CSI2_ADPL_PORT_BASE(csi2->port);
	writel(CSI_SENSOR_INPUT, base + offset + IPU7_CSI2_ADPL_INPUT_MODE);
	writel(1, base + offset + IPU7_CSI2_ADPL_CSI_RX_ERR_IRQ_CLEAR_EN);

	cfg.port = csi2->port;
	cfg.nlanes = csi2->nlanes;

	ret = isys->phy_set_power(isys, &cfg, NULL, true);
	if (ret)
		return ret;

	ipu7_csi2_irq_enable(csi2);

	return 0;
}

static int ipu6_isys_csi2_streaming_change(struct ipu6_isys_subdev *asd,
					   struct v4l2_subdev_state *state,
					   u32 pad,
					   struct v4l2_mbus_frame_desc *desc,
					   u8 *vc, bool enable)
{
	struct v4l2_mbus_frame_desc_entry *this_entry = NULL;
	struct v4l2_subdev_route *route, *this_route = NULL;
	u32 streams_enabled = 0, nodes_streaming = 0;

	for_each_active_route(&state->routing, this_route)
		if (pad == this_route->source_pad)
			break;
	if (!this_route) {
		dev_dbg(asd->sd.dev, "no route found for pad %u\n", pad);
		return -EINVAL;
	}

	for (unsigned int i = 0; i < desc->num_entries; i++) {
		if (desc->entry[i].stream == this_route->sink_stream) {
			this_entry = &desc->entry[i];
			break;
		}
	}
	if (!this_entry) {
		dev_dbg(asd->sd.dev,
			"no frame descriptor entry found for stream %u\n",
			this_route->sink_stream);
		return -EINVAL;
	}

	for_each_active_route(&state->routing, route) {
		struct v4l2_mbus_frame_desc_entry *entry = NULL;

		for (unsigned int i = 0; i < desc->num_entries; i++) {
			if (desc->entry[i].stream == route->sink_stream) {
				entry = &desc->entry[i];
				break;
			}
		}

		if (!entry) {
			dev_dbg(asd->sd.dev, "cannot find stream %u from frame descriptor\n",
				route->sink_stream);
			return -EINVAL;
		}

		if (entry->bus.csi2.vc != this_entry->bus.csi2.vc)
			continue;

		struct media_pad *video_pad =
			media_pad_remote_pad_first(&asd->sd.entity.pads[route->source_pad]);
		if (!video_pad)
			return -EINVAL;

		struct ipu6_isys_video *av =
			container_of_const(video_pad, struct ipu6_isys_video,
					   pad);

		streams_enabled++;
		if (av->streaming || (enable && pad == route->source_pad))
			nodes_streaming++;
	}

	if (vc)
		*vc = this_entry->bus.csi2.vc;

	if (streams_enabled == nodes_streaming) {
		dev_dbg(asd->sd.dev,
			"changing streaming state to %s on \"%s\"\n",
			str_enabled_disabled(enable), asd->sd.entity.name);
		return 1;
	}

	dev_dbg(asd->sd.dev, "not setting streaming %s %s (%u/%u)\n",
		str_enabled_disabled(enable), enable ? "yet" : "anymore",
		nodes_streaming, streams_enabled);

	return 0;
}

static int ipu6_isys_get_frame_desc(struct v4l2_subdev *remote_sd,
				    unsigned int pad,
				    struct v4l2_mbus_frame_desc *desc)
{
	struct v4l2_subdev_format fmt = { .which = V4L2_SUBDEV_FORMAT_ACTIVE };
	int ret;

	ret = v4l2_subdev_call(remote_sd, pad, get_frame_desc, pad, desc);
	if (!ret || ret != -ENOIOCTLCMD)
		return ret;

	ret = v4l2_subdev_call_state_active(remote_sd, pad, get_fmt, &fmt);
	if (ret)
		return ret;

	desc->type = V4L2_MBUS_FRAME_DESC_TYPE_CSI2;
	desc->num_entries = 1;
	desc->entry[0].pixelcode = fmt.format.code;
	desc->entry[0].bus.csi2.dt = ipu6_isys_mbus_code_to_mipi(fmt.format.code);

	return 0;
}

static int ipu6_isys_csi2_enable_streams(struct v4l2_subdev *sd,
					 struct v4l2_subdev_state *state,
					 u32 pad, u64 streams_mask)
{
	struct ipu6_isys_subdev *asd = to_ipu6_isys_subdev(sd);
	struct ipu6_isys_csi2 *csi2 = to_ipu6_isys_csi2(asd);
	struct ipu6_device *isp = asd->isys->adev->isp;
	struct media_pad *remote_pad =
		media_pad_remote_pad_first(&sd->entity.pads[CSI2_PAD_SINK]),
		*vdev_pad = media_pad_remote_pad_unique(&sd->entity.pads[pad]);
	struct ipu6_isys_video *av =
		container_of_const(vdev_pad, struct ipu6_isys_video, pad);
	struct v4l2_subdev *remote_sd =
		media_entity_to_v4l2_subdev(remote_pad->entity);
	struct v4l2_mbus_frame_desc desc = { 0 };
	struct ipu6_isys_stream *stream;
	struct ipu6_isys_buffer_list bl;
	u64 sink_streams;
	int ret;
	u8 vc;

	lockdep_assert_held(&csi2->isys->stream_mutex);

	ret = ipu6_isys_get_frame_desc(remote_sd, remote_pad->index, &desc);
	if (ret)
		return ret;

	list_add(&av->csi2_entry, &csi2->av_head);

	sink_streams =
		v4l2_subdev_state_xlate_streams(state, pad, CSI2_PAD_SINK,
						&streams_mask);
	csi2->stream_ids |= sink_streams;

	ret = ipu6_isys_csi2_streaming_change(asd, state, pad, &desc, &vc,
					      true);
	if (!ret)
		return ret;
	if (ret < 0)
		goto err_av_del;

	ret = pm_runtime_resume_and_get(sd->dev);
	if (ret < 0)
		goto err_del_av;

	ipu6_isys_csi2_setup_watermark(csi2, state, remote_sd);

	stream = ipu6_isys_alloc_stream_firmware(csi2, state, &desc, vc);
	if (IS_ERR(stream)) {
		ret = PTR_ERR(stream);
		dev_err(sd->dev, "allocating firmware stream failed\n");
		goto err_clear_watermark;
	}

	ret = ipu6_isys_buffer_list_get(stream, &bl);
	if (ret < 0) {
		dev_warn(sd->dev, "no buffer available, DRIVER BUG?\n");
		goto err_free_stream_firmware;
	}

	ret = ipu6_isys_start_stream_firmware(stream, &bl, &desc);
	if (ret)
		goto err_requeue_buffers;

	if (!csi2->streaming_vc) {
		ret = (IS_IPU7(isp) || IS_IPU8(isp)) ?
			      ipu7_isys_csi2_stream_enable(csi2) :
			      ipu6_isys_csi2_stream_enable(csi2);
		if (ret)
			goto err_stop_stream_firmware;
	}

	ret = v4l2_subdev_enable_streams(remote_sd, remote_pad->index,
					 csi2->stream_ids);
	if (ret)
		goto err_stop_stream_csi2;

	csi2->streaming_vc |= BIT(vc);

	return 0;

err_stop_stream_csi2:
	if (IS_IPU7(isp) || IS_IPU8(isp))
		ipu7_isys_csi2_stream_disable(csi2);
	else
		ipu6_isys_csi2_stream_disable(csi2);

err_requeue_buffers:
	ipu6_isys_buffer_list_queue(&bl, IPU6_ISYS_BUFFER_LIST_FL_INCOMING, 0);

err_stop_stream_firmware:
	ipu6_isys_stop_stream_firmware(stream);
	ipu6_isys_close_stream_firmware(stream);

err_free_stream_firmware:
	ipu6_isys_free_stream_firmware(stream);

err_clear_watermark:
	ipu6_isys_csi2_clear_watermark(csi2);
	pm_runtime_put(sd->dev);

err_del_av:
	ipu6_isys_csi2_streaming_change(asd, state, pad, &desc, NULL, false);
	csi2->stream_ids &= ~sink_streams;
err_av_del:
	list_del(&av->csi2_entry);

	return ret;
}

static int ipu6_isys_csi2_disable_streams(struct v4l2_subdev *sd,
					  struct v4l2_subdev_state *state,
					  u32 pad, u64 streams_mask)
{
	struct media_pad *remote_pad =
		media_pad_remote_pad_first(&sd->entity.pads[CSI2_PAD_SINK]),
		*vdev_pad = media_pad_remote_pad_unique(&sd->entity.pads[pad]);
	struct ipu6_isys_video *av =
		container_of_const(vdev_pad, struct ipu6_isys_video, pad);
	struct v4l2_subdev *remote_sd =
		media_entity_to_v4l2_subdev(remote_pad->entity);
	struct ipu6_isys_subdev *asd = to_ipu6_isys_subdev(sd);
	struct ipu6_isys_csi2 *csi2 = to_ipu6_isys_csi2(asd);
	struct ipu6_device *isp = asd->isys->adev->isp;
	struct v4l2_mbus_frame_desc desc = { 0 };
	u64 sink_streams;
	int ret;
	u8 vc;

	lockdep_assert_held(&csi2->isys->stream_mutex);

	ret = ipu6_isys_get_frame_desc(remote_sd, remote_pad->index, &desc);
	if (ret)
		return ret;

	sink_streams =
		v4l2_subdev_state_xlate_streams(state, pad, CSI2_PAD_SINK,
						&streams_mask);

	csi2->stream_ids &= ~sink_streams;

	ret = ipu6_isys_csi2_streaming_change(asd, state, pad, &desc, &vc,
					      false);
	if (ret <= 0)
		goto out_del_csi2_entry;

	csi2->streaming_vc &= ~BIT(vc);

	struct ipu6_isys_stream *stream =
		ipu6_isys_find_stream_firmware(csi2, vc);
	ipu6_isys_stop_stream_firmware(stream);

	if (IS_IPU7(isp) || IS_IPU8(isp))
		ipu7_isys_csi2_stream_disable(csi2);
	else
		ipu6_isys_csi2_stream_disable(csi2);

	v4l2_subdev_disable_streams(remote_sd, remote_pad->index,
				    csi2->stream_ids | sink_streams);

	ipu6_isys_close_stream_firmware(stream);
	ipu6_isys_free_stream_firmware(stream);

	ipu6_isys_csi2_clear_watermark(csi2);

	pm_runtime_put(sd->dev);

out_del_csi2_entry:
	list_del(&av->csi2_entry);

	return 0;
}

static int ipu6_isys_csi2_set_sel(struct v4l2_subdev *sd,
				  const struct v4l2_subdev_client_info *ci,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	struct ipu6_isys_subdev *asd = to_ipu6_isys_subdev(sd);
	struct device *dev = &asd->isys->adev->auxdev.dev;
	struct v4l2_mbus_framefmt *sink_ffmt;
	struct v4l2_mbus_framefmt *src_ffmt;
	struct v4l2_rect *crop;

	if (sel->pad == CSI2_PAD_SINK || sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	sink_ffmt = v4l2_subdev_state_get_opposite_stream_format(state,
								 sel->pad,
								 sel->stream);
	if (!sink_ffmt)
		return -EINVAL;

	src_ffmt = v4l2_subdev_state_get_format(state, sel->pad, sel->stream);
	if (!src_ffmt)
		return -EINVAL;

	crop = v4l2_subdev_state_get_crop(state, sel->pad, sel->stream);
	if (!crop)
		return -EINVAL;

	/* Only vertical cropping is supported */
	sel->r.left = 0;
	sel->r.width = sink_ffmt->width;
	/* Non-bayer formats can't be single line cropped */
	if (!ipu6_isys_is_bayer_format(sink_ffmt->code))
		sel->r.top &= ~1;
	sel->r.height = clamp(sel->r.height & ~1, IPU6_ISYS_MIN_HEIGHT,
			      sink_ffmt->height - sel->r.top);
	*crop = sel->r;

	/* update source pad format */
	src_ffmt->width = sel->r.width;
	src_ffmt->height = sel->r.height;
	if (ipu6_isys_is_bayer_format(sink_ffmt->code))
		src_ffmt->code = ipu6_isys_convert_bayer_order(sink_ffmt->code,
							       sel->r.left,
							       sel->r.top);
	dev_dbg(dev, "set crop for %s sel: %d,%d,%d,%d code: 0x%x\n",
		sd->name, sel->r.left, sel->r.top, sel->r.width, sel->r.height,
		src_ffmt->code);

	return 0;
}

static int ipu6_isys_csi2_get_sel(struct v4l2_subdev *sd,
				  const struct v4l2_subdev_client_info *ci,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	struct v4l2_mbus_framefmt *sink_ffmt;
	struct v4l2_rect *crop;
	int ret = 0;

	if (sd->entity.pads[sel->pad].flags & MEDIA_PAD_FL_SINK)
		return -EINVAL;

	sink_ffmt = v4l2_subdev_state_get_opposite_stream_format(state,
								 sel->pad,
								 sel->stream);
	if (!sink_ffmt)
		return -EINVAL;

	crop = v4l2_subdev_state_get_crop(state, sel->pad, sel->stream);
	if (!crop)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = sink_ffmt->width;
		sel->r.height = sink_ffmt->height;
		break;
	case V4L2_SEL_TGT_CROP:
		sel->r = *crop;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_subdev_pad_ops csi2_sd_pad_ops = {
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ipu6_isys_subdev_set_fmt,
	.get_selection = ipu6_isys_csi2_get_sel,
	.set_selection = ipu6_isys_csi2_set_sel,
	.enum_mbus_code = ipu6_isys_subdev_enum_mbus_code,
	.set_routing = ipu6_isys_subdev_set_routing,
	.enable_streams = ipu6_isys_csi2_enable_streams,
	.disable_streams = ipu6_isys_csi2_disable_streams,
};

static const struct v4l2_subdev_ops csi2_sd_ops = {
	.core = &csi2_sd_core_ops,
	.pad = &csi2_sd_pad_ops,
};

static const struct media_entity_operations csi2_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
	.has_pad_interdep = v4l2_subdev_has_pad_interdep,
};

void ipu6_isys_csi2_cleanup(struct ipu6_isys_csi2 *csi2)
{
	if (!csi2->isys)
		return;

	v4l2_device_unregister_subdev(&csi2->asd.sd);
	v4l2_subdev_cleanup(&csi2->asd.sd);
	ipu6_isys_subdev_cleanup(&csi2->asd);
	csi2->isys = NULL;
}

int ipu6_isys_csi2_init(struct ipu6_isys_csi2 *csi2,
			struct ipu6_isys *isys,
			void __iomem *base, unsigned int index)
{
	struct device *dev = &isys->adev->auxdev.dev;
	int ret;

	csi2->isys = isys;
	csi2->base = base;
	csi2->port = index;

	csi2->asd.sd.entity.ops = &csi2_entity_ops;
	csi2->asd.isys = isys;
	ret = ipu6_isys_subdev_init(&csi2->asd, &csi2_sd_ops, 0,
				    NR_OF_CSI2_SINK_PADS, NR_OF_CSI2_SRC_PADS);
	if (ret)
		goto fail;

	INIT_LIST_HEAD(&csi2->av_head);
	INIT_LIST_HEAD(&csi2->streams);
	csi2->asd.source = IPU6_FW_ISYS_STREAM_SRC_CSI2_PORT0 + index;
	csi2->asd.supported_codes = csi2_supported_codes;
	snprintf(csi2->asd.sd.name, sizeof(csi2->asd.sd.name),
		 "Intel IPU%s CSI2 %u",
		 ipu6_isys_entity_version(isys->adev->isp), index);
	v4l2_set_subdevdata(&csi2->asd.sd, &csi2->asd);
	ret = v4l2_subdev_init_finalize(&csi2->asd.sd);
	if (ret) {
		dev_err(dev, "failed to init v4l2 subdev\n");
		goto fail;
	}

	ret = v4l2_device_register_subdev(&isys->v4l2_dev, &csi2->asd.sd);
	if (ret) {
		dev_err(dev, "failed to register v4l2 subdev\n");
		goto fail;
	}

	return 0;

fail:
	ipu6_isys_csi2_cleanup(csi2);

	return ret;
}

void ipu6_isys_csi2_sof_event_by_stream(struct ipu6_isys_stream *stream)
{
	struct video_device *vdev = stream->asd->sd.devnode;
	struct device *dev = &stream->isys->adev->auxdev.dev;
	struct ipu6_isys_csi2 *csi2 = ipu6_isys_subdev_to_csi2(stream->asd);
	struct v4l2_event ev = {
		.type = V4L2_EVENT_FRAME_SYNC,
	};

	ev.u.frame_sync.frame_sequence = atomic_fetch_inc(&stream->sequence);
	v4l2_event_queue(vdev, &ev);

	dev_dbg(dev, "sof_event::csi2-%i sequence: %i, vc: %d\n",
		csi2->port, ev.u.frame_sync.frame_sequence, stream->vc);
}

void ipu6_isys_csi2_eof_event_by_stream(struct ipu6_isys_stream *stream)
{
	struct device *dev = &stream->isys->adev->auxdev.dev;
	struct ipu6_isys_csi2 *csi2 = ipu6_isys_subdev_to_csi2(stream->asd);
	u32 frame_sequence = atomic_read(&stream->sequence);

	dev_dbg(dev, "eof_event::csi2-%i sequence: %i\n",
		csi2->port, frame_sequence);
}
