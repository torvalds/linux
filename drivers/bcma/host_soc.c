/*
 * Broadcom specific AMBA
 * System on Chip (SoC) Host
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
#include "scan.h"
#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_soc.h>

static u8 bcma_host_soc_read8(struct bcma_device *core, u16 offset)
{
	return readb(core->io_addr + offset);
}

static u16 bcma_host_soc_read16(struct bcma_device *core, u16 offset)
{
	return readw(core->io_addr + offset);
}

static u32 bcma_host_soc_read32(struct bcma_device *core, u16 offset)
{
	return readl(core->io_addr + offset);
}

static void bcma_host_soc_write8(struct bcma_device *core, u16 offset,
				 u8 value)
{
	writeb(value, core->io_addr + offset);
}

static void bcma_host_soc_write16(struct bcma_device *core, u16 offset,
				 u16 value)
{
	writew(value, core->io_addr + offset);
}

static void bcma_host_soc_write32(struct bcma_device *core, u16 offset,
				 u32 value)
{
	writel(value, core->io_addr + offset);
}

#ifdef CONFIG_BCMA_BLOCKIO
static void bcma_host_soc_block_read(struct bcma_device *core, void *buffer,
				     size_t count, u16 offset, u8 reg_width)
{
	void __iomem *addr = core->io_addr + offset;

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

		WARN_ON(count & 1);
		while (count) {
			*buf = (__force __le16)__raw_readw(addr);
			buf++;
			count -= 2;
		}
		break;
	}
	case sizeof(u32): {
		__le32 *buf = buffer;

		WARN_ON(count & 3);
		while (count) {
			*buf = (__force __le32)__raw_readl(addr);
			buf++;
			count -= 4;
		}
		break;
	}
	default:
		WARN_ON(1);
	}
}

static void bcma_host_soc_block_write(struct bcma_device *core,
				      const void *buffer,
				      size_t count, u16 offset, u8 reg_width)
{
	void __iomem *addr = core->io_addr + offset;

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

		WARN_ON(count & 1);
		while (count) {
			__raw_writew((__force u16)(*buf), addr);
			buf++;
			count -= 2;
		}
		break;
	}
	case sizeof(u32): {
		const __le32 *buf = buffer;

		WARN_ON(count & 3);
		while (count) {
			__raw_writel((__force u32)(*buf), addr);
			buf++;
			count -= 4;
		}
		break;
	}
	default:
		WARN_ON(1);
	}
}
#endif /* CONFIG_BCMA_BLOCKIO */

static u32 bcma_host_soc_aread32(struct bcma_device *core, u16 offset)
{
	if (WARN_ONCE(!core->io_wrap, "Accessed core has no wrapper/agent\n"))
		return ~0;
	return readl(core->io_wrap + offset);
}

static void bcma_host_soc_awrite32(struct bcma_device *core, u16 offset,
				  u32 value)
{
	if (WARN_ONCE(!core->io_wrap, "Accessed core has no wrapper/agent\n"))
		return;
	writel(value, core->io_wrap + offset);
}

static const struct bcma_host_ops bcma_host_soc_ops = {
	.read8		= bcma_host_soc_read8,
	.read16		= bcma_host_soc_read16,
	.read32		= bcma_host_soc_read32,
	.write8		= bcma_host_soc_write8,
	.write16	= bcma_host_soc_write16,
	.write32	= bcma_host_soc_write32,
#ifdef CONFIG_BCMA_BLOCKIO
	.block_read	= bcma_host_soc_block_read,
	.block_write	= bcma_host_soc_block_write,
#endif
	.aread32	= bcma_host_soc_aread32,
	.awrite32	= bcma_host_soc_awrite32,
};

int __init bcma_host_soc_register(struct bcma_soc *soc)
{
	struct bcma_bus *bus = &soc->bus;

	/* iomap only first core. We have to read some register on this core
	 * to scan the bus.
	 */
	bus->mmio = ioremap_nocache(BCMA_ADDR_BASE, BCMA_CORE_SIZE * 1);
	if (!bus->mmio)
		return -ENOMEM;

	/* Host specific */
	bus->hosttype = BCMA_HOSTTYPE_SOC;
	bus->ops = &bcma_host_soc_ops;

	/* Initialize struct, detect chip */
	bcma_init_bus(bus);

	return 0;
}

int __init bcma_host_soc_init(struct bcma_soc *soc)
{
	struct bcma_bus *bus = &soc->bus;
	int err;

	/* Scan bus and initialize it */
	err = bcma_bus_early_register(bus, &soc->core_cc, &soc->core_mips);
	if (err)
		iounmap(bus->mmio);

	return err;
}
