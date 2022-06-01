// SPDX-License-Identifier: GPL-2.0-only

#include <linux/pci.h>

#include <drm/drm_drv.h>

#include "mgag200_drv.h"

static int mgag200_g200se_init_pci_options(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	bool has_sgram;
	u32 option;
	int err;

	err = pci_read_config_dword(pdev, PCI_MGA_OPTION, &option);
	if (err != PCIBIOS_SUCCESSFUL) {
		dev_err(dev, "pci_read_config_dword(PCI_MGA_OPTION) failed: %d\n", err);
		return pcibios_err_to_errno(err);
	}

	has_sgram = !!(option & PCI_MGA_OPTION_HARDPWMSK);

	option = 0x40049120;
	if (has_sgram)
		option |= PCI_MGA_OPTION_HARDPWMSK;

	return mgag200_init_pci_options(pdev, option, 0x00008000);
}

/*
 * DRM device
 */

static const struct mgag200_device_info mgag200_g200se_a_device_info =
	MGAG200_DEVICE_INFO_INIT(true);

static const struct mgag200_device_info mgag200_g200se_b_device_info =
	MGAG200_DEVICE_INFO_INIT(false);

static void mgag200_g200se_init_unique_id(struct mgag200_g200se_device *g200se)
{
	struct mga_device *mdev = &g200se->base;
	struct drm_device *dev = &mdev->base;

	/* stash G200 SE model number for later use */
	g200se->unique_rev_id = RREG32(0x1e24);

	drm_dbg(dev, "G200 SE unique revision id is 0x%x\n", g200se->unique_rev_id);
}

struct mga_device *mgag200_g200se_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
						enum mga_type type)
{
	struct mgag200_g200se_device *g200se;
	const struct mgag200_device_info *info;
	struct mga_device *mdev;
	struct drm_device *dev;
	resource_size_t vram_available;
	int ret;

	g200se = devm_drm_dev_alloc(&pdev->dev, drv, struct mgag200_g200se_device, base.base);
	if (IS_ERR(g200se))
		return ERR_CAST(g200se);
	mdev = &g200se->base;
	dev = &mdev->base;

	pci_set_drvdata(pdev, dev);

	ret = mgag200_g200se_init_pci_options(pdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_device_preinit(mdev);
	if (ret)
		return ERR_PTR(ret);

	mgag200_g200se_init_unique_id(g200se);

	switch (type) {
	case G200_SE_A:
		info = &mgag200_g200se_a_device_info;
		break;
	case G200_SE_B:
		info = &mgag200_g200se_b_device_info;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	ret = mgag200_device_init(mdev, type, info);
	if (ret)
		return ERR_PTR(ret);

	vram_available = mgag200_device_probe_vram(mdev);

	ret = mgag200_modeset_init(mdev, vram_available);
	if (ret)
		return ERR_PTR(ret);

	return mdev;
}
