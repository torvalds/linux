/*

  Broadcom B43 wireless driver
  Bus abstraction layer

  Copyright (c) 2011 Rafał Miłecki <zajec5@gmail.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include "b43.h"
#include "bus.h"

/* BCMA */
#ifdef CONFIG_B43_BCMA
static int b43_bus_bcma_bus_may_powerdown(struct b43_bus_dev *dev)
{
	return 0; /* bcma_bus_may_powerdown(dev->bdev->bus); */
}
static int b43_bus_bcma_bus_powerup(struct b43_bus_dev *dev,
					  bool dynamic_pctl)
{
	return 0; /* bcma_bus_powerup(dev->sdev->bus, dynamic_pctl); */
}
static int b43_bus_bcma_device_is_enabled(struct b43_bus_dev *dev)
{
	return bcma_core_is_enabled(dev->bdev);
}
static void b43_bus_bcma_device_enable(struct b43_bus_dev *dev,
					     u32 core_specific_flags)
{
	bcma_core_enable(dev->bdev, core_specific_flags);
}
static void b43_bus_bcma_device_disable(struct b43_bus_dev *dev,
					      u32 core_specific_flags)
{
	bcma_core_disable(dev->bdev, core_specific_flags);
}
static u16 b43_bus_bcma_read16(struct b43_bus_dev *dev, u16 offset)
{
	return bcma_read16(dev->bdev, offset);
}
static u32 b43_bus_bcma_read32(struct b43_bus_dev *dev, u16 offset)
{
	return bcma_read32(dev->bdev, offset);
}
static
void b43_bus_bcma_write16(struct b43_bus_dev *dev, u16 offset, u16 value)
{
	bcma_write16(dev->bdev, offset, value);
}
static
void b43_bus_bcma_write32(struct b43_bus_dev *dev, u16 offset, u32 value)
{
	bcma_write32(dev->bdev, offset, value);
}
static
void b43_bus_bcma_block_read(struct b43_bus_dev *dev, void *buffer,
			     size_t count, u16 offset, u8 reg_width)
{
	bcma_block_read(dev->bdev, buffer, count, offset, reg_width);
}
static
void b43_bus_bcma_block_write(struct b43_bus_dev *dev, const void *buffer,
			      size_t count, u16 offset, u8 reg_width)
{
	bcma_block_write(dev->bdev, buffer, count, offset, reg_width);
}

struct b43_bus_dev *b43_bus_dev_bcma_init(struct bcma_device *core)
{
	struct b43_bus_dev *dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->bus_type = B43_BUS_BCMA;
	dev->bdev = core;

	dev->bus_may_powerdown = b43_bus_bcma_bus_may_powerdown;
	dev->bus_powerup = b43_bus_bcma_bus_powerup;
	dev->device_is_enabled = b43_bus_bcma_device_is_enabled;
	dev->device_enable = b43_bus_bcma_device_enable;
	dev->device_disable = b43_bus_bcma_device_disable;

	dev->read16 = b43_bus_bcma_read16;
	dev->read32 = b43_bus_bcma_read32;
	dev->write16 = b43_bus_bcma_write16;
	dev->write32 = b43_bus_bcma_write32;
	dev->block_read = b43_bus_bcma_block_read;
	dev->block_write = b43_bus_bcma_block_write;

	dev->dev = &core->dev;
	dev->dma_dev = core->dma_dev;
	dev->irq = core->irq;

	/*
	dev->board_vendor = core->bus->boardinfo.vendor;
	dev->board_type = core->bus->boardinfo.type;
	dev->board_rev = core->bus->boardinfo.rev;
	*/

	dev->chip_id = core->bus->chipinfo.id;
	dev->chip_rev = core->bus->chipinfo.rev;
	dev->chip_pkg = core->bus->chipinfo.pkg;

	dev->bus_sprom = &core->bus->sprom;

	dev->core_id = core->id.id;
	dev->core_rev = core->id.rev;

	return dev;
}
#endif /* CONFIG_B43_BCMA */

/* SSB */
#ifdef CONFIG_B43_SSB
static int b43_bus_ssb_bus_may_powerdown(struct b43_bus_dev *dev)
{
	return ssb_bus_may_powerdown(dev->sdev->bus);
}
static int b43_bus_ssb_bus_powerup(struct b43_bus_dev *dev,
					  bool dynamic_pctl)
{
	return ssb_bus_powerup(dev->sdev->bus, dynamic_pctl);
}
static int b43_bus_ssb_device_is_enabled(struct b43_bus_dev *dev)
{
	return ssb_device_is_enabled(dev->sdev);
}
static void b43_bus_ssb_device_enable(struct b43_bus_dev *dev,
					     u32 core_specific_flags)
{
	ssb_device_enable(dev->sdev, core_specific_flags);
}
static void b43_bus_ssb_device_disable(struct b43_bus_dev *dev,
					      u32 core_specific_flags)
{
	ssb_device_disable(dev->sdev, core_specific_flags);
}

static u16 b43_bus_ssb_read16(struct b43_bus_dev *dev, u16 offset)
{
	return ssb_read16(dev->sdev, offset);
}
static u32 b43_bus_ssb_read32(struct b43_bus_dev *dev, u16 offset)
{
	return ssb_read32(dev->sdev, offset);
}
static void b43_bus_ssb_write16(struct b43_bus_dev *dev, u16 offset, u16 value)
{
	ssb_write16(dev->sdev, offset, value);
}
static void b43_bus_ssb_write32(struct b43_bus_dev *dev, u16 offset, u32 value)
{
	ssb_write32(dev->sdev, offset, value);
}
static void b43_bus_ssb_block_read(struct b43_bus_dev *dev, void *buffer,
				   size_t count, u16 offset, u8 reg_width)
{
	ssb_block_read(dev->sdev, buffer, count, offset, reg_width);
}
static
void b43_bus_ssb_block_write(struct b43_bus_dev *dev, const void *buffer,
			     size_t count, u16 offset, u8 reg_width)
{
	ssb_block_write(dev->sdev, buffer, count, offset, reg_width);
}

struct b43_bus_dev *b43_bus_dev_ssb_init(struct ssb_device *sdev)
{
	struct b43_bus_dev *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->bus_type = B43_BUS_SSB;
	dev->sdev = sdev;

	dev->bus_may_powerdown = b43_bus_ssb_bus_may_powerdown;
	dev->bus_powerup = b43_bus_ssb_bus_powerup;
	dev->device_is_enabled = b43_bus_ssb_device_is_enabled;
	dev->device_enable = b43_bus_ssb_device_enable;
	dev->device_disable = b43_bus_ssb_device_disable;

	dev->read16 = b43_bus_ssb_read16;
	dev->read32 = b43_bus_ssb_read32;
	dev->write16 = b43_bus_ssb_write16;
	dev->write32 = b43_bus_ssb_write32;
	dev->block_read = b43_bus_ssb_block_read;
	dev->block_write = b43_bus_ssb_block_write;

	dev->dev = sdev->dev;
	dev->dma_dev = sdev->dma_dev;
	dev->irq = sdev->irq;

	dev->board_vendor = sdev->bus->boardinfo.vendor;
	dev->board_type = sdev->bus->boardinfo.type;
	dev->board_rev = sdev->bus->boardinfo.rev;

	dev->chip_id = sdev->bus->chip_id;
	dev->chip_rev = sdev->bus->chip_rev;
	dev->chip_pkg = sdev->bus->chip_package;

	dev->bus_sprom = &sdev->bus->sprom;

	dev->core_id = sdev->id.coreid;
	dev->core_rev = sdev->id.revision;

	return dev;
}
#endif /* CONFIG_B43_SSB */

void *b43_bus_get_wldev(struct b43_bus_dev *dev)
{
	switch (dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		return bcma_get_drvdata(dev->bdev);
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		return ssb_get_drvdata(dev->sdev);
#endif
	}
	return NULL;
}

void b43_bus_set_wldev(struct b43_bus_dev *dev, void *wldev)
{
	switch (dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		bcma_set_drvdata(dev->bdev, wldev);
		break;
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		ssb_set_drvdata(dev->sdev, wldev);
		break;
#endif
	}
}
