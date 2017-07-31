/*
 * Copyright (C) 2016 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * DOC: VC4 DPI module
 *
 * The VC4 DPI hardware supports MIPI DPI type 4 and Nokia ViSSI
 * signals, which are routed out to GPIO0-27 with the ALT2 function.
 */

#include "drm_atomic_helper.h"
#include "drm_crtc_helper.h"
#include "drm_edid.h"
#include "drm_panel.h"
#include "linux/clk.h"
#include "linux/component.h"
#include "linux/of_graph.h"
#include "linux/of_platform.h"
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
	struct platform_device *pdev;

	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct drm_panel *panel;

	void __iomem *regs;

	struct clk *pixel_clock;
	struct clk *core_clock;
};

#define DPI_READ(offset) readl(dpi->regs + (offset))
#define DPI_WRITE(offset, val) writel(val, dpi->regs + (offset))

/* VC4 DPI encoder KMS struct */
struct vc4_dpi_encoder {
	struct vc4_encoder base;
	struct vc4_dpi *dpi;
};

static inline struct vc4_dpi_encoder *
to_vc4_dpi_encoder(struct drm_encoder *encoder)
{
	return container_of(encoder, struct vc4_dpi_encoder, base.base);
}

/* VC4 DPI connector KMS struct */
struct vc4_dpi_connector {
	struct drm_connector base;
	struct vc4_dpi *dpi;

	/* Since the connector is attached to just the one encoder,
	 * this is the reference to it so we can do the best_encoder()
	 * hook.
	 */
	struct drm_encoder *encoder;
};

static inline struct vc4_dpi_connector *
to_vc4_dpi_connector(struct drm_connector *connector)
{
	return container_of(connector, struct vc4_dpi_connector, base);
}

#define DPI_REG(reg) { reg, #reg }
static const struct {
	u32 reg;
	const char *name;
} dpi_regs[] = {
	DPI_REG(DPI_C),
	DPI_REG(DPI_ID),
};

static void vc4_dpi_dump_regs(struct vc4_dpi *dpi)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dpi_regs); i++) {
		DRM_INFO("0x%04x (%s): 0x%08x\n",
			 dpi_regs[i].reg, dpi_regs[i].name,
			 DPI_READ(dpi_regs[i].reg));
	}
}

#ifdef CONFIG_DEBUG_FS
int vc4_dpi_debugfs_regs(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_dpi *dpi = vc4->dpi;
	int i;

	if (!dpi)
		return 0;

	for (i = 0; i < ARRAY_SIZE(dpi_regs); i++) {
		seq_printf(m, "%s (0x%04x): 0x%08x\n",
			   dpi_regs[i].name, dpi_regs[i].reg,
			   DPI_READ(dpi_regs[i].reg));
	}

	return 0;
}
#endif

static enum drm_connector_status
vc4_dpi_connector_detect(struct drm_connector *connector, bool force)
{
	struct vc4_dpi_connector *vc4_connector =
		to_vc4_dpi_connector(connector);
	struct vc4_dpi *dpi = vc4_connector->dpi;

	if (dpi->panel)
		return connector_status_connected;
	else
		return connector_status_disconnected;
}

static void vc4_dpi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int vc4_dpi_connector_get_modes(struct drm_connector *connector)
{
	struct vc4_dpi_connector *vc4_connector =
		to_vc4_dpi_connector(connector);
	struct vc4_dpi *dpi = vc4_connector->dpi;

	if (dpi->panel)
		return drm_panel_get_modes(dpi->panel);

	return 0;
}

static const struct drm_connector_funcs vc4_dpi_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = vc4_dpi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = vc4_dpi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs vc4_dpi_connector_helper_funcs = {
	.get_modes = vc4_dpi_connector_get_modes,
};

static struct drm_connector *vc4_dpi_connector_init(struct drm_device *dev,
						    struct vc4_dpi *dpi)
{
	struct drm_connector *connector = NULL;
	struct vc4_dpi_connector *dpi_connector;

	dpi_connector = devm_kzalloc(dev->dev, sizeof(*dpi_connector),
				     GFP_KERNEL);
	if (!dpi_connector)
		return ERR_PTR(-ENOMEM);

	connector = &dpi_connector->base;

	dpi_connector->encoder = dpi->encoder;
	dpi_connector->dpi = dpi;

	drm_connector_init(dev, connector, &vc4_dpi_connector_funcs,
			   DRM_MODE_CONNECTOR_DPI);
	drm_connector_helper_add(connector, &vc4_dpi_connector_helper_funcs);

	connector->polled = 0;
	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	drm_mode_connector_attach_encoder(connector, dpi->encoder);

	return connector;
}

static const struct drm_encoder_funcs vc4_dpi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static void vc4_dpi_encoder_disable(struct drm_encoder *encoder)
{
	struct vc4_dpi_encoder *vc4_encoder = to_vc4_dpi_encoder(encoder);
	struct vc4_dpi *dpi = vc4_encoder->dpi;

	drm_panel_disable(dpi->panel);

	clk_disable_unprepare(dpi->pixel_clock);

	drm_panel_unprepare(dpi->panel);
}

static void vc4_dpi_encoder_enable(struct drm_encoder *encoder)
{
	struct drm_display_mode *mode = &encoder->crtc->mode;
	struct vc4_dpi_encoder *vc4_encoder = to_vc4_dpi_encoder(encoder);
	struct vc4_dpi *dpi = vc4_encoder->dpi;
	u32 dpi_c = DPI_ENABLE | DPI_OUTPUT_ENABLE_MODE;
	int ret;

	ret = drm_panel_prepare(dpi->panel);
	if (ret) {
		DRM_ERROR("Panel failed to prepare\n");
		return;
	}

	if (dpi->connector->display_info.num_bus_formats) {
		u32 bus_format = dpi->connector->display_info.bus_formats[0];

		switch (bus_format) {
		case MEDIA_BUS_FMT_RGB888_1X24:
			dpi_c |= VC4_SET_FIELD(DPI_FORMAT_24BIT_888_RGB,
					       DPI_FORMAT);
			break;
		case MEDIA_BUS_FMT_BGR888_1X24:
			dpi_c |= VC4_SET_FIELD(DPI_FORMAT_24BIT_888_RGB,
					       DPI_FORMAT);
			dpi_c |= VC4_SET_FIELD(DPI_ORDER_BGR, DPI_ORDER);
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
			DRM_ERROR("Unknown media bus format %d\n", bus_format);
			break;
		}
	}

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		dpi_c |= DPI_HSYNC_INVERT;
	else if (!(mode->flags & DRM_MODE_FLAG_PHSYNC))
		dpi_c |= DPI_HSYNC_DISABLE;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		dpi_c |= DPI_VSYNC_INVERT;
	else if (!(mode->flags & DRM_MODE_FLAG_PVSYNC))
		dpi_c |= DPI_VSYNC_DISABLE;

	DPI_WRITE(DPI_C, dpi_c);

	ret = clk_set_rate(dpi->pixel_clock, mode->clock * 1000);
	if (ret)
		DRM_ERROR("Failed to set clock rate: %d\n", ret);

	ret = clk_prepare_enable(dpi->pixel_clock);
	if (ret)
		DRM_ERROR("Failed to set clock rate: %d\n", ret);

	ret = drm_panel_enable(dpi->panel);
	if (ret) {
		DRM_ERROR("Panel failed to enable\n");
		drm_panel_unprepare(dpi->panel);
		return;
	}
}

static bool vc4_dpi_encoder_mode_fixup(struct drm_encoder *encoder,
				       const struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
		return false;

	return true;
}

static const struct drm_encoder_helper_funcs vc4_dpi_encoder_helper_funcs = {
	.disable = vc4_dpi_encoder_disable,
	.enable = vc4_dpi_encoder_enable,
	.mode_fixup = vc4_dpi_encoder_mode_fixup,
};

static const struct of_device_id vc4_dpi_dt_match[] = {
	{ .compatible = "brcm,bcm2835-dpi", .data = NULL },
	{}
};

/* Walks the OF graph to find the panel node and then asks DRM to look
 * up the panel.
 */
static struct drm_panel *vc4_dpi_get_panel(struct device *dev)
{
	struct device_node *endpoint, *panel_node;
	struct device_node *np = dev->of_node;
	struct drm_panel *panel;

	endpoint = of_graph_get_next_endpoint(np, NULL);
	if (!endpoint) {
		dev_err(dev, "no endpoint to fetch DPI panel\n");
		return NULL;
	}

	/* don't proceed if we have an endpoint but no panel_node tied to it */
	panel_node = of_graph_get_remote_port_parent(endpoint);
	of_node_put(endpoint);
	if (!panel_node) {
		dev_err(dev, "no valid panel node\n");
		return NULL;
	}

	panel = of_drm_find_panel(panel_node);
	of_node_put(panel_node);

	return panel;
}

static int vc4_dpi_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	struct vc4_dpi *dpi;
	struct vc4_dpi_encoder *vc4_dpi_encoder;
	int ret;

	dpi = devm_kzalloc(dev, sizeof(*dpi), GFP_KERNEL);
	if (!dpi)
		return -ENOMEM;

	vc4_dpi_encoder = devm_kzalloc(dev, sizeof(*vc4_dpi_encoder),
				       GFP_KERNEL);
	if (!vc4_dpi_encoder)
		return -ENOMEM;
	vc4_dpi_encoder->base.type = VC4_ENCODER_TYPE_DPI;
	vc4_dpi_encoder->dpi = dpi;
	dpi->encoder = &vc4_dpi_encoder->base.base;

	dpi->pdev = pdev;
	dpi->regs = vc4_ioremap_regs(pdev, 0);
	if (IS_ERR(dpi->regs))
		return PTR_ERR(dpi->regs);

	vc4_dpi_dump_regs(dpi);

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
	if (ret)
		DRM_ERROR("Failed to turn on core clock: %d\n", ret);

	dpi->panel = vc4_dpi_get_panel(dev);

	drm_encoder_init(drm, dpi->encoder, &vc4_dpi_encoder_funcs,
			 DRM_MODE_ENCODER_DPI, NULL);
	drm_encoder_helper_add(dpi->encoder, &vc4_dpi_encoder_helper_funcs);

	dpi->connector = vc4_dpi_connector_init(drm, dpi);
	if (IS_ERR(dpi->connector)) {
		ret = PTR_ERR(dpi->connector);
		goto err_destroy_encoder;
	}

	if (dpi->panel)
		drm_panel_attach(dpi->panel, dpi->connector);

	dev_set_drvdata(dev, dpi);

	vc4->dpi = dpi;

	return 0;

err_destroy_encoder:
	drm_encoder_cleanup(dpi->encoder);
	clk_disable_unprepare(dpi->core_clock);
	return ret;
}

static void vc4_dpi_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	struct vc4_dpi *dpi = dev_get_drvdata(dev);

	if (dpi->panel)
		drm_panel_detach(dpi->panel);

	vc4_dpi_connector_destroy(dpi->connector);
	drm_encoder_cleanup(dpi->encoder);

	clk_disable_unprepare(dpi->core_clock);

	vc4->dpi = NULL;
}

static const struct component_ops vc4_dpi_ops = {
	.bind   = vc4_dpi_bind,
	.unbind = vc4_dpi_unbind,
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
