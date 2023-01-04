// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Samsung Electronics Co.Ltd
 * Authors:
 *	Hyungwon Hwang <human.hwang@samsung.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <video/of_videomode.h>
#include <video/videomode.h>

#include <drm/drm_bridge.h>
#include <drm/drm_encoder.h>
#include <drm/drm_print.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_crtc.h"

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

static const char *const clk_names[] = { "pclk_mic0", "sclk_rgb_vclk_to_mic0" };
#define NUM_CLKS		ARRAY_SIZE(clk_names)
static DEFINE_MUTEX(mic_mutex);

struct exynos_mic {
	struct device *dev;
	void __iomem *reg;
	struct regmap *sysreg;
	struct clk *clks[NUM_CLKS];

	bool i80_mode;
	struct videomode vm;
	struct drm_bridge bridge;

	bool enabled;
};

static void mic_set_path(struct exynos_mic *mic, bool enable)
{
	int ret;
	unsigned int val;

	ret = regmap_read(mic->sysreg, DSD_CFG_MUX, &val);
	if (ret) {
		DRM_DEV_ERROR(mic->dev,
			      "mic: Failed to read system register\n");
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
		DRM_DEV_ERROR(mic->dev,
			      "mic: Failed to read system register\n");
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

	DRM_DEV_DEBUG(mic->dev, "w: %u, h: %u\n", vm.hactive, vm.vactive);
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

static void mic_post_disable(struct drm_bridge *bridge)
{
	struct exynos_mic *mic = bridge->driver_private;

	mutex_lock(&mic_mutex);
	if (!mic->enabled)
		goto already_disabled;

	mic_set_path(mic, 0);

	pm_runtime_put(mic->dev);
	mic->enabled = 0;

already_disabled:
	mutex_unlock(&mic_mutex);
}

static void mic_mode_set(struct drm_bridge *bridge,
			 const struct drm_display_mode *mode,
			 const struct drm_display_mode *adjusted_mode)
{
	struct exynos_mic *mic = bridge->driver_private;

	mutex_lock(&mic_mutex);
	drm_display_mode_to_videomode(mode, &mic->vm);
	mic->i80_mode = to_exynos_crtc(bridge->encoder->crtc)->i80_mode;
	mutex_unlock(&mic_mutex);
}

static void mic_pre_enable(struct drm_bridge *bridge)
{
	struct exynos_mic *mic = bridge->driver_private;
	int ret;

	mutex_lock(&mic_mutex);
	if (mic->enabled)
		goto unlock;

	ret = pm_runtime_resume_and_get(mic->dev);
	if (ret < 0)
		goto unlock;

	mic_set_path(mic, 1);

	ret = mic_sw_reset(mic);
	if (ret) {
		DRM_DEV_ERROR(mic->dev, "Failed to reset\n");
		goto turn_off;
	}

	if (!mic->i80_mode)
		mic_set_porch_timing(mic);
	mic_set_img_size(mic);
	mic_set_output_timing(mic);
	mic_set_reg_on(mic, 1);
	mic->enabled = 1;
	mutex_unlock(&mic_mutex);

	return;

turn_off:
	pm_runtime_put(mic->dev);
unlock:
	mutex_unlock(&mic_mutex);
}

static const struct drm_bridge_funcs mic_bridge_funcs = {
	.post_disable = mic_post_disable,
	.mode_set = mic_mode_set,
	.pre_enable = mic_pre_enable,
};

static int exynos_mic_bind(struct device *dev, struct device *master,
			   void *data)
{
	struct exynos_mic *mic = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct exynos_drm_crtc *crtc = exynos_drm_crtc_get_by_type(drm_dev,
						       EXYNOS_DISPLAY_TYPE_LCD);
	struct drm_encoder *e, *encoder = NULL;

	drm_for_each_encoder(e, drm_dev)
		if (e->possible_crtcs == drm_crtc_mask(&crtc->base))
			encoder = e;
	if (!encoder)
		return -ENODEV;

	mic->bridge.driver_private = mic;

	return drm_bridge_attach(encoder, &mic->bridge, NULL, 0);
}

static void exynos_mic_unbind(struct device *dev, struct device *master,
			      void *data)
{
	struct exynos_mic *mic = dev_get_drvdata(dev);

	mutex_lock(&mic_mutex);
	if (!mic->enabled)
		goto already_disabled;

	pm_runtime_put(mic->dev);

already_disabled:
	mutex_unlock(&mic_mutex);
}

static const struct component_ops exynos_mic_component_ops = {
	.bind	= exynos_mic_bind,
	.unbind	= exynos_mic_unbind,
};

static int exynos_mic_suspend(struct device *dev)
{
	struct exynos_mic *mic = dev_get_drvdata(dev);
	int i;

	for (i = NUM_CLKS - 1; i > -1; i--)
		clk_disable_unprepare(mic->clks[i]);

	return 0;
}

static int exynos_mic_resume(struct device *dev)
{
	struct exynos_mic *mic = dev_get_drvdata(dev);
	int ret, i;

	for (i = 0; i < NUM_CLKS; i++) {
		ret = clk_prepare_enable(mic->clks[i]);
		if (ret < 0) {
			DRM_DEV_ERROR(dev, "Failed to enable clock (%s)\n",
				      clk_names[i]);
			while (--i > -1)
				clk_disable_unprepare(mic->clks[i]);
			return ret;
		}
	}
	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(exynos_mic_pm_ops, exynos_mic_suspend,
				 exynos_mic_resume, NULL);

static int exynos_mic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos_mic *mic;
	struct resource res;
	int ret, i;

	mic = devm_kzalloc(dev, sizeof(*mic), GFP_KERNEL);
	if (!mic) {
		DRM_DEV_ERROR(dev,
			      "mic: Failed to allocate memory for MIC object\n");
		ret = -ENOMEM;
		goto err;
	}

	mic->dev = dev;

	ret = of_address_to_resource(dev->of_node, 0, &res);
	if (ret) {
		DRM_DEV_ERROR(dev, "mic: Failed to get mem region for MIC\n");
		goto err;
	}
	mic->reg = devm_ioremap(dev, res.start, resource_size(&res));
	if (!mic->reg) {
		DRM_DEV_ERROR(dev, "mic: Failed to remap for MIC\n");
		ret = -ENOMEM;
		goto err;
	}

	mic->sysreg = syscon_regmap_lookup_by_phandle(dev->of_node,
							"samsung,disp-syscon");
	if (IS_ERR(mic->sysreg)) {
		DRM_DEV_ERROR(dev, "mic: Failed to get system register.\n");
		ret = PTR_ERR(mic->sysreg);
		goto err;
	}

	for (i = 0; i < NUM_CLKS; i++) {
		mic->clks[i] = devm_clk_get(dev, clk_names[i]);
		if (IS_ERR(mic->clks[i])) {
			DRM_DEV_ERROR(dev, "mic: Failed to get clock (%s)\n",
				      clk_names[i]);
			ret = PTR_ERR(mic->clks[i]);
			goto err;
		}
	}

	platform_set_drvdata(pdev, mic);

	mic->bridge.funcs = &mic_bridge_funcs;
	mic->bridge.of_node = dev->of_node;

	drm_bridge_add(&mic->bridge);

	pm_runtime_enable(dev);

	ret = component_add(dev, &exynos_mic_component_ops);
	if (ret)
		goto err_pm;

	DRM_DEV_DEBUG_KMS(dev, "MIC has been probed\n");

	return 0;

err_pm:
	pm_runtime_disable(dev);
err:
	return ret;
}

static int exynos_mic_remove(struct platform_device *pdev)
{
	struct exynos_mic *mic = platform_get_drvdata(pdev);

	component_del(&pdev->dev, &exynos_mic_component_ops);
	pm_runtime_disable(&pdev->dev);

	drm_bridge_remove(&mic->bridge);

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
		.pm	= pm_ptr(&exynos_mic_pm_ops),
		.owner	= THIS_MODULE,
		.of_match_table = exynos_mic_of_match,
	},
};
