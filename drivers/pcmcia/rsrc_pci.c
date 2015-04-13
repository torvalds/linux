#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include <pcmcia/ss.h>
#include <pcmcia/cistpl.h>
#include "cs_internal.h"


struct pcmcia_align_data {
	unsigned long	mask;
	unsigned long	offset;
};

static resource_size_t pcmcia_align(void *align_data,
				const struct resource *res,
				resource_size_t size, resource_size_t align)
{
	struct pcmcia_align_data *data = align_data;
	resource_size_t start;

	start = (res->start & ~data->mask) + data->offset;
	if (start < res->start)
		start += data->mask + 1;
	return start;
}

static struct resource *find_io_region(struct pcmcia_socket *s,
					unsigned long base, int num,
					unsigned long align)
{
	struct resource *res = pcmcia_make_resource(0, num, IORESOURCE_IO,
						dev_name(&s->dev));
	struct pcmcia_align_data data;
	int ret;

	data.mask = align - 1;
	data.offset = base & data.mask;

	ret = pci_bus_alloc_resource(s->cb_dev->bus, res, num, 1,
					     base, 0, pcmcia_align, &data);
	if (ret != 0) {
		kfree(res);
		res = NULL;
	}
	return res;
}

static int res_pci_find_io(struct pcmcia_socket *s, unsigned int attr,
			unsigned int *base, unsigned int num,
			unsigned int align, struct resource **parent)
{
	int i, ret = 0;

	/* Check for an already-allocated window that must conflict with
	 * what was asked for.  It is a hack because it does not catch all
	 * potential conflicts, just the most obvious ones.
	 */
	for (i = 0; i < MAX_IO_WIN; i++) {
		if (!s->io[i].res)
			continue;

		if (!*base)
			continue;

		if ((s->io[i].res->start & (align-1)) == *base)
			return -EBUSY;
	}

	for (i = 0; i < MAX_IO_WIN; i++) {
		struct resource *res = s->io[i].res;
		unsigned int try;

		if (res && (res->flags & IORESOURCE_BITS) !=
			(attr & IORESOURCE_BITS))
			continue;

		if (!res) {
			if (align == 0)
				align = 0x10000;

			res = s->io[i].res = find_io_region(s, *base, num,
								align);
			if (!res)
				return -EINVAL;

			*base = res->start;
			s->io[i].res->flags =
				((res->flags & ~IORESOURCE_BITS) |
					(attr & IORESOURCE_BITS));
			s->io[i].InUse = num;
			*parent = res;
			return 0;
		}

		/* Try to extend top of window */
		try = res->end + 1;
		if ((*base == 0) || (*base == try)) {
			ret = adjust_resource(s->io[i].res, res->start,
					      resource_size(res) + num);
			if (ret)
				continue;
			*base = try;
			s->io[i].InUse += num;
			*parent = res;
			return 0;
		}

		/* Try to extend bottom of window */
		try = res->start - num;
		if ((*base == 0) || (*base == try)) {
			ret = adjust_resource(s->io[i].res,
					      res->start - num,
					      resource_size(res) + num);
			if (ret)
				continue;
			*base = try;
			s->io[i].InUse += num;
			*parent = res;
			return 0;
		}
	}
	return -EINVAL;
}

static struct resource *res_pci_find_mem(u_long base, u_long num,
		u_long align, int low, struct pcmcia_socket *s)
{
	struct resource *res = pcmcia_make_resource(0, num, IORESOURCE_MEM,
						dev_name(&s->dev));
	struct pcmcia_align_data data;
	unsigned long min;
	int ret;

	if (align < 0x20000)
		align = 0x20000;
	data.mask = align - 1;
	data.offset = base & data.mask;

	min = 0;
	if (!low)
		min = 0x100000UL;

	ret = pci_bus_alloc_resource(s->cb_dev->bus,
			res, num, 1, min, 0,
			pcmcia_align, &data);

	if (ret != 0) {
		kfree(res);
		res = NULL;
	}
	return res;
}


static int res_pci_init(struct pcmcia_socket *s)
{
	if (!s->cb_dev || !(s->features & SS_CAP_PAGE_REGS)) {
		dev_err(&s->dev, "not supported by res_pci\n");
		return -EOPNOTSUPP;
	}
	return 0;
}

struct pccard_resource_ops pccard_nonstatic_ops = {
	.validate_mem = NULL,
	.find_io = res_pci_find_io,
	.find_mem = res_pci_find_mem,
	.init = res_pci_init,
	.exit = NULL,
};
EXPORT_SYMBOL(pccard_nonstatic_ops);
