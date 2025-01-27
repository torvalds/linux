// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_reg.h"

#include "intel_rom.h"
#include "intel_uncore.h"

struct intel_rom {
	/* for PCI ROM */
	struct pci_dev *pdev;
	void __iomem *oprom;

	/* for SPI */
	struct intel_uncore *uncore;
	loff_t offset;

	size_t size;

	u32 (*read32)(struct intel_rom *rom, loff_t offset);
	u16 (*read16)(struct intel_rom *rom, loff_t offset);
	void (*read_block)(struct intel_rom *rom, void *data, loff_t offset, size_t size);
	void (*free)(struct intel_rom *rom);
};

static u32 spi_read32(struct intel_rom *rom, loff_t offset)
{
	intel_uncore_write(rom->uncore, PRIMARY_SPI_ADDRESS,
			   rom->offset + offset);

	return intel_uncore_read(rom->uncore, PRIMARY_SPI_TRIGGER);
}

static u16 spi_read16(struct intel_rom *rom, loff_t offset)
{
	return spi_read32(rom, offset) & 0xffff;
}

struct intel_rom *intel_rom_spi(struct drm_i915_private *i915)
{
	struct intel_rom *rom;
	u32 static_region;

	rom = kzalloc(sizeof(*rom), GFP_KERNEL);
	if (!rom)
		return NULL;

	rom->uncore = &i915->uncore;

	static_region = intel_uncore_read(rom->uncore, SPI_STATIC_REGIONS);
	static_region &= OPTIONROM_SPI_REGIONID_MASK;
	intel_uncore_write(rom->uncore, PRIMARY_SPI_REGIONID, static_region);

	rom->offset = intel_uncore_read(rom->uncore, OROM_OFFSET) & OROM_OFFSET_MASK;

	rom->size = 0x200000;

	rom->read32 = spi_read32;
	rom->read16 = spi_read16;

	return rom;
}

static u32 pci_read32(struct intel_rom *rom, loff_t offset)
{
	return ioread32(rom->oprom + offset);
}

static u16 pci_read16(struct intel_rom *rom, loff_t offset)
{
	return ioread16(rom->oprom + offset);
}

static void pci_read_block(struct intel_rom *rom, void *data,
			   loff_t offset, size_t size)
{
	memcpy_fromio(data, rom->oprom + offset, size);
}

static void pci_free(struct intel_rom *rom)
{
	pci_unmap_rom(rom->pdev, rom->oprom);
}

struct intel_rom *intel_rom_pci(struct drm_i915_private *i915)
{
	struct intel_rom *rom;

	rom = kzalloc(sizeof(*rom), GFP_KERNEL);
	if (!rom)
		return NULL;

	rom->pdev = to_pci_dev(i915->drm.dev);

	rom->oprom = pci_map_rom(rom->pdev, &rom->size);
	if (!rom->oprom) {
		kfree(rom);
		return NULL;
	}

	rom->read32 = pci_read32;
	rom->read16 = pci_read16;
	rom->read_block = pci_read_block;
	rom->free = pci_free;

	return rom;
}

u32 intel_rom_read32(struct intel_rom *rom, loff_t offset)
{
	return rom->read32(rom, offset);
}

u16 intel_rom_read16(struct intel_rom *rom, loff_t offset)
{
	return rom->read16(rom, offset);
}

void intel_rom_read_block(struct intel_rom *rom, void *data,
			  loff_t offset, size_t size)
{
	u32 *ptr = data;
	loff_t index;

	if (rom->read_block) {
		rom->read_block(rom, data, offset, size);
		return;
	}

	for (index = 0; index < size; index += 4)
		*ptr++ = rom->read32(rom, offset + index);
}

loff_t intel_rom_find(struct intel_rom *rom, u32 needle)
{
	loff_t offset;

	for (offset = 0; offset < rom->size; offset += 4) {
		if (rom->read32(rom, offset) == needle)
			return offset;
	}

	return -ENOENT;
}

size_t intel_rom_size(struct intel_rom *rom)
{
	return rom->size;
}

void intel_rom_free(struct intel_rom *rom)
{
	if (rom && rom->free)
		rom->free(rom);

	kfree(rom);
}
