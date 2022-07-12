// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2020 NXP
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_print.h>

#define PC_CTRL_REG			0x0
#define  PC_COMBINE_ENABLE		BIT(0)
#define  PC_DISP_BYPASS(n)		BIT(1 + 21 * (n))
#define  PC_DISP_HSYNC_POLARITY(n)	BIT(2 + 11 * (n))
#define  PC_DISP_HSYNC_POLARITY_POS(n)	DISP_HSYNC_POLARITY(n)
#define  PC_DISP_VSYNC_POLARITY(n)	BIT(3 + 11 * (n))
#define  PC_DISP_VSYNC_POLARITY_POS(n)	DISP_VSYNC_POLARITY(n)
#define  PC_DISP_DVALID_POLARITY(n)	BIT(4 + 11 * (n))
#define  PC_DISP_DVALID_POLARITY_POS(n)	DISP_DVALID_POLARITY(n)
#define  PC_VSYNC_MASK_ENABLE		BIT(5)
#define  PC_SKIP_MODE			BIT(6)
#define  PC_SKIP_NUMBER_MASK		GENMASK(12, 7)
#define  PC_SKIP_NUMBER(n)		FIELD_PREP(PC_SKIP_NUMBER_MASK, (n))
#define  PC_DISP0_PIX_DATA_FORMAT_MASK	GENMASK(18, 16)
#define  PC_DISP0_PIX_DATA_FORMAT(fmt)	\
				FIELD_PREP(PC_DISP0_PIX_DATA_FORMAT_MASK, (fmt))
#define  PC_DISP1_PIX_DATA_FORMAT_MASK	GENMASK(21, 19)
#define  PC_DISP1_PIX_DATA_FORMAT(fmt)	\
				FIELD_PREP(PC_DISP1_PIX_DATA_FORMAT_MASK, (fmt))

#define PC_SW_RESET_REG			0x20
#define  PC_SW_RESET_N			BIT(0)
#define  PC_DISP_SW_RESET_N(n)		BIT(1 + (n))
#define  PC_FULL_RESET_N		(PC_SW_RESET_N |		\
					 PC_DISP_SW_RESET_N(0) |	\
					 PC_DISP_SW_RESET_N(1))

#define PC_REG_SET			0x4
#define PC_REG_CLR			0x8

#define DRIVER_NAME			"imx8qxp-pixel-combiner"

enum imx8qxp_pc_pix_data_format {
	RGB,
	YUV444,
	YUV422,
	SPLIT_RGB,
};

struct imx8qxp_pc_channel {
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct imx8qxp_pc *pc;
	unsigned int stream_id;
	bool is_available;
};

struct imx8qxp_pc {
	struct device *dev;
	struct imx8qxp_pc_channel ch[2];
	struct clk *clk_apb;
	void __iomem *base;
};

static inline u32 imx8qxp_pc_read(struct imx8qxp_pc *pc, unsigned int offset)
{
	return readl(pc->base + offset);
}

static inline void
imx8qxp_pc_write(struct imx8qxp_pc *pc, unsigned int offset, u32 value)
{
	writel(value, pc->base + offset);
}

static inline void
imx8qxp_pc_write_set(struct imx8qxp_pc *pc, unsigned int offset, u32 value)
{
	imx8qxp_pc_write(pc, offset + PC_REG_SET, value);
}

static inline void
imx8qxp_pc_write_clr(struct imx8qxp_pc *pc, unsigned int offset, u32 value)
{
	imx8qxp_pc_write(pc, offset + PC_REG_CLR, value);
}

static enum drm_mode_status
imx8qxp_pc_bridge_mode_valid(struct drm_bridge *bridge,
			     const struct drm_display_info *info,
			     const struct drm_display_mode *mode)
{
	if (mode->hdisplay > 2560)
		return MODE_BAD_HVALUE;

	return MODE_OK;
}

static int imx8qxp_pc_bridge_attach(struct drm_bridge *bridge,
				    enum drm_bridge_attach_flags flags)
{
	struct imx8qxp_pc_channel *ch = bridge->driver_private;
	struct imx8qxp_pc *pc = ch->pc;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)) {
		DRM_DEV_ERROR(pc->dev,
			      "do not support creating a drm_connector\n");
		return -EINVAL;
	}

	if (!bridge->encoder) {
		DRM_DEV_ERROR(pc->dev, "missing encoder\n");
		return -ENODEV;
	}

	return drm_bridge_attach(bridge->encoder,
				 ch->next_bridge, bridge,
				 DRM_BRIDGE_ATTACH_NO_CONNECTOR);
}

static void
imx8qxp_pc_bridge_mode_set(struct drm_bridge *bridge,
			   const struct drm_display_mode *mode,
			   const struct drm_display_mode *adjusted_mode)
{
	struct imx8qxp_pc_channel *ch = bridge->driver_private;
	struct imx8qxp_pc *pc = ch->pc;
	u32 val;
	int ret;

	ret = pm_runtime_get_sync(pc->dev);
	if (ret < 0)
		DRM_DEV_ERROR(pc->dev,
			      "failed to get runtime PM sync: %d\n", ret);

	ret = clk_prepare_enable(pc->clk_apb);
	if (ret)
		DRM_DEV_ERROR(pc->dev, "%s: failed to enable apb clock: %d\n",
			      __func__,  ret);

	/* HSYNC to pixel link is active low. */
	imx8qxp_pc_write_clr(pc, PC_CTRL_REG,
			     PC_DISP_HSYNC_POLARITY(ch->stream_id));

	/* VSYNC to pixel link is active low. */
	imx8qxp_pc_write_clr(pc, PC_CTRL_REG,
			     PC_DISP_VSYNC_POLARITY(ch->stream_id));

	/* Data enable to pixel link is active high. */
	imx8qxp_pc_write_set(pc, PC_CTRL_REG,
			     PC_DISP_DVALID_POLARITY(ch->stream_id));

	/* Mask the first frame output which may be incomplete. */
	imx8qxp_pc_write_set(pc, PC_CTRL_REG, PC_VSYNC_MASK_ENABLE);

	/* Only support RGB currently. */
	val = imx8qxp_pc_read(pc, PC_CTRL_REG);
	if (ch->stream_id == 0) {
		val &= ~PC_DISP0_PIX_DATA_FORMAT_MASK;
		val |= PC_DISP0_PIX_DATA_FORMAT(RGB);
	} else {
		val &= ~PC_DISP1_PIX_DATA_FORMAT_MASK;
		val |= PC_DISP1_PIX_DATA_FORMAT(RGB);
	}
	imx8qxp_pc_write(pc, PC_CTRL_REG, val);

	/* Only support bypass mode currently. */
	imx8qxp_pc_write_set(pc, PC_CTRL_REG, PC_DISP_BYPASS(ch->stream_id));

	clk_disable_unprepare(pc->clk_apb);
}

static void
imx8qxp_pc_bridge_atomic_disable(struct drm_bridge *bridge,
				 struct drm_bridge_state *old_bridge_state)
{
	struct imx8qxp_pc_channel *ch = bridge->driver_private;
	struct imx8qxp_pc *pc = ch->pc;
	int ret;

	ret = pm_runtime_put(pc->dev);
	if (ret < 0)
		DRM_DEV_ERROR(pc->dev, "failed to put runtime PM: %d\n", ret);
}

static const u32 imx8qxp_pc_bus_output_fmts[] = {
	MEDIA_BUS_FMT_RGB888_1X36_CPADLO,
	MEDIA_BUS_FMT_RGB666_1X36_CPADLO,
};

static bool imx8qxp_pc_bus_output_fmt_supported(u32 fmt)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(imx8qxp_pc_bus_output_fmts); i++) {
		if (imx8qxp_pc_bus_output_fmts[i] == fmt)
			return true;
	}

	return false;
}

static u32 *
imx8qxp_pc_bridge_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
					    struct drm_bridge_state *bridge_state,
					    struct drm_crtc_state *crtc_state,
					    struct drm_connector_state *conn_state,
					    u32 output_fmt,
					    unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	if (!imx8qxp_pc_bus_output_fmt_supported(output_fmt))
		return NULL;

	*num_input_fmts = 1;

	input_fmts = kmalloc(sizeof(*input_fmts), GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	switch (output_fmt) {
	case MEDIA_BUS_FMT_RGB888_1X36_CPADLO:
		input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X30_CPADLO;
		break;
	case MEDIA_BUS_FMT_RGB666_1X36_CPADLO:
		input_fmts[0] = MEDIA_BUS_FMT_RGB666_1X30_CPADLO;
		break;
	default:
		kfree(input_fmts);
		input_fmts = NULL;
		break;
	}

	return input_fmts;
}

static u32 *
imx8qxp_pc_bridge_atomic_get_output_bus_fmts(struct drm_bridge *bridge,
					     struct drm_bridge_state *bridge_state,
					     struct drm_crtc_state *crtc_state,
					     struct drm_connector_state *conn_state,
					     unsigned int *num_output_fmts)
{
	*num_output_fmts = ARRAY_SIZE(imx8qxp_pc_bus_output_fmts);
	return kmemdup(imx8qxp_pc_bus_output_fmts,
			sizeof(imx8qxp_pc_bus_output_fmts), GFP_KERNEL);
}

static const struct drm_bridge_funcs imx8qxp_pc_bridge_funcs = {
	.atomic_duplicate_state	= drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_bridge_destroy_state,
	.atomic_reset		= drm_atomic_helper_bridge_reset,
	.mode_valid		= imx8qxp_pc_bridge_mode_valid,
	.attach			= imx8qxp_pc_bridge_attach,
	.mode_set		= imx8qxp_pc_bridge_mode_set,
	.atomic_disable		= imx8qxp_pc_bridge_atomic_disable,
	.atomic_get_input_bus_fmts =
				imx8qxp_pc_bridge_atomic_get_input_bus_fmts,
	.atomic_get_output_bus_fmts =
				imx8qxp_pc_bridge_atomic_get_output_bus_fmts,
};

static int imx8qxp_pc_bridge_probe(struct platform_device *pdev)
{
	struct imx8qxp_pc *pc;
	struct imx8qxp_pc_channel *ch;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child, *remote;
	u32 i;
	int ret;

	pc = devm_kzalloc(dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	pc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->base))
		return PTR_ERR(pc->base);

	pc->dev = dev;

	pc->clk_apb = devm_clk_get(dev, "apb");
	if (IS_ERR(pc->clk_apb)) {
		ret = PTR_ERR(pc->clk_apb);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "failed to get apb clock: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, pc);
	pm_runtime_enable(dev);

	for_each_available_child_of_node(np, child) {
		ret = of_property_read_u32(child, "reg", &i);
		if (ret || i > 1) {
			ret = -EINVAL;
			DRM_DEV_ERROR(dev,
				      "invalid channel(%u) node address\n", i);
			goto free_child;
		}

		ch = &pc->ch[i];
		ch->pc = pc;
		ch->stream_id = i;

		remote = of_graph_get_remote_node(child, 1, 0);
		if (!remote) {
			ret = -ENODEV;
			DRM_DEV_ERROR(dev,
				      "channel%u failed to get port1's remote node: %d\n",
				      i, ret);
			goto free_child;
		}

		ch->next_bridge = of_drm_find_bridge(remote);
		if (!ch->next_bridge) {
			of_node_put(remote);
			ret = -EPROBE_DEFER;
			DRM_DEV_DEBUG_DRIVER(dev,
					     "channel%u failed to find next bridge: %d\n",
					     i, ret);
			goto free_child;
		}

		of_node_put(remote);

		ch->bridge.driver_private = ch;
		ch->bridge.funcs = &imx8qxp_pc_bridge_funcs;
		ch->bridge.of_node = child;
		ch->is_available = true;

		drm_bridge_add(&ch->bridge);
	}

	return 0;

free_child:
	of_node_put(child);

	if (i == 1 && pc->ch[0].next_bridge)
		drm_bridge_remove(&pc->ch[0].bridge);

	pm_runtime_disable(dev);
	return ret;
}

static int imx8qxp_pc_bridge_remove(struct platform_device *pdev)
{
	struct imx8qxp_pc *pc = platform_get_drvdata(pdev);
	struct imx8qxp_pc_channel *ch;
	int i;

	for (i = 0; i < 2; i++) {
		ch = &pc->ch[i];

		if (!ch->is_available)
			continue;

		drm_bridge_remove(&ch->bridge);
		ch->is_available = false;
	}

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int __maybe_unused imx8qxp_pc_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct imx8qxp_pc *pc = platform_get_drvdata(pdev);
	int ret;

	ret = clk_prepare_enable(pc->clk_apb);
	if (ret)
		DRM_DEV_ERROR(pc->dev, "%s: failed to enable apb clock: %d\n",
			      __func__,  ret);

	/* Disable pixel combiner by full reset. */
	imx8qxp_pc_write_clr(pc, PC_SW_RESET_REG, PC_FULL_RESET_N);

	clk_disable_unprepare(pc->clk_apb);

	/* Ensure the reset takes effect. */
	usleep_range(10, 20);

	return ret;
}

static int __maybe_unused imx8qxp_pc_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct imx8qxp_pc *pc = platform_get_drvdata(pdev);
	int ret;

	ret = clk_prepare_enable(pc->clk_apb);
	if (ret) {
		DRM_DEV_ERROR(pc->dev, "%s: failed to enable apb clock: %d\n",
			      __func__, ret);
		return ret;
	}

	/* out of reset */
	imx8qxp_pc_write_set(pc, PC_SW_RESET_REG, PC_FULL_RESET_N);

	clk_disable_unprepare(pc->clk_apb);

	return ret;
}

static const struct dev_pm_ops imx8qxp_pc_pm_ops = {
	SET_RUNTIME_PM_OPS(imx8qxp_pc_runtime_suspend,
			   imx8qxp_pc_runtime_resume, NULL)
};

static const struct of_device_id imx8qxp_pc_dt_ids[] = {
	{ .compatible = "fsl,imx8qm-pixel-combiner", },
	{ .compatible = "fsl,imx8qxp-pixel-combiner", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx8qxp_pc_dt_ids);

static struct platform_driver imx8qxp_pc_bridge_driver = {
	.probe	= imx8qxp_pc_bridge_probe,
	.remove = imx8qxp_pc_bridge_remove,
	.driver	= {
		.pm = &imx8qxp_pc_pm_ops,
		.name = DRIVER_NAME,
		.of_match_table = imx8qxp_pc_dt_ids,
	},
};
module_platform_driver(imx8qxp_pc_bridge_driver);

MODULE_DESCRIPTION("i.MX8QM/QXP pixel combiner bridge driver");
MODULE_AUTHOR("Liu Ying <victor.liu@nxp.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
