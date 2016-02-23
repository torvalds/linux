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
 */
#include "priv.h"

#include <core/pci.h>

struct priv {
	struct pci_dev *pdev;
	void __iomem *rom;
	size_t size;
};

static u32
pcirom_read(void *data, u32 offset, u32 length, struct nvkm_bios *bios)
{
	struct priv *priv = data;
	if (offset + length <= priv->size) {
		memcpy_fromio(bios->data + offset, priv->rom + offset, length);
		return length;
	}
	return 0;
}

static void
pcirom_fini(void *data)
{
	struct priv *priv = data;
	pci_unmap_rom(priv->pdev, priv->rom);
	pci_disable_rom(priv->pdev);
	kfree(priv);
}

static void *
pcirom_init(struct nvkm_bios *bios, const char *name)
{
	struct nvkm_device *device = bios->subdev.device;
	struct priv *priv = NULL;
	struct pci_dev *pdev;
	int ret;

	if (device->func->pci)
		pdev = device->func->pci(device)->pdev;
	else
		return ERR_PTR(-ENODEV);

	if (!(ret = pci_enable_rom(pdev))) {
		if (ret = -ENOMEM,
		    (priv = kmalloc(sizeof(*priv), GFP_KERNEL))) {
			if (ret = -EFAULT,
			    (priv->rom = pci_map_rom(pdev, &priv->size))) {
				priv->pdev = pdev;
				return priv;
			}
			kfree(priv);
		}
		pci_disable_rom(pdev);
	}

	return ERR_PTR(ret);
}

const struct nvbios_source
nvbios_pcirom = {
	.name = "PCIROM",
	.init = pcirom_init,
	.fini = pcirom_fini,
	.read = pcirom_read,
	.rw = true,
};

static void *
platform_init(struct nvkm_bios *bios, const char *name)
{
	struct nvkm_device *device = bios->subdev.device;
	struct pci_dev *pdev;
	struct priv *priv;
	int ret = -ENOMEM;

	if (device->func->pci)
		pdev = device->func->pci(device)->pdev;
	else
		return ERR_PTR(-ENODEV);

	if ((priv = kmalloc(sizeof(*priv), GFP_KERNEL))) {
		if (ret = -ENODEV,
		    (priv->rom = pci_platform_rom(pdev, &priv->size)))
			return priv;
		kfree(priv);
	}

	return ERR_PTR(ret);
}

const struct nvbios_source
nvbios_platform = {
	.name = "PLATFORM",
	.init = platform_init,
	.fini = (void(*)(void *))kfree,
	.read = pcirom_read,
	.rw = true,
};
