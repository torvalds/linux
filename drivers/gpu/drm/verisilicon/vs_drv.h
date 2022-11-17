/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_DRV_H__
#define __VS_DRV_H__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/version.h>

#include <drm/drm_gem.h>
#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
#include <drm/drmP.h>
#endif

#include "vs_plane.h"
#ifdef CONFIG_VERISILICON_MMU
#include "vs_dc_mmu.h"
#endif


/*
 *
 * @dma_dev: device for DMA API.
 *	- use the first attached device if support iommu
	else use drm device (only contiguous buffer support)
 * @domain: iommu domain for DRM.
 *	- all DC IOMMU share same domain to reduce mapping
 * @pitch_alignment: buffer pitch alignment required by sub-devices.
 *
 */
struct vs_drm_private {
	struct device *dma_dev;
	struct iommu_domain *domain;
#ifdef CONFIG_VERISILICON_MMU
	dc_mmu * mmu;
#endif

	unsigned int pitch_alignment;
};

int vs_drm_iommu_attach_device(struct drm_device *drm_dev,
				   struct device *dev);

void vs_drm_iommu_detach_device(struct drm_device *drm_dev,
				struct device *dev);

void vs_drm_update_pitch_alignment(struct drm_device *drm_dev,
				   unsigned int alignment);

static inline struct device *to_dma_dev(struct drm_device *dev)
{
	struct vs_drm_private *priv = dev->dev_private;

	return priv->dma_dev;
}

static inline bool is_iommu_enabled(struct drm_device *dev)
{
	struct vs_drm_private *priv = dev->dev_private;

	return priv->domain != NULL ? true : false;
}

#ifdef CONFIG_STARFIVE_INNO_HDMI
extern struct platform_driver inno_hdmi_driver;
#endif

#endif /* __VS_DRV_H__ */
