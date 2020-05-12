// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************************
 * Copyright (c) 2007-2011, Intel Corporation.
 * All Rights Reserved.
 * Copyright (c) 2008, Tungsten Graphics, Inc. Cedar Park, TX., USA.
 * All Rights Reserved.
 *
 **************************************************************************/

#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>

#include <asm/set_memory.h>

#include <acpi/video.h>

#include <drm/drm.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_irq.h>
#include <drm/drm_pciids.h>
#include <drm/drm_vblank.h>

#include "framebuffer.h"
#include "intel_bios.h"
#include "mid_bios.h"
#include "power.h"
#include "psb_drv.h"
#include "psb_intel_reg.h"
#include "psb_reg.h"

static struct drm_driver driver;
static int psb_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent);

/*
 * The table below contains a mapping of the PCI vendor ID and the PCI Device ID
 * to the different groups of PowerVR 5-series chip designs
 *
 * 0x8086 = Intel Corporation
 *
 * PowerVR SGX535    - Poulsbo    - Intel GMA 500, Intel Atom Z5xx
 * PowerVR SGX535    - Moorestown - Intel GMA 600
 * PowerVR SGX535    - Oaktrail   - Intel GMA 600, Intel Atom Z6xx, E6xx
 * PowerVR SGX540    - Medfield   - Intel Atom Z2460
 * PowerVR SGX544MP2 - Medfield   -
 * PowerVR SGX545    - Cedartrail - Intel GMA 3600, Intel Atom D2500, N2600
 * PowerVR SGX545    - Cedartrail - Intel GMA 3650, Intel Atom D2550, D2700,
 *                                  N2800
 */
static const struct pci_device_id pciidlist[] = {
	{ 0x8086, 0x8108, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &psb_chip_ops },
	{ 0x8086, 0x8109, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &psb_chip_ops },
#if defined(CONFIG_DRM_GMA600)
	{ 0x8086, 0x4100, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &oaktrail_chip_ops },
	{ 0x8086, 0x4101, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &oaktrail_chip_ops },
	{ 0x8086, 0x4102, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &oaktrail_chip_ops },
	{ 0x8086, 0x4103, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &oaktrail_chip_ops },
	{ 0x8086, 0x4104, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &oaktrail_chip_ops },
	{ 0x8086, 0x4105, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &oaktrail_chip_ops },
	{ 0x8086, 0x4106, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &oaktrail_chip_ops },
	{ 0x8086, 0x4107, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &oaktrail_chip_ops },
	{ 0x8086, 0x4108, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &oaktrail_chip_ops },
#endif
#if defined(CONFIG_DRM_MEDFIELD)
	{ 0x8086, 0x0130, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &mdfld_chip_ops },
	{ 0x8086, 0x0131, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &mdfld_chip_ops },
	{ 0x8086, 0x0132, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &mdfld_chip_ops },
	{ 0x8086, 0x0133, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &mdfld_chip_ops },
	{ 0x8086, 0x0134, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &mdfld_chip_ops },
	{ 0x8086, 0x0135, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &mdfld_chip_ops },
	{ 0x8086, 0x0136, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &mdfld_chip_ops },
	{ 0x8086, 0x0137, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &mdfld_chip_ops },
#endif
#if defined(CONFIG_DRM_GMA3600)
	{ 0x8086, 0x0be0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0be1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0be2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0be3, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0be4, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0be5, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0be6, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0be7, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0be8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0be9, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0bea, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0beb, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0bec, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0bed, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0bee, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
	{ 0x8086, 0x0bef, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long) &cdv_chip_ops },
#endif
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pciidlist);

/*
 * Standard IOCTLs.
 */
static const struct drm_ioctl_desc psb_ioctls[] = {
};

static int psb_do_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_gtt *pg = &dev_priv->gtt;

	uint32_t stolen_gtt;

	if (pg->mmu_gatt_start & 0x0FFFFFFF) {
		dev_err(dev->dev, "Gatt must be 256M aligned. This is a bug.\n");
		return -EINVAL;
	}

	stolen_gtt = (pg->stolen_size >> PAGE_SHIFT) * 4;
	stolen_gtt = (stolen_gtt + PAGE_SIZE - 1) >> PAGE_SHIFT;
	stolen_gtt = (stolen_gtt < pg->gtt_pages) ? stolen_gtt : pg->gtt_pages;

	dev_priv->gatt_free_offset = pg->mmu_gatt_start +
	    (stolen_gtt << PAGE_SHIFT) * 1024;

	spin_lock_init(&dev_priv->irqmask_lock);
	spin_lock_init(&dev_priv->lock_2d);

	PSB_WSGX32(0x00000000, PSB_CR_BIF_BANK0);
	PSB_WSGX32(0x00000000, PSB_CR_BIF_BANK1);
	PSB_RSGX32(PSB_CR_BIF_BANK1);

	/* Do not bypass any MMU access, let them pagefault instead */
	PSB_WSGX32((PSB_RSGX32(PSB_CR_BIF_CTRL) & ~_PSB_MMU_ER_MASK),
		   PSB_CR_BIF_CTRL);
	PSB_RSGX32(PSB_CR_BIF_CTRL);

	psb_spank(dev_priv);

	/* mmu_gatt ?? */
	PSB_WSGX32(pg->gatt_start, PSB_CR_BIF_TWOD_REQ_BASE);
	PSB_RSGX32(PSB_CR_BIF_TWOD_REQ_BASE); /* Post */

	return 0;
}

static void psb_driver_unload(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	/* TODO: Kill vblank etc here */

	if (dev_priv) {
		if (dev_priv->backlight_device)
			gma_backlight_exit(dev);
		psb_modeset_cleanup(dev);

		if (dev_priv->ops->chip_teardown)
			dev_priv->ops->chip_teardown(dev);

		psb_intel_opregion_fini(dev);

		if (dev_priv->pf_pd) {
			psb_mmu_free_pagedir(dev_priv->pf_pd);
			dev_priv->pf_pd = NULL;
		}
		if (dev_priv->mmu) {
			struct psb_gtt *pg = &dev_priv->gtt;

			down_read(&pg->sem);
			psb_mmu_remove_pfn_sequence(
				psb_mmu_get_default_pd
				(dev_priv->mmu),
				pg->mmu_gatt_start,
				dev_priv->vram_stolen_size >> PAGE_SHIFT);
			up_read(&pg->sem);
			psb_mmu_driver_takedown(dev_priv->mmu);
			dev_priv->mmu = NULL;
		}
		psb_gtt_takedown(dev);
		if (dev_priv->scratch_page) {
			set_pages_wb(dev_priv->scratch_page, 1);
			__free_page(dev_priv->scratch_page);
			dev_priv->scratch_page = NULL;
		}
		if (dev_priv->vdc_reg) {
			iounmap(dev_priv->vdc_reg);
			dev_priv->vdc_reg = NULL;
		}
		if (dev_priv->sgx_reg) {
			iounmap(dev_priv->sgx_reg);
			dev_priv->sgx_reg = NULL;
		}
		if (dev_priv->aux_reg) {
			iounmap(dev_priv->aux_reg);
			dev_priv->aux_reg = NULL;
		}
		pci_dev_put(dev_priv->aux_pdev);
		pci_dev_put(dev_priv->lpc_pdev);

		/* Destroy VBT data */
		psb_intel_destroy_bios(dev);

		kfree(dev_priv);
		dev->dev_private = NULL;
	}
	gma_power_uninit(dev);
}

static int psb_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct drm_psb_private *dev_priv;
	unsigned long resource_start, resource_len;
	unsigned long irqflags;
	int ret = -ENOMEM;
	struct drm_connector *connector;
	struct gma_encoder *gma_encoder;
	struct psb_gtt *pg;

	/* allocating and initializing driver private data */
	dev_priv = kzalloc(sizeof(*dev_priv), GFP_KERNEL);
	if (dev_priv == NULL)
		return -ENOMEM;

	dev_priv->ops = (struct psb_ops *)flags;
	dev_priv->dev = dev;
	dev->dev_private = (void *) dev_priv;

	pg = &dev_priv->gtt;

	pci_set_master(dev->pdev);

	dev_priv->num_pipe = dev_priv->ops->pipes;

	resource_start = pci_resource_start(dev->pdev, PSB_MMIO_RESOURCE);

	dev_priv->vdc_reg =
	    ioremap(resource_start + PSB_VDC_OFFSET, PSB_VDC_SIZE);
	if (!dev_priv->vdc_reg)
		goto out_err;

	dev_priv->sgx_reg = ioremap(resource_start + dev_priv->ops->sgx_offset,
							PSB_SGX_SIZE);
	if (!dev_priv->sgx_reg)
		goto out_err;

	if (IS_MRST(dev)) {
		int domain = pci_domain_nr(dev->pdev->bus);

		dev_priv->aux_pdev =
			pci_get_domain_bus_and_slot(domain, 0,
						    PCI_DEVFN(3, 0));

		if (dev_priv->aux_pdev) {
			resource_start = pci_resource_start(dev_priv->aux_pdev,
							    PSB_AUX_RESOURCE);
			resource_len = pci_resource_len(dev_priv->aux_pdev,
							PSB_AUX_RESOURCE);
			dev_priv->aux_reg = ioremap(resource_start,
							    resource_len);
			if (!dev_priv->aux_reg)
				goto out_err;

			DRM_DEBUG_KMS("Found aux vdc");
		} else {
			/* Couldn't find the aux vdc so map to primary vdc */
			dev_priv->aux_reg = dev_priv->vdc_reg;
			DRM_DEBUG_KMS("Couldn't find aux pci device");
		}
		dev_priv->gmbus_reg = dev_priv->aux_reg;

		dev_priv->lpc_pdev =
			pci_get_domain_bus_and_slot(domain, 0,
						    PCI_DEVFN(31, 0));
		if (dev_priv->lpc_pdev) {
			pci_read_config_word(dev_priv->lpc_pdev, PSB_LPC_GBA,
				&dev_priv->lpc_gpio_base);
			pci_write_config_dword(dev_priv->lpc_pdev, PSB_LPC_GBA,
				(u32)dev_priv->lpc_gpio_base | (1L<<31));
			pci_read_config_word(dev_priv->lpc_pdev, PSB_LPC_GBA,
				&dev_priv->lpc_gpio_base);
			dev_priv->lpc_gpio_base &= 0xffc0;
			if (dev_priv->lpc_gpio_base)
				DRM_DEBUG_KMS("Found LPC GPIO at 0x%04x\n",
						dev_priv->lpc_gpio_base);
			else {
				pci_dev_put(dev_priv->lpc_pdev);
				dev_priv->lpc_pdev = NULL;
			}
		}
	} else {
		dev_priv->gmbus_reg = dev_priv->vdc_reg;
	}

	psb_intel_opregion_setup(dev);

	ret = dev_priv->ops->chip_setup(dev);
	if (ret)
		goto out_err;

	/* Init OSPM support */
	gma_power_init(dev);

	ret = -ENOMEM;

	dev_priv->scratch_page = alloc_page(GFP_DMA32 | __GFP_ZERO);
	if (!dev_priv->scratch_page)
		goto out_err;

	set_pages_uc(dev_priv->scratch_page, 1);

	ret = psb_gtt_init(dev, 0);
	if (ret)
		goto out_err;

	dev_priv->mmu = psb_mmu_driver_init(dev, 1, 0, 0);
	if (!dev_priv->mmu)
		goto out_err;

	dev_priv->pf_pd = psb_mmu_alloc_pd(dev_priv->mmu, 1, 0);
	if (!dev_priv->pf_pd)
		goto out_err;

	ret = psb_do_init(dev);
	if (ret)
		return ret;

	/* Add stolen memory to SGX MMU */
	down_read(&pg->sem);
	ret = psb_mmu_insert_pfn_sequence(psb_mmu_get_default_pd(dev_priv->mmu),
					  dev_priv->stolen_base >> PAGE_SHIFT,
					  pg->gatt_start,
					  pg->stolen_size >> PAGE_SHIFT, 0);
	up_read(&pg->sem);

	psb_mmu_set_pd_context(psb_mmu_get_default_pd(dev_priv->mmu), 0);
	psb_mmu_set_pd_context(dev_priv->pf_pd, 1);

	PSB_WSGX32(0x20000000, PSB_CR_PDS_EXEC_BASE);
	PSB_WSGX32(0x30000000, PSB_CR_BIF_3D_REQ_BASE);

	acpi_video_register();

	/* Setup vertical blanking handling */
	ret = drm_vblank_init(dev, dev_priv->num_pipe);
	if (ret)
		goto out_err;

	/*
	 * Install interrupt handlers prior to powering off SGX or else we will
	 * crash.
	 */
	dev_priv->vdc_irq_mask = 0;
	dev_priv->pipestat[0] = 0;
	dev_priv->pipestat[1] = 0;
	dev_priv->pipestat[2] = 0;
	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);
	PSB_WVDC32(0xFFFFFFFF, PSB_HWSTAM);
	PSB_WVDC32(0x00000000, PSB_INT_ENABLE_R);
	PSB_WVDC32(0xFFFFFFFF, PSB_INT_MASK_R);
	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);

	drm_irq_install(dev, dev->pdev->irq);

	dev->max_vblank_count = 0xffffff; /* only 24 bits of frame count */
	dev->driver->get_vblank_counter = psb_get_vblank_counter;

	psb_modeset_init(dev);
	psb_fbdev_init(dev);
	drm_kms_helper_poll_init(dev);

	/* Only add backlight support if we have LVDS output */
	list_for_each_entry(connector, &dev->mode_config.connector_list,
			    head) {
		gma_encoder = gma_attached_encoder(connector);

		switch (gma_encoder->type) {
		case INTEL_OUTPUT_LVDS:
		case INTEL_OUTPUT_MIPI:
			ret = gma_backlight_init(dev);
			break;
		}
	}

	if (ret)
		return ret;
	psb_intel_opregion_enable_asle(dev);
#if 0
	/* Enable runtime pm at last */
	pm_runtime_enable(&dev->pdev->dev);
	pm_runtime_set_active(&dev->pdev->dev);
#endif
	/* Intel drm driver load is done, continue doing pvr load */
	return 0;
out_err:
	psb_driver_unload(dev);
	return ret;
}

static inline void get_brightness(struct backlight_device *bd)
{
#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	if (bd) {
		bd->props.brightness = bd->ops->get_brightness(bd);
		backlight_update_status(bd);
	}
#endif
}

static long psb_unlocked_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	static unsigned int runtime_allowed;

	if (runtime_allowed == 1 && dev_priv->is_lvds_on) {
		runtime_allowed++;
		pm_runtime_allow(&dev->pdev->dev);
		dev_priv->rpm_enabled = 1;
	}
	return drm_ioctl(filp, cmd, arg);
	/* FIXME: do we need to wrap the other side of this */
}

static int psb_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct drm_device *dev;
	int ret;

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	dev = drm_dev_alloc(&driver, &pdev->dev);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_pci_disable_device;
	}

	dev->pdev = pdev;
	pci_set_drvdata(pdev, dev);

	ret = psb_driver_load(dev, ent->driver_data);
	if (ret)
		goto err_drm_dev_put;

	ret = drm_dev_register(dev, ent->driver_data);
	if (ret)
		goto err_psb_driver_unload;

	return 0;

err_psb_driver_unload:
	psb_driver_unload(dev);
err_drm_dev_put:
	drm_dev_put(dev);
err_pci_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void psb_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_dev_unregister(dev);
	psb_driver_unload(dev);
	drm_dev_put(dev);
}

static const struct dev_pm_ops psb_pm_ops = {
	.resume = gma_power_resume,
	.suspend = gma_power_suspend,
	.thaw = gma_power_thaw,
	.freeze = gma_power_freeze,
	.restore = gma_power_restore,
	.runtime_suspend = psb_runtime_suspend,
	.runtime_resume = psb_runtime_resume,
	.runtime_idle = psb_runtime_idle,
};

static const struct vm_operations_struct psb_gem_vm_ops = {
	.fault = psb_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct file_operations psb_gem_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = psb_unlocked_ioctl,
	.compat_ioctl = drm_compat_ioctl,
	.mmap = drm_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
};

static struct drm_driver driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM,
	.lastclose = drm_fb_helper_lastclose,

	.num_ioctls = ARRAY_SIZE(psb_ioctls),
	.irq_preinstall = psb_irq_preinstall,
	.irq_postinstall = psb_irq_postinstall,
	.irq_uninstall = psb_irq_uninstall,
	.irq_handler = psb_irq_handler,
	.enable_vblank = psb_enable_vblank,
	.disable_vblank = psb_disable_vblank,
	.get_vblank_counter = psb_get_vblank_counter,

	.gem_free_object = psb_gem_free_object,
	.gem_vm_ops = &psb_gem_vm_ops,

	.dumb_create = psb_gem_dumb_create,
	.ioctls = psb_ioctls,
	.fops = &psb_gem_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL
};

static struct pci_driver psb_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = psb_pci_probe,
	.remove = psb_pci_remove,
	.driver.pm = &psb_pm_ops,
};

static int __init psb_init(void)
{
	return pci_register_driver(&psb_pci_driver);
}

static void __exit psb_exit(void)
{
	pci_unregister_driver(&psb_pci_driver);
}

late_initcall(psb_init);
module_exit(psb_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
