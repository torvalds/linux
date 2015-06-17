/*
 * Copyright (C) 2014 Magnus Damm
 * Copyright (C) 2015 Glider bvba
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#define pr_fmt(fmt)	"board_staging: "  fmt

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "board.h"

static struct device_node *irqc_node __initdata;
static unsigned int irqc_base __initdata;

static bool find_by_address(u64 base_address)
{
	struct device_node *dn = of_find_all_nodes(NULL);
	struct resource res;

	while (dn) {
		if (!of_address_to_resource(dn, 0, &res)) {
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

int __init board_staging_gic_setup_xlate(const char *gic_match,
					 unsigned int base)
{
	WARN_ON(irqc_node);

	irqc_node = of_find_compatible_node(NULL, NULL, gic_match);

	WARN_ON(!irqc_node);
	if (!irqc_node)
		return -ENOENT;

	irqc_base = base;
	return 0;
}

static void __init gic_fixup_resource(struct resource *res)
{
	struct of_phandle_args irq_data;
	unsigned int hwirq = res->start;
	unsigned int virq;

	if (resource_type(res) != IORESOURCE_IRQ || !irqc_node)
		return;

	irq_data.np = irqc_node;
	irq_data.args_count = 3;
	irq_data.args[0] = 0;
	irq_data.args[1] = hwirq - irqc_base;
	switch (res->flags &
		(IORESOURCE_IRQ_LOWEDGE | IORESOURCE_IRQ_HIGHEDGE |
		 IORESOURCE_IRQ_LOWLEVEL | IORESOURCE_IRQ_HIGHLEVEL)) {
	case IORESOURCE_IRQ_LOWEDGE:
		irq_data.args[2] = IRQ_TYPE_EDGE_FALLING;
		break;
	case IORESOURCE_IRQ_HIGHEDGE:
		irq_data.args[2] = IRQ_TYPE_EDGE_RISING;
		break;
	case IORESOURCE_IRQ_LOWLEVEL:
		irq_data.args[2] = IRQ_TYPE_LEVEL_LOW;
		break;
	case IORESOURCE_IRQ_HIGHLEVEL:
	default:
		irq_data.args[2] = IRQ_TYPE_LEVEL_HIGH;
		break;
	}

	virq = irq_create_of_mapping(&irq_data);
	if (WARN_ON(!virq))
		return;

	pr_debug("hwirq %u -> virq %u\n", hwirq, virq);
	res->start = virq;
}

void __init board_staging_gic_fixup_resources(struct resource *res,
					      unsigned int nres)
{
	unsigned int i;

	for (i = 0; i < nres; i++)
		gic_fixup_resource(&res[i]);
}
