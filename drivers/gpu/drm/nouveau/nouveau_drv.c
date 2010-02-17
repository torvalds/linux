/*
 * Copyright 2005 Stephane Marchesin.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/console.h>

#include "drmP.h"
#include "drm.h"
#include "drm_crtc_helper.h"
#include "nouveau_drv.h"
#include "nouveau_hw.h"
#include "nouveau_fb.h"
#include "nouveau_fbcon.h"
#include "nv50_display.h"

#include "drm_pciids.h"

MODULE_PARM_DESC(ctxfw, "Use external firmware blob for grctx init (NV40)");
int nouveau_ctxfw = 0;
module_param_named(ctxfw, nouveau_ctxfw, int, 0400);

MODULE_PARM_DESC(noagp, "Disable AGP");
int nouveau_noagp;
module_param_named(noagp, nouveau_noagp, int, 0400);

MODULE_PARM_DESC(modeset, "Enable kernel modesetting");
static int nouveau_modeset = -1; /* kms */
module_param_named(modeset, nouveau_modeset, int, 0400);

MODULE_PARM_DESC(vbios, "Override default VBIOS location");
char *nouveau_vbios;
module_param_named(vbios, nouveau_vbios, charp, 0400);

MODULE_PARM_DESC(vram_pushbuf, "Force DMA push buffers to be in VRAM");
int nouveau_vram_pushbuf;
module_param_named(vram_pushbuf, nouveau_vram_pushbuf, int, 0400);

MODULE_PARM_DESC(vram_notify, "Force DMA notifiers to be in VRAM");
int nouveau_vram_notify = 1;
module_param_named(vram_notify, nouveau_vram_notify, int, 0400);

MODULE_PARM_DESC(duallink, "Allow dual-link TMDS (>=GeForce 8)");
int nouveau_duallink = 1;
module_param_named(duallink, nouveau_duallink, int, 0400);

MODULE_PARM_DESC(uscript_lvds, "LVDS output script table ID (>=GeForce 8)");
int nouveau_uscript_lvds = -1;
module_param_named(uscript_lvds, nouveau_uscript_lvds, int, 0400);

MODULE_PARM_DESC(uscript_tmds, "TMDS output script table ID (>=GeForce 8)");
int nouveau_uscript_tmds = -1;
module_param_named(uscript_tmds, nouveau_uscript_tmds, int, 0400);

MODULE_PARM_DESC(ignorelid, "Ignore ACPI lid status");
int nouveau_ignorelid = 0;
module_param_named(ignorelid, nouveau_ignorelid, int, 0400);

MODULE_PARM_DESC(noagp, "Disable all acceleration");
int nouveau_noaccel = 0;
module_param_named(noaccel, nouveau_noaccel, int, 0400);

MODULE_PARM_DESC(noagp, "Disable fbcon acceleration");
int nouveau_nofbaccel = 0;
module_param_named(nofbaccel, nouveau_nofbaccel, int, 0400);

MODULE_PARM_DESC(tv_norm, "Default TV norm.\n"
		 "\t\tSupported: PAL, PAL-M, PAL-N, PAL-Nc, NTSC-M, NTSC-J,\n"
		 "\t\t\thd480i, hd480p, hd576i, hd576p, hd720p, hd1080i.\n"
		 "\t\tDefault: PAL\n"
		 "\t\t*NOTE* Ignored for cards with external TV encoders.");
char *nouveau_tv_norm;
module_param_named(tv_norm, nouveau_tv_norm, charp, 0400);

MODULE_PARM_DESC(reg_debug, "Register access debug bitmask:\n"
		"\t\t0x1 mc, 0x2 video, 0x4 fb, 0x8 extdev,\n"
		"\t\t0x10 crtc, 0x20 ramdac, 0x40 vgacrtc, 0x80 rmvio,\n"
		"\t\t0x100 vgaattr, 0x200 EVO (G80+). ");
int nouveau_reg_debug;
module_param_named(reg_debug, nouveau_reg_debug, int, 0600);

int nouveau_fbpercrtc;
#if 0
module_param_named(fbpercrtc, nouveau_fbpercrtc, int, 0400);
#endif

static struct pci_device_id pciidlist[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID),
		.class = PCI_BASE_CLASS_DISPLAY << 16,
		.class_mask  = 0xff << 16,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA_SGS, PCI_ANY_ID),
		.class = PCI_BASE_CLASS_DISPLAY << 16,
		.class_mask  = 0xff << 16,
	},
	{}
};

MODULE_DEVICE_TABLE(pci, pciidlist);

static struct drm_driver driver;

static int __devinit
nouveau_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	return drm_get_dev(pdev, ent, &driver);
}

static void
nouveau_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static int
nouveau_pci_suspend(struct pci_dev *pdev, pm_message_t pm_state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_channel *chan;
	struct drm_crtc *crtc;
	uint32_t fbdev_flags;
	int ret, i;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	if (pm_state.event == PM_EVENT_PRETHAW)
		return 0;

	fbdev_flags = dev_priv->fbdev_info->flags;
	dev_priv->fbdev_info->flags |= FBINFO_HWACCEL_DISABLED;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_framebuffer *nouveau_fb;

		nouveau_fb = nouveau_framebuffer(crtc->fb);
		if (!nouveau_fb || !nouveau_fb->nvbo)
			continue;

		nouveau_bo_unpin(nouveau_fb->nvbo);
	}

	NV_INFO(dev, "Evicting buffers...\n");
	ttm_bo_evict_mm(&dev_priv->ttm.bdev, TTM_PL_VRAM);

	NV_INFO(dev, "Idling channels...\n");
	for (i = 0; i < pfifo->channels; i++) {
		struct nouveau_fence *fence = NULL;

		chan = dev_priv->fifos[i];
		if (!chan || (dev_priv->card_type >= NV_50 &&
			      chan == dev_priv->fifos[0]))
			continue;

		ret = nouveau_fence_new(chan, &fence, true);
		if (ret == 0) {
			ret = nouveau_fence_wait(fence, NULL, false, false);
			nouveau_fence_unref((void *)&fence);
		}

		if (ret) {
			NV_ERROR(dev, "Failed to idle channel %d for suspend\n",
				 chan->id);
		}
	}

	pgraph->fifo_access(dev, false);
	nouveau_wait_for_idle(dev);
	pfifo->reassign(dev, false);
	pfifo->disable(dev);
	pfifo->unload_context(dev);
	pgraph->unload_context(dev);

	NV_INFO(dev, "Suspending GPU objects...\n");
	ret = nouveau_gpuobj_suspend(dev);
	if (ret) {
		NV_ERROR(dev, "... failed: %d\n", ret);
		goto out_abort;
	}

	ret = pinstmem->suspend(dev);
	if (ret) {
		NV_ERROR(dev, "... failed: %d\n", ret);
		nouveau_gpuobj_suspend_cleanup(dev);
		goto out_abort;
	}

	NV_INFO(dev, "And we're gone!\n");
	pci_save_state(pdev);
	if (pm_state.event == PM_EVENT_SUSPEND) {
		pci_disable_device(pdev);
		pci_set_power_state(pdev, PCI_D3hot);
	}

	acquire_console_sem();
	fb_set_suspend(dev_priv->fbdev_info, 1);
	release_console_sem();
	dev_priv->fbdev_info->flags = fbdev_flags;
	return 0;

out_abort:
	NV_INFO(dev, "Re-enabling acceleration..\n");
	pfifo->enable(dev);
	pfifo->reassign(dev, true);
	pgraph->fifo_access(dev, true);
	return ret;
}

static int
nouveau_pci_resume(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	struct drm_crtc *crtc;
	uint32_t fbdev_flags;
	int ret, i;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	fbdev_flags = dev_priv->fbdev_info->flags;
	dev_priv->fbdev_info->flags |= FBINFO_HWACCEL_DISABLED;

	NV_INFO(dev, "We're back, enabling device...\n");
	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	if (pci_enable_device(pdev))
		return -1;
	pci_set_master(dev->pdev);

	NV_INFO(dev, "POSTing device...\n");
	ret = nouveau_run_vbios_init(dev);
	if (ret)
		return ret;

	if (dev_priv->gart_info.type == NOUVEAU_GART_AGP) {
		ret = nouveau_mem_init_agp(dev);
		if (ret) {
			NV_ERROR(dev, "error reinitialising AGP: %d\n", ret);
			return ret;
		}
	}

	NV_INFO(dev, "Reinitialising engines...\n");
	engine->instmem.resume(dev);
	engine->mc.init(dev);
	engine->timer.init(dev);
	engine->fb.init(dev);
	engine->graph.init(dev);
	engine->fifo.init(dev);

	NV_INFO(dev, "Restoring GPU objects...\n");
	nouveau_gpuobj_resume(dev);

	nouveau_irq_postinstall(dev);

	/* Re-write SKIPS, they'll have been lost over the suspend */
	if (nouveau_vram_pushbuf) {
		struct nouveau_channel *chan;
		int j;

		for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
			chan = dev_priv->fifos[i];
			if (!chan || !chan->pushbuf_bo)
				continue;

			for (j = 0; j < NOUVEAU_DMA_SKIPS; j++)
				nouveau_bo_wr32(chan->pushbuf_bo, i, 0);
		}
	}

	NV_INFO(dev, "Restoring mode...\n");
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_framebuffer *nouveau_fb;

		nouveau_fb = nouveau_framebuffer(crtc->fb);
		if (!nouveau_fb || !nouveau_fb->nvbo)
			continue;

		nouveau_bo_pin(nouveau_fb->nvbo, TTM_PL_FLAG_VRAM);
	}

	if (dev_priv->card_type < NV_50) {
		nv04_display_restore(dev);
		NVLockVgaCrtcs(dev, false);
	} else
		nv50_display_init(dev);

	/* Force CLUT to get re-loaded during modeset */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

		nv_crtc->lut.depth = 0;
	}

	acquire_console_sem();
	fb_set_suspend(dev_priv->fbdev_info, 0);
	release_console_sem();

	nouveau_fbcon_zfill(dev);

	drm_helper_resume_force_mode(dev);
	dev_priv->fbdev_info->flags = fbdev_flags;
	return 0;
}

static struct drm_driver driver = {
	.driver_features =
		DRIVER_USE_AGP | DRIVER_PCI_DMA | DRIVER_SG |
		DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | DRIVER_GEM,
	.load = nouveau_load,
	.firstopen = nouveau_firstopen,
	.lastclose = nouveau_lastclose,
	.unload = nouveau_unload,
	.preclose = nouveau_preclose,
#if defined(CONFIG_DRM_NOUVEAU_DEBUG)
	.debugfs_init = nouveau_debugfs_init,
	.debugfs_cleanup = nouveau_debugfs_takedown,
#endif
	.irq_preinstall = nouveau_irq_preinstall,
	.irq_postinstall = nouveau_irq_postinstall,
	.irq_uninstall = nouveau_irq_uninstall,
	.irq_handler = nouveau_irq_handler,
	.reclaim_buffers = drm_core_reclaim_buffers,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.ioctls = nouveau_ioctls,
	.fops = {
		.owner = THIS_MODULE,
		.open = drm_open,
		.release = drm_release,
		.unlocked_ioctl = drm_ioctl,
		.mmap = nouveau_ttm_mmap,
		.poll = drm_poll,
		.fasync = drm_fasync,
#if defined(CONFIG_COMPAT)
		.compat_ioctl = nouveau_compat_ioctl,
#endif
	},
	.pci_driver = {
		.name = DRIVER_NAME,
		.id_table = pciidlist,
		.probe = nouveau_pci_probe,
		.remove = nouveau_pci_remove,
		.suspend = nouveau_pci_suspend,
		.resume = nouveau_pci_resume
	},

	.gem_init_object = nouveau_gem_object_new,
	.gem_free_object = nouveau_gem_object_del,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
#ifdef GIT_REVISION
	.date = GIT_REVISION,
#else
	.date = DRIVER_DATE,
#endif
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int __init nouveau_init(void)
{
	driver.num_ioctls = nouveau_max_ioctl;

	if (nouveau_modeset == -1) {
#ifdef CONFIG_VGA_CONSOLE
		if (vgacon_text_force())
			nouveau_modeset = 0;
		else
#endif
			nouveau_modeset = 1;
	}

	if (nouveau_modeset == 1)
		driver.driver_features |= DRIVER_MODESET;

	return drm_init(&driver);
}

static void __exit nouveau_exit(void)
{
	drm_exit(&driver);
}

module_init(nouveau_init);
module_exit(nouveau_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
