
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of_address.h>

int __of_address_to_resource(struct device_node *dev, const u32 *addrp,
			     u64 size, unsigned int flags,
			     struct resource *r)
{
	u64 taddr;

	if ((flags & (IORESOURCE_IO | IORESOURCE_MEM)) == 0)
		return -EINVAL;
	taddr = of_translate_address(dev, addrp);
	if (taddr == OF_BAD_ADDR)
		return -EINVAL;
	memset(r, 0, sizeof(struct resource));
	if (flags & IORESOURCE_IO) {
		unsigned long port;
		port = pci_address_to_pio(taddr);
		if (port == (unsigned long)-1)
			return -EINVAL;
		r->start = port;
		r->end = port + size - 1;
	} else {
		r->start = taddr;
		r->end = taddr + size - 1;
	}
	r->flags = flags;
	r->name = dev->name;
	return 0;
}

/**
 * of_address_to_resource - Translate device tree address and return as resource
 *
 * Note that if your address is a PIO address, the conversion will fail if
 * the physical address can't be internally converted to an IO token with
 * pci_address_to_pio(), that is because it's either called to early or it
 * can't be matched to any host bridge IO space
 */
int of_address_to_resource(struct device_node *dev, int index,
			   struct resource *r)
{
	const u32	*addrp;
	u64		size;
	unsigned int	flags;

	addrp = of_get_address(dev, index, &size, &flags);
	if (addrp == NULL)
		return -EINVAL;
	return __of_address_to_resource(dev, addrp, size, flags, r);
}
EXPORT_SYMBOL_GPL(of_address_to_resource);


/**
 * of_iomap - Maps the memory mapped IO for a given device_node
 * @device:	the device whose io range will be mapped
 * @index:	index of the io range
 *
 * Returns a pointer to the mapped memory
 */
void __iomem *of_iomap(struct device_node *np, int index)
{
	struct resource res;

	if (of_address_to_resource(np, index, &res))
		return NULL;

	return ioremap(res.start, 1 + res.end - res.start);
}
EXPORT_SYMBOL(of_iomap);
