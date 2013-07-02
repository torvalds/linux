/*
 * drivers/pci/rom.c
 *
 * (C) Copyright 2004 Jon Smirl <jonsmirl@yahoo.com>
 * (C) Copyright 2004 Silicon Graphics, Inc. Jesse Barnes <jbarnes@sgi.com>
 *
 * PCI ROM access routines
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
	struct resource *res = pdev->resource + PCI_ROM_RESOURCE;
	struct pci_bus_region region;
	u32 rom_addr;

	if (!res->flags)
		return -1;

	pcibios_resource_to_bus(pdev, &region, res);
	pci_read_config_dword(pdev, pdev->rom_base_reg, &rom_addr);
	rom_addr &= ~PCI_ROM_ADDRESS_MASK;
	rom_addr |= region.start | PCI_ROM_ADDRESS_ENABLE;
	pci_write_config_dword(pdev, pdev->rom_base_reg, rom_addr);
	return 0;
}

/**
 * pci_disable_rom - disable ROM decoding for a PCI device
 * @pdev: PCI device to disable
 *
 * Disable ROM decoding on a PCI device by turning off the last bit in the
 * ROM BAR.
 */
void pci_disable_rom(struct pci_dev *pdev)
{
	u32 rom_addr;
	pci_read_config_dword(pdev, pdev->rom_base_reg, &rom_addr);
	rom_addr &= ~PCI_ROM_ADDRESS_ENABLE;
	pci_write_config_dword(pdev, pdev->rom_base_reg, rom_addr);
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
size_t pci_get_rom_size(struct pci_dev *pdev, void __iomem *rom, size_t size)
{
	void __iomem *image;
	int last_image;

	image = rom;
	do {
		void __iomem *pds;
		/* Standard PCI ROMs start out with these bytes 55 AA */
		if (readb(image) != 0x55) {
			dev_err(&pdev->dev, "Invalid ROM contents\n");
			break;
		}
		if (readb(image + 1) != 0xAA)
			break;
		/* get the PCI data structure and check its signature */
		pds = image + readw(image + 24);
		if (readb(pds) != 'P')
			break;
		if (readb(pds + 1) != 'C')
			break;
		if (readb(pds + 2) != 'I')
			break;
		if (readb(pds + 3) != 'R')
			break;
		last_image = readb(pds + 21) & 0x80;
		/* this length is reliable */
		image += readw(pds + 16) * 512;
	} while (!last_image);

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

	/*
	 * IORESOURCE_ROM_SHADOW set on x86, x86_64 and IA64 supports legacy
	 * memory map if the VGA enable bit of the Bridge Control register is
	 * set for embedded VGA.
	 */
	if (res->flags & IORESOURCE_ROM_SHADOW) {
		/* primary video rom always starts here */
		start = (loff_t)0xC0000;
		*size = 0x20000; /* cover C000:0 through E000:0 */
	} else {
		if (res->flags &
			(IORESOURCE_ROM_COPY | IORESOURCE_ROM_BIOS_COPY)) {
			*size = pci_resource_len(pdev, PCI_ROM_RESOURCE);
			return (void __iomem *)(unsigned long)
				pci_resource_start(pdev, PCI_ROM_RESOURCE);
		} else {
			/* assign the ROM an address if it doesn't have one */
			if (res->parent == NULL &&
			    pci_assign_resource(pdev,PCI_ROM_RESOURCE))
				return NULL;
			start = pci_resource_start(pdev, PCI_ROM_RESOURCE);
			*size = pci_resource_len(pdev, PCI_ROM_RESOURCE);
			if (*size == 0)
				return NULL;

			/* Enable ROM space decodes */
			if (pci_enable_rom(pdev))
				return NULL;
		}
	}

	rom = ioremap(start, *size);
	if (!rom) {
		/* restore enable if ioremap fails */
		if (!(res->flags & (IORESOURCE_ROM_ENABLE |
				    IORESOURCE_ROM_SHADOW |
				    IORESOURCE_ROM_COPY)))
			pci_disable_rom(pdev);
		return NULL;
	}

	/*
	 * Try to find the true size of the ROM since sometimes the PCI window
	 * size is much larger than the actual size of the ROM.
	 * True size is important if the ROM is going to be copied.
	 */
	*size = pci_get_rom_size(pdev, rom, *size);
	return rom;
}

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

	if (res->flags & (IORESOURCE_ROM_COPY | IORESOURCE_ROM_BIOS_COPY))
		return;

	iounmap(rom);

	/* Disable again before continuing, leave enabled if pci=rom */
	if (!(res->flags & (IORESOURCE_ROM_ENABLE | IORESOURCE_ROM_SHADOW)))
		pci_disable_rom(pdev);
}

/**
 * pci_cleanup_rom - free the ROM copy created by pci_map_rom_copy
 * @pdev: pointer to pci device struct
 *
 * Free the copied ROM if we allocated one.
 */
void pci_cleanup_rom(struct pci_dev *pdev)
{
	struct resource *res = &pdev->resource[PCI_ROM_RESOURCE];
	if (res->flags & IORESOURCE_ROM_COPY) {
		kfree((void*)(unsigned long)res->start);
		res->flags &= ~IORESOURCE_ROM_COPY;
		res->start = 0;
		res->end = 0;
	}
}

/**
 * pci_platform_rom - provides a pointer to any ROM image provided by the
 * platform
 * @pdev: pointer to pci device struct
 * @size: pointer to receive size of pci window over ROM
 */
void __iomem *pci_platform_rom(struct pci_dev *pdev, size_t *size)
{
	if (pdev->rom && pdev->romlen) {
		*size = pdev->romlen;
		return phys_to_virt((phys_addr_t)pdev->rom);
	}

	return NULL;
}

EXPORT_SYMBOL(pci_map_rom);
EXPORT_SYMBOL(pci_unmap_rom);
EXPORT_SYMBOL_GPL(pci_enable_rom);
EXPORT_SYMBOL_GPL(pci_disable_rom);
EXPORT_SYMBOL(pci_platform_rom);
