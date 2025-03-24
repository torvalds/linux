// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe cooling device
 *
 * Copyright (C) 2023-2024 Intel Corporation
 */

#include <linux/build_bug.h>
#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci-bwctrl.h>
#include <linux/slab.h>
#include <linux/sprintf.h>
#include <linux/thermal.h>

#define COOLING_DEV_TYPE_PREFIX		"PCIe_Port_Link_Speed_"

static int pcie_cooling_get_max_level(struct thermal_cooling_device *cdev, unsigned long *state)
{
	struct pci_dev *port = cdev->devdata;

	/* cooling state 0 is same as the maximum PCIe speed */
	*state = port->subordinate->max_bus_speed - PCIE_SPEED_2_5GT;

	return 0;
}

static int pcie_cooling_get_cur_level(struct thermal_cooling_device *cdev, unsigned long *state)
{
	struct pci_dev *port = cdev->devdata;

	/* cooling state 0 is same as the maximum PCIe speed */
	*state = cdev->max_state - (port->subordinate->cur_bus_speed - PCIE_SPEED_2_5GT);

	return 0;
}

static int pcie_cooling_set_cur_level(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct pci_dev *port = cdev->devdata;
	enum pci_bus_speed speed;

	/* cooling state 0 is same as the maximum PCIe speed */
	speed = (cdev->max_state - state) + PCIE_SPEED_2_5GT;

	return pcie_set_target_speed(port, speed, true);
}

static struct thermal_cooling_device_ops pcie_cooling_ops = {
	.get_max_state = pcie_cooling_get_max_level,
	.get_cur_state = pcie_cooling_get_cur_level,
	.set_cur_state = pcie_cooling_set_cur_level,
};

struct thermal_cooling_device *pcie_cooling_device_register(struct pci_dev *port)
{
	char *name __free(kfree) =
		kasprintf(GFP_KERNEL, COOLING_DEV_TYPE_PREFIX "%s", pci_name(port));
	if (!name)
		return ERR_PTR(-ENOMEM);

	return thermal_cooling_device_register(name, port, &pcie_cooling_ops);
}

void pcie_cooling_device_unregister(struct thermal_cooling_device *cdev)
{
	thermal_cooling_device_unregister(cdev);
}

/* For bus_speed <-> state arithmetic */
static_assert(PCIE_SPEED_2_5GT + 1 == PCIE_SPEED_5_0GT);
static_assert(PCIE_SPEED_5_0GT + 1 == PCIE_SPEED_8_0GT);
static_assert(PCIE_SPEED_8_0GT + 1 == PCIE_SPEED_16_0GT);
static_assert(PCIE_SPEED_16_0GT + 1 == PCIE_SPEED_32_0GT);
static_assert(PCIE_SPEED_32_0GT + 1 == PCIE_SPEED_64_0GT);

MODULE_AUTHOR("Ilpo Järvinen <ilpo.jarvinen@linux.intel.com>");
MODULE_DESCRIPTION("PCIe cooling driver");
