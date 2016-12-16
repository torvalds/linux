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
			phys_addr_t mapbase,
			void __iomem *base)
{
	return 0;
}

static int chameleon_parse_gdd(struct mcb_bus *bus,
			phys_addr_t mapbase,
			void __iomem *base)
{
	struct chameleon_gdd __iomem *gdd =
		(struct chameleon_gdd __iomem *) base;
	struct mcb_device *mdev;
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

	pr_debug("Found a 16z%03d\n", mdev->id);

	mdev->irq.start = GDD_IRQ(reg1);
	mdev->irq.end = GDD_IRQ(reg1);
	mdev->irq.flags = IORESOURCE_IRQ;

	mdev->mem.start = mapbase + offset;
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

int chameleon_parse_cells(struct mcb_bus *bus, phys_addr_t mapbase,
			void __iomem *base)
{
	char __iomem *p = base;
	struct chameleon_fpga_header *header;
	uint32_t dtype;
	int num_cells = 0;
	int ret = 0;
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
		kfree(header);
		return -ENODEV;
	}
	p += hsize;

	bus->revision = header->revision;
	bus->model = header->model;
	bus->minor = header->minor;
	snprintf(bus->name, CHAMELEON_FILENAME_LEN + 1, "%s",
		 header->filename);

	for_each_chameleon_cell(dtype, p) {
		switch (dtype) {
		case CHAMELEON_DTYPE_GENERAL:
			ret = chameleon_parse_gdd(bus, mapbase, p);
			if (ret < 0)
				goto out;
			p += sizeof(struct chameleon_gdd);
			break;
		case CHAMELEON_DTYPE_BRIDGE:
			chameleon_parse_bdd(bus, mapbase, p);
			p += sizeof(struct chameleon_bdd);
			break;
		case CHAMELEON_DTYPE_END:
			break;
		default:
			pr_err("Invalid chameleon descriptor type 0x%x\n",
				dtype);
			kfree(header);
			return -EINVAL;
		}
		num_cells++;
	}

	if (num_cells == 0)
		num_cells = -EINVAL;

	kfree(header);
	return num_cells;

out:
	kfree(header);
	return ret;
}
EXPORT_SYMBOL_GPL(chameleon_parse_cells);
