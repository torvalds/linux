/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023, STMicroelectronics - All Rights Reserved
 */

#ifndef _STM32_FIREWALL_H
#define _STM32_FIREWALL_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>

/**
 * STM32_PERIPHERAL_FIREWALL:		This type of firewall protects peripherals
 * STM32_MEMORY_FIREWALL:		This type of firewall protects memories/subsets of memory
 *					zones
 * STM32_NOTYPE_FIREWALL:		Undefined firewall type
 */

#define STM32_PERIPHERAL_FIREWALL	BIT(1)
#define STM32_MEMORY_FIREWALL		BIT(2)
#define STM32_NOTYPE_FIREWALL		BIT(3)

/**
 * struct stm32_firewall_controller - Information on firewall controller supplying services
 *
 * @name:			Name of the firewall controller
 * @dev:			Device reference of the firewall controller
 * @mmio:			Base address of the firewall controller
 * @entry:			List entry of the firewall controller list
 * @type:			Type of firewall
 * @max_entries:		Number of entries covered by the firewall
 * @grant_access:		Callback used to grant access for a device access against a
 *				firewall controller
 * @release_access:		Callback used to release resources taken by a device when access was
 *				granted
 * @grant_memory_range_access:	Callback used to grant access for a device to a given memory region
 */
struct stm32_firewall_controller {
	const char *name;
	struct device *dev;
	void __iomem *mmio;
	struct list_head entry;
	unsigned int type;
	unsigned int max_entries;

	int (*grant_access)(struct stm32_firewall_controller *ctrl, u32 id);
	void (*release_access)(struct stm32_firewall_controller *ctrl, u32 id);
	int (*grant_memory_range_access)(struct stm32_firewall_controller *ctrl, phys_addr_t paddr,
					 size_t size);
};

/**
 * stm32_firewall_controller_register - Register a firewall controller to the STM32 firewall
 *					framework
 * @firewall_controller:	Firewall controller to register
 *
 * Returns 0 in case of success or -ENODEV if no controller was given.
 */
int stm32_firewall_controller_register(struct stm32_firewall_controller *firewall_controller);

/**
 * stm32_firewall_controller_unregister - Unregister a firewall controller from the STM32
 *					  firewall framework
 * @firewall_controller:	Firewall controller to unregister
 */
void stm32_firewall_controller_unregister(struct stm32_firewall_controller *firewall_controller);

/**
 * stm32_firewall_populate_bus - Populate device tree nodes that have a correct firewall
 *				 configuration. This is used at boot-time only, as a sanity check
 *				 between device tree and firewalls hardware configurations to
 *				 prevent a kernel crash when a device driver is not granted access
 *
 * @firewall_controller:	Firewall controller which nodes will be populated or not
 *
 * Returns 0 in case of success or appropriate errno code if error occurred.
 */
int stm32_firewall_populate_bus(struct stm32_firewall_controller *firewall_controller);

#endif /* _STM32_FIREWALL_H */
