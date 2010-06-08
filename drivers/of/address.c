
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of_address.h>

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
