// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Broadcom Limited
 */

/**
 * DOC: VC4 DPI module
 *
 * The VC4 DPI hardware supports MIPI DPI type 4 and Nokia ViSSI
 * signals.  On BCM2835, these can be routed out to GPIO0-27 with the
 * ALT2 function.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/media-bus-format.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include "vc4_drv.h"
#include "vc4_regs.h"

#define DPI_C			0x00
# define DPI_OUTPUT_ENABLE_MODE		BIT(16)

/* The order field takes the incoming 24 bit RGB from the pixel valve
 * and shuffles the 3 channels.
 */
# define DPI_ORDER_MASK			VC4_MASK(15, 14)
# define DPI_ORDER_SHIFT		14
# define DPI_ORDER_RGB			0
# define DPI_ORDER_BGR			1
# define DPI_ORDER_GRB			2
# define DPI_ORDER_BRG			3

/* The format field takes the ORDER-shuffled pixel valve data and
 * formats it onto the output lines.
 */
# define DPI_FORMAT_MASK		VC4_MASK(13, 11)
# define DPI_FORMAT_SHIFT		11
/* This define is named in the hardware, but actually just outputs 0. */
# define DPI_FORMAT_9BIT_666_RGB	0
/* Outputs 00000000rrrrrggggggbbbbb */
# define DPI_FORMAT_16BIT_565_RGB_1	1
/* Outputs 000rrrrr00gggggg000bbbbb */
# define DPI_FORMAT_16BIT_565_RGB_2	2
/* Outputs 00rrrrr000gggggg00bbbbb0 */
# define DPI_FORMAT_16BIT_565_RGB_3	3
/* Outputs 000000rrrrrrggggggbbbbbb */
# define DPI_FORMAT_18BIT_666_RGB_1	4
/* Outputs 00rrrrrr00gggggg00bbbbbb */
# define DPI_FORMAT_18BIT_666_RGB_2	5
/* Outputs rrrrrrrrggggggggbbbbbbbb */
# define DPI_FORMAT_24BIT_888_RGB	6

/* Reverses the polarity of the corresponding signal */
# define DPI_PIXEL_CLK_INVERT		BIT(10)
# define DPI_HSYNC_INVERT		BIT(9)
# define DPI_VSYNC_INVERT		BIT(8)
# define DPI_OUTPUT_ENABLE_INVERT	BIT(7)

/* Outputs the signal the falling clock edge instead of rising. */
# define DPI_HSYNC_NEGATE		BIT(6)
# define DPI_VSYNC_NEGATE		BIT(5)
# define DPI_OUTPUT_ENABLE_NEGATE	BIT(4)

/* Disables the signal */
# define DPI_HSYNC_DISABLE		BIT(3)
# define DPI_VSYNC_DISABLE		BIT(2)
# define DPI_OUTPUT_ENABLE_DISABLE	BIT(1)

/* Power gate to the device, full reset at 0 -> 1 transition */
# define DPI_ENABLE			BIT(0)

/* All other registers besides DPI_C return the ID */
#define DPI_ID			0x04
# define DPI_ID_VALUE		0x00647069

/* General DPI hardware state. */
struct vc4_dpi {
	struct vc4_encoder encoder;

	struct platform_device *pdev;

	void __iomem *regs;

	struct clk *pixel_clock;
	struct clk *core_clock;

	struct debugfs_regset32 regset;
};

static inline struct vc4_dpi *
to_vc4_dpi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct vc4_dpi, encoder.base);
}

#define DPI_READ(offset) readl(dpi->regs + (offset))
#define DPI_WRITE(offset, val) writel(val, dpi->regs + (offset))

static const struct debugfs_reg32 dpi_regs[] = {
	VC4_REG32(DPI_C),
	VC4_REG32(DPI_ID),
};

static void vc4_dpi_encoder_disable(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct vc4_dpi *dpi = to_vc4_dpi(encoder);
	int idx;

	if (!drm_dev_enter(dev, &idx))
		return;

	clk_disable_unprepare(dpi->pixel_clock);

	drm_dev_exit(idx);
}

static void vc4_dpi_encoder_enable(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct drm_display_mode *mode = &encoder->crtc->mode;
	struct vc4_dpi *dpi = to_vc4_dpi(encoder);
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector = NULL, *connector_scan;
	u32 dpi_c = DPI_ENABLE;
	int idx;
	int ret;

	/* Look up the connector attached to DPI so we can get the
	 * bus_format.  Ideally the bridge would tell us the
	 * bus_format we want, but it doesn't yet, so assume that it's
	 * uniform throughout the bridge chain.
	 */
	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector_scan, &conn_iter) {
		if (connector_scan->encoder == encoder) {
			connector = connector_scan;
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	/* Default to 24bit if no connector or format found. */
	dpi_c |= VC4_SET_FIELD(DPI_FORMAT_24BIT_888_RGB, DPI_FORMAT);

	if (connector) {
		if (connector->display_info.num_bus_formats) {
			u32 bus_format = connector->display_info.bus_formats[0];

			dpi_c &= ~DPI_FORMAT_MASK;

			switch (bus_format) {
			case MEDIA_BUS_FMT_RGB888_1X24:
				dpi_c |= VC4_SET_FIELD(DPI_FORMAT_24BIT_888_RGB,
						       DPI_FORMAT);
				break;
			case MEDIA_BUS_FMT_BGR888_1X24:
				dpi_c |= VC4_SET_FIELD(DPI_FORMAT_24BIT_888_RGB,
						       DPI_FORMAT);
				dpi_c |= VC4_SET_FIELD(DPI_ORDER_BGR,
						       DPI_ORDER);
				break;
			case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
				dpi_c |= VC4_SET_FIELD(DPI_FORMAT_18BIT_666_RGB_2,
						       DPI_FORMAT);
				break;
			case MEDIA_BUS_FMT_RGB666_1X18:
				dpi_c |= VC4_SET_FIELD(DPI_FORMAT_18BIT_666_RGB_1,
						       DPI_FORMAT);
				break;
			case MEDIA_BUS_FMT_RGB565_1X16:
				dpi_c |= VC4_SET_FIELD(DPI_FORMAT_16BIT_565_RGB_3,
						       DPI_FORMAT);
				break;
			default:
				DRM_ERROR("Unknown media bus format %d\n",
					  bus_format);
				break;
			}
		}

		if (connector->display_info.bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE)
			dpi_c |= DPI_PIXEL_CLK_INVERT;

		if (connector->display_info.bus_flags & DRM_BUS_FLAG_DE_LOW)
			dpi_c |= DPI_OUTPUT_ENABLE_INVERT;
	}

	if (mode->flags & DRM_MODE_FLAG_CSYNC) {
		if (mode->flags & DRM_MODE_FLAG_NCSYNC)
			dpi_c |= DPI_OUTPUT_ENABLE_INVERT;
	} else {
		dpi_c |= DPI_OUTPUT_ENABLE_MODE;

		if (mode->flags & DRM_MODE_FLAG_NHSYNC)
			dpi_c |= DPI_HSYNC_INVERT;
		else if (!(mode->flags & DRM_MODE_FLAG_PHSYNC))
			dpi_c |= DPI_HSYNC_DISABLE;

		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			dpi_c |= DPI_VSYNC_INVERT;
		else if (!(mode->flags & DRM_MODE_FLAG_PVSYNC))
			dpi_c |= DPI_VSYNC_DISABLE;
	}

	if (!drm_dev_enter(dev, &idx))
		return;

	DPI_WRITE(DPI_C, dpi_c);

	ret = clk_set_rate(dpi->pixel_clock, mode->clock * 1000);
	if (ret)
		DRM_ERROR("Failed to set clock rate: %d\n", ret);

	ret = clk_prepare_enable(dpi->pixel_clock);
	if (ret)
		DRM_ERROR("Failed to set clock rate: %d\n", ret);

	drm_dev_exit(idx);
}

static enum drm_mode_status vc4_dpi_encoder_mode_valid(struct drm_encoder *encoder,
						       const struct drm_display_mode *mode)
{
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	return MODE_OK;
}

static const struct drm_encoder_helper_funcs vc4_dpi_encoder_helper_funcs = {
	.disable = vc4_dpi_encoder_disable,
	.enable = vc4_dpi_encoder_enable,
	.mode_valid = vc4_dpi_encoder_mode_valid,
};

static int vc4_dpi_late_register(struct drm_encoder *encoder)
{
	struct drm_device *drm = encoder->dev;
	struct vc4_dpi *dpi = to_vc4_dpi(encoder);
	int ret;

	ret = vc4_debugfs_add_regset32(drm->primary, "dpi_regs", &dpi->regset);
	if (ret)
		return ret;

	return 0;
}

static const struct drm_encoder_funcs vc4_dpi_encoder_funcs = {
	.late_register = vc4_dpi_late_register,
};

static const struct of_device_id vc4_dpi_dt_match[] = {
	{ .compatible = "brcm,bcm2835-dpi", .data = NULL },
	{}
};

/* Sets up the next link in the display chain, whether it's a panel or
 * a bridge.
 */
static int vc4_dpi_init_bridge(struct vc4_dpi *dpi)
{
	struct drm_device *drm = dpi->encoder.base.dev;
	struct device *dev = &dpi->pdev->dev;
	struct drm_bridge *bridge;

	bridge = drmm_of_get_bridge(drm, dev->of_node, 0, 0);
	if (IS_ERR(bridge)) {
		/* If nothing was connected in the DT, that's not an
		 * error.
		 */
		if (PTR_ERR(bridge) == -ENODEV)
			return 0;
		else
			return PTR_ERR(bridge);
	}

	return drm_bridge_attach(&dpi->encoder.base, bridge, NULL, 0);
}

static void vc4_dpi_disable_clock(void *ptr)
{
	struct vc4_dpi *dpi = ptr;

	clk_disable_unprepare(dpi->core_clock);
}

static int vc4_dpi_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dpi *dpi;
	int ret;

	dpi = drmm_kzalloc(drm, sizeof(*dpi), GFP_KERNEL);
	if (!dpi)
		return -ENOMEM;

	dpi->encoder.type = VC4_ENCODER_TYPE_DPI;
	dpi->pdev = pdev;
	dpi->regs = vc4_ioremap_regs(pdev, 0);
	if (IS_ERR(dpi->regs))
		return PTR_ERR(dpi->regs);
	dpi->regset.base = dpi->regs;
	dpi->regset.regs = dpi_regs;
	dpi->regset.nregs = ARRAY_SIZE(dpi_regs);

	if (DPI_READ(DPI_ID) != DPI_ID_VALUE) {
		dev_err(dev, "Port returned 0x%08x for ID instead of 0x%08x\n",
			DPI_READ(DPI_ID), DPI_ID_VALUE);
		return -ENODEV;
	}

	dpi->core_clock = devm_clk_get(dev, "core");
	if (IS_ERR(dpi->core_clock)) {
		ret = PTR_ERR(dpi->core_clock);
		if (ret != -EPROBE_DEFER)
			DRM_ERROR("Failed to get core clock: %d\n", ret);
		return ret;
	}

	dpi->pixel_clock = devm_clk_get(dev, "pixel");
	if (IS_ERR(dpi->pixel_clock)) {
		ret = PTR_ERR(dpi->pixel_clock);
		if (ret != -EPROBE_DEFER)
			DRM_ERROR("Failed to get pixel clock: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(dpi->core_clock);
	if (ret) {
		DRM_ERROR("Failed to turn on core clock: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(dev, vc4_dpi_disable_clock, dpi);
	if (ret)
		return ret;

	ret = drmm_encoder_init(drm, &dpi->encoder.base,
				&vc4_dpi_encoder_funcs,
				DRM_MODE_ENCODER_DPI,
				NULL);
	if (ret)
		return ret;

	drm_encoder_helper_add(&dpi->encoder.base, &vc4_dpi_encoder_helper_funcs);

	ret = vc4_dpi_init_bridge(dpi);
	if (ret)
		return ret;

	dev_set_drvdata(dev, dpi);

	return 0;
}

static const struct component_ops vc4_dpi_ops = {
	.bind   = vc4_dpi_bind,
};

static int vc4_dpi_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &vc4_dpi_ops);
}

static int vc4_dpi_dev_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vc4_dpi_ops);
	return 0;
}

struct platform_driver vc4_dpi_driver = {
	.probe = vc4_dpi_dev_probe,
	.remove = vc4_dpi_dev_remove,
	.driver = {
		.name = "vc4_dpi",
		.of_match_table = vc4_dpi_dt_match,
	},
};
