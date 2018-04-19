// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/zorro.h>
#include <net/ax88796.h>
#include <asm/amigaints.h>

#define ZORRO_PROD_INDIVIDUAL_COMPUTERS_X_SURF100 \
		ZORRO_ID(INDIVIDUAL_COMPUTERS, 0x64, 0)

#define XS100_IRQSTATUS_BASE 0x40
#define XS100_8390_BASE 0x800

/* Longword-access area. Translated to 2 16-bit access cycles by the
 * X-Surf 100 FPGA
 */
#define XS100_8390_DATA32_BASE 0x8000
#define XS100_8390_DATA32_SIZE 0x2000
/* Sub-Areas for fast data register access; addresses relative to area begin */
#define XS100_8390_DATA_READ32_BASE 0x0880
#define XS100_8390_DATA_WRITE32_BASE 0x0C80
#define XS100_8390_DATA_AREA_SIZE 0x80

#define __NS8390_init ax_NS8390_init

/* force unsigned long back to 'void __iomem *' */
#define ax_convert_addr(_a) ((void __force __iomem *)(_a))

#define ei_inb(_a) z_readb(ax_convert_addr(_a))
#define ei_outb(_v, _a) z_writeb(_v, ax_convert_addr(_a))

#define ei_inw(_a) z_readw(ax_convert_addr(_a))
#define ei_outw(_v, _a) z_writew(_v, ax_convert_addr(_a))

#define ei_inb_p(_a) ei_inb(_a)
#define ei_outb_p(_v, _a) ei_outb(_v, _a)

/* define EI_SHIFT() to take into account our register offsets */
#define EI_SHIFT(x) (ei_local->reg_offset[(x)])

/* Ensure we have our RCR base value */
#define AX88796_PLATFORM

static unsigned char version[] =
		"ax88796.c: Copyright 2005,2007 Simtec Electronics\n";

#include "lib8390.c"

/* from ne.c */
#define NE_CMD		EI_SHIFT(0x00)
#define NE_RESET	EI_SHIFT(0x1f)
#define NE_DATAPORT	EI_SHIFT(0x10)

struct xsurf100_ax_plat_data {
	struct ax_plat_data ax;
	void __iomem *base_regs;
	void __iomem *data_area;
};

static int is_xsurf100_network_irq(struct platform_device *pdev)
{
	struct xsurf100_ax_plat_data *xs100 = dev_get_platdata(&pdev->dev);

	return (readw(xs100->base_regs + XS100_IRQSTATUS_BASE) & 0xaaaa) != 0;
}

/* These functions guarantee that the iomem is accessed with 32 bit
 * cycles only. z_memcpy_fromio / z_memcpy_toio don't
 */
static void z_memcpy_fromio32(void *dst, const void __iomem *src, size_t bytes)
{
	while (bytes > 32) {
		asm __volatile__
		   ("movem.l (%0)+,%%d0-%%d7\n"
		    "movem.l %%d0-%%d7,(%1)\n"
		    "adda.l #32,%1" : "=a"(src), "=a"(dst)
		    : "0"(src), "1"(dst) : "d0", "d1", "d2", "d3", "d4",
					   "d5", "d6", "d7", "memory");
		bytes -= 32;
	}
	while (bytes) {
		*(uint32_t *)dst = z_readl(src);
		src += 4;
		dst += 4;
		bytes -= 4;
	}
}

static void z_memcpy_toio32(void __iomem *dst, const void *src, size_t bytes)
{
	while (bytes) {
		z_writel(*(const uint32_t *)src, dst);
		src += 4;
		dst += 4;
		bytes -= 4;
	}
}

static void xs100_write(struct net_device *dev, const void *src,
			unsigned int count)
{
	struct ei_device *ei_local = netdev_priv(dev);
	struct platform_device *pdev = to_platform_device(dev->dev.parent);
	struct xsurf100_ax_plat_data *xs100 = dev_get_platdata(&pdev->dev);

	/* copy whole blocks */
	while (count > XS100_8390_DATA_AREA_SIZE) {
		z_memcpy_toio32(xs100->data_area +
				XS100_8390_DATA_WRITE32_BASE, src,
				XS100_8390_DATA_AREA_SIZE);
		src += XS100_8390_DATA_AREA_SIZE;
		count -= XS100_8390_DATA_AREA_SIZE;
	}
	/* copy whole dwords */
	z_memcpy_toio32(xs100->data_area + XS100_8390_DATA_WRITE32_BASE,
			src, count & ~3);
	src += count & ~3;
	if (count & 2) {
		ei_outw(*(uint16_t *)src, ei_local->mem + NE_DATAPORT);
		src += 2;
	}
	if (count & 1)
		ei_outb(*(uint8_t *)src, ei_local->mem + NE_DATAPORT);
}

static void xs100_read(struct net_device *dev, void *dst, unsigned int count)
{
	struct ei_device *ei_local = netdev_priv(dev);
	struct platform_device *pdev = to_platform_device(dev->dev.parent);
	struct xsurf100_ax_plat_data *xs100 = dev_get_platdata(&pdev->dev);

	/* copy whole blocks */
	while (count > XS100_8390_DATA_AREA_SIZE) {
		z_memcpy_fromio32(dst, xs100->data_area +
				  XS100_8390_DATA_READ32_BASE,
				  XS100_8390_DATA_AREA_SIZE);
		dst += XS100_8390_DATA_AREA_SIZE;
		count -= XS100_8390_DATA_AREA_SIZE;
	}
	/* copy whole dwords */
	z_memcpy_fromio32(dst, xs100->data_area + XS100_8390_DATA_READ32_BASE,
			  count & ~3);
	dst += count & ~3;
	if (count & 2) {
		*(uint16_t *)dst = ei_inw(ei_local->mem + NE_DATAPORT);
		dst += 2;
	}
	if (count & 1)
		*(uint8_t *)dst = ei_inb(ei_local->mem + NE_DATAPORT);
}

/* Block input and output, similar to the Crynwr packet driver. If
 * you are porting to a new ethercard, look at the packet driver
 * source for hints. The NEx000 doesn't share the on-board packet
 * memory -- you have to put the packet out through the "remote DMA"
 * dataport using ei_outb.
 */
static void xs100_block_input(struct net_device *dev, int count,
			      struct sk_buff *skb, int ring_offset)
{
	struct ei_device *ei_local = netdev_priv(dev);
	void __iomem *nic_base = ei_local->mem;
	char *buf = skb->data;

	if (ei_local->dmaing) {
		netdev_err(dev,
			   "DMAing conflict in %s [DMAstat:%d][irqlock:%d]\n",
			   __func__,
			   ei_local->dmaing, ei_local->irqlock);
		return;
	}

	ei_local->dmaing |= 0x01;

	ei_outb(E8390_NODMA + E8390_PAGE0 + E8390_START, nic_base + NE_CMD);
	ei_outb(count & 0xff, nic_base + EN0_RCNTLO);
	ei_outb(count >> 8, nic_base + EN0_RCNTHI);
	ei_outb(ring_offset & 0xff, nic_base + EN0_RSARLO);
	ei_outb(ring_offset >> 8, nic_base + EN0_RSARHI);
	ei_outb(E8390_RREAD + E8390_START, nic_base + NE_CMD);

	xs100_read(dev, buf, count);

	ei_local->dmaing &= ~1;
}

static void xs100_block_output(struct net_device *dev, int count,
			       const unsigned char *buf, const int start_page)
{
	struct ei_device *ei_local = netdev_priv(dev);
	void __iomem *nic_base = ei_local->mem;
	unsigned long dma_start;

	/* Round the count up for word writes. Do we need to do this?
	 * What effect will an odd byte count have on the 8390?  I
	 * should check someday.
	 */
	if (ei_local->word16 && (count & 0x01))
		count++;

	/* This *shouldn't* happen. If it does, it's the last thing
	 * you'll see
	 */
	if (ei_local->dmaing) {
		netdev_err(dev,
			   "DMAing conflict in %s [DMAstat:%d][irqlock:%d]\n",
			   __func__,
			   ei_local->dmaing, ei_local->irqlock);
		return;
	}

	ei_local->dmaing |= 0x01;
	/* We should already be in page 0, but to be safe... */
	ei_outb(E8390_PAGE0 + E8390_START + E8390_NODMA, nic_base + NE_CMD);

	ei_outb(ENISR_RDC, nic_base + EN0_ISR);

	/* Now the normal output. */
	ei_outb(count & 0xff, nic_base + EN0_RCNTLO);
	ei_outb(count >> 8, nic_base + EN0_RCNTHI);
	ei_outb(0x00, nic_base + EN0_RSARLO);
	ei_outb(start_page, nic_base + EN0_RSARHI);

	ei_outb(E8390_RWRITE + E8390_START, nic_base + NE_CMD);

	xs100_write(dev, buf, count);

	dma_start = jiffies;

	while ((ei_inb(nic_base + EN0_ISR) & ENISR_RDC) == 0) {
		if (jiffies - dma_start > 2 * HZ / 100) {	/* 20ms */
			netdev_warn(dev, "timeout waiting for Tx RDC.\n");
			ei_local->reset_8390(dev);
			ax_NS8390_init(dev, 1);
			break;
		}
	}

	ei_outb(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_local->dmaing &= ~0x01;
}

static int xsurf100_probe(struct zorro_dev *zdev,
			  const struct zorro_device_id *ent)
{
	struct platform_device *pdev;
	struct xsurf100_ax_plat_data ax88796_data;
	struct resource res[2] = {
		DEFINE_RES_NAMED(IRQ_AMIGA_PORTS, 1, NULL,
				 IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE),
		DEFINE_RES_MEM(zdev->resource.start + XS100_8390_BASE,
			       4 * 0x20)
	};
	int reg;
	/* This table is referenced in the device structure, so it must
	 * outlive the scope of xsurf100_probe.
	 */
	static u32 reg_offsets[32];
	int ret = 0;

	/* X-Surf 100 control and 32 bit ring buffer data access areas.
	 * These resources are not used by the ax88796 driver, so must
	 * be requested here and passed via platform data.
	 */

	if (!request_mem_region(zdev->resource.start, 0x100, zdev->name)) {
		dev_err(&zdev->dev, "cannot reserve X-Surf 100 control registers\n");
		return -ENXIO;
	}

	if (!request_mem_region(zdev->resource.start +
				XS100_8390_DATA32_BASE,
				XS100_8390_DATA32_SIZE,
				"X-Surf 100 32-bit data access")) {
		dev_err(&zdev->dev, "cannot reserve 32-bit area\n");
		ret = -ENXIO;
		goto exit_req;
	}

	for (reg = 0; reg < 0x20; reg++)
		reg_offsets[reg] = 4 * reg;

	memset(&ax88796_data, 0, sizeof(ax88796_data));
	ax88796_data.ax.flags = AXFLG_HAS_EEPROM;
	ax88796_data.ax.wordlength = 2;
	ax88796_data.ax.dcr_val = 0x48;
	ax88796_data.ax.rcr_val = 0x40;
	ax88796_data.ax.reg_offsets = reg_offsets;
	ax88796_data.ax.check_irq = is_xsurf100_network_irq;
	ax88796_data.base_regs = ioremap(zdev->resource.start, 0x100);

	/* error handling for ioremap regs */
	if (!ax88796_data.base_regs) {
		dev_err(&zdev->dev, "Cannot ioremap area %pR (registers)\n",
			&zdev->resource);

		ret = -ENXIO;
		goto exit_req2;
	}

	ax88796_data.data_area = ioremap(zdev->resource.start +
			XS100_8390_DATA32_BASE, XS100_8390_DATA32_SIZE);

	/* error handling for ioremap data */
	if (!ax88796_data.data_area) {
		dev_err(&zdev->dev,
			"Cannot ioremap area %pR offset %x (32-bit access)\n",
			&zdev->resource,  XS100_8390_DATA32_BASE);

		ret = -ENXIO;
		goto exit_mem;
	}

	ax88796_data.ax.block_output = xs100_block_output;
	ax88796_data.ax.block_input = xs100_block_input;

	pdev = platform_device_register_resndata(&zdev->dev, "ax88796",
						 zdev->slotaddr, res, 2,
						 &ax88796_data,
						 sizeof(ax88796_data));

	if (IS_ERR(pdev)) {
		dev_err(&zdev->dev, "cannot register platform device\n");
		ret = -ENXIO;
		goto exit_mem2;
	}

	zorro_set_drvdata(zdev, pdev);

	if (!ret)
		return 0;

 exit_mem2:
	iounmap(ax88796_data.data_area);

 exit_mem:
	iounmap(ax88796_data.base_regs);

 exit_req2:
	release_mem_region(zdev->resource.start + XS100_8390_DATA32_BASE,
			   XS100_8390_DATA32_SIZE);

 exit_req:
	release_mem_region(zdev->resource.start, 0x100);

	return ret;
}

static void xsurf100_remove(struct zorro_dev *zdev)
{
	struct platform_device *pdev = zorro_get_drvdata(zdev);
	struct xsurf100_ax_plat_data *xs100 = dev_get_platdata(&pdev->dev);

	platform_device_unregister(pdev);

	iounmap(xs100->base_regs);
	release_mem_region(zdev->resource.start, 0x100);
	iounmap(xs100->data_area);
	release_mem_region(zdev->resource.start + XS100_8390_DATA32_BASE,
			   XS100_8390_DATA32_SIZE);
}

static const struct zorro_device_id xsurf100_zorro_tbl[] = {
	{ ZORRO_PROD_INDIVIDUAL_COMPUTERS_X_SURF100, },
	{ 0 }
};

MODULE_DEVICE_TABLE(zorro, xsurf100_zorro_tbl);

static struct zorro_driver xsurf100_driver = {
	.name           = "xsurf100",
	.id_table       = xsurf100_zorro_tbl,
	.probe          = xsurf100_probe,
	.remove         = xsurf100_remove,
};

module_driver(xsurf100_driver, zorro_register_driver, zorro_unregister_driver);

MODULE_DESCRIPTION("X-Surf 100 driver");
MODULE_AUTHOR("Michael Karcher <kernel@mkarcher.dialup.fu-berlin.de>");
MODULE_LICENSE("GPL v2");
