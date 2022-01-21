// SPDX-License-Identifier: GPL-2.0
/*
 * PCI ROM access routines
 *
 * (C) Copyright 2004 Jon Smirl <jonsmirl@yahoo.com>
 * (C) Copyright 2004 Silicon Graphics, Inc. Jesse Barnes <jbarnes@sgi.com>
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "pci.h"

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
	int last_image;
	unsigned int length;

	image = rom;
	do {
		void __iomem *pds;
		/* Standard PCI ROMs start out with these bytes 55 AA */
		if (readw(image) != 0xAA55) {
			pci_info(pdev, "Invalid PCI ROM header signature: expecting 0xaa55, got %#06x\n",
				 readw(image));
			break;
		}
		/* get the PCI data structure and check its "PCIR" signature */
		pds = image + readw(image + 24);
		if (readl(pds) != 0x52494350) {
			pci_info(pdev, "Invalid PCI ROM data signature: expecting 0x52494350, got %#010x\n",
				 readl(pds));
			break;
		}
		last_image = readb(pds + 21) & 0x80;
		length = readw(pds + 16);
		image += length * 512;
		/* Avoid iterating through memory outside the resource window */
		if (image >= rom + size)
			break;
		if (!last_image) {
			if (readw(image) != 0xAA55) {
				pci_info(pdev, "No more image in the PCI ROM\n");
				break;
			}
		}
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
