/**
 * 	mpc5xxx_get_bus_frequency - Find the bus frequency for a device
 * 	@node:	device node
 *
 * 	Returns bus frequency (IPS on MPC512x, IPB on MPC52xx),
 * 	or 0 if the bus frequency cannot be found.
 */

#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/export.h>
#include <asm/mpc5xxx.h>

unsigned long mpc5xxx_get_bus_frequency(struct device_node *node)
{
	struct device_node *np;
	const unsigned int *p_bus_freq = NULL;

	of_node_get(node);
	while (node) {
		p_bus_freq = of_get_property(node, "bus-frequency", NULL);
		if (p_bus_freq)
			break;

		np = of_get_parent(node);
		of_node_put(node);
		node = np;
	}
	of_node_put(node);

	return p_bus_freq ? *p_bus_freq : 0;
}
EXPORT_SYMBOL(mpc5xxx_get_bus_frequency);
