/*
 * Copyright (C) 2015 Samsung Electronics Co.Ltd
 * Authors:
 *	Hyungwon Hwang <human.hwang@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundationr
 */

#include <linux/platform_device.h>
#include <video/of_videomode.h>
#include <linux/of_address.h>
#include <video/videomode.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <drm/drmP.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

/* Sysreg registers for MIC */
#define DSD_CFG_MUX	0x1004
#define MIC0_RGB_MUX	(1 << 0)
#define MIC0_I80_MUX	(1 << 1)
#define MIC0_ON_MUX	(1 << 5)

/* MIC registers */
#define MIC_OP				0x0
#define MIC_IP_VER			0x0004
#define MIC_V_TIMING_0			0x0008
#define MIC_V_TIMING_1			0x000C
#define MIC_IMG_SIZE			0x0010
#define MIC_INPUT_TIMING_0		0x0014
#define MIC_INPUT_TIMING_1		0x0018
#define MIC_2D_OUTPUT_TIMING_0		0x001C
#define MIC_2D_OUTPUT_TIMING_1		0x0020
#define MIC_2D_OUTPUT_TIMING_2		0x0024
#define MIC_3D_OUTPUT_TIMING_0		0x0028
#define MIC_3D_OUTPUT_TIMING_1		0x002C
#define MIC_3D_OUTPUT_TIMING_2		0x0030
#define MIC_CORE_PARA_0			0x0034
#define MIC_CORE_PARA_1			0x0038
#define MIC_CTC_CTRL			0x0040
#define MIC_RD_DATA			0x0044

#define MIC_UPD_REG			(1 << 31)
#define MIC_ON_REG			(1 << 30)
#define MIC_TD_ON_REG			(1 << 29)
#define MIC_BS_CHG_OUT			(1 << 16)
#define MIC_VIDEO_TYPE(x)		(((x) & 0xf) << 12)
#define MIC_PSR_EN			(1 << 5)
#define MIC_SW_RST			(1 << 4)
#define MIC_ALL_RST			(1 << 3)
#define MIC_CORE_VER_CONTROL		(1 << 2)
#define MIC_MODE_SEL_COMMAND_MODE	(1 << 1)
#define MIC_MODE_SEL_MASK		(1 << 1)
#define MIC_CORE_EN			(1 << 0)

#define MIC_V_PULSE_WIDTH(x)		(((x) & 0x3fff) << 16)
#define MIC_V_PERIOD_LINE(x)		((x) & 0x3fff)

#define MIC_VBP_SIZE(x)			(((x) & 0x3fff) << 16)
#define MIC_VFP_SIZE(x)			((x) & 0x3fff)

#define MIC_IMG_V_SIZE(x)		(((x) & 0x3fff) << 16)
#define MIC_IMG_H_SIZE(x)		((x) & 0x3fff)

#define MIC_H_PULSE_WIDTH_IN(x)		(((x) & 0x3fff) << 16)
#define MIC_H_PERIOD_PIXEL_IN(x)	((x) & 0x3fff)

#define MIC_HBP_SIZE_IN(x)		(((x) & 0x3fff) << 16)
#define MIC_HFP_SIZE_IN(x)		((x) & 0x3fff)

#define MIC_H_PULSE_WIDTH_2D(x)		(((x) & 0x3fff) << 16)
#define MIC_H_PERIOD_PIXEL_2D(x)	((x) & 0x3fff)

#define MIC_HBP_SIZE_2D(x)		(((x) & 0x3fff) << 16)
#define MIC_HFP_SIZE_2D(x)		((x) & 0x3fff)

#define MIC_BS_SIZE_2D(x)	((x) & 0x3fff)

enum {
	ENDPOINT_DECON_NODE,
	ENDPOINT_DSI_NODE,
	NUM_ENDPOINTS
};

static char *clk_names[] = { "pclk_mic0", "sclk_rgb_vclk_to_mic0" };
#define NUM_CLKS		ARRAY_SIZE(clk_names)
static DEFINE_MUTEX(mic_mutex);

struct exynos_mic {
	struct device *dev;
	void __iomem *reg;
	struct regmap *sysreg;
	struct clk *clks[NUM_CLKS];

	bool i80_mode;
	struct videomode vm;
	struct drm_encoder *encoder;
	struct drm_bridge bridge;

	bool enabled;
};

static void mic_set_path(struct exynos_mic *mic, bool enable)
{
	int ret;
	unsigned int val;

	ret = regmap_read(mic->sysreg, DSD_CFG_MUX, &val);
	if (ret) {
		DRM_ERROR("mic: Failed to read system register\n");
		return;
	}

	if (enable) {
		if (mic->i80_mode)
			val |= MIC0_I80_MUX;
		else
			val |= MIC0_RGB_MUX;

		val |=  MIC0_ON_MUX;
	} else
		val &= ~(MIC0_RGB_MUX | MIC0_I80_MUX | MIC0_ON_MUX);

	ret = regmap_write(mic->sysreg, DSD_CFG_MUX, val);
	if (ret)
		DRM_ERROR("mic: Failed to read system register\n");
}

static int mic_sw_reset(struct exynos_mic *mic)
{
	unsigned int retry = 100;
	int ret;

	writel(MIC_SW_RST, mic->reg + MIC_OP);

	while (retry-- > 0) {
		ret = readl(mic->reg + MIC_OP);
		if (!(ret & MIC_SW_RST))
			return 0;

		udelay(10);
	}

	return -ETIMEDOUT;
}

static void mic_set_porch_timing(struct exynos_mic *mic)
{
	struct videomode vm = mic->vm;
	u32 reg;

	reg = MIC_V_PULSE_WIDTH(vm.vsync_len) +
		MIC_V_PERIOD_LINE(vm.vsync_len + vm.vactive +
				vm.vback_porch + vm.vfront_porch);
	writel(reg, mic->reg + MIC_V_TIMING_0);

	reg = MIC_VBP_SIZE(vm.vback_porch) +
		MIC_VFP_SIZE(vm.vfront_porch);
	writel(reg, mic->reg + MIC_V_TIMING_1);

	reg = MIC_V_PULSE_WIDTH(vm.hsync_len) +
		MIC_V_PERIOD_LINE(vm.hsync_len + vm.hactive +
				vm.hback_porch + vm.hfront_porch);
	writel(reg, mic->reg + MIC_INPUT_TIMING_0);

	reg = MIC_VBP_SIZE(vm.hback_porch) +
		MIC_VFP_SIZE(vm.hfront_porch);
	writel(reg, mic->reg + MIC_INPUT_TIMING_1);
}

static void mic_set_img_size(struct exynos_mic *mic)
{
	struct videomode *vm = &mic->vm;
	u32 reg;

	reg = MIC_IMG_H_SIZE(vm->hactive) +
		MIC_IMG_V_SIZE(vm->vactive);

	writel(reg, mic->reg + MIC_IMG_SIZE);
}

static void mic_set_output_timing(struct exynos_mic *mic)
{
	struct videomode vm = mic->vm;
	u32 reg, bs_size_2d;

	DRM_DEBUG("w: %u, h: %u\n", vm.hactive, vm.vactive);
	bs_size_2d = ((vm.hactive >> 2) << 1) + (vm.vactive % 4);
	reg = MIC_BS_SIZE_2D(bs_size_2d);
	writel(reg, mic->reg + MIC_2D_OUTPUT_TIMING_2);

	if (!mic->i80_mode) {
		reg = MIC_H_PULSE_WIDTH_2D(vm.hsync_len) +
			MIC_H_PERIOD_PIXEL_2D(vm.hsync_len + bs_size_2d +
					vm.hback_porch + vm.hfront_porch);
		writel(reg, mic->reg + MIC_2D_OUTPUT_TIMING_0);

		reg = MIC_HBP_SIZE_2D(vm.hback_porch) +
			MIC_H_PERIOD_PIXEL_2D(vm.hfront_porch);
		writel(reg, mic->reg + MIC_2D_OUTPUT_TIMING_1);
	}
}

static void mic_set_reg_on(struct exynos_mic *mic, bool enable)
{
	u32 reg = readl(mic->reg + MIC_OP);

	if (enable) {
		reg &= ~(MIC_MODE_SEL_MASK | MIC_CORE_VER_CONTROL | MIC_PSR_EN);
		reg |= (MIC_CORE_EN | MIC_BS_CHG_OUT | MIC_ON_REG);

		reg  &= ~MIC_MODE_SEL_COMMAND_MODE;
		if (mic->i80_mode)
			reg |= MIC_MODE_SEL_COMMAND_MODE;
	} else {
		reg &= ~MIC_CORE_EN;
	}

	reg |= MIC_UPD_REG;
	writel(reg, mic->reg + MIC_OP);
}

static struct device_node *get_remote_node(struct device_node *from, int reg)
{
	struct device_node *endpoint = NULL, *remote_node = NULL;

	endpoint = of_graph_get_endpoint_by_regs(from, reg, -1);
	if (!endpoint) {
		DRM_ERROR("mic: Failed to find remote port from %s",
				from->full_name);
		goto exit;
	}

	remote_node = of_graph_get_remote_port_parent(endpoint);
	if (!remote_node) {
		DRM_ERROR("mic: Failed to find remote port parent from %s",
							from->full_name);
		goto exit;
	}

exit:
	of_node_put(endpoint);
	return remote_node;
}

static int parse_dt(struct exynos_mic *mic)
{
	int ret = 0, i, j;
	struct device_node *remote_node;
	struct device_node *nodes[3];

	/*
	 * The order of endpoints does matter.
	 * The first node must be for decon and the second one must be for dsi.
	 */
	for (i = 0, j = 0; i < NUM_ENDPOINTS; i++) {
		remote_node = get_remote_node(mic->dev->of_node, i);
		if (!remote_node) {
			ret = -EPIPE;
			goto exit;
		}
		nodes[j++] = remote_node;

		switch (i) {
		case ENDPOINT_DECON_NODE:
			/* decon node */
			if (of_get_child_by_name(remote_node,
						"i80-if-timings"))
				mic->i80_mode = 1;

			break;
		case ENDPOINT_DSI_NODE:
			/* panel node */
			remote_node = get_remote_node(remote_node, 1);
			if (!remote_node) {
				ret = -EPIPE;
				goto exit;
			}
			nodes[j++] = remote_node;

			break;
		default:
			DRM_ERROR("mic: Unknown endpoint from MIC");
			break;
		}
	}

exit:
	while (--j > -1)
		of_node_put(nodes[j]);

	return ret;
}

static void mic_disable(struct drm_bridge *bridge) { }

static void mic_post_disable(struct drm_bridge *bridge)
{
	struct exynos_mic *mic = bridge->driver_private;
	int i;

	mutex_lock(&mic_mutex);
	if (!mic->enabled)
		goto already_disabled;

	mic_set_path(mic, 0);

	for (i = NUM_CLKS - 1; i > -1; i--)
		clk_disable_unprepare(mic->clks[i]);

	mic->enabled = 0;

already_disabled:
	mutex_unlock(&mic_mutex);
}

static void mic_mode_set(struct drm_bridge *bridge,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct exynos_mic *mic = bridge->driver_private;

	mutex_lock(&mic_mutex);
	drm_display_mode_to_videomode(mode, &mic->vm);
	mutex_unlock(&mic_mutex);
}

static void mic_pre_enable(struct drm_bridge *bridge)
{
	struct exynos_mic *mic = bridge->driver_private;
	int ret, i;

	mutex_lock(&mic_mutex);
	if (mic->enabled)
		goto already_enabled;

	for (i = 0; i < NUM_CLKS; i++) {
		ret = clk_prepare_enable(mic->clks[i]);
		if (ret < 0) {
			DRM_ERROR("Failed to enable clock (%s)\n",
							clk_names[i]);
			goto turn_off_clks;
		}
	}

	mic_set_path(mic, 1);

	ret = mic_sw_reset(mic);
	if (ret) {
		DRM_ERROR("Failed to reset\n");
		goto turn_off_clks;
	}

	if (!mic->i80_mode)
		mic_set_porch_timing(mic);
	mic_set_img_size(mic);
	mic_set_output_timing(mic);
	mic_set_reg_on(mic, 1);
	mic->enabled = 1;
	mutex_unlock(&mic_mutex);

	return;

turn_off_clks:
	while (--i > -1)
		clk_disable_unprepare(mic->clks[i]);
already_enabled:
	mutex_unlock(&mic_mutex);
}

static void mic_enable(struct drm_bridge *bridge) { }

static const struct drm_bridge_funcs mic_bridge_funcs = {
	.disable = mic_disable,
	.post_disable = mic_post_disable,
	.mode_set = mic_mode_set,
	.pre_enable = mic_pre_enable,
	.enable = mic_enable,
};

static int exynos_mic_bind(struct device *dev, struct device *master,
			   void *data)
{
	struct exynos_mic *mic = dev_get_drvdata(dev);
	int ret;

	mic->bridge.funcs = &mic_bridge_funcs;
	mic->bridge.of_node = dev->of_node;
	mic->bridge.driver_private = mic;
	ret = drm_bridge_add(&mic->bridge);
	if (ret)
		DRM_ERROR("mic: Failed to add MIC to the global bridge list\n");

	return ret;
}

static void exynos_mic_unbind(struct device *dev, struct device *master,
			      void *data)
{
	struct exynos_mic *mic = dev_get_drvdata(dev);
	int i;

	mutex_lock(&mic_mutex);
	if (!mic->enabled)
		goto already_disabled;

	for (i = NUM_CLKS - 1; i > -1; i--)
		clk_disable_unprepare(mic->clks[i]);

already_disabled:
	mutex_unlock(&mic_mutex);

	drm_bridge_remove(&mic->bridge);
}

static const struct component_ops exynos_mic_component_ops = {
	.bind	= exynos_mic_bind,
	.unbind	= exynos_mic_unbind,
};

static int exynos_mic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos_mic *mic;
	struct resource res;
	int ret, i;

	mic = devm_kzalloc(dev, sizeof(*mic), GFP_KERNEL);
	if (!mic) {
		DRM_ERROR("mic: Failed to allocate memory for MIC object\n");
		ret = -ENOMEM;
		goto err;
	}

	mic->dev = dev;

	ret = parse_dt(mic);
	if (ret)
		goto err;

	ret = of_address_to_resource(dev->of_node, 0, &res);
	if (ret) {
		DRM_ERROR("mic: Failed to get mem region for MIC\n");
		goto err;
	}
	mic->reg = devm_ioremap(dev, res.start, resource_size(&res));
	if (!mic->reg) {
		DRM_ERROR("mic: Failed to remap for MIC\n");
		ret = -ENOMEM;
		goto err;
	}

	mic->sysreg = syscon_regmap_lookup_by_phandle(dev->of_node,
							"samsung,disp-syscon");
	if (IS_ERR(mic->sysreg)) {
		DRM_ERROR("mic: Failed to get system register.\n");
		ret = PTR_ERR(mic->sysreg);
		goto err;
	}

	for (i = 0; i < NUM_CLKS; i++) {
		mic->clks[i] = devm_clk_get(dev, clk_names[i]);
		if (IS_ERR(mic->clks[i])) {
			DRM_ERROR("mic: Failed to get clock (%s)\n",
								clk_names[i]);
			ret = PTR_ERR(mic->clks[i]);
			goto err;
		}
	}

	platform_set_drvdata(pdev, mic);

	DRM_DEBUG_KMS("MIC has been probed\n");
	return component_add(dev, &exynos_mic_component_ops);

err:
	return ret;
}

static int exynos_mic_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &exynos_mic_component_ops);
	return 0;
}

static const struct of_device_id exynos_mic_of_match[] = {
	{ .compatible = "samsung,exynos5433-mic" },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos_mic_of_match);

struct platform_driver mic_driver = {
	.probe		= exynos_mic_probe,
	.remove		= exynos_mic_remove,
	.driver		= {
		.name	= "exynos-mic",
		.owner	= THIS_MODULE,
		.of_match_table = exynos_mic_of_match,
	},
};
