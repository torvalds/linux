/*
 * ACPI GSI IRQ layer
 *
 * Copyright (C) 2015 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/acpi.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>

enum acpi_irq_model_id acpi_irq_model;

static struct fwnode_handle *acpi_gsi_domain_id;

static unsigned int acpi_gsi_get_irq_type(int trigger, int polarity)
{
	switch (polarity) {
	case ACPI_ACTIVE_LOW:
		return trigger == ACPI_EDGE_SENSITIVE ?
		       IRQ_TYPE_EDGE_FALLING :
		       IRQ_TYPE_LEVEL_LOW;
	case ACPI_ACTIVE_HIGH:
		return trigger == ACPI_EDGE_SENSITIVE ?
		       IRQ_TYPE_EDGE_RISING :
		       IRQ_TYPE_LEVEL_HIGH;
	case ACPI_ACTIVE_BOTH:
		if (trigger == ACPI_EDGE_SENSITIVE)
			return IRQ_TYPE_EDGE_BOTH;
	default:
		return IRQ_TYPE_NONE;
	}
}

/**
 * acpi_gsi_to_irq() - Retrieve the linux irq number for a given GSI
 * @gsi: GSI IRQ number to map
 * @irq: pointer where linux IRQ number is stored
 *
 * irq location updated with irq value [>0 on success, 0 on failure]
 *
 * Returns: linux IRQ number on success (>0)
 *          -EINVAL on failure
 */
int acpi_gsi_to_irq(u32 gsi, unsigned int *irq)
{
	struct irq_domain *d = irq_find_matching_fwnode(acpi_gsi_domain_id,
							DOMAIN_BUS_ANY);

	*irq = irq_find_mapping(d, gsi);
	/*
	 * *irq == 0 means no mapping, that should
	 * be reported as a failure
	 */
	return (*irq > 0) ? *irq : -EINVAL;
}
EXPORT_SYMBOL_GPL(acpi_gsi_to_irq);

/**
 * acpi_register_gsi() - Map a GSI to a linux IRQ number
 * @dev: device for which IRQ has to be mapped
 * @gsi: GSI IRQ number
 * @trigger: trigger type of the GSI number to be mapped
 * @polarity: polarity of the GSI to be mapped
 *
 * Returns: a valid linux IRQ number on success
 *          -EINVAL on failure
 */
int acpi_register_gsi(struct device *dev, u32 gsi, int trigger,
		      int polarity)
{
	struct irq_fwspec fwspec;

	if (WARN_ON(!acpi_gsi_domain_id)) {
		pr_warn("GSI: No registered irqchip, giving up\n");
		return -EINVAL;
	}

	fwspec.fwnode = acpi_gsi_domain_id;
	fwspec.param[0] = gsi;
	fwspec.param[1] = acpi_gsi_get_irq_type(trigger, polarity);
	fwspec.param_count = 2;

	return irq_create_fwspec_mapping(&fwspec);
}
EXPORT_SYMBOL_GPL(acpi_register_gsi);

/**
 * acpi_unregister_gsi() - Free a GSI<->linux IRQ number mapping
 * @gsi: GSI IRQ number
 */
void acpi_unregister_gsi(u32 gsi)
{
	struct irq_domain *d = irq_find_matching_fwnode(acpi_gsi_domain_id,
							DOMAIN_BUS_ANY);
	int irq = irq_find_mapping(d, gsi);

	irq_dispose_mapping(irq);
}
EXPORT_SYMBOL_GPL(acpi_unregister_gsi);

/**
 * acpi_set_irq_model - Setup the GSI irqdomain information
 * @model: the value assigned to acpi_irq_model
 * @fwnode: the irq_domain identifier for mapping and looking up
 *          GSI interrupts
 */
void __init acpi_set_irq_model(enum acpi_irq_model_id model,
			       struct fwnode_handle *fwnode)
{
	acpi_irq_model = model;
	acpi_gsi_domain_id = fwnode;
}
