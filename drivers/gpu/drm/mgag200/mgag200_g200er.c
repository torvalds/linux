// SPDX-License-Identifier: GPL-2.0-only

#include <linux/pci.h>

#include <drm/drm_drv.h>

#include "mgag200_drv.h"

static void mgag200_g200er_init_registers(struct mga_device *mdev)
{
	static const u8 dacvalue[] = {
		MGAG200_DAC_DEFAULT(0x00, 0xc9, 0x1f, 0x00, 0x00, 0x00)
	};

	size_t i;

	for (i = 0; i < ARRAY_SIZE(dacvalue); i++) {
		if ((i <= 0x17) ||
		    (i == 0x1b) ||
		    (i == 0x1c) ||
		    ((i >= 0x1f) && (i <= 0x29)) ||
		    ((i >= 0x30) && (i <= 0x37)))
			continue;
		WREG_DAC(i, dacvalue[i]);
	}

	WREG_DAC(0x90, 0); /* G200ER specific */

	mgag200_init_registers(mdev);
}

/*
 * DRM device
 */

static const struct mgag200_device_info mgag200_g200er_device_info =
	MGAG200_DEVICE_INFO_INIT(2048, 2048, 55000, false, 1, 0, false);

struct mga_device *mgag200_g200er_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
						enum mga_type type)
{
	struct mga_device *mdev;
	struct drm_device *dev;
	resource_size_t vram_available;
	int ret;

	mdev = devm_drm_dev_alloc(&pdev->dev, drv, struct mga_device, base);
	if (IS_ERR(mdev))
		return mdev;
	dev = &mdev->base;

	pci_set_drvdata(pdev, dev);

	ret = mgag200_device_preinit(mdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_device_init(mdev, type, &mgag200_g200er_device_info);
	if (ret)
		return ERR_PTR(ret);

	mgag200_g200er_init_registers(mdev);

	vram_available = mgag200_device_probe_vram(mdev);

	ret = mgag200_modeset_init(mdev, vram_available);
	if (ret)
		return ERR_PTR(ret);

	return mdev;
}
