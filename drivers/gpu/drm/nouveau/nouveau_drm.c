/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include <linux/console.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <core/device.h>
#include <core/client.h>
#include <core/gpuobj.h>
#include <core/class.h>

#include <subdev/device.h>
#include <subdev/vm.h>

#include "nouveau_drm.h"
#include "nouveau_irq.h"
#include "nouveau_dma.h"
#include "nouveau_ttm.h"
#include "nouveau_gem.h"
#include "nouveau_agp.h"
#include "nouveau_vga.h"
#include "nouveau_pm.h"
#include "nouveau_acpi.h"
#include "nouveau_bios.h"
#include "nouveau_ioctl.h"
#include "nouveau_abi16.h"
#include "nouveau_fbcon.h"
#include "nouveau_fence.h"

MODULE_PARM_DESC(config, "option string to pass to driver core");
static char *nouveau_config;
module_param_named(config, nouveau_config, charp, 0400);

MODULE_PARM_DESC(debug, "debug string to pass to driver core");
static char *nouveau_debug;
module_param_named(debug, nouveau_debug, charp, 0400);

MODULE_PARM_DESC(noaccel, "disable kernel/abi16 acceleration");
static int nouveau_noaccel = 0;
module_param_named(noaccel, nouveau_noaccel, int, 0400);

MODULE_PARM_DESC(modeset, "enable driver (default: auto, "
		          "0 = disabled, 1 = enabled, 2 = headless)");
int nouveau_modeset = -1;
module_param_named(modeset, nouveau_modeset, int, 0400);

static struct drm_driver driver;

static u64
nouveau_name(struct pci_dev *pdev)
{
	u64 name = (u64)pci_domain_nr(pdev->bus) << 32;
	name |= pdev->bus->number << 16;
	name |= PCI_SLOT(pdev->devfn) << 8;
	return name | PCI_FUNC(pdev->devfn);
}

static int
nouveau_cli_create(struct pci_dev *pdev, const char *name,
		   int size, void **pcli)
{
	struct nouveau_cli *cli;
	int ret;

	*pcli = NULL;
	ret = nouveau_client_create_(name, nouveau_name(pdev), nouveau_config,
				     nouveau_debug, size, pcli);
	cli = *pcli;
	if (ret) {
		if (cli)
			nouveau_client_destroy(&cli->base);
		*pcli = NULL;
		return ret;
	}

	mutex_init(&cli->mutex);
	return 0;
}

static void
nouveau_cli_destroy(struct nouveau_cli *cli)
{
	struct nouveau_object *client = nv_object(cli);
	nouveau_vm_ref(NULL, &cli->base.vm, NULL);
	nouveau_client_fini(&cli->base, false);
	atomic_set(&client->refcount, 1);
	nouveau_object_ref(NULL, &client);
}

static void
nouveau_accel_fini(struct nouveau_drm *drm)
{
	nouveau_gpuobj_ref(NULL, &drm->notify);
	nouveau_channel_del(&drm->channel);
	nouveau_channel_del(&drm->cechan);
	if (drm->fence)
		nouveau_fence(drm)->dtor(drm);
}

static void
nouveau_accel_init(struct nouveau_drm *drm)
{
	struct nouveau_device *device = nv_device(drm->device);
	struct nouveau_object *object;
	u32 arg0, arg1;
	int ret;

	if (nouveau_noaccel)
		return;

	/* initialise synchronisation routines */
	if      (device->card_type < NV_10) ret = nv04_fence_create(drm);
	else if (device->card_type < NV_50) ret = nv10_fence_create(drm);
	else if (device->chipset   <  0x84) ret = nv50_fence_create(drm);
	else if (device->card_type < NV_C0) ret = nv84_fence_create(drm);
	else                                ret = nvc0_fence_create(drm);
	if (ret) {
		NV_ERROR(drm, "failed to initialise sync subsystem, %d\n", ret);
		nouveau_accel_fini(drm);
		return;
	}

	if (device->card_type >= NV_E0) {
		ret = nouveau_channel_new(drm, &drm->client, NVDRM_DEVICE,
					  NVDRM_CHAN + 1,
					  NVE0_CHANNEL_IND_ENGINE_CE0 |
					  NVE0_CHANNEL_IND_ENGINE_CE1, 0,
					  &drm->cechan);
		if (ret)
			NV_ERROR(drm, "failed to create ce channel, %d\n", ret);

		arg0 = NVE0_CHANNEL_IND_ENGINE_GR;
		arg1 = 1;
	} else {
		arg0 = NvDmaFB;
		arg1 = NvDmaTT;
	}

	ret = nouveau_channel_new(drm, &drm->client, NVDRM_DEVICE, NVDRM_CHAN,
				  arg0, arg1, &drm->channel);
	if (ret) {
		NV_ERROR(drm, "failed to create kernel channel, %d\n", ret);
		nouveau_accel_fini(drm);
		return;
	}

	if (device->card_type < NV_C0) {
		ret = nouveau_gpuobj_new(drm->device, NULL, 32, 0, 0,
					&drm->notify);
		if (ret) {
			NV_ERROR(drm, "failed to allocate notifier, %d\n", ret);
			nouveau_accel_fini(drm);
			return;
		}

		ret = nouveau_object_new(nv_object(drm),
					 drm->channel->handle, NvNotify0,
					 0x003d, &(struct nv_dma_class) {
						.flags = NV_DMA_TARGET_VRAM |
							 NV_DMA_ACCESS_RDWR,
						.start = drm->notify->addr,
						.limit = drm->notify->addr + 31
						}, sizeof(struct nv_dma_class),
					 &object);
		if (ret) {
			nouveau_accel_fini(drm);
			return;
		}
	}


	nouveau_bo_move_init(drm);
}

static int nouveau_drm_probe(struct pci_dev *pdev,
			     const struct pci_device_id *pent)
{
	struct nouveau_device *device;
	struct apertures_struct *aper;
	bool boot = false;
	int ret;

	/* remove conflicting drivers (vesafb, efifb etc) */
	aper = alloc_apertures(3);
	if (!aper)
		return -ENOMEM;

	aper->ranges[0].base = pci_resource_start(pdev, 1);
	aper->ranges[0].size = pci_resource_len(pdev, 1);
	aper->count = 1;

	if (pci_resource_len(pdev, 2)) {
		aper->ranges[aper->count].base = pci_resource_start(pdev, 2);
		aper->ranges[aper->count].size = pci_resource_len(pdev, 2);
		aper->count++;
	}

	if (pci_resource_len(pdev, 3)) {
		aper->ranges[aper->count].base = pci_resource_start(pdev, 3);
		aper->ranges[aper->count].size = pci_resource_len(pdev, 3);
		aper->count++;
	}

#ifdef CONFIG_X86
	boot = pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW;
#endif
	remove_conflicting_framebuffers(aper, "nouveaufb", boot);
	kfree(aper);

	ret = nouveau_device_create(pdev, nouveau_name(pdev), pci_name(pdev),
				    nouveau_config, nouveau_debug, &device);
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = drm_get_pci_dev(pdev, pent, &driver);
	if (ret) {
		nouveau_object_ref(NULL, (struct nouveau_object **)&device);
		return ret;
	}

	return 0;
}

static int
nouveau_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct pci_dev *pdev = dev->pdev;
	struct nouveau_device *device;
	struct nouveau_drm *drm;
	int ret;

	ret = nouveau_cli_create(pdev, "DRM", sizeof(*drm), (void**)&drm);
	if (ret)
		return ret;

	dev->dev_private = drm;
	drm->dev = dev;

	INIT_LIST_HEAD(&drm->clients);
	spin_lock_init(&drm->tile.lock);

	/* make sure AGP controller is in a consistent state before we
	 * (possibly) execute vbios init tables (see nouveau_agp.h)
	 */
	if (drm_pci_device_is_agp(dev) && dev->agp) {
		/* dummy device object, doesn't init anything, but allows
		 * agp code access to registers
		 */
		ret = nouveau_object_new(nv_object(drm), NVDRM_CLIENT,
					 NVDRM_DEVICE, 0x0080,
					 &(struct nv_device_class) {
						.device = ~0,
						.disable =
						 ~(NV_DEVICE_DISABLE_MMIO |
						   NV_DEVICE_DISABLE_IDENTIFY),
						.debug0 = ~0,
					 }, sizeof(struct nv_device_class),
					 &drm->device);
		if (ret)
			goto fail_device;

		nouveau_agp_reset(drm);
		nouveau_object_del(nv_object(drm), NVDRM_CLIENT, NVDRM_DEVICE);
	}

	ret = nouveau_object_new(nv_object(drm), NVDRM_CLIENT, NVDRM_DEVICE,
				 0x0080, &(struct nv_device_class) {
					.device = ~0,
					.disable = 0,
					.debug0 = 0,
				 }, sizeof(struct nv_device_class),
				 &drm->device);
	if (ret)
		goto fail_device;

	/* workaround an odd issue on nvc1 by disabling the device's
	 * nosnoop capability.  hopefully won't cause issues until a
	 * better fix is found - assuming there is one...
	 */
	device = nv_device(drm->device);
	if (nv_device(drm->device)->chipset == 0xc1)
		nv_mask(device, 0x00088080, 0x00000800, 0x00000000);

	nouveau_vga_init(drm);
	nouveau_agp_init(drm);

	if (device->card_type >= NV_50) {
		ret = nouveau_vm_new(nv_device(drm->device), 0, (1ULL << 40),
				     0x1000, &drm->client.base.vm);
		if (ret)
			goto fail_device;
	}

	ret = nouveau_ttm_init(drm);
	if (ret)
		goto fail_ttm;

	ret = nouveau_bios_init(dev);
	if (ret)
		goto fail_bios;

	ret = nouveau_irq_init(dev);
	if (ret)
		goto fail_irq;

	ret = nouveau_display_create(dev);
	if (ret)
		goto fail_dispctor;

	if (dev->mode_config.num_crtc) {
		ret = nouveau_display_init(dev);
		if (ret)
			goto fail_dispinit;
	}

	nouveau_pm_init(dev);

	nouveau_accel_init(drm);
	nouveau_fbcon_init(dev);
	return 0;

fail_dispinit:
	nouveau_display_destroy(dev);
fail_dispctor:
	nouveau_irq_fini(dev);
fail_irq:
	nouveau_bios_takedown(dev);
fail_bios:
	nouveau_ttm_fini(drm);
fail_ttm:
	nouveau_agp_fini(drm);
	nouveau_vga_fini(drm);
fail_device:
	nouveau_cli_destroy(&drm->client);
	return ret;
}

static int
nouveau_drm_unload(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	nouveau_fbcon_fini(dev);
	nouveau_accel_fini(drm);

	nouveau_pm_fini(dev);

	if (dev->mode_config.num_crtc)
		nouveau_display_fini(dev);
	nouveau_display_destroy(dev);

	nouveau_irq_fini(dev);
	nouveau_bios_takedown(dev);

	nouveau_ttm_fini(drm);
	nouveau_agp_fini(drm);
	nouveau_vga_fini(drm);

	nouveau_cli_destroy(&drm->client);
	return 0;
}

static void
nouveau_drm_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_object *device;

	device = drm->client.base.device;
	drm_put_dev(dev);

	nouveau_object_ref(NULL, &device);
	nouveau_object_debug();
}

int
nouveau_do_suspend(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_cli *cli;
	int ret;

	if (dev->mode_config.num_crtc) {
		NV_INFO(drm, "suspending fbcon...\n");
		nouveau_fbcon_set_suspend(dev, 1);

		NV_INFO(drm, "suspending display...\n");
		ret = nouveau_display_suspend(dev);
		if (ret)
			return ret;
	}

	NV_INFO(drm, "evicting buffers...\n");
	ttm_bo_evict_mm(&drm->ttm.bdev, TTM_PL_VRAM);

	if (drm->fence && nouveau_fence(drm)->suspend) {
		if (!nouveau_fence(drm)->suspend(drm))
			return -ENOMEM;
	}

	NV_INFO(drm, "suspending client object trees...\n");
	list_for_each_entry(cli, &drm->clients, head) {
		ret = nouveau_client_fini(&cli->base, true);
		if (ret)
			goto fail_client;
	}

	ret = nouveau_client_fini(&drm->client.base, true);
	if (ret)
		goto fail_client;

	nouveau_agp_fini(drm);
	return 0;

fail_client:
	list_for_each_entry_continue_reverse(cli, &drm->clients, head) {
		nouveau_client_init(&cli->base);
	}

	if (dev->mode_config.num_crtc) {
		NV_INFO(drm, "resuming display...\n");
		nouveau_display_resume(dev);
	}
	return ret;
}

int nouveau_pmops_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int ret;

	if (drm_dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	ret = nouveau_do_suspend(drm_dev);
	if (ret)
		return ret;

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

int
nouveau_do_resume(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_cli *cli;

	NV_INFO(drm, "re-enabling device...\n");

	nouveau_agp_reset(drm);

	NV_INFO(drm, "resuming client object trees...\n");
	nouveau_client_init(&drm->client.base);
	nouveau_agp_init(drm);

	list_for_each_entry(cli, &drm->clients, head) {
		nouveau_client_init(&cli->base);
	}

	if (drm->fence && nouveau_fence(drm)->resume)
		nouveau_fence(drm)->resume(drm);

	nouveau_run_vbios_init(dev);
	nouveau_irq_postinstall(dev);
	nouveau_pm_resume(dev);

	if (dev->mode_config.num_crtc) {
		NV_INFO(drm, "resuming display...\n");
		nouveau_display_resume(dev);
	}
	return 0;
}

int nouveau_pmops_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int ret;

	if (drm_dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	pci_set_master(pdev);

	return nouveau_do_resume(drm_dev);
}

static int nouveau_pmops_freeze(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	return nouveau_do_suspend(drm_dev);
}

static int nouveau_pmops_thaw(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	return nouveau_do_resume(drm_dev);
}


static int
nouveau_drm_open(struct drm_device *dev, struct drm_file *fpriv)
{
	struct pci_dev *pdev = dev->pdev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_cli *cli;
	char name[16];
	int ret;

	snprintf(name, sizeof(name), "%d", pid_nr(fpriv->pid));

	ret = nouveau_cli_create(pdev, name, sizeof(*cli), (void **)&cli);
	if (ret)
		return ret;

	if (nv_device(drm->device)->card_type >= NV_50) {
		ret = nouveau_vm_new(nv_device(drm->device), 0, (1ULL << 40),
				     0x1000, &cli->base.vm);
		if (ret) {
			nouveau_cli_destroy(cli);
			return ret;
		}
	}

	fpriv->driver_priv = cli;

	mutex_lock(&drm->client.mutex);
	list_add(&cli->head, &drm->clients);
	mutex_unlock(&drm->client.mutex);
	return 0;
}

static void
nouveau_drm_preclose(struct drm_device *dev, struct drm_file *fpriv)
{
	struct nouveau_cli *cli = nouveau_cli(fpriv);
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (cli->abi16)
		nouveau_abi16_fini(cli->abi16);

	mutex_lock(&drm->client.mutex);
	list_del(&cli->head);
	mutex_unlock(&drm->client.mutex);
}

static void
nouveau_drm_postclose(struct drm_device *dev, struct drm_file *fpriv)
{
	struct nouveau_cli *cli = nouveau_cli(fpriv);
	nouveau_cli_destroy(cli);
}

static struct drm_ioctl_desc
nouveau_ioctls[] = {
	DRM_IOCTL_DEF_DRV(NOUVEAU_GETPARAM, nouveau_abi16_ioctl_getparam, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_SETPARAM, nouveau_abi16_ioctl_setparam, DRM_UNLOCKED|DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_ALLOC, nouveau_abi16_ioctl_channel_alloc, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_FREE, nouveau_abi16_ioctl_channel_free, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GROBJ_ALLOC, nouveau_abi16_ioctl_grobj_alloc, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_NOTIFIEROBJ_ALLOC, nouveau_abi16_ioctl_notifierobj_alloc, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GPUOBJ_FREE, nouveau_abi16_ioctl_gpuobj_free, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_NEW, nouveau_gem_ioctl_new, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_PUSHBUF, nouveau_gem_ioctl_pushbuf, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_PREP, nouveau_gem_ioctl_cpu_prep, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_FINI, nouveau_gem_ioctl_cpu_fini, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_INFO, nouveau_gem_ioctl_info, DRM_UNLOCKED|DRM_AUTH),
};

static const struct file_operations
nouveau_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = nouveau_ttm_mmap,
	.poll = drm_poll,
	.fasync = drm_fasync,
	.read = drm_read,
#if defined(CONFIG_COMPAT)
	.compat_ioctl = nouveau_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct drm_driver
driver = {
	.driver_features =
		DRIVER_USE_AGP | DRIVER_PCI_DMA | DRIVER_SG |
		DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | DRIVER_GEM |
		DRIVER_MODESET | DRIVER_PRIME,

	.load = nouveau_drm_load,
	.unload = nouveau_drm_unload,
	.open = nouveau_drm_open,
	.preclose = nouveau_drm_preclose,
	.postclose = nouveau_drm_postclose,
	.lastclose = nouveau_vga_lastclose,

	.irq_preinstall = nouveau_irq_preinstall,
	.irq_postinstall = nouveau_irq_postinstall,
	.irq_uninstall = nouveau_irq_uninstall,
	.irq_handler = nouveau_irq_handler,

	.get_vblank_counter = drm_vblank_count,
	.enable_vblank = nouveau_vblank_enable,
	.disable_vblank = nouveau_vblank_disable,

	.ioctls = nouveau_ioctls,
	.fops = &nouveau_driver_fops,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_pin = nouveau_gem_prime_pin,
	.gem_prime_get_sg_table = nouveau_gem_prime_get_sg_table,
	.gem_prime_import_sg_table = nouveau_gem_prime_import_sg_table,
	.gem_prime_vmap = nouveau_gem_prime_vmap,
	.gem_prime_vunmap = nouveau_gem_prime_vunmap,

	.gem_init_object = nouveau_gem_object_new,
	.gem_free_object = nouveau_gem_object_del,
	.gem_open_object = nouveau_gem_object_open,
	.gem_close_object = nouveau_gem_object_close,

	.dumb_create = nouveau_display_dumb_create,
	.dumb_map_offset = nouveau_display_dumb_map_offset,
	.dumb_destroy = nouveau_display_dumb_destroy,

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

static struct pci_device_id
nouveau_drm_pci_table[] = {
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

static const struct dev_pm_ops nouveau_pm_ops = {
	.suspend = nouveau_pmops_suspend,
	.resume = nouveau_pmops_resume,
	.freeze = nouveau_pmops_freeze,
	.thaw = nouveau_pmops_thaw,
	.poweroff = nouveau_pmops_freeze,
	.restore = nouveau_pmops_resume,
};

static struct pci_driver
nouveau_drm_pci_driver = {
	.name = "nouveau",
	.id_table = nouveau_drm_pci_table,
	.probe = nouveau_drm_probe,
	.remove = nouveau_drm_remove,
	.driver.pm = &nouveau_pm_ops,
};

static int __init
nouveau_drm_init(void)
{
	driver.num_ioctls = ARRAY_SIZE(nouveau_ioctls);

	if (nouveau_modeset == -1) {
#ifdef CONFIG_VGA_CONSOLE
		if (vgacon_text_force())
			nouveau_modeset = 0;
#endif
	}

	if (!nouveau_modeset)
		return 0;

	nouveau_register_dsm_handler();
	return drm_pci_init(&driver, &nouveau_drm_pci_driver);
}

static void __exit
nouveau_drm_exit(void)
{
	if (!nouveau_modeset)
		return;

	drm_pci_exit(&driver, &nouveau_drm_pci_driver);
	nouveau_unregister_dsm_handler();
}

module_init(nouveau_drm_init);
module_exit(nouveau_drm_exit);

MODULE_DEVICE_TABLE(pci, nouveau_drm_pci_table);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
