// SPDX-License-Identifier: GPL-2.0
/*
 * PCI ROM access routines
 *
 * (C) Copyright 2004 Jon Smirl <jonsmirl@yahoo.com>
 * (C) Copyright 2004 Silicon Graphics, Inc. Jesse Barnes <jbarnes@sgi.com>
 */

#include <linux/align.h>
#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/overflow.h>
#include <linux/pci.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#include "pci.h"

#define PCI_ROM_HEADER_SIZE			0x1A
#define PCI_ROM_POINTER_TO_DATA_STRUCT		0x18
#define PCI_ROM_LAST_IMAGE_INDICATOR		0x15
#define PCI_ROM_LAST_IMAGE_INDICATOR_BIT	BIT(7)
#define PCI_ROM_IMAGE_LEN			0x10
#define PCI_ROM_IMAGE_SECTOR_SIZE		SZ_512
#define PCI_ROM_IMAGE_SIGNATURE			0xAA55

/* Data structure signature is "PCIR" in ASCII representation */
#define PCI_ROM_DATA_STRUCT_SIGNATURE		0x52494350
#define PCI_ROM_DATA_STRUCT_LEN			0x0A

/*
 * Per PCI Firmware r3.3, sec 5.1.3, a conformant PCI Data Structure is at
 * least 24 bytes (0x18), large enough to cover every fixed field this
 * driver reads (up to the Indicator byte at offset 0x15).  Reject smaller
 * device-claimed lengths so the follow-up readers in pci_get_rom_size()
 * cannot escape the mapped ROM window.
 */
#define PCI_ROM_DATA_STRUCT_MIN_LEN		0x18

/**
 * pci_enable_rom - enable ROM decoding for a PCI device
 * @pdev: PCI device to enable
 *
 * Enable ROM decoding on @dev.  This involves simply turning on the last
 * bit of the PCI ROM BAR.  Note that some cards may share address decoders
 * between the ROM and other resources, so enabling it may disable access
 * to MMIO registers or other card memory.
 */
int pci_enable_rom(struct pci_dev *pdev)
{
	struct resource *res = &pdev->resource[PCI_ROM_RESOURCE];
	struct pci_bus_region region;
	u32 rom_addr;

	if (!res->flags)
		return -1;

	/* Nothing to enable if we're using a shadow copy in RAM */
	if (res->flags & IORESOURCE_ROM_SHADOW)
		return 0;

	/*
	 * Ideally pci_update_resource() would update the ROM BAR address,
	 * and we would only set the enable bit here.  But apparently some
	 * devices have buggy ROM BARs that read as zero when disabled.
	 */
	pcibios_resource_to_bus(pdev->bus, &region, res);
	pci_read_config_dword(pdev, pdev->rom_base_reg, &rom_addr);
	rom_addr &= ~PCI_ROM_ADDRESS_MASK;
	rom_addr |= region.start | PCI_ROM_ADDRESS_ENABLE;
	pci_write_config_dword(pdev, pdev->rom_base_reg, rom_addr);
	return 0;
}
EXPORT_SYMBOL_GPL(pci_enable_rom);

/**
 * pci_disable_rom - disable ROM decoding for a PCI device
 * @pdev: PCI device to disable
 *
 * Disable ROM decoding on a PCI device by turning off the last bit in the
 * ROM BAR.
 */
void pci_disable_rom(struct pci_dev *pdev)
{
	struct resource *res = &pdev->resource[PCI_ROM_RESOURCE];
	u32 rom_addr;

	if (res->flags & IORESOURCE_ROM_SHADOW)
		return;

	pci_read_config_dword(pdev, pdev->rom_base_reg, &rom_addr);
	rom_addr &= ~PCI_ROM_ADDRESS_ENABLE;
	pci_write_config_dword(pdev, pdev->rom_base_reg, rom_addr);
}
EXPORT_SYMBOL_GPL(pci_disable_rom);

static bool pci_rom_header_valid(struct pci_dev *pdev, void __iomem *image,
				 void __iomem *rom, size_t size,
				 bool expect_valid)
{
	unsigned long rom_end = (unsigned long)rom + size - 1;
	unsigned long header_end;
	u16 signature;

	/*
	 * Per PCI Firmware r3.3, sec 5.1, each image must start on a
	 * 512-byte boundary and must contain the PCI Expansion ROM header.
	 * Because @rom is page-aligned (returned by ioremap()), checking
	 * 512-byte alignment of @image is equivalent to enforcing the
	 * spec's sector-aligned layout within the ROM.  This also
	 * satisfies the natural-alignment requirement of readw() on archs
	 * such as arm64 that disallow unaligned IOMEM access.
	 */
	if (!IS_ALIGNED((unsigned long)image, PCI_ROM_IMAGE_SECTOR_SIZE))
		return false;

	if (check_add_overflow((unsigned long)image, PCI_ROM_HEADER_SIZE - 1,
				&header_end))
		return false;

	if (image < rom || header_end > rom_end)
		return false;

	/* Standard PCI ROMs start out with these bytes 55 AA */
	signature = readw(image);
	if (signature != PCI_ROM_IMAGE_SIGNATURE) {
		if (expect_valid) {
			pci_info(pdev, "Invalid PCI ROM header signature: expecting %#06x, got %#06x\n",
				 PCI_ROM_IMAGE_SIGNATURE, signature);
		} else {
			pci_info(pdev, "No more images in PCI ROM\n");
		}
		return false;
	}

	return true;
}

static bool pci_rom_data_struct_valid(struct pci_dev *pdev, void __iomem *pds,
				      void __iomem *rom, size_t size)
{
	unsigned long rom_end = (unsigned long)rom + size - 1;
	unsigned long end;
	u32 signature;
	u16 data_len;

	/*
	 * Some CPU architectures require IOMEM access addresses to be
	 * aligned, for example arm64, so since we're about to call
	 * readl(), check here for 4-byte alignment.
	 */
	if (!IS_ALIGNED((unsigned long)pds, 4))
		return false;

	if (check_add_overflow((unsigned long)pds, PCI_ROM_DATA_STRUCT_LEN + 1,
				&end))
		return false;

	if (pds < rom || end > rom_end)
		return false;

	signature = readl(pds);
	if (signature != PCI_ROM_DATA_STRUCT_SIGNATURE) {
		pci_info(pdev, "Invalid PCI ROM data signature: expecting %#010x, got %#010x\n",
			 PCI_ROM_DATA_STRUCT_SIGNATURE, signature);
		return false;
	}

	data_len = readw(pds + PCI_ROM_DATA_STRUCT_LEN);
	if (data_len < PCI_ROM_DATA_STRUCT_MIN_LEN || data_len == U16_MAX)
		return false;

	if (check_add_overflow((unsigned long)pds, data_len - 1, &end))
		return false;

	if (end > rom_end)
		return false;

	return true;
}

/**
 * pci_get_rom_size - obtain the actual size of the ROM image
 * @pdev: target PCI device
 * @rom: kernel virtual pointer to image of ROM
 * @size: size of PCI window
 *  return: size of actual ROM image
 *
 * Determine the actual length of the ROM image.
 * The PCI window size could be much larger than the
 * actual image size.
 */
static size_t pci_get_rom_size(struct pci_dev *pdev, void __iomem *rom,
			       size_t size)
{
	void __iomem *image;
	unsigned int length;
	bool last_image;

	image = rom;
	do {
		void __iomem *pds;
		if (!pci_rom_header_valid(pdev, image, rom, size, true))
			break;

		/* Get the PCI data structure and check its "PCIR" signature */
		pds = image + readw(image + PCI_ROM_POINTER_TO_DATA_STRUCT);
		if (!pci_rom_data_struct_valid(pdev, pds, rom, size))
			break;

		last_image = readb(pds + PCI_ROM_LAST_IMAGE_INDICATOR) &
				   PCI_ROM_LAST_IMAGE_INDICATOR_BIT;
		length = readw(pds + PCI_ROM_IMAGE_LEN);
		image += length * PCI_ROM_IMAGE_SECTOR_SIZE;

		if (!last_image &&
		    !pci_rom_header_valid(pdev, image, rom, size, false))
			break;
	} while (length && !last_image);

	/* never return a size larger than the PCI resource window */
	/* there are known ROMs that get the size wrong */
	return min((size_t)(image - rom), size);
}

/**
 * pci_map_rom - map a PCI ROM to kernel space
 * @pdev: pointer to pci device struct
 * @size: pointer to receive size of pci window over ROM
 *
 * Return: kernel virtual pointer to image of ROM
 *
 * Map a PCI ROM into kernel space. If ROM is boot video ROM,
 * the shadow BIOS copy will be returned instead of the
 * actual ROM.
 */
void __iomem *pci_map_rom(struct pci_dev *pdev, size_t *size)
{
	struct resource *res = &pdev->resource[PCI_ROM_RESOURCE];
	loff_t start;
	void __iomem *rom;

	/* assign the ROM an address if it doesn't have one */
	if (res->parent == NULL && pci_assign_resource(pdev, PCI_ROM_RESOURCE))
		return NULL;

	start = pci_resource_start(pdev, PCI_ROM_RESOURCE);
	*size = pci_resource_len(pdev, PCI_ROM_RESOURCE);
	if (*size == 0)
		return NULL;

	/* Enable ROM space decodes */
	if (pci_enable_rom(pdev))
		return NULL;

	rom = ioremap(start, *size);
	if (!rom)
		goto err_ioremap;

	/*
	 * Try to find the true size of the ROM since sometimes the PCI window
	 * size is much larger than the actual size of the ROM.
	 * True size is important if the ROM is going to be copied.
	 */
	*size = pci_get_rom_size(pdev, rom, *size);
	if (!*size)
		goto invalid_rom;

	return rom;

invalid_rom:
	iounmap(rom);
err_ioremap:
	/* restore enable if ioremap fails */
	if (!(res->flags & IORESOURCE_ROM_ENABLE))
		pci_disable_rom(pdev);
	return NULL;
}
EXPORT_SYMBOL(pci_map_rom);

/**
 * pci_unmap_rom - unmap the ROM from kernel space
 * @pdev: pointer to pci device struct
 * @rom: virtual address of the previous mapping
 *
 * Remove a mapping of a previously mapped ROM
 */
void pci_unmap_rom(struct pci_dev *pdev, void __iomem *rom)
{
	struct resource *res = &pdev->resource[PCI_ROM_RESOURCE];

	iounmap(rom);

	/* Disable again before continuing */
	if (!(res->flags & IORESOURCE_ROM_ENABLE))
		pci_disable_rom(pdev);
}
EXPORT_SYMBOL(pci_unmap_rom);
