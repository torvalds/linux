#include <linux/init.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include "board.h"

static bool find_by_address(u64 base_address)
{
	struct device_node *dn = of_find_all_nodes(NULL);
	struct resource res;

	while (dn) {
		if (of_can_translate_address(dn)
		    && !of_address_to_resource(dn, 0, &res)) {
			if (res.start == base_address) {
				of_node_put(dn);
				return true;
			}
		}
		dn = of_find_all_nodes(dn);
	}

	return false;
}

bool __init board_staging_dt_node_available(const struct resource *resource,
					    unsigned int num_resources)
{
	unsigned int i;

	for (i = 0; i < num_resources; i++) {
		const struct resource *r = resource + i;

		if (resource_type(r) == IORESOURCE_MEM)
			if (find_by_address(r->start))
				return true; /* DT node available */
	}

	return false; /* Nothing found */
}
