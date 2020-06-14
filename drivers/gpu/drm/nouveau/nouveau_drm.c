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
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/vga_switcheroo.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include <core/gpuobj.h>
#include <core/option.h>
#include <core/pci.h>
#include <core/tegra.h>

#include <nvif/driver.h>
#include <nvif/fifo.h>
#include <nvif/user.h>

#include <nvif/class.h>
#include <nvif/cl0002.h>
#include <nvif/cla06f.h>
#include <nvif/if0004.h>

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_ttm.h"
#include "nouveau_gem.h"
#include "nouveau_vga.h"
#include "nouveau_led.h"
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
#include "nouveau_platform.h"

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

MODULE_PARM_DESC(atomic, "Expose atomic ioctl (default: disabled)");
static int nouveau_atomic = 0;
module_param_named(atomic, nouveau_atomic, int, 0400);

MODULE_PARM_DESC(runpm, "disable (0), force enable (1), optimus only default (-1)");
static int nouveau_runtime_pm = -1;
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
		return nouveau_platform_name(to_platform_device(dev->dev));
}

static inline bool
nouveau_cli_work_ready(struct dma_fence *fence)
{
	if (!dma_fence_is_signaled(fence))
		return false;
	dma_fence_put(fence);
	return true;
}

static void
nouveau_cli_work(struct work_struct *w)
{
	struct nouveau_cli *cli = container_of(w, typeof(*cli), work);
	struct nouveau_cli_work *work, *wtmp;
	mutex_lock(&cli->lock);
	list_for_each_entry_safe(work, wtmp, &cli->worker, head) {
		if (!work->fence || nouveau_cli_work_ready(work->fence)) {
			list_del(&work->head);
			work->func(work);
		}
	}
	mutex_unlock(&cli->lock);
}

static void
nouveau_cli_work_fence(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct nouveau_cli_work *work = container_of(cb, typeof(*work), cb);
	schedule_work(&work->cli->work);
}

void
nouveau_cli_work_queue(struct nouveau_cli *cli, struct dma_fence *fence,
		       struct nouveau_cli_work *work)
{
	work->fence = dma_fence_get(fence);
	work->cli = cli;
	mutex_lock(&cli->lock);
	list_add_tail(&work->head, &cli->worker);
	if (dma_fence_add_callback(fence, &work->cb, nouveau_cli_work_fence))
		nouveau_cli_work_fence(fence, &work->cb);
	mutex_unlock(&cli->lock);
}

static void
nouveau_cli_fini(struct nouveau_cli *cli)
{
	/* All our channels are dead now, which means all the fences they
	 * own are signalled, and all callback functions have been called.
	 *
	 * So, after flushing the workqueue, there should be nothing left.
	 */
	flush_work(&cli->work);
	WARN_ON(!list_empty(&cli->worker));

	usif_client_fini(cli);
	nouveau_vmm_fini(&cli->vmm);
	nvif_mmu_fini(&cli->mmu);
	nvif_device_fini(&cli->device);
	mutex_lock(&cli->drm->master.lock);
	nvif_client_fini(&cli->base);
	mutex_unlock(&cli->drm->master.lock);
}

static int
nouveau_cli_init(struct nouveau_drm *drm, const char *sname,
		 struct nouveau_cli *cli)
{
	static const struct nvif_mclass
	mems[] = {
		{ NVIF_CLASS_MEM_GF100, -1 },
		{ NVIF_CLASS_MEM_NV50 , -1 },
		{ NVIF_CLASS_MEM_NV04 , -1 },
		{}
	};
	static const struct nvif_mclass
	mmus[] = {
		{ NVIF_CLASS_MMU_GF100, -1 },
		{ NVIF_CLASS_MMU_NV50 , -1 },
		{ NVIF_CLASS_MMU_NV04 , -1 },
		{}
	};
	static const struct nvif_mclass
	vmms[] = {
		{ NVIF_CLASS_VMM_GP100, -1 },
		{ NVIF_CLASS_VMM_GM200, -1 },
		{ NVIF_CLASS_VMM_GF100, -1 },
		{ NVIF_CLASS_VMM_NV50 , -1 },
		{ NVIF_CLASS_VMM_NV04 , -1 },
		{}
	};
	u64 device = nouveau_name(drm->dev);
	int ret;

	snprintf(cli->name, sizeof(cli->name), "%s", sname);
	cli->drm = drm;
	mutex_init(&cli->mutex);
	usif_client_init(cli);

	INIT_WORK(&cli->work, nouveau_cli_work);
	INIT_LIST_HEAD(&cli->worker);
	mutex_init(&cli->lock);

	if (cli == &drm->master) {
		ret = nvif_driver_init(NULL, nouveau_config, nouveau_debug,
				       cli->name, device, &cli->base);
	} else {
		mutex_lock(&drm->master.lock);
		ret = nvif_client_init(&drm->master.base, cli->name, device,
				       &cli->base);
		mutex_unlock(&drm->master.lock);
	}
	if (ret) {
		NV_PRINTK(err, cli, "Client allocation failed: %d\n", ret);
		goto done;
	}

	ret = nvif_device_init(&cli->base.object, 0, NV_DEVICE,
			       &(struct nv_device_v0) {
					.device = ~0,
			       }, sizeof(struct nv_device_v0),
			       &cli->device);
	if (ret) {
		NV_PRINTK(err, cli, "Device allocation failed: %d\n", ret);
		goto done;
	}

	ret = nvif_mclass(&cli->device.object, mmus);
	if (ret < 0) {
		NV_PRINTK(err, cli, "No supported MMU class\n");
		goto done;
	}

	ret = nvif_mmu_init(&cli->device.object, mmus[ret].oclass, &cli->mmu);
	if (ret) {
		NV_PRINTK(err, cli, "MMU allocation failed: %d\n", ret);
		goto done;
	}

	ret = nvif_mclass(&cli->mmu.object, vmms);
	if (ret < 0) {
		NV_PRINTK(err, cli, "No supported VMM class\n");
		goto done;
	}

	ret = nouveau_vmm_init(cli, vmms[ret].oclass, &cli->vmm);
	if (ret) {
		NV_PRINTK(err, cli, "VMM allocation failed: %d\n", ret);
		goto done;
	}

	ret = nvif_mclass(&cli->mmu.object, mems);
	if (ret < 0) {
		NV_PRINTK(err, cli, "No supported MEM class\n");
		goto done;
	}

	cli->mem = &mems[ret];
	return 0;
done:
	if (ret)
		nouveau_cli_fini(cli);
	return ret;
}

static void
nouveau_accel_fini(struct nouveau_drm *drm)
{
	nouveau_channel_idle(drm->channel);
	nvif_object_fini(&drm->ntfy);
	nvkm_gpuobj_del(&drm->notify);
	nvif_notify_fini(&drm->flip);
	nvif_object_fini(&drm->nvsw);
	nouveau_channel_del(&drm->channel);

	nouveau_channel_idle(drm->cechan);
	nvif_object_fini(&drm->ttm.copy);
	nouveau_channel_del(&drm->cechan);

	if (drm->fence)
		nouveau_fence(drm)->dtor(drm);
}

static void
nouveau_accel_init(struct nouveau_drm *drm)
{
	struct nvif_device *device = &drm->client.device;
	struct nvif_sclass *sclass;
	u32 arg0, arg1;
	int ret, i, n;

	if (nouveau_noaccel)
		return;

	ret = nouveau_channels_init(drm);
	if (ret)
		return;

	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_VOLTA) {
		ret = nvif_user_init(device);
		if (ret)
			return;
	}

	/* initialise synchronisation routines */
	/*XXX: this is crap, but the fence/channel stuff is a little
	 *     backwards in some places.  this will be fixed.
	 */
	ret = n = nvif_object_sclass_get(&device->object, &sclass);
	if (ret < 0)
		return;

	for (ret = -ENOSYS, i = 0; i < n; i++) {
		switch (sclass[i].oclass) {
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
		case KEPLER_CHANNEL_GPFIFO_B:
		case MAXWELL_CHANNEL_GPFIFO_A:
		case PASCAL_CHANNEL_GPFIFO_A:
		case VOLTA_CHANNEL_GPFIFO_A:
			ret = nvc0_fence_create(drm);
			break;
		default:
			break;
		}
	}

	nvif_object_sclass_put(&sclass);
	if (ret) {
		NV_ERROR(drm, "failed to initialise sync subsystem, %d\n", ret);
		nouveau_accel_fini(drm);
		return;
	}

	if (device->info.family >= NV_DEVICE_INFO_V0_KEPLER) {
		ret = nouveau_channel_new(drm, &drm->client.device,
					  nvif_fifo_runlist_ce(device), 0,
					  &drm->cechan);
		if (ret)
			NV_ERROR(drm, "failed to create ce channel, %d\n", ret);

		arg0 = nvif_fifo_runlist(device, NV_DEVICE_INFO_ENGINE_GR);
		arg1 = 1;
	} else
	if (device->info.chipset >= 0xa3 &&
	    device->info.chipset != 0xaa &&
	    device->info.chipset != 0xac) {
		ret = nouveau_channel_new(drm, &drm->client.device,
					  NvDmaFB, NvDmaTT, &drm->cechan);
		if (ret)
			NV_ERROR(drm, "failed to create ce channel, %d\n", ret);

		arg0 = NvDmaFB;
		arg1 = NvDmaTT;
	} else {
		arg0 = NvDmaFB;
		arg1 = NvDmaTT;
	}

	ret = nouveau_channel_new(drm, &drm->client.device,
				  arg0, arg1, &drm->channel);
	if (ret) {
		NV_ERROR(drm, "failed to create kernel channel, %d\n", ret);
		nouveau_accel_fini(drm);
		return;
	}

	if (device->info.family < NV_DEVICE_INFO_V0_TESLA) {
		ret = nvif_object_init(&drm->channel->user, NVDRM_NVSW,
				       nouveau_abi16_swclass(drm), NULL, 0,
				       &drm->nvsw);
		if (ret == 0) {
			ret = RING_SPACE(drm->channel, 2);
			if (ret == 0) {
				BEGIN_NV04(drm->channel, NvSubSw, 0, 1);
				OUT_RING  (drm->channel, drm->nvsw.handle);
			}

			ret = nvif_notify_init(&drm->nvsw,
					       nouveau_flip_complete,
					       false, NV04_NVSW_NTFY_UEVENT,
					       NULL, 0, 0, &drm->flip);
			if (ret == 0)
				ret = nvif_notify_get(&drm->flip);
			if (ret) {
				nouveau_accel_fini(drm);
				return;
			}
		}

		if (ret) {
			NV_ERROR(drm, "failed to allocate sw class, %d\n", ret);
			nouveau_accel_fini(drm);
			return;
		}
	}

	if (device->info.family < NV_DEVICE_INFO_V0_FERMI) {
		ret = nvkm_gpuobj_new(nvxx_device(&drm->client.device), 32, 0,
				      false, NULL, &drm->notify);
		if (ret) {
			NV_ERROR(drm, "failed to allocate notifier, %d\n", ret);
			nouveau_accel_fini(drm);
			return;
		}

		ret = nvif_object_init(&drm->channel->user, NvNotify0,
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
	struct nvkm_device *device;
	struct apertures_struct *aper;
	bool boot = false;
	int ret;

	if (vga_switcheroo_client_probe_defer(pdev))
		return -EPROBE_DEFER;

	/* We need to check that the chipset is supported before booting
	 * fbdev off the hardware, as there's no way to put it back.
	 */
	ret = nvkm_device_pci_new(pdev, NULL, "error", true, false, 0, &device);
	if (ret)
		return ret;

	nvkm_device_del(&device);

	/* Remove conflicting drivers (vesafb, efifb etc). */
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
		drm_fb_helper_remove_conflicting_framebuffers(aper, "nouveaufb", boot);
	kfree(aper);

	ret = nvkm_device_pci_new(pdev, nouveau_config, nouveau_debug,
				  true, true, ~0ULL, &device);
	if (ret)
		return ret;

	pci_set_master(pdev);

	if (nouveau_atomic)
		driver_pci.driver_features |= DRIVER_ATOMIC;

	ret = drm_get_pci_dev(pdev, pent, &driver_pci);
	if (ret) {
		nvkm_device_del(&device);
		return ret;
	}

	return 0;
}

static int
nouveau_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct nouveau_drm *drm;
	int ret;

	if (!(drm = kzalloc(sizeof(*drm), GFP_KERNEL)))
		return -ENOMEM;
	dev->dev_private = drm;
	drm->dev = dev;

	ret = nouveau_cli_init(drm, "DRM-master", &drm->master);
	if (ret)
		return ret;

	ret = nouveau_cli_init(drm, "DRM", &drm->client);
	if (ret)
		return ret;

	dev->irq_enabled = true;

	nvxx_client(&drm->client.base)->debug =
		nvkm_dbgopt(nouveau_debug, "DRM");

	INIT_LIST_HEAD(&drm->clients);
	spin_lock_init(&drm->tile.lock);

	/* workaround an odd issue on nvc1 by disabling the device's
	 * nosnoop capability.  hopefully won't cause issues until a
	 * better fix is found - assuming there is one...
	 */
	if (drm->client.device.info.chipset == 0xc1)
		nvif_mask(&drm->client.device.object, 0x00088080, 0x00000800, 0x00000000);

	nouveau_vga_init(drm);

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

	nouveau_debugfs_init(drm);
	nouveau_hwmon_init(dev);
	nouveau_accel_init(drm);
	nouveau_fbcon_init(dev);
	nouveau_led_init(dev);

	if (nouveau_pmops_runtime()) {
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
	nouveau_vga_fini(drm);
	nouveau_cli_fini(&drm->client);
	nouveau_cli_fini(&drm->master);
	kfree(drm);
	return ret;
}

static void
nouveau_drm_unload(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (nouveau_pmops_runtime()) {
		pm_runtime_get_sync(dev->dev);
		pm_runtime_forbid(dev->dev);
	}

	nouveau_led_fini(dev);
	nouveau_fbcon_fini(dev);
	nouveau_accel_fini(drm);
	nouveau_hwmon_fini(dev);
	nouveau_debugfs_fini(drm);

	if (dev->mode_config.num_crtc)
		nouveau_display_fini(dev, false, false);
	nouveau_display_destroy(dev);

	nouveau_bios_takedown(dev);

	nouveau_ttm_fini(drm);
	nouveau_vga_fini(drm);

	nouveau_cli_fini(&drm->client);
	nouveau_cli_fini(&drm->master);
	kfree(drm);
}

void
nouveau_drm_device_remove(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_client *client;
	struct nvkm_device *device;

	dev->irq_enabled = false;
	client = nvxx_client(&drm->client.base);
	device = nvkm_device_find(client->device);
	drm_put_dev(dev);

	nvkm_device_del(&device);
}

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
	int ret;

	nouveau_led_suspend(dev);

	if (dev->mode_config.num_crtc) {
		NV_DEBUG(drm, "suspending console...\n");
		nouveau_fbcon_set_suspend(dev, 1);
		NV_DEBUG(drm, "suspending display...\n");
		ret = nouveau_display_suspend(dev, runtime);
		if (ret)
			return ret;
	}

	NV_DEBUG(drm, "evicting buffers...\n");
	ttm_bo_evict_mm(&drm->ttm.bdev, TTM_PL_VRAM);

	NV_DEBUG(drm, "waiting for kernel channels to go idle...\n");
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

	NV_DEBUG(drm, "suspending fence...\n");
	if (drm->fence && nouveau_fence(drm)->suspend) {
		if (!nouveau_fence(drm)->suspend(drm)) {
			ret = -ENOMEM;
			goto fail_display;
		}
	}

	NV_DEBUG(drm, "suspending object tree...\n");
	ret = nvif_client_suspend(&drm->master.base);
	if (ret)
		goto fail_client;

	return 0;

fail_client:
	if (drm->fence && nouveau_fence(drm)->resume)
		nouveau_fence(drm)->resume(drm);

fail_display:
	if (dev->mode_config.num_crtc) {
		NV_DEBUG(drm, "resuming display...\n");
		nouveau_display_resume(dev, runtime);
	}
	return ret;
}

static int
nouveau_do_resume(struct drm_device *dev, bool runtime)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	NV_DEBUG(drm, "resuming object tree...\n");
	nvif_client_resume(&drm->master.base);

	NV_DEBUG(drm, "resuming fence...\n");
	if (drm->fence && nouveau_fence(drm)->resume)
		nouveau_fence(drm)->resume(drm);

	nouveau_run_vbios_init(dev);

	if (dev->mode_config.num_crtc) {
		NV_DEBUG(drm, "resuming display...\n");
		nouveau_display_resume(dev, runtime);
		NV_DEBUG(drm, "resuming console...\n");
		nouveau_fbcon_set_suspend(dev, 0);
	}

	nouveau_led_resume(dev);

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
	udelay(200);
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

	ret = nouveau_do_resume(drm_dev, false);

	/* Monitors may have been connected / disconnected during suspend */
	schedule_work(&nouveau_drm(drm_dev)->hpd_work);

	return ret;
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

bool
nouveau_pmops_runtime(void)
{
	if (nouveau_runtime_pm == -1)
		return nouveau_is_optimus() || nouveau_is_v1_dsm();
	return nouveau_runtime_pm == 1;
}

static int
nouveau_pmops_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int ret;

	if (!nouveau_pmops_runtime()) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

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
	struct nvif_device *device = &nouveau_drm(drm_dev)->client.device;
	int ret;

	if (!nouveau_pmops_runtime()) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	pci_set_master(pdev);

	ret = nouveau_do_resume(drm_dev, true);

	/* do magic */
	nvif_mask(&device->object, 0x088488, (1 << 25), (1 << 25));
	drm_dev->switch_power_state = DRM_SWITCH_POWER_ON;

	/* Monitors may have been connected / disconnected during suspend */
	schedule_work(&nouveau_drm(drm_dev)->hpd_work);

	return ret;
}

static int
nouveau_pmops_runtime_idle(struct device *dev)
{
	if (!nouveau_pmops_runtime()) {
		pm_runtime_forbid(dev);
		return -EBUSY;
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
	if (ret < 0 && ret != -EACCES) {
		pm_runtime_put_autosuspend(dev->dev);
		return ret;
	}

	get_task_comm(tmpname, current);
	snprintf(name, sizeof(name), "%s[%d]", tmpname, pid_nr(fpriv->pid));

	if (!(cli = kzalloc(sizeof(*cli), GFP_KERNEL))) {
		ret = -ENOMEM;
		goto done;
	}

	ret = nouveau_cli_init(drm, name, cli);
	if (ret)
		goto done;

	cli->base.super = false;

	fpriv->driver_priv = cli;

	mutex_lock(&drm->client.mutex);
	list_add(&cli->head, &drm->clients);
	mutex_unlock(&drm->client.mutex);

done:
	if (ret && cli) {
		nouveau_cli_fini(cli);
		kfree(cli);
	}

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
	return ret;
}

static void
nouveau_drm_postclose(struct drm_device *dev, struct drm_file *fpriv)
{
	struct nouveau_cli *cli = nouveau_cli(fpriv);
	struct nouveau_drm *drm = nouveau_drm(dev);

	pm_runtime_get_sync(dev->dev);

	mutex_lock(&cli->mutex);
	if (cli->abi16)
		nouveau_abi16_fini(cli->abi16);
	mutex_unlock(&cli->mutex);

	mutex_lock(&drm->client.mutex);
	list_del(&cli->head);
	mutex_unlock(&drm->client.mutex);

	nouveau_cli_fini(cli);
	kfree(cli);
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
}

static const struct drm_ioctl_desc
nouveau_ioctls[] = {
	DRM_IOCTL_DEF_DRV(NOUVEAU_GETPARAM, nouveau_abi16_ioctl_getparam, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_SETPARAM, nouveau_abi16_ioctl_setparam, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_ALLOC, nouveau_abi16_ioctl_channel_alloc, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_FREE, nouveau_abi16_ioctl_channel_free, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GROBJ_ALLOC, nouveau_abi16_ioctl_grobj_alloc, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_NOTIFIEROBJ_ALLOC, nouveau_abi16_ioctl_notifierobj_alloc, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GPUOBJ_FREE, nouveau_abi16_ioctl_gpuobj_free, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_NEW, nouveau_gem_ioctl_new, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_PUSHBUF, nouveau_gem_ioctl_pushbuf, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_PREP, nouveau_gem_ioctl_cpu_prep, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_FINI, nouveau_gem_ioctl_cpu_fini, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_INFO, nouveau_gem_ioctl_info, DRM_AUTH|DRM_RENDER_ALLOW),
};

long
nouveau_drm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct drm_file *filp = file->private_data;
	struct drm_device *dev = filp->minor->dev;
	long ret;

	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0 && ret != -EACCES) {
		pm_runtime_put_autosuspend(dev->dev);
		return ret;
	}

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
		DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME | DRIVER_RENDER
#if defined(CONFIG_NOUVEAU_LEGACY_CTX_SUPPORT)
		| DRIVER_KMS_LEGACY_CONTEXT
#endif
		,

	.load = nouveau_drm_load,
	.unload = nouveau_drm_unload,
	.open = nouveau_drm_open,
	.postclose = nouveau_drm_postclose,
	.lastclose = nouveau_vga_lastclose,

#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = nouveau_drm_debugfs_init,
#endif

	.enable_vblank = nouveau_display_vblank_enable,
	.disable_vblank = nouveau_display_vblank_disable,
	.get_scanout_position = nouveau_display_scanoutpos,
	.get_vblank_timestamp = drm_calc_vbltimestamp_from_scanoutpos,

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

	.gem_free_object_unlocked = nouveau_gem_object_del,
	.gem_open_object = nouveau_gem_object_open,
	.gem_close_object = nouveau_gem_object_close,

	.dumb_create = nouveau_display_dumb_create,
	.dumb_map_offset = nouveau_display_dumb_map_offset,

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
	DRM_DEBUG_DRIVER("... hdmimhz      : %d\n", nouveau_hdmimhz);
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
nouveau_platform_device_create(const struct nvkm_device_tegra_func *func,
			       struct platform_device *pdev,
			       struct nvkm_device **pdevice)
{
	struct drm_device *drm;
	int err;

	err = nvkm_device_tegra_new(func, pdev, nouveau_config, nouveau_debug,
				    true, true, ~0ULL, pdevice);
	if (err)
		goto err_free;

	drm = drm_dev_alloc(&driver_platform, &pdev->dev);
	if (IS_ERR(drm)) {
		err = PTR_ERR(drm);
		goto err_free;
	}

	platform_set_drvdata(pdev, drm);

	return drm;

err_free:
	nvkm_device_del(pdevice);

	return ERR_PTR(err);
}

static int __init
nouveau_drm_init(void)
{
	driver_pci = driver_stub;
	driver_platform = driver_stub;

	nouveau_display_options();

	if (nouveau_modeset == -1) {
		if (vgacon_text_force())
			nouveau_modeset = 0;
	}

	if (!nouveau_modeset)
		return 0;

#ifdef CONFIG_NOUVEAU_PLATFORM_DRIVER
	platform_driver_register(&nouveau_platform_driver);
#endif

	nouveau_register_dsm_handler();
	nouveau_backlight_ctor();

#ifdef CONFIG_PCI
	return pci_register_driver(&nouveau_drm_pci_driver);
#else
	return 0;
#endif
}

static void __exit
nouveau_drm_exit(void)
{
	if (!nouveau_modeset)
		return;

#ifdef CONFIG_PCI
	pci_unregister_driver(&nouveau_drm_pci_driver);
#endif
	nouveau_backlight_dtor();
	nouveau_unregister_dsm_handler();

#ifdef CONFIG_NOUVEAU_PLATFORM_DRIVER
	platform_driver_unregister(&nouveau_platform_driver);
#endif
}

module_init(nouveau_drm_init);
module_exit(nouveau_drm_exit);

MODULE_DEVICE_TABLE(pci, nouveau_drm_pci_table);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
