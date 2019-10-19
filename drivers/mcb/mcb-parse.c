// SPDX-License-Identifier: GPL-2.0-only
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/mcb.h>

#include "mcb-internal.h"

struct mcb_parse_priv {
	phys_addr_t mapbase;
	void __iomem *base;
};

#define for_each_chameleon_cell(dtype, p)	\
	for ((dtype) = get_next_dtype((p));	\
	     (dtype) != CHAMELEON_DTYPE_END;	\
	     (dtype) = get_next_dtype((p)))

static inline uint32_t get_next_dtype(void __iomem *p)
{
	uint32_t dtype;

	dtype = readl(p);
	return dtype >> 28;
}

static int chameleon_parse_bdd(struct mcb_bus *bus,
			struct chameleon_bar *cb,
			void __iomem *base)
{
	return 0;
}

static int chameleon_parse_gdd(struct mcb_bus *bus,
			struct chameleon_bar *cb,
			void __iomem *base, int bar_count)
{
	struct chameleon_gdd __iomem *gdd =
		(struct chameleon_gdd __iomem *) base;
	struct mcb_device *mdev;
	u32 dev_mapbase;
	u32 offset;
	u32 size;
	int ret;
	__le32 reg1;
	__le32 reg2;

	mdev = mcb_alloc_dev(bus);
	if (!mdev)
		return -ENOMEM;

	reg1 = readl(&gdd->reg1);
	reg2 = readl(&gdd->reg2);
	offset = readl(&gdd->offset);
	size = readl(&gdd->size);

	mdev->id = GDD_DEV(reg1);
	mdev->rev = GDD_REV(reg1);
	mdev->var = GDD_VAR(reg1);
	mdev->bar = GDD_BAR(reg2);
	mdev->group = GDD_GRP(reg2);
	mdev->inst = GDD_INS(reg2);

	/*
	 * If the BAR is missing, dev_mapbase is zero, or if the
	 * device is IO mapped we just print a warning and go on with the
	 * next device, instead of completely stop the gdd parser
	 */
	if (mdev->bar > bar_count - 1) {
		pr_info("No BAR for 16z%03d\n", mdev->id);
		ret = 0;
		goto err;
	}

	dev_mapbase = cb[mdev->bar].addr;
	if (!dev_mapbase) {
		pr_info("BAR not assigned for 16z%03d\n", mdev->id);
		ret = 0;
		goto err;
	}

	if (dev_mapbase & 0x01) {
		pr_info("IO mapped Device (16z%03d) not yet supported\n",
			mdev->id);
		ret = 0;
		goto err;
	}

	pr_debug("Found a 16z%03d\n", mdev->id);

	mdev->irq.start = GDD_IRQ(reg1);
	mdev->irq.end = GDD_IRQ(reg1);
	mdev->irq.flags = IORESOURCE_IRQ;

	mdev->mem.start = dev_mapbase + offset;

	mdev->mem.end = mdev->mem.start + size - 1;
	mdev->mem.flags = IORESOURCE_MEM;

	mdev->is_added = false;

	ret = mcb_device_register(bus, mdev);
	if (ret < 0)
		goto err;

	return 0;

err:
	mcb_free_dev(mdev);

	return ret;
}

static void chameleon_parse_bar(void __iomem *base,
				struct chameleon_bar *cb, int bar_count)
{
	char __iomem *p = base;
	int i;

	/* skip reg1 */
	p += sizeof(__le32);

	for (i = 0; i < bar_count; i++) {
		cb[i].addr = readl(p);
		cb[i].size = readl(p + 4);

		p += sizeof(struct chameleon_bar);
	}
}

static int chameleon_get_bar(char __iomem **base, phys_addr_t mapbase,
			     struct chameleon_bar **cb)
{
	struct chameleon_bar *c;
	int bar_count;
	__le32 reg;
	u32 dtype;

	/*
	 * For those devices which are not connected
	 * to the PCI Bus (e.g. LPC) there is a bar
	 * descriptor located directly after the
	 * chameleon header. This header is comparable
	 * to a PCI header.
	 */
	dtype = get_next_dtype(*base);
	if (dtype == CHAMELEON_DTYPE_BAR) {
		reg = readl(*base);

		bar_count = BAR_CNT(reg);
		if (bar_count <= 0 || bar_count > CHAMELEON_BAR_MAX)
			return -ENODEV;

		c = kcalloc(bar_count, sizeof(struct chameleon_bar),
			    GFP_KERNEL);
		if (!c)
			return -ENOMEM;

		chameleon_parse_bar(*base, c, bar_count);
		*base += BAR_DESC_SIZE(bar_count);
	} else {
		c = kzalloc(sizeof(struct chameleon_bar), GFP_KERNEL);
		if (!c)
			return -ENOMEM;

		bar_count = 1;
		c->addr = mapbase;
	}

	*cb = c;

	return bar_count;
}

int chameleon_parse_cells(struct mcb_bus *bus, phys_addr_t mapbase,
			void __iomem *base)
{
	struct chameleon_fpga_header *header;
	struct chameleon_bar *cb;
	char __iomem *p = base;
	int num_cells = 0;
	uint32_t dtype;
	int bar_count;
	int ret;
	u32 hsize;

	hsize = sizeof(struct chameleon_fpga_header);

	header = kzalloc(hsize, GFP_KERNEL);
	if (!header)
		return -ENOMEM;

	/* Extract header information */
	memcpy_fromio(header, p, hsize);
	/* We only support chameleon v2 at the moment */
	header->magic = le16_to_cpu(header->magic);
	if (header->magic != CHAMELEONV2_MAGIC) {
		pr_err("Unsupported chameleon version 0x%x\n",
				header->magic);
		ret = -ENODEV;
		goto free_header;
	}
	p += hsize;

	bus->revision = header->revision;
	bus->model = header->model;
	bus->minor = header->minor;
	snprintf(bus->name, CHAMELEON_FILENAME_LEN + 1, "%s",
		 header->filename);

	bar_count = chameleon_get_bar(&p, mapbase, &cb);
	if (bar_count < 0) {
		ret = bar_count;
		goto free_header;
	}

	for_each_chameleon_cell(dtype, p) {
		switch (dtype) {
		case CHAMELEON_DTYPE_GENERAL:
			ret = chameleon_parse_gdd(bus, cb, p, bar_count);
			if (ret < 0)
				goto free_bar;
			p += sizeof(struct chameleon_gdd);
			break;
		case CHAMELEON_DTYPE_BRIDGE:
			chameleon_parse_bdd(bus, cb, p);
			p += sizeof(struct chameleon_bdd);
			break;
		case CHAMELEON_DTYPE_END:
			break;
		default:
			pr_err("Invalid chameleon descriptor type 0x%x\n",
				dtype);
			ret = -EINVAL;
			goto free_bar;
		}
		num_cells++;
	}

	if (num_cells == 0)
		num_cells = -EINVAL;

	kfree(cb);
	kfree(header);
	return num_cells;

free_bar:
	kfree(cb);
free_header:
	kfree(header);

	return ret;
}
EXPORT_SYMBOL_GPL(chameleon_parse_cells);
