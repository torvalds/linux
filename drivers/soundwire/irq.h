/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef __SDW_IRQ_H
#define __SDW_IRQ_H

#include <linux/soundwire/sdw.h>
#include <linux/fwnode.h>

#if IS_ENABLED(CONFIG_IRQ_DOMAIN)

int sdw_irq_create(struct sdw_bus *bus,
		   struct fwnode_handle *fwnode);
void sdw_irq_delete(struct sdw_bus *bus);
void sdw_irq_create_mapping(struct sdw_slave *slave);

#else /* CONFIG_IRQ_DOMAIN */

static inline int sdw_irq_create(struct sdw_bus *bus,
				 struct fwnode_handle *fwnode)
{
	return 0;
}

static inline void sdw_irq_delete(struct sdw_bus *bus)
{
}

static inline void sdw_irq_create_mapping(struct sdw_slave *slave)
{
}

#endif /* CONFIG_IRQ_DOMAIN */

#endif /* __SDW_IRQ_H */
