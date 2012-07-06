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
#include <core/class.h>

#include <subdev/device.h>

#include "nouveau_drm.h"

int __devinit nouveau_pci_probe(struct pci_dev *, const struct pci_device_id *);
void nouveau_pci_remove(struct pci_dev *);
int nouveau_pci_suspend(struct pci_dev *, pm_message_t);
int nouveau_pci_resume(struct pci_dev *);
int __init nouveau_init(struct pci_driver *);
void __exit nouveau_exit(struct pci_driver *);

int nouveau_load(struct drm_device *, unsigned long);
int nouveau_unload(struct drm_device *);
void *nouveau_newpriv(struct drm_device *);

MODULE_PARM_DESC(config, "option string to pass to driver core");
static char *nouveau_config;
module_param_named(config, nouveau_config, charp, 0400);

MODULE_PARM_DESC(debug, "debug string to pass to driver core");
static char *nouveau_debug;
module_param_named(debug, nouveau_debug, charp, 0400);

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
	nouveau_client_fini(&cli->base, false);
	atomic_set(&client->refcount, 1);
	nouveau_object_ref(NULL, &client);
}

static int __devinit
nouveau_drm_probe(struct pci_dev *pdev, const struct pci_device_id *pent)
{
	struct nouveau_device *device;
	int ret;

	ret = nouveau_device_create(pdev, nouveau_name(pdev), pci_name(pdev),
				    nouveau_config, nouveau_debug, &device);
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = nouveau_pci_probe(pdev, pent);
	if (ret) {
		nouveau_device_destroy(&device);
		return ret;
	}

	return 0;
}

int
nouveau_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct pci_dev *pdev = dev->pdev;
	struct nouveau_drm *drm;
	int ret;

	ret = nouveau_cli_create(pdev, 0, sizeof(*drm), (void**)&drm);
	dev->dev_private = drm;
	if (ret)
		return ret;

	INIT_LIST_HEAD(&drm->clients);
	drm->dev = dev;

	ret = nouveau_object_new(nv_object(drm), NVDRM_CLIENT, NVDRM_DEVICE,
				 0x0080, &(struct nv_device_class) {
					.device = ~0,
					.disable = 0,
					.debug0 = 0,
				 }, sizeof(struct nv_device_class),
				 &drm->device);
	if (ret)
		goto fail_device;

	ret = nouveau_load(dev, flags);
	if (ret)
		goto fail_device;

	return 0;

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

	ret = nouveau_unload(dev);
	if (ret)
		return ret;

	pci_set_drvdata(pdev, drm->client.base.device);
	nouveau_cli_destroy(&drm->client);
	return 0;
}

static void
nouveau_drm_remove(struct pci_dev *pdev)
{
	struct nouveau_device *device;
	nouveau_pci_remove(pdev);
	device = pci_get_drvdata(pdev);
	nouveau_device_destroy(&device);
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

	ret = nouveau_pci_suspend(pdev, pm_state);
	if (ret)
		return ret;

	list_for_each_entry(cli, &drm->clients, head) {
		ret = nouveau_client_fini(&cli->base, true);
		if (ret)
			goto fail_client;
	}

	ret = nouveau_client_fini(&drm->client.base, true);
	if (ret)
		goto fail_client;

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

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	pci_set_master(pdev);

	nouveau_client_init(&drm->client.base);

	list_for_each_entry(cli, &drm->clients, head) {
		nouveau_client_init(&cli->base);
	}

	return nouveau_pci_resume(pdev);
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
