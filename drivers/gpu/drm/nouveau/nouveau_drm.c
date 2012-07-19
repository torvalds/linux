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

#include <linux/module.h>
#include <linux/pci.h>

#include <core/device.h>
#include <core/client.h>
#include <core/gpuobj.h>
#include <core/class.h>

#include <subdev/device.h>
#include <subdev/vm.h>

#include "nouveau_drm.h"
#include "nouveau_dma.h"
#include "nouveau_agp.h"
#include "nouveau_abi16.h"
#include "nouveau_fbcon.h"
#include "nouveau_fence.h"

#include "nouveau_ttm.h"

int __devinit nouveau_pci_probe(struct pci_dev *, const struct pci_device_id *);
void nouveau_pci_remove(struct pci_dev *);
int nouveau_pci_suspend(struct pci_dev *, pm_message_t);
int nouveau_pci_resume(struct pci_dev *);
int __init nouveau_init(struct pci_driver *);
void __exit nouveau_exit(struct pci_driver *);

int nouveau_load(struct drm_device *, unsigned long);
int nouveau_unload(struct drm_device *);

MODULE_PARM_DESC(config, "option string to pass to driver core");
static char *nouveau_config;
module_param_named(config, nouveau_config, charp, 0400);

MODULE_PARM_DESC(debug, "debug string to pass to driver core");
static char *nouveau_debug;
module_param_named(debug, nouveau_debug, charp, 0400);

MODULE_PARM_DESC(noaccel, "disable kernel/abi16 acceleration");
static int nouveau_noaccel = 0;
module_param_named(noaccel, nouveau_noaccel, int, 0400);

static u64
nouveau_name(struct pci_dev *pdev)
{
	u64 name = (u64)pci_domain_nr(pdev->bus) << 32;
	name |= pdev->bus->number << 16;
	name |= PCI_SLOT(pdev->devfn) << 8;
	return name | PCI_FUNC(pdev->devfn);
}

static int
nouveau_cli_create(struct pci_dev *pdev, u32 name, int size, void **pcli)
{
	struct nouveau_cli *cli;
	int ret;

	ret = nouveau_client_create_(name, nouveau_name(pdev), nouveau_config,
				     nouveau_debug, size, pcli);
	cli = *pcli;
	if (ret)
		return ret;

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
	if (drm->fence)
		nouveau_fence(drm)->dtor(drm);
}

static void
nouveau_accel_init(struct nouveau_drm *drm)
{
	struct nouveau_device *device = nv_device(drm->device);
	struct nouveau_object *object;
	int ret;

	if (nouveau_noaccel)
		return;

	/* initialise synchronisation routines */
	if      (device->card_type < NV_10) ret = nv04_fence_create(drm);
	else if (device->chipset   <  0x84) ret = nv10_fence_create(drm);
	else if (device->card_type < NV_C0) ret = nv84_fence_create(drm);
	else                                ret = nvc0_fence_create(drm);
	if (ret) {
		NV_ERROR(drm, "failed to initialise sync subsystem, %d\n", ret);
		nouveau_accel_fini(drm);
		return;
	}

	ret = nouveau_channel_new(drm, &drm->client, NVDRM_DEVICE, NVDRM_CHAN,
				  NvDmaFB, NvDmaTT, &drm->channel);
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


	nouveau_bo_move_init(drm->channel);
}

static int __devinit
nouveau_drm_probe(struct pci_dev *pdev, const struct pci_device_id *pent)
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

	ret = nouveau_device_create(pdev, nouveau_name(pdev), pci_name(pdev),
				    nouveau_config, nouveau_debug, &device);
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = nouveau_pci_probe(pdev, pent);
	if (ret) {
		nouveau_object_ref(NULL, (struct nouveau_object **)&device);
		return ret;
	}

	return 0;
}

int
nouveau_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct pci_dev *pdev = dev->pdev;
	struct nouveau_device *device;
	struct nouveau_drm *drm;
	int ret;

	ret = nouveau_cli_create(pdev, 0, sizeof(*drm), (void**)&drm);
	dev->dev_private = drm;
	if (ret)
		return ret;

	INIT_LIST_HEAD(&drm->clients);
	spin_lock_init(&drm->tile.lock);
	drm->dev = dev;

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

	device = nv_device(drm->device);

	/* initialise AGP */
	nouveau_agp_init(drm);

	if (device->card_type >= NV_50) {
		ret = nouveau_vm_new(nv_device(drm->device), 0, (1ULL << 40),
				     0x1000, &drm->client.base.vm);
		if (ret)
			goto fail_device;
	}

	ret = nouveau_ttm_init(drm);
	if (ret)
		goto fail_device;

	ret = nouveau_load(dev, flags);
	if (ret)
		goto fail_load;

	nouveau_accel_init(drm);
	nouveau_fbcon_init(dev);
	return 0;

fail_load:
	nouveau_ttm_fini(drm);
fail_device:
	nouveau_cli_destroy(&drm->client);
	return ret;
}

int
nouveau_drm_unload(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct pci_dev *pdev = dev->pdev;
	int ret;

	nouveau_fbcon_fini(dev);
	nouveau_accel_fini(drm);

	ret = nouveau_unload(dev);
	if (ret)
		return ret;

	nouveau_ttm_fini(drm);
	nouveau_agp_fini(drm);

	pci_set_drvdata(pdev, drm->client.base.device);
	nouveau_cli_destroy(&drm->client);
	return 0;
}

static void
nouveau_drm_remove(struct pci_dev *pdev)
{
	struct nouveau_object *device;
	nouveau_pci_remove(pdev);
	device = pci_get_drvdata(pdev);
	nouveau_object_ref(NULL, &device);
	nouveau_object_debug();
}

int
nouveau_drm_suspend(struct pci_dev *pdev, pm_message_t pm_state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_cli *cli;
	int ret;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF ||
	    pm_state.event == PM_EVENT_PRETHAW)
		return 0;

	NV_INFO(drm, "suspending fbcon...\n");
	nouveau_fbcon_set_suspend(dev, 1);

	NV_INFO(drm, "suspending drm...\n");
	ret = nouveau_pci_suspend(pdev, pm_state);
	if (ret)
		return ret;

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

	pci_save_state(pdev);
	if (pm_state.event == PM_EVENT_SUSPEND) {
		pci_disable_device(pdev);
		pci_set_power_state(pdev, PCI_D3hot);
	}

	return 0;

fail_client:
	list_for_each_entry_continue_reverse(cli, &drm->clients, head) {
		nouveau_client_init(&cli->base);
	}

	nouveau_pci_resume(pdev);
	return ret;
}

int
nouveau_drm_resume(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_cli *cli;
	int ret;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	NV_INFO(drm, "re-enabling device...\n");
	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	pci_set_master(pdev);

	nouveau_agp_reset(drm);

	NV_INFO(drm, "resuming client object trees...\n");
	nouveau_client_init(&drm->client.base);
	nouveau_agp_init(drm);

	list_for_each_entry(cli, &drm->clients, head) {
		nouveau_client_init(&cli->base);
	}

	if (drm->fence && nouveau_fence(drm)->resume)
		nouveau_fence(drm)->resume(drm);

	return nouveau_pci_resume(pdev);
}

int
nouveau_drm_open(struct drm_device *dev, struct drm_file *fpriv)
{
	struct pci_dev *pdev = dev->pdev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_cli *cli;
	int ret;

	ret = nouveau_cli_create(pdev, fpriv->pid, sizeof(*cli), (void **)&cli);
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

void
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

void
nouveau_drm_postclose(struct drm_device *dev, struct drm_file *fpriv)
{
	struct nouveau_cli *cli = nouveau_cli(fpriv);
	nouveau_cli_destroy(cli);
}

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

static struct pci_driver
nouveau_drm_pci_driver = {
	.name = "nouveau",
	.id_table = nouveau_drm_pci_table,
	.probe = nouveau_drm_probe,
	.remove = nouveau_drm_remove,
	.suspend = nouveau_drm_suspend,
	.resume = nouveau_drm_resume,
};

static int __init
nouveau_drm_init(void)
{
	return nouveau_init(&nouveau_drm_pci_driver);
}

static void __exit
nouveau_drm_exit(void)
{
	nouveau_exit(&nouveau_drm_pci_driver);
}

module_init(nouveau_drm_init);
module_exit(nouveau_drm_exit);

MODULE_DEVICE_TABLE(pci, nouveau_drm_pci_table);
MODULE_AUTHOR("Nouveau Project");
MODULE_DESCRIPTION("nVidia Riva/TNT/GeForce/Quadro/Tesla");
MODULE_LICENSE("GPL and additional rights");
