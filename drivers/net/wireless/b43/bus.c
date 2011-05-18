/*

  Broadcom B43 wireless driver
  Bus abstraction layer

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


/* SSB */
static inline u16 b43_bus_ssb_read16(struct b43_bus_dev *dev, u16 offset)
{
	return ssb_read16(dev->sdev, offset);
}
static inline u32 b43_bus_ssb_read32(struct b43_bus_dev *dev, u16 offset)
{
	return ssb_read32(dev->sdev, offset);
}
static inline
void b43_bus_ssb_write16(struct b43_bus_dev *dev, u16 offset, u16 value)
{
	ssb_write16(dev->sdev, offset, value);
}
static inline
void b43_bus_ssb_write32(struct b43_bus_dev *dev, u16 offset, u32 value)
{
	ssb_write32(dev->sdev, offset, value);
}
static inline
void b43_bus_ssb_block_read(struct b43_bus_dev *dev, void *buffer,
			    size_t count, u16 offset, u8 reg_width)
{
	ssb_block_read(dev->sdev, buffer, count, offset, reg_width);
}
static inline
void b43_bus_ssb_block_write(struct b43_bus_dev *dev, const void *buffer,
			     size_t count, u16 offset, u8 reg_width)
{
	ssb_block_write(dev->sdev, buffer, count, offset, reg_width);
}

struct b43_bus_dev *b43_bus_dev_ssb_init(struct ssb_device *sdev)
{
	struct b43_bus_dev *dev = kzalloc(sizeof(*dev), GFP_KERNEL);

	dev->bus_type = B43_BUS_SSB;
	dev->sdev = sdev;

	dev->read16 = b43_bus_ssb_read16;
	dev->read32 = b43_bus_ssb_read32;
	dev->write16 = b43_bus_ssb_write16;
	dev->write32 = b43_bus_ssb_write32;
	dev->block_read = b43_bus_ssb_block_read;
	dev->block_write = b43_bus_ssb_block_write;

	dev->dev = sdev->dev;
	dev->dma_dev = sdev->dma_dev;
	dev->irq = sdev->irq;

	dev->bus_sprom = &sdev->bus->sprom;

	dev->core_id = sdev->id.coreid;
	dev->core_rev = sdev->id.revision;

	return dev;
}
