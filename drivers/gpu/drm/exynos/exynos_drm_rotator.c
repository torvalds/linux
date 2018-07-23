/*
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Authors:
 *	YoungJun Cho <yj44.cho@samsung.com>
 *	Eunchul Kim <chulspro.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundationr
 */

#include <linux/kernel.h>
#include <linux/component.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>

#include <drm/drmP.h>
#include <drm/exynos_drm.h>
#include "regs-rotator.h"
#include "exynos_drm_drv.h"
#include "exynos_drm_iommu.h"
#include "exynos_drm_ipp.h"

/*
 * Rotator supports image crop/rotator and input/output DMA operations.
 * input DMA reads image data from the memory.
 * output DMA writes image data to memory.
 */

#define ROTATOR_AUTOSUSPEND_DELAY	2000

#define rot_read(offset)	readl(rot->regs + (offset))
#define rot_write(cfg, offset)	writel(cfg, rot->regs + (offset))

enum rot_irq_status {
	ROT_IRQ_STATUS_COMPLETE	= 8,
	ROT_IRQ_STATUS_ILLEGAL	= 9,
};

struct rot_variant {
	const struct exynos_drm_ipp_formats *formats;
	unsigned int	num_formats;
};

/*
 * A structure of rotator context.
 * @ippdrv: prepare initialization using ippdrv.
 * @regs: memory mapped io registers.
 * @clock: rotator gate clock.
 * @limit_tbl: limitation of rotator.
 * @irq: irq number.
 */
struct rot_context {
	struct exynos_drm_ipp ipp;
	struct drm_device *drm_dev;
	struct device	*dev;
	void __iomem	*regs;
	struct clk	*clock;
	const struct exynos_drm_ipp_formats *formats;
	unsigned int	num_formats;
	struct exynos_drm_ipp_task	*task;
};

static void rotator_reg_set_irq(struct rot_context *rot, bool enable)
{
	u32 val = rot_read(ROT_CONFIG);

	if (enable == true)
		val |= ROT_CONFIG_IRQ;
	else
		val &= ~ROT_CONFIG_IRQ;

	rot_write(val, ROT_CONFIG);
}

static enum rot_irq_status rotator_reg_get_irq_status(struct rot_context *rot)
{
	u32 val = rot_read(ROT_STATUS);

	val = ROT_STATUS_IRQ(val);

	if (val == ROT_STATUS_IRQ_VAL_COMPLETE)
		return ROT_IRQ_STATUS_COMPLETE;

	return ROT_IRQ_STATUS_ILLEGAL;
}

static irqreturn_t rotator_irq_handler(int irq, void *arg)
{
	struct rot_context *rot = arg;
	enum rot_irq_status irq_status;
	u32 val;

	/* Get execution result */
	irq_status = rotator_reg_get_irq_status(rot);

	/* clear status */
	val = rot_read(ROT_STATUS);
	val |= ROT_STATUS_IRQ_PENDING((u32)irq_status);
	rot_write(val, ROT_STATUS);

	if (rot->task) {
		struct exynos_drm_ipp_task *task = rot->task;

		rot->task = NULL;
		pm_runtime_mark_last_busy(rot->dev);
		pm_runtime_put_autosuspend(rot->dev);
		exynos_drm_ipp_task_done(task,
			irq_status == ROT_IRQ_STATUS_COMPLETE ? 0 : -EINVAL);
	}

	return IRQ_HANDLED;
}

static void rotator_src_set_fmt(struct rot_context *rot, u32 fmt)
{
	u32 val;

	val = rot_read(ROT_CONTROL);
	val &= ~ROT_CONTROL_FMT_MASK;

	switch (fmt) {
	case DRM_FORMAT_NV12:
		val |= ROT_CONTROL_FMT_YCBCR420_2P;
		break;
	case DRM_FORMAT_XRGB8888:
		val |= ROT_CONTROL_FMT_RGB888;
		break;
	}

	rot_write(val, ROT_CONTROL);
}

static void rotator_src_set_buf(struct rot_context *rot,
				struct exynos_drm_ipp_buffer *buf)
{
	u32 val;

	/* Set buffer size configuration */
	val = ROT_SET_BUF_SIZE_H(buf->buf.height) |
	      ROT_SET_BUF_SIZE_W(buf->buf.pitch[0] / buf->format->cpp[0]);
	rot_write(val, ROT_SRC_BUF_SIZE);

	/* Set crop image position configuration */
	val = ROT_CROP_POS_Y(buf->rect.y) | ROT_CROP_POS_X(buf->rect.x);
	rot_write(val, ROT_SRC_CROP_POS);
	val = ROT_SRC_CROP_SIZE_H(buf->rect.h) |
	      ROT_SRC_CROP_SIZE_W(buf->rect.w);
	rot_write(val, ROT_SRC_CROP_SIZE);

	/* Set buffer DMA address */
	rot_write(buf->dma_addr[0], ROT_SRC_BUF_ADDR(0));
	rot_write(buf->dma_addr[1], ROT_SRC_BUF_ADDR(1));
}

static void rotator_dst_set_transf(struct rot_context *rot,
				   unsigned int rotation)
{
	u32 val;

	/* Set transform configuration */
	val = rot_read(ROT_CONTROL);
	val &= ~ROT_CONTROL_FLIP_MASK;

	if (rotation & DRM_MODE_REFLECT_X)
		val |= ROT_CONTROL_FLIP_HORIZONTAL;
	if (rotation & DRM_MODE_REFLECT_Y)
		val |= ROT_CONTROL_FLIP_VERTICAL;

	val &= ~ROT_CONTROL_ROT_MASK;

	if (rotation & DRM_MODE_ROTATE_90)
		val |= ROT_CONTROL_ROT_90;
	else if (rotation & DRM_MODE_ROTATE_180)
		val |= ROT_CONTROL_ROT_180;
	else if (rotation & DRM_MODE_ROTATE_270)
		val |= ROT_CONTROL_ROT_270;

	rot_write(val, ROT_CONTROL);
}

static void rotator_dst_set_buf(struct rot_context *rot,
				struct exynos_drm_ipp_buffer *buf)
{
	u32 val;

	/* Set buffer size configuration */
	val = ROT_SET_BUF_SIZE_H(buf->buf.height) |
	      ROT_SET_BUF_SIZE_W(buf->buf.pitch[0] / buf->format->cpp[0]);
	rot_write(val, ROT_DST_BUF_SIZE);

	/* Set crop image position configuration */
	val = ROT_CROP_POS_Y(buf->rect.y) | ROT_CROP_POS_X(buf->rect.x);
	rot_write(val, ROT_DST_CROP_POS);

	/* Set buffer DMA address */
	rot_write(buf->dma_addr[0], ROT_DST_BUF_ADDR(0));
	rot_write(buf->dma_addr[1], ROT_DST_BUF_ADDR(1));
}

static void rotator_start(struct rot_context *rot)
{
	u32 val;

	/* Set interrupt enable */
	rotator_reg_set_irq(rot, true);

	val = rot_read(ROT_CONTROL);
	val |= ROT_CONTROL_START;
	rot_write(val, ROT_CONTROL);
}

static int rotator_commit(struct exynos_drm_ipp *ipp,
			  struct exynos_drm_ipp_task *task)
{
	struct rot_context *rot =
			container_of(ipp, struct rot_context, ipp);

	pm_runtime_get_sync(rot->dev);
	rot->task = task;

	rotator_src_set_fmt(rot, task->src.buf.fourcc);
	rotator_src_set_buf(rot, &task->src);
	rotator_dst_set_transf(rot, task->transform.rotation);
	rotator_dst_set_buf(rot, &task->dst);
	rotator_start(rot);

	return 0;
}

static const struct exynos_drm_ipp_funcs ipp_funcs = {
	.commit = rotator_commit,
};

static int rotator_bind(struct device *dev, struct device *master, void *data)
{
	struct rot_context *rot = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct exynos_drm_ipp *ipp = &rot->ipp;

	rot->drm_dev = drm_dev;
	drm_iommu_attach_device(drm_dev, dev);

	exynos_drm_ipp_register(drm_dev, ipp, &ipp_funcs,
			   DRM_EXYNOS_IPP_CAP_CROP | DRM_EXYNOS_IPP_CAP_ROTATE,
			   rot->formats, rot->num_formats, "rotator");

	dev_info(dev, "The exynos rotator has been probed successfully\n");

	return 0;
}

static void rotator_unbind(struct device *dev, struct device *master,
			void *data)
{
	struct rot_context *rot = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct exynos_drm_ipp *ipp = &rot->ipp;

	exynos_drm_ipp_unregister(drm_dev, ipp);
	drm_iommu_detach_device(rot->drm_dev, rot->dev);
}

static const struct component_ops rotator_component_ops = {
	.bind	= rotator_bind,
	.unbind = rotator_unbind,
};

static int rotator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource	*regs_res;
	struct rot_context *rot;
	const struct rot_variant *variant;
	int irq;
	int ret;

	rot = devm_kzalloc(dev, sizeof(*rot), GFP_KERNEL);
	if (!rot)
		return -ENOMEM;

	variant = of_device_get_match_data(dev);
	rot->formats = variant->formats;
	rot->num_formats = variant->num_formats;
	rot->dev = dev;
	regs_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rot->regs = devm_ioremap_resource(dev, regs_res);
	if (IS_ERR(rot->regs))
		return PTR_ERR(rot->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "failed to get irq\n");
		return irq;
	}

	ret = devm_request_irq(dev, irq, rotator_irq_handler, 0, dev_name(dev),
			       rot);
	if (ret < 0) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}

	rot->clock = devm_clk_get(dev, "rotator");
	if (IS_ERR(rot->clock)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(rot->clock);
	}

	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, ROTATOR_AUTOSUSPEND_DELAY);
	pm_runtime_enable(dev);
	platform_set_drvdata(pdev, rot);

	ret = component_add(dev, &rotator_component_ops);
	if (ret)
		goto err_component;

	return 0;

err_component:
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);
	return ret;
}

static int rotator_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	component_del(dev, &rotator_component_ops);
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);

	return 0;
}

#ifdef CONFIG_PM
static int rotator_runtime_suspend(struct device *dev)
{
	struct rot_context *rot = dev_get_drvdata(dev);

	clk_disable_unprepare(rot->clock);
	return 0;
}

static int rotator_runtime_resume(struct device *dev)
{
	struct rot_context *rot = dev_get_drvdata(dev);

	return clk_prepare_enable(rot->clock);
}
#endif

static const struct drm_exynos_ipp_limit rotator_4210_rbg888_limits[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 8, SZ_16K }, .v = { 8, SZ_16K }) },
	{ IPP_SIZE_LIMIT(AREA, .h.align = 4, .v.align = 4) },
};

static const struct drm_exynos_ipp_limit rotator_4412_rbg888_limits[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 8, SZ_8K }, .v = { 8, SZ_8K }) },
	{ IPP_SIZE_LIMIT(AREA, .h.align = 4, .v.align = 4) },
};

static const struct drm_exynos_ipp_limit rotator_5250_rbg888_limits[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 8, SZ_8K }, .v = { 8, SZ_8K }) },
	{ IPP_SIZE_LIMIT(AREA, .h.align = 2, .v.align = 2) },
};

static const struct drm_exynos_ipp_limit rotator_4210_yuv_limits[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 32, SZ_64K }, .v = { 32, SZ_64K }) },
	{ IPP_SIZE_LIMIT(AREA, .h.align = 8, .v.align = 8) },
};

static const struct drm_exynos_ipp_limit rotator_4412_yuv_limits[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 32, SZ_32K }, .v = { 32, SZ_32K }) },
	{ IPP_SIZE_LIMIT(AREA, .h.align = 8, .v.align = 8) },
};

static const struct exynos_drm_ipp_formats rotator_4210_formats[] = {
	{ IPP_SRCDST_FORMAT(XRGB8888, rotator_4210_rbg888_limits) },
	{ IPP_SRCDST_FORMAT(NV12, rotator_4210_yuv_limits) },
};

static const struct exynos_drm_ipp_formats rotator_4412_formats[] = {
	{ IPP_SRCDST_FORMAT(XRGB8888, rotator_4412_rbg888_limits) },
	{ IPP_SRCDST_FORMAT(NV12, rotator_4412_yuv_limits) },
};

static const struct exynos_drm_ipp_formats rotator_5250_formats[] = {
	{ IPP_SRCDST_FORMAT(XRGB8888, rotator_5250_rbg888_limits) },
	{ IPP_SRCDST_FORMAT(NV12, rotator_4412_yuv_limits) },
};

static const struct rot_variant rotator_4210_data = {
	.formats = rotator_4210_formats,
	.num_formats = ARRAY_SIZE(rotator_4210_formats),
};

static const struct rot_variant rotator_4412_data = {
	.formats = rotator_4412_formats,
	.num_formats = ARRAY_SIZE(rotator_4412_formats),
};

static const struct rot_variant rotator_5250_data = {
	.formats = rotator_5250_formats,
	.num_formats = ARRAY_SIZE(rotator_5250_formats),
};

static const struct of_device_id exynos_rotator_match[] = {
	{
		.compatible = "samsung,exynos4210-rotator",
		.data = &rotator_4210_data,
	}, {
		.compatible = "samsung,exynos4212-rotator",
		.data = &rotator_4412_data,
	}, {
		.compatible = "samsung,exynos5250-rotator",
		.data = &rotator_5250_data,
	}, {
	},
};
MODULE_DEVICE_TABLE(of, exynos_rotator_match);

static const struct dev_pm_ops rotator_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rotator_runtime_suspend, rotator_runtime_resume,
									NULL)
};

struct platform_driver rotator_driver = {
	.probe		= rotator_probe,
	.remove		= rotator_remove,
	.driver		= {
		.name	= "exynos-rotator",
		.owner	= THIS_MODULE,
		.pm	= &rotator_pm_ops,
		.of_match_table = exynos_rotator_match,
	},
};
