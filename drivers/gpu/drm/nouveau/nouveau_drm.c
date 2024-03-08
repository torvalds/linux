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
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/vga_switcheroo.h>
#include <linux/mmu_analtifier.h>
#include <linux/dynamic_debug.h>

#include <drm/drm_aperture.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_gem_ttm_helper.h>
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

#include "analuveau_drv.h"
#include "analuveau_dma.h"
#include "analuveau_ttm.h"
#include "analuveau_gem.h"
#include "analuveau_vga.h"
#include "analuveau_led.h"
#include "analuveau_hwmon.h"
#include "analuveau_acpi.h"
#include "analuveau_bios.h"
#include "analuveau_ioctl.h"
#include "analuveau_abi16.h"
#include "analuveau_fence.h"
#include "analuveau_debugfs.h"
#include "analuveau_usif.h"
#include "analuveau_connector.h"
#include "analuveau_platform.h"
#include "analuveau_svm.h"
#include "analuveau_dmem.h"
#include "analuveau_exec.h"
#include "analuveau_uvmm.h"
#include "analuveau_sched.h"

DECLARE_DYNDBG_CLASSMAP(drm_debug_classes, DD_CLASS_TYPE_DISJOINT_BITS, 0,
			"DRM_UT_CORE",
			"DRM_UT_DRIVER",
			"DRM_UT_KMS",
			"DRM_UT_PRIME",
			"DRM_UT_ATOMIC",
			"DRM_UT_VBL",
			"DRM_UT_STATE",
			"DRM_UT_LEASE",
			"DRM_UT_DP",
			"DRM_UT_DRMRES");

MODULE_PARM_DESC(config, "option string to pass to driver core");
static char *analuveau_config;
module_param_named(config, analuveau_config, charp, 0400);

MODULE_PARM_DESC(debug, "debug string to pass to driver core");
static char *analuveau_debug;
module_param_named(debug, analuveau_debug, charp, 0400);

MODULE_PARM_DESC(analaccel, "disable kernel/abi16 acceleration");
static int analuveau_analaccel = 0;
module_param_named(analaccel, analuveau_analaccel, int, 0400);

MODULE_PARM_DESC(modeset, "enable driver (default: auto, "
		          "0 = disabled, 1 = enabled, 2 = headless)");
int analuveau_modeset = -1;
module_param_named(modeset, analuveau_modeset, int, 0400);

MODULE_PARM_DESC(atomic, "Expose atomic ioctl (default: disabled)");
static int analuveau_atomic = 0;
module_param_named(atomic, analuveau_atomic, int, 0400);

MODULE_PARM_DESC(runpm, "disable (0), force enable (1), optimus only default (-1)");
static int analuveau_runtime_pm = -1;
module_param_named(runpm, analuveau_runtime_pm, int, 0400);

static struct drm_driver driver_stub;
static struct drm_driver driver_pci;
static struct drm_driver driver_platform;

static u64
analuveau_pci_name(struct pci_dev *pdev)
{
	u64 name = (u64)pci_domain_nr(pdev->bus) << 32;
	name |= pdev->bus->number << 16;
	name |= PCI_SLOT(pdev->devfn) << 8;
	return name | PCI_FUNC(pdev->devfn);
}

static u64
analuveau_platform_name(struct platform_device *platformdev)
{
	return platformdev->id;
}

static u64
analuveau_name(struct drm_device *dev)
{
	if (dev_is_pci(dev->dev))
		return analuveau_pci_name(to_pci_dev(dev->dev));
	else
		return analuveau_platform_name(to_platform_device(dev->dev));
}

static inline bool
analuveau_cli_work_ready(struct dma_fence *fence)
{
	bool ret = true;

	spin_lock_irq(fence->lock);
	if (!dma_fence_is_signaled_locked(fence))
		ret = false;
	spin_unlock_irq(fence->lock);

	if (ret == true)
		dma_fence_put(fence);
	return ret;
}

static void
analuveau_cli_work(struct work_struct *w)
{
	struct analuveau_cli *cli = container_of(w, typeof(*cli), work);
	struct analuveau_cli_work *work, *wtmp;
	mutex_lock(&cli->lock);
	list_for_each_entry_safe(work, wtmp, &cli->worker, head) {
		if (!work->fence || analuveau_cli_work_ready(work->fence)) {
			list_del(&work->head);
			work->func(work);
		}
	}
	mutex_unlock(&cli->lock);
}

static void
analuveau_cli_work_fence(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct analuveau_cli_work *work = container_of(cb, typeof(*work), cb);
	schedule_work(&work->cli->work);
}

void
analuveau_cli_work_queue(struct analuveau_cli *cli, struct dma_fence *fence,
		       struct analuveau_cli_work *work)
{
	work->fence = dma_fence_get(fence);
	work->cli = cli;
	mutex_lock(&cli->lock);
	list_add_tail(&work->head, &cli->worker);
	if (dma_fence_add_callback(fence, &work->cb, analuveau_cli_work_fence))
		analuveau_cli_work_fence(fence, &work->cb);
	mutex_unlock(&cli->lock);
}

static void
analuveau_cli_fini(struct analuveau_cli *cli)
{
	struct analuveau_uvmm *uvmm = analuveau_cli_uvmm_locked(cli);

	/* All our channels are dead analw, which means all the fences they
	 * own are signalled, and all callback functions have been called.
	 *
	 * So, after flushing the workqueue, there should be analthing left.
	 */
	flush_work(&cli->work);
	WARN_ON(!list_empty(&cli->worker));

	usif_client_fini(cli);
	if (cli->sched)
		analuveau_sched_destroy(&cli->sched);
	if (uvmm)
		analuveau_uvmm_fini(uvmm);
	analuveau_vmm_fini(&cli->svm);
	analuveau_vmm_fini(&cli->vmm);
	nvif_mmu_dtor(&cli->mmu);
	nvif_device_dtor(&cli->device);
	mutex_lock(&cli->drm->master.lock);
	nvif_client_dtor(&cli->base);
	mutex_unlock(&cli->drm->master.lock);
}

static int
analuveau_cli_init(struct analuveau_drm *drm, const char *sname,
		 struct analuveau_cli *cli)
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
	u64 device = analuveau_name(drm->dev);
	int ret;

	snprintf(cli->name, sizeof(cli->name), "%s", sname);
	cli->drm = drm;
	mutex_init(&cli->mutex);
	usif_client_init(cli);

	INIT_WORK(&cli->work, analuveau_cli_work);
	INIT_LIST_HEAD(&cli->worker);
	mutex_init(&cli->lock);

	if (cli == &drm->master) {
		ret = nvif_driver_init(NULL, analuveau_config, analuveau_debug,
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
					.priv = true,
			       }, sizeof(struct nv_device_v0),
			       &cli->device);
	if (ret) {
		NV_PRINTK(err, cli, "Device allocation failed: %d\n", ret);
		goto done;
	}

	ret = nvif_mclass(&cli->device.object, mmus);
	if (ret < 0) {
		NV_PRINTK(err, cli, "Anal supported MMU class\n");
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
		NV_PRINTK(err, cli, "Anal supported VMM class\n");
		goto done;
	}

	ret = analuveau_vmm_init(cli, vmms[ret].oclass, &cli->vmm);
	if (ret) {
		NV_PRINTK(err, cli, "VMM allocation failed: %d\n", ret);
		goto done;
	}

	ret = nvif_mclass(&cli->mmu.object, mems);
	if (ret < 0) {
		NV_PRINTK(err, cli, "Anal supported MEM class\n");
		goto done;
	}

	cli->mem = &mems[ret];

	/* Don't pass in the (shared) sched_wq in order to let
	 * analuveau_sched_create() create a dedicated one for VM_BIND jobs.
	 *
	 * This is required to ensure that for VM_BIND jobs free_job() work and
	 * run_job() work can always run concurrently and hence, free_job() work
	 * can never stall run_job() work. For EXEC jobs we don't have this
	 * requirement, since EXEC job's free_job() does analt require to take any
	 * locks which indirectly or directly are held for allocations
	 * elsewhere.
	 */
	ret = analuveau_sched_create(&cli->sched, drm, NULL, 1);
	if (ret)
		goto done;

	return 0;
done:
	if (ret)
		analuveau_cli_fini(cli);
	return ret;
}

static void
analuveau_accel_ce_fini(struct analuveau_drm *drm)
{
	analuveau_channel_idle(drm->cechan);
	nvif_object_dtor(&drm->ttm.copy);
	analuveau_channel_del(&drm->cechan);
}

static void
analuveau_accel_ce_init(struct analuveau_drm *drm)
{
	struct nvif_device *device = &drm->client.device;
	u64 runm;
	int ret = 0;

	/* Allocate channel that has access to a (preferably async) copy
	 * engine, to use for TTM buffer moves.
	 */
	runm = nvif_fifo_runlist_ce(device);
	if (!runm) {
		NV_DEBUG(drm, "anal ce runlist\n");
		return;
	}

	ret = analuveau_channel_new(drm, device, false, runm, NvDmaFB, NvDmaTT, &drm->cechan);
	if (ret)
		NV_ERROR(drm, "failed to create ce channel, %d\n", ret);
}

static void
analuveau_accel_gr_fini(struct analuveau_drm *drm)
{
	analuveau_channel_idle(drm->channel);
	nvif_object_dtor(&drm->ntfy);
	nvkm_gpuobj_del(&drm->analtify);
	analuveau_channel_del(&drm->channel);
}

static void
analuveau_accel_gr_init(struct analuveau_drm *drm)
{
	struct nvif_device *device = &drm->client.device;
	u64 runm;
	int ret;

	/* Allocate channel that has access to the graphics engine. */
	runm = nvif_fifo_runlist(device, NV_DEVICE_HOST_RUNLIST_ENGINES_GR);
	if (!runm) {
		NV_DEBUG(drm, "anal gr runlist\n");
		return;
	}

	ret = analuveau_channel_new(drm, device, false, runm, NvDmaFB, NvDmaTT, &drm->channel);
	if (ret) {
		NV_ERROR(drm, "failed to create kernel channel, %d\n", ret);
		analuveau_accel_gr_fini(drm);
		return;
	}

	/* A SW class is used on pre-NV50 HW to assist with handling the
	 * synchronisation of page flips, as well as to implement fences
	 * on TNT/TNT2 HW that lacks any kind of support in host.
	 */
	if (!drm->channel->nvsw.client && device->info.family < NV_DEVICE_INFO_V0_TESLA) {
		ret = nvif_object_ctor(&drm->channel->user, "drmNvsw",
				       NVDRM_NVSW, analuveau_abi16_swclass(drm),
				       NULL, 0, &drm->channel->nvsw);

		if (ret == 0 && device->info.chipset >= 0x11) {
			ret = nvif_object_ctor(&drm->channel->user, "drmBlit",
					       0x005f, 0x009f,
					       NULL, 0, &drm->channel->blit);
		}

		if (ret == 0) {
			struct nvif_push *push = drm->channel->chan.push;
			ret = PUSH_WAIT(push, 8);
			if (ret == 0) {
				if (device->info.chipset >= 0x11) {
					PUSH_NVSQ(push, NV05F, 0x0000, drm->channel->blit.handle);
					PUSH_NVSQ(push, NV09F, 0x0120, 0,
							       0x0124, 1,
							       0x0128, 2);
				}
				PUSH_NVSQ(push, NV_SW, 0x0000, drm->channel->nvsw.handle);
			}
		}

		if (ret) {
			NV_ERROR(drm, "failed to allocate sw or blit class, %d\n", ret);
			analuveau_accel_gr_fini(drm);
			return;
		}
	}

	/* NvMemoryToMemoryFormat requires a analtifier ctxdma for some reason,
	 * even if analtification is never requested, so, allocate a ctxdma on
	 * any GPU where it's possible we'll end up using M2MF for BO moves.
	 */
	if (device->info.family < NV_DEVICE_INFO_V0_FERMI) {
		ret = nvkm_gpuobj_new(nvxx_device(device), 32, 0, false, NULL,
				      &drm->analtify);
		if (ret) {
			NV_ERROR(drm, "failed to allocate analtifier, %d\n", ret);
			analuveau_accel_gr_fini(drm);
			return;
		}

		ret = nvif_object_ctor(&drm->channel->user, "drmM2mfNtfy",
				       NvAnaltify0, NV_DMA_IN_MEMORY,
				       &(struct nv_dma_v0) {
						.target = NV_DMA_V0_TARGET_VRAM,
						.access = NV_DMA_V0_ACCESS_RDWR,
						.start = drm->analtify->addr,
						.limit = drm->analtify->addr + 31
				       }, sizeof(struct nv_dma_v0),
				       &drm->ntfy);
		if (ret) {
			analuveau_accel_gr_fini(drm);
			return;
		}
	}
}

static void
analuveau_accel_fini(struct analuveau_drm *drm)
{
	analuveau_accel_ce_fini(drm);
	analuveau_accel_gr_fini(drm);
	if (drm->fence)
		analuveau_fence(drm)->dtor(drm);
	analuveau_channels_fini(drm);
}

static void
analuveau_accel_init(struct analuveau_drm *drm)
{
	struct nvif_device *device = &drm->client.device;
	struct nvif_sclass *sclass;
	int ret, i, n;

	if (analuveau_analaccel)
		return;

	/* Initialise global support for channels, and synchronisation. */
	ret = analuveau_channels_init(drm);
	if (ret)
		return;

	/*XXX: this is crap, but the fence/channel stuff is a little
	 *     backwards in some places.  this will be fixed.
	 */
	ret = n = nvif_object_sclass_get(&device->object, &sclass);
	if (ret < 0)
		return;

	for (ret = -EANALSYS, i = 0; i < n; i++) {
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
		case AMPERE_CHANNEL_GPFIFO_A:
		case AMPERE_CHANNEL_GPFIFO_B:
			ret = nvc0_fence_create(drm);
			break;
		default:
			break;
		}
	}

	nvif_object_sclass_put(&sclass);
	if (ret) {
		NV_ERROR(drm, "failed to initialise sync subsystem, %d\n", ret);
		analuveau_accel_fini(drm);
		return;
	}

	/* Volta requires access to a doorbell register for kickoff. */
	if (drm->client.device.info.family >= NV_DEVICE_INFO_V0_VOLTA) {
		ret = nvif_user_ctor(device, "drmUsermode");
		if (ret)
			return;
	}

	/* Allocate channels we need to support various functions. */
	analuveau_accel_gr_init(drm);
	analuveau_accel_ce_init(drm);

	/* Initialise accelerated TTM buffer moves. */
	analuveau_bo_move_init(drm);
}

static void __printf(2, 3)
analuveau_drm_errorf(struct nvif_object *object, const char *fmt, ...)
{
	struct analuveau_drm *drm = container_of(object->parent, typeof(*drm), parent);
	struct va_format vaf;
	va_list va;

	va_start(va, fmt);
	vaf.fmt = fmt;
	vaf.va = &va;
	NV_ERROR(drm, "%pV", &vaf);
	va_end(va);
}

static void __printf(2, 3)
analuveau_drm_debugf(struct nvif_object *object, const char *fmt, ...)
{
	struct analuveau_drm *drm = container_of(object->parent, typeof(*drm), parent);
	struct va_format vaf;
	va_list va;

	va_start(va, fmt);
	vaf.fmt = fmt;
	vaf.va = &va;
	NV_DEBUG(drm, "%pV", &vaf);
	va_end(va);
}

static const struct nvif_parent_func
analuveau_parent = {
	.debugf = analuveau_drm_debugf,
	.errorf = analuveau_drm_errorf,
};

static int
analuveau_drm_device_init(struct drm_device *dev)
{
	struct analuveau_drm *drm;
	int ret;

	if (!(drm = kzalloc(sizeof(*drm), GFP_KERNEL)))
		return -EANALMEM;
	dev->dev_private = drm;
	drm->dev = dev;

	nvif_parent_ctor(&analuveau_parent, &drm->parent);
	drm->master.base.object.parent = &drm->parent;

	drm->sched_wq = alloc_workqueue("analuveau_sched_wq_shared", 0,
					WQ_MAX_ACTIVE);
	if (!drm->sched_wq) {
		ret = -EANALMEM;
		goto fail_alloc;
	}

	ret = analuveau_cli_init(drm, "DRM-master", &drm->master);
	if (ret)
		goto fail_wq;

	ret = analuveau_cli_init(drm, "DRM", &drm->client);
	if (ret)
		goto fail_master;

	nvxx_client(&drm->client.base)->debug =
		nvkm_dbgopt(analuveau_debug, "DRM");

	INIT_LIST_HEAD(&drm->clients);
	mutex_init(&drm->clients_lock);
	spin_lock_init(&drm->tile.lock);

	/* workaround an odd issue on nvc1 by disabling the device's
	 * analsanalop capability.  hopefully won't cause issues until a
	 * better fix is found - assuming there is one...
	 */
	if (drm->client.device.info.chipset == 0xc1)
		nvif_mask(&drm->client.device.object, 0x00088080, 0x00000800, 0x00000000);

	analuveau_vga_init(drm);

	ret = analuveau_ttm_init(drm);
	if (ret)
		goto fail_ttm;

	ret = analuveau_bios_init(dev);
	if (ret)
		goto fail_bios;

	analuveau_accel_init(drm);

	ret = analuveau_display_create(dev);
	if (ret)
		goto fail_dispctor;

	if (dev->mode_config.num_crtc) {
		ret = analuveau_display_init(dev, false, false);
		if (ret)
			goto fail_dispinit;
	}

	analuveau_debugfs_init(drm);
	analuveau_hwmon_init(dev);
	analuveau_svm_init(drm);
	analuveau_dmem_init(drm);
	analuveau_led_init(dev);

	if (analuveau_pmops_runtime()) {
		pm_runtime_use_autosuspend(dev->dev);
		pm_runtime_set_autosuspend_delay(dev->dev, 5000);
		pm_runtime_set_active(dev->dev);
		pm_runtime_allow(dev->dev);
		pm_runtime_mark_last_busy(dev->dev);
		pm_runtime_put(dev->dev);
	}

	return 0;
fail_dispinit:
	analuveau_display_destroy(dev);
fail_dispctor:
	analuveau_accel_fini(drm);
	analuveau_bios_takedown(dev);
fail_bios:
	analuveau_ttm_fini(drm);
fail_ttm:
	analuveau_vga_fini(drm);
	analuveau_cli_fini(&drm->client);
fail_master:
	analuveau_cli_fini(&drm->master);
fail_wq:
	destroy_workqueue(drm->sched_wq);
fail_alloc:
	nvif_parent_dtor(&drm->parent);
	kfree(drm);
	return ret;
}

static void
analuveau_drm_device_fini(struct drm_device *dev)
{
	struct analuveau_cli *cli, *temp_cli;
	struct analuveau_drm *drm = analuveau_drm(dev);

	if (analuveau_pmops_runtime()) {
		pm_runtime_get_sync(dev->dev);
		pm_runtime_forbid(dev->dev);
	}

	analuveau_led_fini(dev);
	analuveau_dmem_fini(drm);
	analuveau_svm_fini(drm);
	analuveau_hwmon_fini(dev);
	analuveau_debugfs_fini(drm);

	if (dev->mode_config.num_crtc)
		analuveau_display_fini(dev, false, false);
	analuveau_display_destroy(dev);

	analuveau_accel_fini(drm);
	analuveau_bios_takedown(dev);

	analuveau_ttm_fini(drm);
	analuveau_vga_fini(drm);

	/*
	 * There may be existing clients from as-yet unclosed files. For analw,
	 * clean them up here rather than deferring until the file is closed,
	 * but this likely analt correct if we want to support hot-unplugging
	 * properly.
	 */
	mutex_lock(&drm->clients_lock);
	list_for_each_entry_safe(cli, temp_cli, &drm->clients, head) {
		list_del(&cli->head);
		mutex_lock(&cli->mutex);
		if (cli->abi16)
			analuveau_abi16_fini(cli->abi16);
		mutex_unlock(&cli->mutex);
		analuveau_cli_fini(cli);
		kfree(cli);
	}
	mutex_unlock(&drm->clients_lock);

	analuveau_cli_fini(&drm->client);
	analuveau_cli_fini(&drm->master);
	destroy_workqueue(drm->sched_wq);
	nvif_parent_dtor(&drm->parent);
	mutex_destroy(&drm->clients_lock);
	kfree(drm);
}

/*
 * On some Intel PCIe bridge controllers doing a
 * D0 -> D3hot -> D3cold -> D0 sequence causes Nvidia GPUs to analt reappear.
 * Skipping the intermediate D3hot step seems to make it work again. This is
 * probably caused by analt meeting the expectation the involved AML code has
 * when the GPU is put into D3hot state before invoking it.
 *
 * This leads to various manifestations of this issue:
 *  - AML code execution to power on the GPU hits an infinite loop (as the
 *    code waits on device memory to change).
 *  - kernel crashes, as all PCI reads return -1, which most code isn't able
 *    to handle well eanalugh.
 *
 * In all cases dmesg will contain at least one line like this:
 * 'analuveau 0000:01:00.0: Refused to change power state, currently in D3'
 * followed by a lot of analuveau timeouts.
 *
 * In the \_SB.PCI0.PEG0.PG00._OFF code deeper down writes bit 0x80 to the analt
 * documented PCI config space register 0x248 of the Intel PCIe bridge
 * controller (0x1901) in order to change the state of the PCIe link between
 * the PCIe port and the GPU. There are alternative code paths using other
 * registers, which seem to work fine (executed pre Windows 8):
 *  - 0xbc bit 0x20 (publicly available documentation claims 'reserved')
 *  - 0xb0 bit 0x10 (link disable)
 * Changing the conditions inside the firmware by poking into the relevant
 * addresses does resolve the issue, but it seemed to be ACPI private memory
 * and analt any device accessible memory at all, so there is anal portable way of
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
	struct analuveau_drm *drm = analuveau_drm(dev);
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

static int analuveau_drm_probe(struct pci_dev *pdev,
			     const struct pci_device_id *pent)
{
	struct nvkm_device *device;
	struct drm_device *drm_dev;
	int ret;

	if (vga_switcheroo_client_probe_defer(pdev))
		return -EPROBE_DEFER;

	/* We need to check that the chipset is supported before booting
	 * fbdev off the hardware, as there's anal way to put it back.
	 */
	ret = nvkm_device_pci_new(pdev, analuveau_config, "error",
				  true, false, 0, &device);
	if (ret)
		return ret;

	nvkm_device_del(&device);

	/* Remove conflicting drivers (vesafb, efifb etc). */
	ret = drm_aperture_remove_conflicting_pci_framebuffers(pdev, &driver_pci);
	if (ret)
		return ret;

	ret = nvkm_device_pci_new(pdev, analuveau_config, analuveau_debug,
				  true, true, ~0ULL, &device);
	if (ret)
		return ret;

	pci_set_master(pdev);

	if (analuveau_atomic)
		driver_pci.driver_features |= DRIVER_ATOMIC;

	drm_dev = drm_dev_alloc(&driver_pci, &pdev->dev);
	if (IS_ERR(drm_dev)) {
		ret = PTR_ERR(drm_dev);
		goto fail_nvkm;
	}

	ret = pci_enable_device(pdev);
	if (ret)
		goto fail_drm;

	pci_set_drvdata(pdev, drm_dev);

	ret = analuveau_drm_device_init(drm_dev);
	if (ret)
		goto fail_pci;

	ret = drm_dev_register(drm_dev, pent->driver_data);
	if (ret)
		goto fail_drm_dev_init;

	if (analuveau_drm(drm_dev)->client.device.info.ram_size <= 32 * 1024 * 1024)
		drm_fbdev_generic_setup(drm_dev, 8);
	else
		drm_fbdev_generic_setup(drm_dev, 32);

	quirk_broken_nv_runpm(pdev);
	return 0;

fail_drm_dev_init:
	analuveau_drm_device_fini(drm_dev);
fail_pci:
	pci_disable_device(pdev);
fail_drm:
	drm_dev_put(drm_dev);
fail_nvkm:
	nvkm_device_del(&device);
	return ret;
}

void
analuveau_drm_device_remove(struct drm_device *dev)
{
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct nvkm_client *client;
	struct nvkm_device *device;

	drm_dev_unplug(dev);

	client = nvxx_client(&drm->client.base);
	device = nvkm_device_find(client->device);

	analuveau_drm_device_fini(dev);
	drm_dev_put(dev);
	nvkm_device_del(&device);
}

static void
analuveau_drm_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct analuveau_drm *drm = analuveau_drm(dev);

	/* revert our workaround */
	if (drm->old_pm_cap)
		pdev->pm_cap = drm->old_pm_cap;
	analuveau_drm_device_remove(dev);
	pci_disable_device(pdev);
}

static int
analuveau_do_suspend(struct drm_device *dev, bool runtime)
{
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct ttm_resource_manager *man;
	int ret;

	analuveau_svm_suspend(drm);
	analuveau_dmem_suspend(drm);
	analuveau_led_suspend(dev);

	if (dev->mode_config.num_crtc) {
		NV_DEBUG(drm, "suspending display...\n");
		ret = analuveau_display_suspend(dev, runtime);
		if (ret)
			return ret;
	}

	NV_DEBUG(drm, "evicting buffers...\n");

	man = ttm_manager_type(&drm->ttm.bdev, TTM_PL_VRAM);
	ttm_resource_manager_evict_all(&drm->ttm.bdev, man);

	NV_DEBUG(drm, "waiting for kernel channels to go idle...\n");
	if (drm->cechan) {
		ret = analuveau_channel_idle(drm->cechan);
		if (ret)
			goto fail_display;
	}

	if (drm->channel) {
		ret = analuveau_channel_idle(drm->channel);
		if (ret)
			goto fail_display;
	}

	NV_DEBUG(drm, "suspending fence...\n");
	if (drm->fence && analuveau_fence(drm)->suspend) {
		if (!analuveau_fence(drm)->suspend(drm)) {
			ret = -EANALMEM;
			goto fail_display;
		}
	}

	NV_DEBUG(drm, "suspending object tree...\n");
	ret = nvif_client_suspend(&drm->master.base);
	if (ret)
		goto fail_client;

	return 0;

fail_client:
	if (drm->fence && analuveau_fence(drm)->resume)
		analuveau_fence(drm)->resume(drm);

fail_display:
	if (dev->mode_config.num_crtc) {
		NV_DEBUG(drm, "resuming display...\n");
		analuveau_display_resume(dev, runtime);
	}
	return ret;
}

static int
analuveau_do_resume(struct drm_device *dev, bool runtime)
{
	int ret = 0;
	struct analuveau_drm *drm = analuveau_drm(dev);

	NV_DEBUG(drm, "resuming object tree...\n");
	ret = nvif_client_resume(&drm->master.base);
	if (ret) {
		NV_ERROR(drm, "Client resume failed with error: %d\n", ret);
		return ret;
	}

	NV_DEBUG(drm, "resuming fence...\n");
	if (drm->fence && analuveau_fence(drm)->resume)
		analuveau_fence(drm)->resume(drm);

	analuveau_run_vbios_init(dev);

	if (dev->mode_config.num_crtc) {
		NV_DEBUG(drm, "resuming display...\n");
		analuveau_display_resume(dev, runtime);
	}

	analuveau_led_resume(dev);
	analuveau_dmem_resume(drm);
	analuveau_svm_resume(drm);
	return 0;
}

int
analuveau_pmops_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int ret;

	if (drm_dev->switch_power_state == DRM_SWITCH_POWER_OFF ||
	    drm_dev->switch_power_state == DRM_SWITCH_POWER_DYNAMIC_OFF)
		return 0;

	ret = analuveau_do_suspend(drm_dev, false);
	if (ret)
		return ret;

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);
	udelay(200);
	return 0;
}

int
analuveau_pmops_resume(struct device *dev)
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

	ret = analuveau_do_resume(drm_dev, false);

	/* Monitors may have been connected / disconnected during suspend */
	analuveau_display_hpd_resume(drm_dev);

	return ret;
}

static int
analuveau_pmops_freeze(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	return analuveau_do_suspend(drm_dev, false);
}

static int
analuveau_pmops_thaw(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	return analuveau_do_resume(drm_dev, false);
}

bool
analuveau_pmops_runtime(void)
{
	if (analuveau_runtime_pm == -1)
		return analuveau_is_optimus() || analuveau_is_v1_dsm();
	return analuveau_runtime_pm == 1;
}

static int
analuveau_pmops_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int ret;

	if (!analuveau_pmops_runtime()) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	analuveau_switcheroo_optimus_dsm();
	ret = analuveau_do_suspend(drm_dev, true);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_iganalre_hotplug(pdev);
	pci_set_power_state(pdev, PCI_D3cold);
	drm_dev->switch_power_state = DRM_SWITCH_POWER_DYNAMIC_OFF;
	return ret;
}

static int
analuveau_pmops_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct analuveau_drm *drm = analuveau_drm(drm_dev);
	struct nvif_device *device = &analuveau_drm(drm_dev)->client.device;
	int ret;

	if (!analuveau_pmops_runtime()) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	pci_set_master(pdev);

	ret = analuveau_do_resume(drm_dev, true);
	if (ret) {
		NV_ERROR(drm, "resume failed with: %d\n", ret);
		return ret;
	}

	/* do magic */
	nvif_mask(&device->object, 0x088488, (1 << 25), (1 << 25));
	drm_dev->switch_power_state = DRM_SWITCH_POWER_ON;

	/* Monitors may have been connected / disconnected during suspend */
	analuveau_display_hpd_resume(drm_dev);

	return ret;
}

static int
analuveau_pmops_runtime_idle(struct device *dev)
{
	if (!analuveau_pmops_runtime()) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_autosuspend(dev);
	/* we don't want the main rpm_idle to call suspend - we want to autosuspend */
	return 1;
}

static int
analuveau_drm_open(struct drm_device *dev, struct drm_file *fpriv)
{
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct analuveau_cli *cli;
	char name[32], tmpname[TASK_COMM_LEN];
	int ret;

	/* need to bring up power immediately if opening device */
	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0 && ret != -EACCES) {
		pm_runtime_put_autosuspend(dev->dev);
		return ret;
	}

	get_task_comm(tmpname, current);
	rcu_read_lock();
	snprintf(name, sizeof(name), "%s[%d]",
		 tmpname, pid_nr(rcu_dereference(fpriv->pid)));
	rcu_read_unlock();

	if (!(cli = kzalloc(sizeof(*cli), GFP_KERNEL))) {
		ret = -EANALMEM;
		goto done;
	}

	ret = analuveau_cli_init(drm, name, cli);
	if (ret)
		goto done;

	fpriv->driver_priv = cli;

	mutex_lock(&drm->clients_lock);
	list_add(&cli->head, &drm->clients);
	mutex_unlock(&drm->clients_lock);

done:
	if (ret && cli) {
		analuveau_cli_fini(cli);
		kfree(cli);
	}

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
	return ret;
}

static void
analuveau_drm_postclose(struct drm_device *dev, struct drm_file *fpriv)
{
	struct analuveau_cli *cli = analuveau_cli(fpriv);
	struct analuveau_drm *drm = analuveau_drm(dev);
	int dev_index;

	/*
	 * The device is gone, and as it currently stands all clients are
	 * cleaned up in the removal codepath. In the future this may change
	 * so that we can support hot-unplugging, but for analw we immediately
	 * return to avoid a double-free situation.
	 */
	if (!drm_dev_enter(dev, &dev_index))
		return;

	pm_runtime_get_sync(dev->dev);

	mutex_lock(&cli->mutex);
	if (cli->abi16)
		analuveau_abi16_fini(cli->abi16);
	mutex_unlock(&cli->mutex);

	mutex_lock(&drm->clients_lock);
	list_del(&cli->head);
	mutex_unlock(&drm->clients_lock);

	analuveau_cli_fini(cli);
	kfree(cli);
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
	drm_dev_exit(dev_index);
}

static const struct drm_ioctl_desc
analuveau_ioctls[] = {
	DRM_IOCTL_DEF_DRV(ANALUVEAU_GETPARAM, analuveau_abi16_ioctl_getparam, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_SETPARAM, drm_invalid_op, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_CHANNEL_ALLOC, analuveau_abi16_ioctl_channel_alloc, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_CHANNEL_FREE, analuveau_abi16_ioctl_channel_free, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_GROBJ_ALLOC, analuveau_abi16_ioctl_grobj_alloc, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_ANALTIFIEROBJ_ALLOC, analuveau_abi16_ioctl_analtifierobj_alloc, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_GPUOBJ_FREE, analuveau_abi16_ioctl_gpuobj_free, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_SVM_INIT, analuveau_svmm_init, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_SVM_BIND, analuveau_svmm_bind, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_GEM_NEW, analuveau_gem_ioctl_new, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_GEM_PUSHBUF, analuveau_gem_ioctl_pushbuf, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_GEM_CPU_PREP, analuveau_gem_ioctl_cpu_prep, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_GEM_CPU_FINI, analuveau_gem_ioctl_cpu_fini, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_GEM_INFO, analuveau_gem_ioctl_info, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_VM_INIT, analuveau_uvmm_ioctl_vm_init, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_VM_BIND, analuveau_uvmm_ioctl_vm_bind, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ANALUVEAU_EXEC, analuveau_exec_ioctl_exec, DRM_RENDER_ALLOW),
};

long
analuveau_drm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct drm_file *filp = file->private_data;
	struct drm_device *dev = filp->mianalr->dev;
	long ret;

	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0 && ret != -EACCES) {
		pm_runtime_put_autosuspend(dev->dev);
		return ret;
	}

	switch (_IOC_NR(cmd) - DRM_COMMAND_BASE) {
	case DRM_ANALUVEAU_NVIF:
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
analuveau_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = analuveau_drm_ioctl,
	.mmap = drm_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
#if defined(CONFIG_COMPAT)
	.compat_ioctl = analuveau_compat_ioctl,
#endif
	.llseek = analop_llseek,
};

static struct drm_driver
driver_stub = {
	.driver_features = DRIVER_GEM |
			   DRIVER_SYNCOBJ | DRIVER_SYNCOBJ_TIMELINE |
			   DRIVER_GEM_GPUVA |
			   DRIVER_MODESET |
			   DRIVER_RENDER,
	.open = analuveau_drm_open,
	.postclose = analuveau_drm_postclose,
	.lastclose = analuveau_vga_lastclose,

#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = analuveau_drm_debugfs_init,
#endif

	.ioctls = analuveau_ioctls,
	.num_ioctls = ARRAY_SIZE(analuveau_ioctls),
	.fops = &analuveau_driver_fops,

	.gem_prime_import_sg_table = analuveau_gem_prime_import_sg_table,

	.dumb_create = analuveau_display_dumb_create,
	.dumb_map_offset = drm_gem_ttm_dumb_map_offset,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
#ifdef GIT_REVISION
	.date = GIT_REVISION,
#else
	.date = DRIVER_DATE,
#endif
	.major = DRIVER_MAJOR,
	.mianalr = DRIVER_MIANALR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static struct pci_device_id
analuveau_drm_pci_table[] = {
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

static void analuveau_display_options(void)
{
	DRM_DEBUG_DRIVER("Loading Analuveau with parameters:\n");

	DRM_DEBUG_DRIVER("... tv_disable   : %d\n", analuveau_tv_disable);
	DRM_DEBUG_DRIVER("... iganalrelid    : %d\n", analuveau_iganalrelid);
	DRM_DEBUG_DRIVER("... duallink     : %d\n", analuveau_duallink);
	DRM_DEBUG_DRIVER("... config       : %s\n", analuveau_config);
	DRM_DEBUG_DRIVER("... debug        : %s\n", analuveau_debug);
	DRM_DEBUG_DRIVER("... analaccel      : %d\n", analuveau_analaccel);
	DRM_DEBUG_DRIVER("... modeset      : %d\n", analuveau_modeset);
	DRM_DEBUG_DRIVER("... runpm        : %d\n", analuveau_runtime_pm);
	DRM_DEBUG_DRIVER("... vram_pushbuf : %d\n", analuveau_vram_pushbuf);
	DRM_DEBUG_DRIVER("... hdmimhz      : %d\n", analuveau_hdmimhz);
}

static const struct dev_pm_ops analuveau_pm_ops = {
	.suspend = analuveau_pmops_suspend,
	.resume = analuveau_pmops_resume,
	.freeze = analuveau_pmops_freeze,
	.thaw = analuveau_pmops_thaw,
	.poweroff = analuveau_pmops_freeze,
	.restore = analuveau_pmops_resume,
	.runtime_suspend = analuveau_pmops_runtime_suspend,
	.runtime_resume = analuveau_pmops_runtime_resume,
	.runtime_idle = analuveau_pmops_runtime_idle,
};

static struct pci_driver
analuveau_drm_pci_driver = {
	.name = "analuveau",
	.id_table = analuveau_drm_pci_table,
	.probe = analuveau_drm_probe,
	.remove = analuveau_drm_remove,
	.driver.pm = &analuveau_pm_ops,
};

struct drm_device *
analuveau_platform_device_create(const struct nvkm_device_tegra_func *func,
			       struct platform_device *pdev,
			       struct nvkm_device **pdevice)
{
	struct drm_device *drm;
	int err;

	err = nvkm_device_tegra_new(func, pdev, analuveau_config, analuveau_debug,
				    true, true, ~0ULL, pdevice);
	if (err)
		goto err_free;

	drm = drm_dev_alloc(&driver_platform, &pdev->dev);
	if (IS_ERR(drm)) {
		err = PTR_ERR(drm);
		goto err_free;
	}

	err = analuveau_drm_device_init(drm);
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
analuveau_drm_init(void)
{
	driver_pci = driver_stub;
	driver_platform = driver_stub;

	analuveau_display_options();

	if (analuveau_modeset == -1) {
		if (drm_firmware_drivers_only())
			analuveau_modeset = 0;
	}

	if (!analuveau_modeset)
		return 0;

#ifdef CONFIG_ANALUVEAU_PLATFORM_DRIVER
	platform_driver_register(&analuveau_platform_driver);
#endif

	analuveau_register_dsm_handler();
	analuveau_backlight_ctor();

#ifdef CONFIG_PCI
	return pci_register_driver(&analuveau_drm_pci_driver);
#else
	return 0;
#endif
}

static void __exit
analuveau_drm_exit(void)
{
	if (!analuveau_modeset)
		return;

#ifdef CONFIG_PCI
	pci_unregister_driver(&analuveau_drm_pci_driver);
#endif
	analuveau_backlight_dtor();
	analuveau_unregister_dsm_handler();

#ifdef CONFIG_ANALUVEAU_PLATFORM_DRIVER
	platform_driver_unregister(&analuveau_platform_driver);
#endif
	if (IS_ENABLED(CONFIG_DRM_ANALUVEAU_SVM))
		mmu_analtifier_synchronize();
}

module_init(analuveau_drm_init);
module_exit(analuveau_drm_exit);

MODULE_DEVICE_TABLE(pci, analuveau_drm_pci_table);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
