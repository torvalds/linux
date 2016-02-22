/*
 * Copyright (C) 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**
 * DOC: VC4 HVS module.
 *
 * The HVS is the piece of hardware that does translation, scaling,
 * colorspace conversion, and compositing of pixels stored in
 * framebuffers into a FIFO of pixels going out to the Pixel Valve
 * (CRTC).  It operates at the system clock rate (the system audio
 * clock gate, specifically), which is much higher than the pixel
 * clock rate.
 *
 * There is a single global HVS, with multiple output FIFOs that can
 * be consumed by the PVs.  This file just manages the resources for
 * the HVS, while the vc4_crtc.c code actually drives HVS setup for
 * each CRTC.
 */

#include "linux/component.h"
#include "vc4_drv.h"
#include "vc4_regs.h"

#define HVS_REG(reg) { reg, #reg }
static const struct {
	u32 reg;
	const char *name;
} hvs_regs[] = {
	HVS_REG(SCALER_DISPCTRL),
	HVS_REG(SCALER_DISPSTAT),
	HVS_REG(SCALER_DISPID),
	HVS_REG(SCALER_DISPECTRL),
	HVS_REG(SCALER_DISPPROF),
	HVS_REG(SCALER_DISPDITHER),
	HVS_REG(SCALER_DISPEOLN),
	HVS_REG(SCALER_DISPLIST0),
	HVS_REG(SCALER_DISPLIST1),
	HVS_REG(SCALER_DISPLIST2),
	HVS_REG(SCALER_DISPLSTAT),
	HVS_REG(SCALER_DISPLACT0),
	HVS_REG(SCALER_DISPLACT1),
	HVS_REG(SCALER_DISPLACT2),
	HVS_REG(SCALER_DISPCTRL0),
	HVS_REG(SCALER_DISPBKGND0),
	HVS_REG(SCALER_DISPSTAT0),
	HVS_REG(SCALER_DISPBASE0),
	HVS_REG(SCALER_DISPCTRL1),
	HVS_REG(SCALER_DISPBKGND1),
	HVS_REG(SCALER_DISPSTAT1),
	HVS_REG(SCALER_DISPBASE1),
	HVS_REG(SCALER_DISPCTRL2),
	HVS_REG(SCALER_DISPBKGND2),
	HVS_REG(SCALER_DISPSTAT2),
	HVS_REG(SCALER_DISPBASE2),
	HVS_REG(SCALER_DISPALPHA2),
};

void vc4_hvs_dump_state(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(hvs_regs); i++) {
		DRM_INFO("0x%04x (%s): 0x%08x\n",
			 hvs_regs[i].reg, hvs_regs[i].name,
			 HVS_READ(hvs_regs[i].reg));
	}

	DRM_INFO("HVS ctx:\n");
	for (i = 0; i < 64; i += 4) {
		DRM_INFO("0x%08x (%s): 0x%08x 0x%08x 0x%08x 0x%08x\n",
			 i * 4, i < HVS_BOOTLOADER_DLIST_END ? "B" : "D",
			 readl((u32 __iomem *)vc4->hvs->dlist + i + 0),
			 readl((u32 __iomem *)vc4->hvs->dlist + i + 1),
			 readl((u32 __iomem *)vc4->hvs->dlist + i + 2),
			 readl((u32 __iomem *)vc4->hvs->dlist + i + 3));
	}
}

#ifdef CONFIG_DEBUG_FS
int vc4_hvs_debugfs_regs(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(hvs_regs); i++) {
		seq_printf(m, "%s (0x%04x): 0x%08x\n",
			   hvs_regs[i].name, hvs_regs[i].reg,
			   HVS_READ(hvs_regs[i].reg));
	}

	return 0;
}
#endif

static int vc4_hvs_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dev *vc4 = drm->dev_private;
	struct vc4_hvs *hvs = NULL;

	hvs = devm_kzalloc(&pdev->dev, sizeof(*hvs), GFP_KERNEL);
	if (!hvs)
		return -ENOMEM;

	hvs->pdev = pdev;

	hvs->regs = vc4_ioremap_regs(pdev, 0);
	if (IS_ERR(hvs->regs))
		return PTR_ERR(hvs->regs);

	hvs->dlist = hvs->regs + SCALER_DLIST_START;

	vc4->hvs = hvs;
	return 0;
}

static void vc4_hvs_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dev *vc4 = drm->dev_private;

	vc4->hvs = NULL;
}

static const struct component_ops vc4_hvs_ops = {
	.bind   = vc4_hvs_bind,
	.unbind = vc4_hvs_unbind,
};

static int vc4_hvs_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &vc4_hvs_ops);
}

static int vc4_hvs_dev_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vc4_hvs_ops);
	return 0;
}

static const struct of_device_id vc4_hvs_dt_match[] = {
	{ .compatible = "brcm,bcm2835-hvs" },
	{}
};

struct platform_driver vc4_hvs_driver = {
	.probe = vc4_hvs_dev_probe,
	.remove = vc4_hvs_dev_remove,
	.driver = {
		.name = "vc4_hvs",
		.of_match_table = vc4_hvs_dt_match,
	},
};
