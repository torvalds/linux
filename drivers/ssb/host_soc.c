/*
 * Sonics Silicon Backplane SoC host related functions.
 * Subsystem core
 *
 * Copyright 2005, Broadcom Corporation
 * Copyright 2006, 2007, Michael Buesch <m@bues.ch>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/ssb/ssb.h>

#include "ssb_private.h"

static u8 ssb_host_soc_read8(struct ssb_device *dev, u16 offset)
{
	struct ssb_bus *bus = dev->bus;

	offset += dev->core_index * SSB_CORE_SIZE;
	return readb(bus->mmio + offset);
}

static u16 ssb_host_soc_read16(struct ssb_device *dev, u16 offset)
{
	struct ssb_bus *bus = dev->bus;

	offset += dev->core_index * SSB_CORE_SIZE;
	return readw(bus->mmio + offset);
}

static u32 ssb_host_soc_read32(struct ssb_device *dev, u16 offset)
{
	struct ssb_bus *bus = dev->bus;

	offset += dev->core_index * SSB_CORE_SIZE;
	return readl(bus->mmio + offset);
}

#ifdef CONFIG_SSB_BLOCKIO
static void ssb_host_soc_block_read(struct ssb_device *dev, void *buffer,
				    size_t count, u16 offset, u8 reg_width)
{
	struct ssb_bus *bus = dev->bus;
	void __iomem *addr;

	offset += dev->core_index * SSB_CORE_SIZE;
	addr = bus->mmio + offset;

	switch (reg_width) {
	case sizeof(u8): {
		u8 *buf = buffer;

		while (count) {
			*buf = __raw_readb(addr);
			buf++;
			count--;
		}
		break;
	}
	case sizeof(u16): {
		__le16 *buf = buffer;

		SSB_WARN_ON(count & 1);
		while (count) {
			*buf = (__force __le16)__raw_readw(addr);
			buf++;
			count -= 2;
		}
		break;
	}
	case sizeof(u32): {
		__le32 *buf = buffer;

		SSB_WARN_ON(count & 3);
		while (count) {
			*buf = (__force __le32)__raw_readl(addr);
			buf++;
			count -= 4;
		}
		break;
	}
	default:
		SSB_WARN_ON(1);
	}
}
#endif /* CONFIG_SSB_BLOCKIO */

static void ssb_host_soc_write8(struct ssb_device *dev, u16 offset, u8 value)
{
	struct ssb_bus *bus = dev->bus;

	offset += dev->core_index * SSB_CORE_SIZE;
	writeb(value, bus->mmio + offset);
}

static void ssb_host_soc_write16(struct ssb_device *dev, u16 offset, u16 value)
{
	struct ssb_bus *bus = dev->bus;

	offset += dev->core_index * SSB_CORE_SIZE;
	writew(value, bus->mmio + offset);
}

static void ssb_host_soc_write32(struct ssb_device *dev, u16 offset, u32 value)
{
	struct ssb_bus *bus = dev->bus;

	offset += dev->core_index * SSB_CORE_SIZE;
	writel(value, bus->mmio + offset);
}

#ifdef CONFIG_SSB_BLOCKIO
static void ssb_host_soc_block_write(struct ssb_device *dev, const void *buffer,
				     size_t count, u16 offset, u8 reg_width)
{
	struct ssb_bus *bus = dev->bus;
	void __iomem *addr;

	offset += dev->core_index * SSB_CORE_SIZE;
	addr = bus->mmio + offset;

	switch (reg_width) {
	case sizeof(u8): {
		const u8 *buf = buffer;

		while (count) {
			__raw_writeb(*buf, addr);
			buf++;
			count--;
		}
		break;
	}
	case sizeof(u16): {
		const __le16 *buf = buffer;

		SSB_WARN_ON(count & 1);
		while (count) {
			__raw_writew((__force u16)(*buf), addr);
			buf++;
			count -= 2;
		}
		break;
	}
	case sizeof(u32): {
		const __le32 *buf = buffer;

		SSB_WARN_ON(count & 3);
		while (count) {
			__raw_writel((__force u32)(*buf), addr);
			buf++;
			count -= 4;
		}
		break;
	}
	default:
		SSB_WARN_ON(1);
	}
}
#endif /* CONFIG_SSB_BLOCKIO */

/* Ops for the plain SSB bus without a host-device (no PCI or PCMCIA). */
const struct ssb_bus_ops ssb_host_soc_ops = {
	.read8		= ssb_host_soc_read8,
	.read16		= ssb_host_soc_read16,
	.read32		= ssb_host_soc_read32,
	.write8		= ssb_host_soc_write8,
	.write16	= ssb_host_soc_write16,
	.write32	= ssb_host_soc_write32,
#ifdef CONFIG_SSB_BLOCKIO
	.block_read	= ssb_host_soc_block_read,
	.block_write	= ssb_host_soc_block_write,
#endif
};
