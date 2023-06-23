// SPDX-License-Identifier: GPL-2.0-only

#include <linux/pci.h>
#include <linux/vmalloc.h>

#include <drm/drm_drv.h>

#include "mgag200_drv.h"

static int mgag200_g200_init_pci_options(struct pci_dev *pdev)
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

	if (has_sgram)
		option = 0x4049cd21;
	else
		option = 0x40499121;

	return mgag200_init_pci_options(pdev, option, 0x00008000);
}

/*
 * DRM Device
 */

static const struct mgag200_device_info mgag200_g200_device_info =
	MGAG200_DEVICE_INFO_INIT(2048, 2048, 0, false, 1, 3, false);

static void mgag200_g200_interpret_bios(struct mgag200_g200_device *g200,
					const unsigned char *bios, size_t size)
{
	static const char matrox[] = {'M', 'A', 'T', 'R', 'O', 'X'};
	static const unsigned int expected_length[6] = {
		0, 64, 64, 64, 128, 128
	};
	struct mga_device *mdev = &g200->base;
	struct drm_device *dev = &mdev->base;
	const unsigned char *pins;
	unsigned int pins_len, version;
	int offset;
	int tmp;

	/* Test for MATROX string. */
	if (size < 45 + sizeof(matrox))
		return;
	if (memcmp(&bios[45], matrox, sizeof(matrox)) != 0)
		return;

	/* Get the PInS offset. */
	if (size < MGA_BIOS_OFFSET + 2)
		return;
	offset = (bios[MGA_BIOS_OFFSET + 1] << 8) | bios[MGA_BIOS_OFFSET];

	/* Get PInS data structure. */

	if (size < offset + 6)
		return;
	pins = bios + offset;
	if (pins[0] == 0x2e && pins[1] == 0x41) {
		version = pins[5];
		pins_len = pins[2];
	} else {
		version = 1;
		pins_len = pins[0] + (pins[1] << 8);
	}

	if (version < 1 || version > 5) {
		drm_warn(dev, "Unknown BIOS PInS version: %d\n", version);
		return;
	}
	if (pins_len != expected_length[version]) {
		drm_warn(dev, "Unexpected BIOS PInS size: %d expected: %d\n",
			 pins_len, expected_length[version]);
		return;
	}
	if (size < offset + pins_len)
		return;

	drm_dbg_kms(dev, "MATROX BIOS PInS version %d size: %d found\n", version, pins_len);

	/* Extract the clock values */

	switch (version) {
	case 1:
		tmp = pins[24] + (pins[25] << 8);
		if (tmp)
			g200->pclk_max = tmp * 10;
		break;
	case 2:
		if (pins[41] != 0xff)
			g200->pclk_max = (pins[41] + 100) * 1000;
		break;
	case 3:
		if (pins[36] != 0xff)
			g200->pclk_max = (pins[36] + 100) * 1000;
		if (pins[52] & 0x20)
			g200->ref_clk = 14318;
		break;
	case 4:
		if (pins[39] != 0xff)
			g200->pclk_max = pins[39] * 4 * 1000;
		if (pins[92] & 0x01)
			g200->ref_clk = 14318;
		break;
	case 5:
		tmp = pins[4] ? 8000 : 6000;
		if (pins[123] != 0xff)
			g200->pclk_min = pins[123] * tmp;
		if (pins[38] != 0xff)
			g200->pclk_max = pins[38] * tmp;
		if (pins[110] & 0x01)
			g200->ref_clk = 14318;
		break;
	default:
		break;
	}
}

static void mgag200_g200_init_refclk(struct mgag200_g200_device *g200)
{
	struct mga_device *mdev = &g200->base;
	struct drm_device *dev = &mdev->base;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	unsigned char __iomem *rom;
	unsigned char *bios;
	size_t size;

	g200->pclk_min = 50000;
	g200->pclk_max = 230000;
	g200->ref_clk = 27050;

	rom = pci_map_rom(pdev, &size);
	if (!rom)
		return;

	bios = vmalloc(size);
	if (!bios)
		goto out;
	memcpy_fromio(bios, rom, size);

	if (size != 0 && bios[0] == 0x55 && bios[1] == 0xaa)
		mgag200_g200_interpret_bios(g200, bios, size);

	drm_dbg_kms(dev, "pclk_min: %ld pclk_max: %ld ref_clk: %ld\n",
		    g200->pclk_min, g200->pclk_max, g200->ref_clk);

	vfree(bios);
out:
	pci_unmap_rom(pdev, rom);
}

struct mga_device *mgag200_g200_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
					      enum mga_type type)
{
	struct mgag200_g200_device *g200;
	struct mga_device *mdev;
	struct drm_device *dev;
	resource_size_t vram_available;
	int ret;

	g200 = devm_drm_dev_alloc(&pdev->dev, drv, struct mgag200_g200_device, base.base);
	if (IS_ERR(g200))
		return ERR_CAST(g200);
	mdev = &g200->base;
	dev = &mdev->base;

	pci_set_drvdata(pdev, dev);

	ret = mgag200_g200_init_pci_options(pdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_device_preinit(mdev);
	if (ret)
		return ERR_PTR(ret);

	mgag200_g200_init_refclk(g200);

	ret = mgag200_device_init(mdev, type, &mgag200_g200_device_info);
	if (ret)
		return ERR_PTR(ret);

	vram_available = mgag200_device_probe_vram(mdev);

	ret = mgag200_modeset_init(mdev, vram_available);
	if (ret)
		return ERR_PTR(ret);

	return mdev;
}
