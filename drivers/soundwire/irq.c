// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2023 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <linux/device.h>
#include <linux/fwnode.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/soundwire/sdw.h>
#include "irq.h"

static int sdw_irq_map(struct irq_domain *h, unsigned int virq,
		       irq_hw_number_t hw)
{
	struct sdw_bus *bus = h->host_data;

	irq_set_chip_data(virq, bus);
	irq_set_chip(virq, &bus->irq_chip);
	irq_set_nested_thread(virq, 1);
	irq_set_noprobe(virq);

	return 0;
}

static const struct irq_domain_ops sdw_domain_ops = {
	.map	= sdw_irq_map,
};

int sdw_irq_create(struct sdw_bus *bus,
		   struct fwnode_handle *fwnode)
{
	bus->irq_chip.name = dev_name(bus->dev);

	bus->domain = irq_domain_create_linear(fwnode, SDW_MAX_DEVICES,
					       &sdw_domain_ops, bus);
	if (!bus->domain) {
		dev_err(bus->dev, "Failed to add IRQ domain\n");
		return -EINVAL;
	}

	return 0;
}

void sdw_irq_delete(struct sdw_bus *bus)
{
	irq_domain_remove(bus->domain);
}

void sdw_irq_create_mapping(struct sdw_slave *slave)
{
	slave->irq = irq_create_mapping(slave->bus->domain, slave->dev_num);
	if (!slave->irq)
		dev_warn(&slave->dev, "Failed to map IRQ\n");
}

void sdw_irq_dispose_mapping(struct sdw_slave *slave)
{
	irq_dispose_mapping(irq_find_mapping(slave->bus->domain, slave->dev_num));
}
