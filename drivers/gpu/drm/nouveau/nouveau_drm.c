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
#include <linux/mmu_notifier.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_vblank.h>

#include <core/gpuobj.h>
#include <core/option.h>
#include <core/pci.h>
#include <core/tegra.h>

#include <nvif/driver.h>
#include <nvif/fifo.h>
#include <nvif/push006c.h>
#include <nvif/user.h>

#include <nvif/class.h>
#include <nvif/cl0002.h>
#include <nvif/cla06f.h>

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
#include "nouveau_svm.h"
#include "nouveau_dmem.h"

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
	nouveau_vmm_fini(&cli->svm);
	nouveau_vmm_fini(&cli->vmm);
	nvif_mmu_dtor(&cli->mmu);
	nvif_device_dtor(&cli->device);
	mutex_lock(&cli->drm->master.lock);
	nvif_client_dtor(&cli->base);
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
		ret = nvif_client_ctor(&drm->master.base, cli->name, device,
				       &cli->base);
		mutex_unlock(&drm->master.lock);
	}
	if (ret) {
		NV_PRINTK(err, cli, "Client allocation failed: %d\n", ret);
		goto done;
	}

	ret = nvif_device_ctor(&cli->base.object, "drmDevice", 0, NV_DEVICE,
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

	ret = nvif_mmu_ctor(&cli->device.object, "drmMmu", mmus[ret].oclass,
			    &cli->mmu);
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
nouveau_accel_ce_fini(struct nouveau_drm *drm)
{
	nouveau_channel_idle(drm->cechan);
	nvif_object_dtor(&drm->ttm.copy);
	nouveau_channel_del(&drm->cechan);
}

static void
nouveau_accel_ce_init(struct nouveau_drm *drm)
{
	struct nvif_device *device = &drm->client.device;
	int ret = 0;

	/* Allocate channel that has access to a (preferably async) copy
	 * engine, to use for TTM buffer moves.
	 */
	if (device->info.family >= NV_DEVICE_INFO_V0_KEPLER) {
		ret = nouveau_channel_new(drm, device,
					  nvif_fifo_runlist_ce(device), 0,
					  true, &drm->cechan);
	} else
	if (device->info.chipset >= 0xa3 &&
	    device->info.chipset != 0xaa &&
	    device->info.chipset != 0xac) {
		/* Prior to Kepler, there's only a single runlist, so all
		 * engines can be accessed from any channel.
		 *
		 * We still want to use a separate channel though.
		 */
		ret = nouveau_channel_new(drm, device, NvDmaFB, NvDmaTT, false,
					  &drm->cechan);
	}

	if (ret)
		NV_ERROR(drm, "failed to create ce channel, %d\n", ret);
}

static void
nouveau_accel_gr_fini(struct nouveau_drm *drm)
{
	nouveau_channel_idle(drm->channel);
	nvif_object_dtor(&drm->ntfy);
	nvkm_gpuobj_del(&drm->notify);
	nouveau_channel_del(&drm->channel);
}

static void
nouveau_accel_gr_init(struct nouveau_drm *drm)
{
	struct nvif_device *device = &drm->client.device;
	u32 arg0, arg1;
	int ret;

	/* Allocate channel that has access to the graphics engine. */
	if (device->info.family >= NV_DEVICE_INFO_V0_KEPLER) {
		arg0 = nvif_fifo_runlist(device, NV_DEVICE_INFO_ENGINE_GR);
		arg1 = 1;
	} else {
		arg0 = NvDmaFB;
		arg1 = NvDmaTT;
	}

	ret = nouveau_channel_new(drm, device, arg0, arg1, false,
				  &drm->channel);
	if (ret) {
		NV_ERROR(drm, "failed to create kernel channel, %d\n", ret);
		nouveau_accel_gr_fini(drm);
		return;
	}

	/* A SW class is used on pre-NV50 HW to assist with handling the
	 * synchronisation of page flips, as well as to implement fences
	 * on TNT/TNT2 HW that lacks any kind of support in host.
	 */
	if (!drm->channel->nvsw.client && device->info.family < NV_DEVICE_INFO_V0_TESLA) {
		ret = nvif_object_ctor(&drm->channel->user, "drmNvsw",
				       NVDRM_NVSW, nouveau_abi16_swclass(drm),
				       NULL, 0, &drm->channel->nvsw);
		if (ret == 0) {
			struct nvif_push *push = drm->channel->chan.push;
			ret = PUSH_WAIT(push, 2);
			if (ret == 0)
				PUSH_NVSQ(push, NV_SW, 0x0000, drm->channel->nvsw.handle);
		}

		if (ret) {
			NV_ERROR(drm, "failed to allocate sw class, %d\n", ret);
			nouveau_accel_gr_fini(drm);
			return;
		}
	}

	/* NvMemoryToMemoryFormat requires a notifier ctxdma for some reason,
	 * even if notification is never requested, so, allocate a ctxdma on
	 * any GPU where it's possible we'll end up using M2MF for BO moves.
	 */
	if (device->info.family < NV_DEVICE_INFO_V0_FERMI) {
		ret = nvkm_gpuobj_new(nvxx_device(device), 32, 0, false, NULL,
				      &drm->notify);
		if (ret) {
			NV_ERROR(drm, "failed to allocate notifier, %d\n", ret);
			nouveau_accel_gr_fini(drm);
			return;
		}

		ret = nvif_object_ctor(&drm->channel->user, "drmM2mfNtfy",
				       NvNotify0, NV_DMA_IN_MEMORY,
				       &(struct nv_dma_v0) {
						.target = NV_DMA_V0_TARGET_VRAM,
						.access = NV_DMA_V0_ACCESS_RDWR,
						.start = drm->notify->addr,
						.limit = drm->notify->addr + 31
				       }, sizeof(struct nv_dma_v0),
				       &drm->ntfy);
		if (ret) {
			nouveau_accel_gr_fini(drm);
			return;
		}
	}
}

static void
nouveau_accel_fini(struct nouveau_drm *drm)
{
	nouveau_accel_ce_fini(drm);
	nouveau_accel_gr_fini(drm);
	if (drm->fence)
		nouveau_fence(drm)->dtor(drm);
}

static void
nouveau_accel_init(struct nouveau_drm *drm)
{
	struct nvif_device *device = &drm->client.device;
	struct nvif_sclass *sclass;
	int ret, i, n;

	if (nouveau_noaccel)
		return;

	/* Initialise global support for channels, and synchronisation. */
	ret = nouveau_channels_init(drm);
	if (ret)
		return;

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
		case TURING_CHANNEL_GPFIFO_A:
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

	/* Volta requires access to a doorbell register for kickoff. */
	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_VOLTA) {
		ret = nvif_user_ctor(device, "drmUsermode");
		if (ret)
			return;
	}

	/* Allocate channels we need to support various functions. */
	nouveau_accel_gr_init(drm);
	nouveau_accel_ce_init(drm);

	/* Initialise accelerated TTM buffer moves. */
	nouveau_bo_move_init(drm);
}

static void __printf(2, 3)
nouveau_drm_errorf(struct nvif_object *object, const char *fmt, ...)
{
	struct nouveau_drm *drm = container_of(object->parent, typeof(*drm), parent);
	struct va_format vaf;
	va_list va;

	va_start(va, fmt);
	vaf.fmt = fmt;
	vaf.va = &va;
	NV_ERROR(drm, "%pV", &vaf);
	va_end(va);
}

static void __printf(2, 3)
nouveau_drm_debugf(struct nvif_object *object, const char *fmt, ...)
{
	struct nouveau_drm *drm = container_of(object->parent, typeof(*drm), parent);
	struct va_format vaf;
	va_list va;

	va_start(va, fmt);
	vaf.fmt = fmt;
	vaf.va = &va;
	NV_DEBUG(drm, "%pV", &vaf);
	va_end(va);
}

static const struct nvif_parent_func
nouveau_parent = {
	.debugf = nouveau_drm_debugf,
	.errorf = nouveau_drm_errorf,
};

static int
nouveau_drm_device_init(struct drm_device *dev)
{
	struct nouveau_drm *drm;
	int ret;

	if (!(drm = kzalloc(sizeof(*drm), GFP_KERNEL)))
		return -ENOMEM;
	dev->dev_private = drm;
	drm->dev = dev;

	nvif_parent_ctor(&nouveau_parent, &drm->parent);
	drm->master.base.object.parent = &drm->parent;

	ret = nouveau_cli_init(drm, "DRM-master", &drm->master);
	if (ret)
		goto fail_alloc;

	ret = nouveau_cli_init(drm, "DRM", &drm->client);
	if (ret)
		goto fail_master;

	dev->irq_enabled = true;

	nvxx_client(&drm->client.base)->debug =
		nvkm_dbgopt(nouveau_debug, "DRM");

	INIT_LIST_HEAD(&drm->clients);
	mutex_init(&drm->clients_lock);
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

	nouveau_accel_init(drm);

	ret = nouveau_display_create(dev);
	if (ret)
		goto fail_dispctor;

	if (dev->mode_config.num_crtc) {
		ret = nouveau_display_init(dev, false, false);
		if (ret)
			goto fail_dispinit;
	}

	nouveau_debugfs_init(drm);
	nouveau_hwmon_init(dev);
	nouveau_svm_init(drm);
	nouveau_dmem_init(drm);
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
	nouveau_accel_fini(drm);
	nouveau_bios_takedown(dev);
fail_bios:
	nouveau_ttm_fini(drm);
fail_ttm:
	nouveau_vga_fini(drm);
	nouveau_cli_fini(&drm->client);
fail_master:
	nouveau_cli_fini(&drm->master);
fail_alloc:
	nvif_parent_dtor(&drm->parent);
	kfree(drm);
	return ret;
}

static void
nouveau_drm_device_fini(struct drm_device *dev)
{
	struct nouveau_cli *cli, *temp_cli;
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (nouveau_pmops_runtime()) {
		pm_runtime_get_sync(dev->dev);
		pm_runtime_forbid(dev->dev);
	}

	nouveau_led_fini(dev);
	nouveau_fbcon_fini(dev);
	nouveau_dmem_fini(drm);
	nouveau_svm_fini(drm);
	nouveau_hwmon_fini(dev);
	nouveau_debugfs_fini(drm);

	if (dev->mode_config.num_crtc)
		nouveau_display_fini(dev, false, false);
	nouveau_display_destroy(dev);

	nouveau_accel_fini(drm);
	nouveau_bios_takedown(dev);

	nouveau_ttm_fini(drm);
	nouveau_vga_fini(drm);

	/*
	 * There may be existing clients from as-yet unclosed files. For now,
	 * clean them up here rather than deferring until the file is closed,
	 * but this likely not correct if we want to support hot-unplugging
	 * properly.
	 */
	mutex_lock(&drm->clients_lock);
	list_for_each_entry_safe(cli, temp_cli, &drm->clients, head) {
		list_del(&cli->head);
		mutex_lock(&cli->mutex);
		if (cli->abi16)
			nouveau_abi16_fini(cli->abi16);
		mutex_unlock(&cli->mutex);
		nouveau_cli_fini(cli);
		kfree(cli);
	}
	mutex_unlock(&drm->clients_lock);

	nouveau_cli_fini(&drm->client);
	nouveau_cli_fini(&drm->master);
	nvif_parent_dtor(&drm->parent);
	mutex_destroy(&drm->clients_lock);
	kfree(drm);
}

/*
 * On some Intel PCIe bridge controllers doing a
 * D0 -> D3hot -> D3cold -> D0 sequence causes Nvidia GPUs to not reappear.
 * Skipping the intermediate D3hot step seems to make it work again. This is
 * probably caused by not meeting the expectation the involved AML code has
 * when the GPU is put into D3hot state before invoking it.
 *
 * This leads to various manifestations of this issue:
 *  - AML code execution to power on the GPU hits an infinite loop (as the
 *    code waits on device memory to change).
 *  - kernel crashes, as all PCI reads return -1, which most code isn't able
 *    to handle well enough.
 *
 * In all cases dmesg will contain at least one line like this:
 * 'nouveau 0000:01:00.0: Refused to change power state, currently in D3'
 * followed by a lot of nouveau timeouts.
 *
 * In the \_SB.PCI0.PEG0.PG00._OFF code deeper down writes bit 0x80 to the not
 * documented PCI config space register 0x248 of the Intel PCIe bridge
 * controller (0x1901) in order to change the state of the PCIe link between
 * the PCIe port and the GPU. There are alternative code paths using other
 * registers, which seem to work fine (executed pre Windows 8):
 *  - 0xbc bit 0x20 (publicly available documentation claims 'reserved')
 *  - 0xb0 bit 0x10 (link disable)
 * Changing the conditions inside the firmware by poking into the relevant
 * addresses does resolve the issue, but it seemed to be ACPI private memory
 * and not any device accessible memory at all, so there is no portable way of
 * changing the conditions.
 * On a XPS 9560 that means bits [0,3] on \CPEX need to be cleared.
 *
 * The only systems where this behavior can be seen are hybrid graphics laptops
 * with a secondary Nvidia Maxwell, Pascal or Turing GPU. It's unclear whether
 * this issue only occurs in combination with listed Intel PCIe bridge
 * controllers and the mentioned GPUs or other devices as well.
 *
 * documentation on the PCIe bridge controller can be found in the
 * "7th Generation Intel® Processor Families for H Platforms Datasheet Volume 2"
 * Section "12 PCI Express* Controller (x16) Registers"
 */

static void quirk_broken_nv_runpm(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct pci_dev *bridge = pci_upstream_bridge(pdev);

	if (!bridge || bridge->vendor != PCI_VENDOR_ID_INTEL)
		return;

	switch (bridge->device) {
	case 0x1901:
		drm->old_pm_cap = pdev->pm_cap;
		pdev->pm_cap = 0;
		NV_INFO(drm, "Disabling PCI power management to avoid bug\n");
		break;
	}
}

static int nouveau_drm_probe(struct pci_dev *pdev,
			     const struct pci_device_id *pent)
{
	struct nvkm_device *device;
	struct drm_device *drm_dev;
	int ret;

	if (vga_switcheroo_client_probe_defer(pdev))
		return -EPROBE_DEFER;

	/* We need to check that the chipset is supported before booting
	 * fbdev off the hardware, as there's no way to put it back.
	 */
	ret = nvkm_device_pci_new(pdev, nouveau_config, "error",
				  true, false, 0, &device);
	if (ret)
		return ret;

	nvkm_device_del(&device);

	/* Remove conflicting drivers (vesafb, efifb etc). */
	ret = drm_fb_helper_remove_conflicting_pci_framebuffers(pdev, "nouveaufb");
	if (ret)
		return ret;

	ret = nvkm_device_pci_new(pdev, nouveau_config, nouveau_debug,
				  true, true, ~0ULL, &device);
	if (ret)
		return ret;

	pci_set_master(pdev);

	if (nouveau_atomic)
		driver_pci.driver_features |= DRIVER_ATOMIC;

	drm_dev = drm_dev_alloc(&driver_pci, &pdev->dev);
	if (IS_ERR(drm_dev)) {
		ret = PTR_ERR(drm_dev);
		goto fail_nvkm;
	}

	ret = pci_enable_device(pdev);
	if (ret)
		goto fail_drm;

	drm_dev->pdev = pdev;
	pci_set_drvdata(pdev, drm_dev);

	ret = nouveau_drm_device_init(drm_dev);
	if (ret)
		goto fail_pci;

	ret = drm_dev_register(drm_dev, pent->driver_data);
	if (ret)
		goto fail_drm_dev_init;

	quirk_broken_nv_runpm(pdev);
	return 0;

fail_drm_dev_init:
	nouveau_drm_device_fini(drm_dev);
fail_pci:
	pci_disable_device(pdev);
fail_drm:
	drm_dev_put(drm_dev);
fail_nvkm:
	nvkm_device_del(&device);
	return ret;
}

void
nouveau_drm_device_remove(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_client *client;
	struct nvkm_device *device;

	drm_dev_unplug(dev);

	dev->irq_enabled = false;
	client = nvxx_client(&drm->client.base);
	device = nvkm_device_find(client->device);

	nouveau_drm_device_fini(dev);
	drm_dev_put(dev);
	nvkm_device_del(&device);
}

static void
nouveau_drm_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct nouveau_drm *drm = nouveau_drm(dev);

	/* revert our workaround */
	if (drm->old_pm_cap)
		pdev->pm_cap = drm->old_pm_cap;
	nouveau_drm_device_remove(dev);
	pci_disable_device(pdev);
}

static int
nouveau_do_suspend(struct drm_device *dev, bool runtime)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	int ret;

	nouveau_svm_suspend(drm);
	nouveau_dmem_suspend(drm);
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
	int ret = 0;
	struct nouveau_drm *drm = nouveau_drm(dev);

	NV_DEBUG(drm, "resuming object tree...\n");
	ret = nvif_client_resume(&drm->master.base);
	if (ret) {
		NV_ERROR(drm, "Client resume failed with error: %d\n", ret);
		return ret;
	}

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
	nouveau_dmem_resume(drm);
	nouveau_svm_resume(drm);
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
	nouveau_display_hpd_resume(drm_dev);

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
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
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
	if (ret) {
		NV_ERROR(drm, "resume failed with: %d\n", ret);
		return ret;
	}

	/* do magic */
	nvif_mask(&device->object, 0x088488, (1 << 25), (1 << 25));
	drm_dev->switch_power_state = DRM_SWITCH_POWER_ON;

	/* Monitors may have been connected / disconnected during suspend */
	nouveau_display_hpd_resume(drm_dev);

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

	mutex_lock(&drm->clients_lock);
	list_add(&cli->head, &drm->clients);
	mutex_unlock(&drm->clients_lock);

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
	int dev_index;

	/*
	 * The device is gone, and as it currently stands all clients are
	 * cleaned up in the removal codepath. In the future this may change
	 * so that we can support hot-unplugging, but for now we immediately
	 * return to avoid a double-free situation.
	 */
	if (!drm_dev_enter(dev, &dev_index))
		return;

	pm_runtime_get_sync(dev->dev);

	mutex_lock(&cli->mutex);
	if (cli->abi16)
		nouveau_abi16_fini(cli->abi16);
	mutex_unlock(&cli->mutex);

	mutex_lock(&drm->clients_lock);
	list_del(&cli->head);
	mutex_unlock(&drm->clients_lock);

	nouveau_cli_fini(cli);
	kfree(cli);
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
	drm_dev_exit(dev_index);
}

static const struct drm_ioctl_desc
nouveau_ioctls[] = {
	DRM_IOCTL_DEF_DRV(NOUVEAU_GETPARAM, nouveau_abi16_ioctl_getparam, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_SETPARAM, drm_invalid_op, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_ALLOC, nouveau_abi16_ioctl_channel_alloc, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_FREE, nouveau_abi16_ioctl_channel_free, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GROBJ_ALLOC, nouveau_abi16_ioctl_grobj_alloc, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_NOTIFIEROBJ_ALLOC, nouveau_abi16_ioctl_notifierobj_alloc, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GPUOBJ_FREE, nouveau_abi16_ioctl_gpuobj_free, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_SVM_INIT, nouveau_svmm_init, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_SVM_BIND, nouveau_svmm_bind, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_NEW, nouveau_gem_ioctl_new, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_PUSHBUF, nouveau_gem_ioctl_pushbuf, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_PREP, nouveau_gem_ioctl_cpu_prep, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_FINI, nouveau_gem_ioctl_cpu_fini, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_INFO, nouveau_gem_ioctl_info, DRM_RENDER_ALLOW),
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
		DRIVER_GEM | DRIVER_MODESET | DRIVER_RENDER
#if defined(CONFIG_NOUVEAU_LEGACY_CTX_SUPPORT)
		| DRIVER_KMS_LEGACY_CONTEXT
#endif
		,

	.open = nouveau_drm_open,
	.postclose = nouveau_drm_postclose,
	.lastclose = nouveau_vga_lastclose,

#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = nouveau_drm_debugfs_init,
#endif

	.ioctls = nouveau_ioctls,
	.num_ioctls = ARRAY_SIZE(nouveau_ioctls),
	.fops = &nouveau_driver_fops,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_pin = nouveau_gem_prime_pin,
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

	err = nouveau_drm_device_init(drm);
	if (err)
		goto err_put;

	platform_set_drvdata(pdev, drm);

	return drm;

err_put:
	drm_dev_put(drm);
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
	if (IS_ENABLED(CONFIG_DRM_NOUVEAU_SVM))
		mmu_notifier_synchronize();
}

module_init(nouveau_drm_init);
module_exit(nouveau_drm_exit);

MODULE_DEVICE_TABLE(pci, nouveau_drm_pci_table);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
