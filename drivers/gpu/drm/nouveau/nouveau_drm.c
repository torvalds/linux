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
#include <linux/pm_runtime.h>
#include <linux/vga_switcheroo.h>

#include "drmP.h"
#include "drm_crtc_helper.h"

#include <core/device.h>
#include <core/gpuobj.h>
#include <core/option.h>

#include "nouveau_drm.h"
#include "nouveau_dma.h"
#include "nouveau_ttm.h"
#include "nouveau_gem.h"
#include "nouveau_agp.h"
#include "nouveau_vga.h"
#include "nouveau_sysfs.h"
#include "nouveau_hwmon.h"
#include "nouveau_acpi.h"
#include "nouveau_bios.h"
#include "nouveau_ioctl.h"
#include "nouveau_abi16.h"
#include "nouveau_fbcon.h"
#include "nouveau_fence.h"
#include "nouveau_debugfs.h"
#include "nouveau_usif.h"
#include "nouveau_connector.h"

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

MODULE_PARM_DESC(runpm, "disable (0), force enable (1), optimus only default (-1)");
int nouveau_runtime_pm = -1;
module_param_named(runpm, nouveau_runtime_pm, int, 0400);

static struct drm_driver driver_stub;
static struct drm_driver driver_pci;
static struct drm_driver driver_platform;

static u64
nouveau_pci_name(struct pci_dev *pdev)
{
	u64 name = (u64)pci_domain_nr(pdev->bus) << 32;
	name |= pdev->bus->number << 16;
	name |= PCI_SLOT(pdev->devfn) << 8;
	return name | PCI_FUNC(pdev->devfn);
}

static u64
nouveau_platform_name(struct platform_device *platformdev)
{
	return platformdev->id;
}

static u64
nouveau_name(struct drm_device *dev)
{
	if (dev->pdev)
		return nouveau_pci_name(dev->pdev);
	else
		return nouveau_platform_name(dev->platformdev);
}

static int
nouveau_cli_create(u64 name, const char *sname,
		   int size, void **pcli)
{
	struct nouveau_cli *cli = *pcli = kzalloc(size, GFP_KERNEL);
	if (cli) {
		int ret = nvif_client_init(NULL, NULL, sname, name,
					   nouveau_config, nouveau_debug,
					  &cli->base);
		if (ret == 0) {
			mutex_init(&cli->mutex);
			usif_client_init(cli);
		}
		return ret;
	}
	return -ENOMEM;
}

static void
nouveau_cli_destroy(struct nouveau_cli *cli)
{
	nouveau_vm_ref(NULL, &nvkm_client(&cli->base)->vm, NULL);
	nvif_client_fini(&cli->base);
	usif_client_fini(cli);
}

static void
nouveau_accel_fini(struct nouveau_drm *drm)
{
	nouveau_channel_del(&drm->channel);
	nvif_object_fini(&drm->ntfy);
	nouveau_gpuobj_ref(NULL, &drm->notify);
	nvif_object_fini(&drm->nvsw);
	nouveau_channel_del(&drm->cechan);
	nvif_object_fini(&drm->ttm.copy);
	if (drm->fence)
		nouveau_fence(drm)->dtor(drm);
}

static void
nouveau_accel_init(struct nouveau_drm *drm)
{
	struct nvif_device *device = &drm->device;
	u32 arg0, arg1;
	u32 sclass[16];
	int ret, i;

	if (nouveau_noaccel)
		return;

	/* initialise synchronisation routines */
	/*XXX: this is crap, but the fence/channel stuff is a little
	 *     backwards in some places.  this will be fixed.
	 */
	ret = nvif_object_sclass(&device->base, sclass, ARRAY_SIZE(sclass));
	if (ret < 0)
		return;

	for (ret = -ENOSYS, i = 0; ret && i < ARRAY_SIZE(sclass); i++) {
		switch (sclass[i]) {
		case NV03_CHANNEL_DMA:
			ret = nv04_fence_create(drm);
			break;
		case NV10_CHANNEL_DMA:
			ret = nv10_fence_create(drm);
			break;
		case NV17_CHANNEL_DMA:
		case NV40_CHANNEL_DMA:
			ret = nv17_fence_create(drm);
			break;
		case NV50_CHANNEL_GPFIFO:
			ret = nv50_fence_create(drm);
			break;
		case G82_CHANNEL_GPFIFO:
			ret = nv84_fence_create(drm);
			break;
		case FERMI_CHANNEL_GPFIFO:
		case KEPLER_CHANNEL_GPFIFO_A:
			ret = nvc0_fence_create(drm);
			break;
		default:
			break;
		}
	}

	if (ret) {
		NV_ERROR(drm, "failed to initialise sync subsystem, %d\n", ret);
		nouveau_accel_fini(drm);
		return;
	}

	if (device->info.family >= NV_DEVICE_INFO_V0_KEPLER) {
		ret = nouveau_channel_new(drm, &drm->device, NVDRM_CHAN + 1,
					  KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_CE0|
					  KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_CE1,
					  0, &drm->cechan);
		if (ret)
			NV_ERROR(drm, "failed to create ce channel, %d\n", ret);

		arg0 = KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_GR;
		arg1 = 1;
	} else
	if (device->info.chipset >= 0xa3 &&
	    device->info.chipset != 0xaa &&
	    device->info.chipset != 0xac) {
		ret = nouveau_channel_new(drm, &drm->device, NVDRM_CHAN + 1,
					  NvDmaFB, NvDmaTT, &drm->cechan);
		if (ret)
			NV_ERROR(drm, "failed to create ce channel, %d\n", ret);

		arg0 = NvDmaFB;
		arg1 = NvDmaTT;
	} else {
		arg0 = NvDmaFB;
		arg1 = NvDmaTT;
	}

	ret = nouveau_channel_new(drm, &drm->device, NVDRM_CHAN, arg0, arg1,
				 &drm->channel);
	if (ret) {
		NV_ERROR(drm, "failed to create kernel channel, %d\n", ret);
		nouveau_accel_fini(drm);
		return;
	}

	ret = nvif_object_init(drm->channel->object, NULL, NVDRM_NVSW,
			       nouveau_abi16_swclass(drm), NULL, 0, &drm->nvsw);
	if (ret == 0) {
		struct nouveau_software_chan *swch;
		ret = RING_SPACE(drm->channel, 2);
		if (ret == 0) {
			if (device->info.family < NV_DEVICE_INFO_V0_FERMI) {
				BEGIN_NV04(drm->channel, NvSubSw, 0, 1);
				OUT_RING  (drm->channel, NVDRM_NVSW);
			} else
			if (device->info.family < NV_DEVICE_INFO_V0_KEPLER) {
				BEGIN_NVC0(drm->channel, FermiSw, 0, 1);
				OUT_RING  (drm->channel, 0x001f0000);
			}
		}
		swch = (void *)nvkm_object(&drm->nvsw)->parent;
		swch->flip = nouveau_flip_complete;
		swch->flip_data = drm->channel;
	}

	if (ret) {
		NV_ERROR(drm, "failed to allocate software object, %d\n", ret);
		nouveau_accel_fini(drm);
		return;
	}

	if (device->info.family < NV_DEVICE_INFO_V0_FERMI) {
		ret = nouveau_gpuobj_new(nvkm_object(&drm->device), NULL, 32,
					 0, 0, &drm->notify);
		if (ret) {
			NV_ERROR(drm, "failed to allocate notifier, %d\n", ret);
			nouveau_accel_fini(drm);
			return;
		}

		ret = nvif_object_init(drm->channel->object, NULL, NvNotify0,
				       NV_DMA_IN_MEMORY,
				       &(struct nv_dma_v0) {
						.target = NV_DMA_V0_TARGET_VRAM,
						.access = NV_DMA_V0_ACCESS_RDWR,
						.start = drm->notify->addr,
						.limit = drm->notify->addr + 31
				       }, sizeof(struct nv_dma_v0),
				       &drm->ntfy);
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
	if (nouveau_modeset != 2)
		remove_conflicting_framebuffers(aper, "nouveaufb", boot);
	kfree(aper);

	ret = nouveau_device_create(pdev, NOUVEAU_BUS_PCI,
				    nouveau_pci_name(pdev), pci_name(pdev),
				    nouveau_config, nouveau_debug, &device);
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = drm_get_pci_dev(pdev, pent, &driver_pci);
	if (ret) {
		nouveau_object_ref(NULL, (struct nouveau_object **)&device);
		return ret;
	}

	return 0;
}

#define PCI_CLASS_MULTIMEDIA_HD_AUDIO 0x0403

static void
nouveau_get_hdmi_dev(struct nouveau_drm *drm)
{
	struct pci_dev *pdev = drm->dev->pdev;

	if (!pdev) {
		DRM_INFO("not a PCI device; no HDMI\n");
		drm->hdmi_device = NULL;
		return;
	}

	/* subfunction one is a hdmi audio device? */
	drm->hdmi_device = pci_get_bus_and_slot((unsigned int)pdev->bus->number,
						PCI_DEVFN(PCI_SLOT(pdev->devfn), 1));

	if (!drm->hdmi_device) {
		NV_DEBUG(drm, "hdmi device not found %d %d %d\n", pdev->bus->number, PCI_SLOT(pdev->devfn), 1);
		return;
	}

	if ((drm->hdmi_device->class >> 8) != PCI_CLASS_MULTIMEDIA_HD_AUDIO) {
		NV_DEBUG(drm, "possible hdmi device not audio %d\n", drm->hdmi_device->class);
		pci_dev_put(drm->hdmi_device);
		drm->hdmi_device = NULL;
		return;
	}
}

static int
nouveau_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct pci_dev *pdev = dev->pdev;
	struct nouveau_drm *drm;
	int ret;

	ret = nouveau_cli_create(nouveau_name(dev), "DRM", sizeof(*drm),
				 (void **)&drm);
	if (ret)
		return ret;

	dev->dev_private = drm;
	drm->dev = dev;
	nvkm_client(&drm->client.base)->debug =
		nouveau_dbgopt(nouveau_debug, "DRM");

	INIT_LIST_HEAD(&drm->clients);
	spin_lock_init(&drm->tile.lock);

	nouveau_get_hdmi_dev(drm);

	/* make sure AGP controller is in a consistent state before we
	 * (possibly) execute vbios init tables (see nouveau_agp.h)
	 */
	if (pdev && drm_pci_device_is_agp(dev) && dev->agp) {
		const u64 enables = NV_DEVICE_V0_DISABLE_IDENTIFY |
				    NV_DEVICE_V0_DISABLE_MMIO;
		/* dummy device object, doesn't init anything, but allows
		 * agp code access to registers
		 */
		ret = nvif_device_init(&drm->client.base.base, NULL,
				       NVDRM_DEVICE, NV_DEVICE,
				       &(struct nv_device_v0) {
						.device = ~0,
						.disable = ~enables,
						.debug0 = ~0,
				       }, sizeof(struct nv_device_v0),
				       &drm->device);
		if (ret)
			goto fail_device;

		nouveau_agp_reset(drm);
		nvif_device_fini(&drm->device);
	}

	ret = nvif_device_init(&drm->client.base.base, NULL, NVDRM_DEVICE,
			       NV_DEVICE,
			       &(struct nv_device_v0) {
					.device = ~0,
					.disable = 0,
					.debug0 = 0,
			       }, sizeof(struct nv_device_v0),
			       &drm->device);
	if (ret)
		goto fail_device;

	dev->irq_enabled = true;

	/* workaround an odd issue on nvc1 by disabling the device's
	 * nosnoop capability.  hopefully won't cause issues until a
	 * better fix is found - assuming there is one...
	 */
	if (drm->device.info.chipset == 0xc1)
		nvif_mask(&drm->device, 0x00088080, 0x00000800, 0x00000000);

	nouveau_vga_init(drm);
	nouveau_agp_init(drm);

	if (drm->device.info.family >= NV_DEVICE_INFO_V0_TESLA) {
		ret = nouveau_vm_new(nvkm_device(&drm->device), 0, (1ULL << 40),
				     0x1000, &drm->client.vm);
		if (ret)
			goto fail_device;

		nvkm_client(&drm->client.base)->vm = drm->client.vm;
	}

	ret = nouveau_ttm_init(drm);
	if (ret)
		goto fail_ttm;

	ret = nouveau_bios_init(dev);
	if (ret)
		goto fail_bios;

	ret = nouveau_display_create(dev);
	if (ret)
		goto fail_dispctor;

	if (dev->mode_config.num_crtc) {
		ret = nouveau_display_init(dev);
		if (ret)
			goto fail_dispinit;
	}

	nouveau_sysfs_init(dev);
	nouveau_hwmon_init(dev);
	nouveau_accel_init(drm);
	nouveau_fbcon_init(dev);

	if (nouveau_runtime_pm != 0) {
		pm_runtime_use_autosuspend(dev->dev);
		pm_runtime_set_autosuspend_delay(dev->dev, 5000);
		pm_runtime_set_active(dev->dev);
		pm_runtime_allow(dev->dev);
		pm_runtime_mark_last_busy(dev->dev);
		pm_runtime_put(dev->dev);
	}
	return 0;

fail_dispinit:
	nouveau_display_destroy(dev);
fail_dispctor:
	nouveau_bios_takedown(dev);
fail_bios:
	nouveau_ttm_fini(drm);
fail_ttm:
	nouveau_agp_fini(drm);
	nouveau_vga_fini(drm);
fail_device:
	nvif_device_fini(&drm->device);
	nouveau_cli_destroy(&drm->client);
	return ret;
}

static int
nouveau_drm_unload(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	pm_runtime_get_sync(dev->dev);
	nouveau_fbcon_fini(dev);
	nouveau_accel_fini(drm);
	nouveau_hwmon_fini(dev);
	nouveau_sysfs_fini(dev);

	if (dev->mode_config.num_crtc)
		nouveau_display_fini(dev);
	nouveau_display_destroy(dev);

	nouveau_bios_takedown(dev);

	nouveau_ttm_fini(drm);
	nouveau_agp_fini(drm);
	nouveau_vga_fini(drm);

	nvif_device_fini(&drm->device);
	if (drm->hdmi_device)
		pci_dev_put(drm->hdmi_device);
	nouveau_cli_destroy(&drm->client);
	return 0;
}

void
nouveau_drm_device_remove(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_client *client;
	struct nouveau_object *device;

	dev->irq_enabled = false;
	client = nvkm_client(&drm->client.base);
	device = client->device;
	drm_put_dev(dev);

	nouveau_object_ref(NULL, &device);
	nouveau_object_debug();
}
EXPORT_SYMBOL(nouveau_drm_device_remove);

static void
nouveau_drm_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	nouveau_drm_device_remove(dev);
}

static int
nouveau_do_suspend(struct drm_device *dev, bool runtime)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_cli *cli;
	int ret;

	if (dev->mode_config.num_crtc) {
		NV_INFO(drm, "suspending console...\n");
		nouveau_fbcon_set_suspend(dev, 1);
		NV_INFO(drm, "suspending display...\n");
		ret = nouveau_display_suspend(dev, runtime);
		if (ret)
			return ret;
	}

	NV_INFO(drm, "evicting buffers...\n");
	ttm_bo_evict_mm(&drm->ttm.bdev, TTM_PL_VRAM);

	NV_INFO(drm, "waiting for kernel channels to go idle...\n");
	if (drm->cechan) {
		ret = nouveau_channel_idle(drm->cechan);
		if (ret)
			goto fail_display;
	}

	if (drm->channel) {
		ret = nouveau_channel_idle(drm->channel);
		if (ret)
			goto fail_display;
	}

	NV_INFO(drm, "suspending client object trees...\n");
	if (drm->fence && nouveau_fence(drm)->suspend) {
		if (!nouveau_fence(drm)->suspend(drm)) {
			ret = -ENOMEM;
			goto fail_display;
		}
	}

	list_for_each_entry(cli, &drm->clients, head) {
		ret = nvif_client_suspend(&cli->base);
		if (ret)
			goto fail_client;
	}

	NV_INFO(drm, "suspending kernel object tree...\n");
	ret = nvif_client_suspend(&drm->client.base);
	if (ret)
		goto fail_client;

	nouveau_agp_fini(drm);
	return 0;

fail_client:
	list_for_each_entry_continue_reverse(cli, &drm->clients, head) {
		nvif_client_resume(&cli->base);
	}

	if (drm->fence && nouveau_fence(drm)->resume)
		nouveau_fence(drm)->resume(drm);

fail_display:
	if (dev->mode_config.num_crtc) {
		NV_INFO(drm, "resuming display...\n");
		nouveau_display_resume(dev, runtime);
	}
	return ret;
}

static int
nouveau_do_resume(struct drm_device *dev, bool runtime)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_cli *cli;

	NV_INFO(drm, "re-enabling device...\n");

	nouveau_agp_reset(drm);

	NV_INFO(drm, "resuming kernel object tree...\n");
	nvif_client_resume(&drm->client.base);
	nouveau_agp_init(drm);

	NV_INFO(drm, "resuming client object trees...\n");
	if (drm->fence && nouveau_fence(drm)->resume)
		nouveau_fence(drm)->resume(drm);

	list_for_each_entry(cli, &drm->clients, head) {
		nvif_client_resume(&cli->base);
	}

	nouveau_run_vbios_init(dev);

	if (dev->mode_config.num_crtc) {
		NV_INFO(drm, "resuming display...\n");
		nouveau_display_resume(dev, runtime);
		NV_INFO(drm, "resuming console...\n");
		nouveau_fbcon_set_suspend(dev, 0);
	}

	return 0;
}

int
nouveau_pmops_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int ret;

	if (drm_dev->switch_power_state == DRM_SWITCH_POWER_OFF ||
	    drm_dev->switch_power_state == DRM_SWITCH_POWER_DYNAMIC_OFF)
		return 0;

	ret = nouveau_do_suspend(drm_dev, false);
	if (ret)
		return ret;

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);
	return 0;
}

int
nouveau_pmops_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int ret;

	if (drm_dev->switch_power_state == DRM_SWITCH_POWER_OFF ||
	    drm_dev->switch_power_state == DRM_SWITCH_POWER_DYNAMIC_OFF)
		return 0;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	pci_set_master(pdev);

	return nouveau_do_resume(drm_dev, false);
}

static int
nouveau_pmops_freeze(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	return nouveau_do_suspend(drm_dev, false);
}

static int
nouveau_pmops_thaw(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	return nouveau_do_resume(drm_dev, false);
}

static int
nouveau_pmops_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int ret;

	if (nouveau_runtime_pm == 0) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	/* are we optimus enabled? */
	if (nouveau_runtime_pm == -1 && !nouveau_is_optimus() && !nouveau_is_v1_dsm()) {
		DRM_DEBUG_DRIVER("failing to power off - not optimus\n");
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	nv_debug_level(SILENT);
	drm_kms_helper_poll_disable(drm_dev);
	vga_switcheroo_set_dynamic_switch(pdev, VGA_SWITCHEROO_OFF);
	nouveau_switcheroo_optimus_dsm();
	ret = nouveau_do_suspend(drm_dev, true);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_ignore_hotplug(pdev);
	pci_set_power_state(pdev, PCI_D3cold);
	drm_dev->switch_power_state = DRM_SWITCH_POWER_DYNAMIC_OFF;
	return ret;
}

static int
nouveau_pmops_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct nvif_device *device = &nouveau_drm(drm_dev)->device;
	int ret;

	if (nouveau_runtime_pm == 0)
		return -EINVAL;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	pci_set_master(pdev);

	ret = nouveau_do_resume(drm_dev, true);
	drm_kms_helper_poll_enable(drm_dev);
	/* do magic */
	nvif_mask(device, 0x88488, (1 << 25), (1 << 25));
	vga_switcheroo_set_dynamic_switch(pdev, VGA_SWITCHEROO_ON);
	drm_dev->switch_power_state = DRM_SWITCH_POWER_ON;
	nv_debug_level(NORMAL);
	return ret;
}

static int
nouveau_pmops_runtime_idle(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct drm_crtc *crtc;

	if (nouveau_runtime_pm == 0) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	/* are we optimus enabled? */
	if (nouveau_runtime_pm == -1 && !nouveau_is_optimus() && !nouveau_is_v1_dsm()) {
		DRM_DEBUG_DRIVER("failing to power off - not optimus\n");
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	/* if we have a hdmi audio device - make sure it has a driver loaded */
	if (drm->hdmi_device) {
		if (!drm->hdmi_device->driver) {
			DRM_DEBUG_DRIVER("failing to power off - no HDMI audio driver loaded\n");
			pm_runtime_mark_last_busy(dev);
			return -EBUSY;
		}
	}

	list_for_each_entry(crtc, &drm->dev->mode_config.crtc_list, head) {
		if (crtc->enabled) {
			DRM_DEBUG_DRIVER("failing to power off - crtc active\n");
			return -EBUSY;
		}
	}
	pm_runtime_mark_last_busy(dev);
	pm_runtime_autosuspend(dev);
	/* we don't want the main rpm_idle to call suspend - we want to autosuspend */
	return 1;
}

static int
nouveau_drm_open(struct drm_device *dev, struct drm_file *fpriv)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_cli *cli;
	char name[32], tmpname[TASK_COMM_LEN];
	int ret;

	/* need to bring up power immediately if opening device */
	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	get_task_comm(tmpname, current);
	snprintf(name, sizeof(name), "%s[%d]", tmpname, pid_nr(fpriv->pid));

	ret = nouveau_cli_create(nouveau_name(dev), name, sizeof(*cli),
			(void **)&cli);

	if (ret)
		goto out_suspend;

	cli->base.super = false;

	if (drm->device.info.family >= NV_DEVICE_INFO_V0_TESLA) {
		ret = nouveau_vm_new(nvkm_device(&drm->device), 0, (1ULL << 40),
				     0x1000, &cli->vm);
		if (ret) {
			nouveau_cli_destroy(cli);
			goto out_suspend;
		}

		nvkm_client(&cli->base)->vm = cli->vm;
	}

	fpriv->driver_priv = cli;

	mutex_lock(&drm->client.mutex);
	list_add(&cli->head, &drm->clients);
	mutex_unlock(&drm->client.mutex);

out_suspend:
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return ret;
}

static void
nouveau_drm_preclose(struct drm_device *dev, struct drm_file *fpriv)
{
	struct nouveau_cli *cli = nouveau_cli(fpriv);
	struct nouveau_drm *drm = nouveau_drm(dev);

	pm_runtime_get_sync(dev->dev);

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
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
}

static const struct drm_ioctl_desc
nouveau_ioctls[] = {
	DRM_IOCTL_DEF_DRV(NOUVEAU_GETPARAM, nouveau_abi16_ioctl_getparam, DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_SETPARAM, nouveau_abi16_ioctl_setparam, DRM_UNLOCKED|DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_ALLOC, nouveau_abi16_ioctl_channel_alloc, DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_FREE, nouveau_abi16_ioctl_channel_free, DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GROBJ_ALLOC, nouveau_abi16_ioctl_grobj_alloc, DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_NOTIFIEROBJ_ALLOC, nouveau_abi16_ioctl_notifierobj_alloc, DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GPUOBJ_FREE, nouveau_abi16_ioctl_gpuobj_free, DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_NEW, nouveau_gem_ioctl_new, DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_PUSHBUF, nouveau_gem_ioctl_pushbuf, DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_PREP, nouveau_gem_ioctl_cpu_prep, DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_FINI, nouveau_gem_ioctl_cpu_fini, DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_INFO, nouveau_gem_ioctl_info, DRM_UNLOCKED|DRM_AUTH|DRM_RENDER_ALLOW),
};

long
nouveau_drm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct drm_file *filp = file->private_data;
	struct drm_device *dev = filp->minor->dev;
	long ret;

	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	switch (_IOC_NR(cmd) - DRM_COMMAND_BASE) {
	case DRM_NOUVEAU_NVIF:
		ret = usif_ioctl(filp, (void __user *)arg, _IOC_SIZE(cmd));
		break;
	default:
		ret = drm_ioctl(file, cmd, arg);
		break;
	}

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
	return ret;
}

static const struct file_operations
nouveau_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = nouveau_drm_ioctl,
	.mmap = nouveau_ttm_mmap,
	.poll = drm_poll,
	.read = drm_read,
#if defined(CONFIG_COMPAT)
	.compat_ioctl = nouveau_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct drm_driver
driver_stub = {
	.driver_features =
		DRIVER_USE_AGP |
		DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME | DRIVER_RENDER,

	.load = nouveau_drm_load,
	.unload = nouveau_drm_unload,
	.open = nouveau_drm_open,
	.preclose = nouveau_drm_preclose,
	.postclose = nouveau_drm_postclose,
	.lastclose = nouveau_vga_lastclose,

#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = nouveau_debugfs_init,
	.debugfs_cleanup = nouveau_debugfs_takedown,
#endif

	.get_vblank_counter = drm_vblank_count,
	.enable_vblank = nouveau_display_vblank_enable,
	.disable_vblank = nouveau_display_vblank_disable,
	.get_scanout_position = nouveau_display_scanoutpos,
	.get_vblank_timestamp = nouveau_display_vblstamp,

	.ioctls = nouveau_ioctls,
	.num_ioctls = ARRAY_SIZE(nouveau_ioctls),
	.fops = &nouveau_driver_fops,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_pin = nouveau_gem_prime_pin,
	.gem_prime_res_obj = nouveau_gem_prime_res_obj,
	.gem_prime_unpin = nouveau_gem_prime_unpin,
	.gem_prime_get_sg_table = nouveau_gem_prime_get_sg_table,
	.gem_prime_import_sg_table = nouveau_gem_prime_import_sg_table,
	.gem_prime_vmap = nouveau_gem_prime_vmap,
	.gem_prime_vunmap = nouveau_gem_prime_vunmap,

	.gem_free_object = nouveau_gem_object_del,
	.gem_open_object = nouveau_gem_object_open,
	.gem_close_object = nouveau_gem_object_close,

	.dumb_create = nouveau_display_dumb_create,
	.dumb_map_offset = nouveau_display_dumb_map_offset,
	.dumb_destroy = drm_gem_dumb_destroy,

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

static void nouveau_display_options(void)
{
	DRM_DEBUG_DRIVER("Loading Nouveau with parameters:\n");

	DRM_DEBUG_DRIVER("... tv_disable   : %d\n", nouveau_tv_disable);
	DRM_DEBUG_DRIVER("... ignorelid    : %d\n", nouveau_ignorelid);
	DRM_DEBUG_DRIVER("... duallink     : %d\n", nouveau_duallink);
	DRM_DEBUG_DRIVER("... nofbaccel    : %d\n", nouveau_nofbaccel);
	DRM_DEBUG_DRIVER("... config       : %s\n", nouveau_config);
	DRM_DEBUG_DRIVER("... debug        : %s\n", nouveau_debug);
	DRM_DEBUG_DRIVER("... noaccel      : %d\n", nouveau_noaccel);
	DRM_DEBUG_DRIVER("... modeset      : %d\n", nouveau_modeset);
	DRM_DEBUG_DRIVER("... runpm        : %d\n", nouveau_runtime_pm);
	DRM_DEBUG_DRIVER("... vram_pushbuf : %d\n", nouveau_vram_pushbuf);
	DRM_DEBUG_DRIVER("... pstate       : %d\n", nouveau_pstate);
}

static const struct dev_pm_ops nouveau_pm_ops = {
	.suspend = nouveau_pmops_suspend,
	.resume = nouveau_pmops_resume,
	.freeze = nouveau_pmops_freeze,
	.thaw = nouveau_pmops_thaw,
	.poweroff = nouveau_pmops_freeze,
	.restore = nouveau_pmops_resume,
	.runtime_suspend = nouveau_pmops_runtime_suspend,
	.runtime_resume = nouveau_pmops_runtime_resume,
	.runtime_idle = nouveau_pmops_runtime_idle,
};

static struct pci_driver
nouveau_drm_pci_driver = {
	.name = "nouveau",
	.id_table = nouveau_drm_pci_table,
	.probe = nouveau_drm_probe,
	.remove = nouveau_drm_remove,
	.driver.pm = &nouveau_pm_ops,
};

struct drm_device *
nouveau_platform_device_create_(struct platform_device *pdev, int size,
				void **pobject)
{
	struct drm_device *drm;
	int err;

	err = nouveau_device_create_(pdev, NOUVEAU_BUS_PLATFORM,
				    nouveau_platform_name(pdev),
				    dev_name(&pdev->dev), nouveau_config,
				    nouveau_debug, size, pobject);
	if (err)
		return ERR_PTR(err);

	drm = drm_dev_alloc(&driver_platform, &pdev->dev);
	if (!drm) {
		err = -ENOMEM;
		goto err_free;
	}

	err = drm_dev_set_unique(drm, "%s", dev_name(&pdev->dev));
	if (err < 0)
		goto err_free;

	drm->platformdev = pdev;
	platform_set_drvdata(pdev, drm);

	return drm;

err_free:
	nouveau_object_ref(NULL, (struct nouveau_object **)pobject);

	return ERR_PTR(err);
}
EXPORT_SYMBOL(nouveau_platform_device_create_);

static int __init
nouveau_drm_init(void)
{
	driver_pci = driver_stub;
	driver_pci.set_busid = drm_pci_set_busid;
	driver_platform = driver_stub;
	driver_platform.set_busid = drm_platform_set_busid;

	nouveau_display_options();

	if (nouveau_modeset == -1) {
#ifdef CONFIG_VGA_CONSOLE
		if (vgacon_text_force())
			nouveau_modeset = 0;
#endif
	}

	if (!nouveau_modeset)
		return 0;

	nouveau_register_dsm_handler();
	return drm_pci_init(&driver_pci, &nouveau_drm_pci_driver);
}

static void __exit
nouveau_drm_exit(void)
{
	if (!nouveau_modeset)
		return;

	drm_pci_exit(&driver_pci, &nouveau_drm_pci_driver);
	nouveau_unregister_dsm_handler();
}

module_init(nouveau_drm_init);
module_exit(nouveau_drm_exit);

MODULE_DEVICE_TABLE(pci, nouveau_drm_pci_table);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
