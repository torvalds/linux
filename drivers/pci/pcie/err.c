// SPDX-License-Identifier: GPL-2.0
/*
 * This file implements the error recovery as a core part of PCIe error
 * reporting. When a PCIe error is delivered, an error message will be
 * collected and printed to console, then, an error recovery procedure
 * will be executed by following the PCI error recovery rules.
 *
 * Copyright (C) 2006 Intel Corp.
 *	Tom Long Nguyen (tom.l.nguyen@intel.com)
 *	Zhang Yanmin (yanmin.zhang@intel.com)
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/aer.h>
#include "portdrv.h"
#include "../pci.h"

struct aer_broadcast_data {
	enum pci_channel_state state;
	enum pci_ers_result result;
};

static pci_ers_result_t merge_result(enum pci_ers_result orig,
				  enum pci_ers_result new)
{
	if (new == PCI_ERS_RESULT_NO_AER_DRIVER)
		return PCI_ERS_RESULT_NO_AER_DRIVER;

	if (new == PCI_ERS_RESULT_NONE)
		return orig;

	switch (orig) {
	case PCI_ERS_RESULT_CAN_RECOVER:
	case PCI_ERS_RESULT_RECOVERED:
		orig = new;
		break;
	case PCI_ERS_RESULT_DISCONNECT:
		if (new == PCI_ERS_RESULT_NEED_RESET)
			orig = PCI_ERS_RESULT_NEED_RESET;
		break;
	default:
		break;
	}

	return orig;
}

static int report_error_detected(struct pci_dev *dev, void *data)
{
	pci_ers_result_t vote;
	const struct pci_error_handlers *err_handler;
	struct aer_broadcast_data *result_data;

	result_data = (struct aer_broadcast_data *) data;

	device_lock(&dev->dev);
	dev->error_state = result_data->state;

	if (!dev->driver ||
		!dev->driver->err_handler ||
		!dev->driver->err_handler->error_detected) {
		/*
		 * If any device in the subtree does not have an error_detected
		 * callback, PCI_ERS_RESULT_NO_AER_DRIVER prevents subsequent
		 * error callbacks of "any" device in the subtree, and will
		 * exit in the disconnected error state.
		 */
		if (dev->hdr_type != PCI_HEADER_TYPE_BRIDGE)
			vote = PCI_ERS_RESULT_NO_AER_DRIVER;
		else
			vote = PCI_ERS_RESULT_NONE;
	} else {
		err_handler = dev->driver->err_handler;
		vote = err_handler->error_detected(dev, result_data->state);
		pci_uevent_ers(dev, PCI_ERS_RESULT_NONE);
	}

	result_data->result = merge_result(result_data->result, vote);
	device_unlock(&dev->dev);
	return 0;
}

static int report_mmio_enabled(struct pci_dev *dev, void *data)
{
	pci_ers_result_t vote;
	const struct pci_error_handlers *err_handler;
	struct aer_broadcast_data *result_data;

	result_data = (struct aer_broadcast_data *) data;

	device_lock(&dev->dev);
	if (!dev->driver ||
		!dev->driver->err_handler ||
		!dev->driver->err_handler->mmio_enabled)
		goto out;

	err_handler = dev->driver->err_handler;
	vote = err_handler->mmio_enabled(dev);
	result_data->result = merge_result(result_data->result, vote);
out:
	device_unlock(&dev->dev);
	return 0;
}

static int report_slot_reset(struct pci_dev *dev, void *data)
{
	pci_ers_result_t vote;
	const struct pci_error_handlers *err_handler;
	struct aer_broadcast_data *result_data;

	result_data = (struct aer_broadcast_data *) data;

	device_lock(&dev->dev);
	if (!dev->driver ||
		!dev->driver->err_handler ||
		!dev->driver->err_handler->slot_reset)
		goto out;

	err_handler = dev->driver->err_handler;
	vote = err_handler->slot_reset(dev);
	result_data->result = merge_result(result_data->result, vote);
out:
	device_unlock(&dev->dev);
	return 0;
}

static int report_resume(struct pci_dev *dev, void *data)
{
	const struct pci_error_handlers *err_handler;

	device_lock(&dev->dev);
	dev->error_state = pci_channel_io_normal;

	if (!dev->driver ||
		!dev->driver->err_handler ||
		!dev->driver->err_handler->resume)
		goto out;

	err_handler = dev->driver->err_handler;
	err_handler->resume(dev);
	pci_uevent_ers(dev, PCI_ERS_RESULT_RECOVERED);
out:
	device_unlock(&dev->dev);
	return 0;
}

/**
 * default_reset_link - default reset function
 * @dev: pointer to pci_dev data structure
 *
 * Invoked when performing link reset on a Downstream Port or a
 * Root Port with no aer driver.
 */
static pci_ers_result_t default_reset_link(struct pci_dev *dev)
{
	int rc;

	rc = pci_bus_error_reset(dev);
	pci_printk(KERN_DEBUG, dev, "downstream link has been reset\n");
	return rc ? PCI_ERS_RESULT_DISCONNECT : PCI_ERS_RESULT_RECOVERED;
}

static pci_ers_result_t reset_link(struct pci_dev *dev, u32 service)
{
	pci_ers_result_t status;
	struct pcie_port_service_driver *driver = NULL;

	driver = pcie_port_find_service(dev, service);
	if (driver && driver->reset_link) {
		status = driver->reset_link(dev);
	} else if (dev->has_secondary_link) {
		status = default_reset_link(dev);
	} else {
		pci_printk(KERN_DEBUG, dev, "no link-reset support at upstream device %s\n",
			pci_name(dev));
		return PCI_ERS_RESULT_DISCONNECT;
	}

	if (status != PCI_ERS_RESULT_RECOVERED) {
		pci_printk(KERN_DEBUG, dev, "link reset at upstream device %s failed\n",
			pci_name(dev));
		return PCI_ERS_RESULT_DISCONNECT;
	}

	return status;
}

/**
 * broadcast_error_message - handle message broadcast to downstream drivers
 * @dev: pointer to from where in a hierarchy message is broadcasted down
 * @state: error state
 * @error_mesg: message to print
 * @cb: callback to be broadcasted
 *
 * Invoked during error recovery process. Once being invoked, the content
 * of error severity will be broadcasted to all downstream drivers in a
 * hierarchy in question.
 */
static pci_ers_result_t broadcast_error_message(struct pci_dev *dev,
	enum pci_channel_state state,
	char *error_mesg,
	int (*cb)(struct pci_dev *, void *))
{
	struct aer_broadcast_data result_data;

	pci_printk(KERN_DEBUG, dev, "broadcast %s message\n", error_mesg);
	result_data.state = state;
	if (cb == report_error_detected)
		result_data.result = PCI_ERS_RESULT_CAN_RECOVER;
	else
		result_data.result = PCI_ERS_RESULT_RECOVERED;

	pci_walk_bus(dev->subordinate, cb, &result_data);
	return result_data.result;
}

/**
 * pcie_do_fatal_recovery - handle fatal error recovery process
 * @dev: pointer to a pci_dev data structure of agent detecting an error
 *
 * Invoked when an error is fatal. Once being invoked, removes the devices
 * beneath this AER agent, followed by reset link e.g. secondary bus reset
 * followed by re-enumeration of devices.
 */
void pcie_do_fatal_recovery(struct pci_dev *dev, u32 service)
{
	struct pci_dev *udev;
	struct pci_bus *parent;
	struct pci_dev *pdev, *temp;
	pci_ers_result_t result;

	if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE)
		udev = dev;
	else
		udev = dev->bus->self;

	parent = udev->subordinate;
	pci_lock_rescan_remove();
	pci_dev_get(dev);
	list_for_each_entry_safe_reverse(pdev, temp, &parent->devices,
					 bus_list) {
		pci_dev_get(pdev);
		pci_dev_set_disconnected(pdev, NULL);
		if (pci_has_subordinate(pdev))
			pci_walk_bus(pdev->subordinate,
				     pci_dev_set_disconnected, NULL);
		pci_stop_and_remove_bus_device(pdev);
		pci_dev_put(pdev);
	}

	result = reset_link(udev, service);

	if ((service == PCIE_PORT_SERVICE_AER) &&
	    (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE)) {
		/*
		 * If the error is reported by a bridge, we think this error
		 * is related to the downstream link of the bridge, so we
		 * do error recovery on all subordinates of the bridge instead
		 * of the bridge and clear the error status of the bridge.
		 */
		pci_aer_clear_fatal_status(dev);
		pci_aer_clear_device_status(dev);
	}

	if (result == PCI_ERS_RESULT_RECOVERED) {
		if (pcie_wait_for_link(udev, true))
			pci_rescan_bus(udev->bus);
		pci_info(dev, "Device recovery from fatal error successful\n");
	} else {
		pci_uevent_ers(dev, PCI_ERS_RESULT_DISCONNECT);
		pci_info(dev, "Device recovery from fatal error failed\n");
	}

	pci_dev_put(dev);
	pci_unlock_rescan_remove();
}

/**
 * pcie_do_nonfatal_recovery - handle nonfatal error recovery process
 * @dev: pointer to a pci_dev data structure of agent detecting an error
 *
 * Invoked when an error is nonfatal/fatal. Once being invoked, broadcast
 * error detected message to all downstream drivers within a hierarchy in
 * question and return the returned code.
 */
void pcie_do_nonfatal_recovery(struct pci_dev *dev)
{
	pci_ers_result_t status;
	enum pci_channel_state state;

	state = pci_channel_io_normal;

	/*
	 * Error recovery runs on all subordinates of the first downstream port.
	 * If the downstream port detected the error, it is cleared at the end.
	 */
	if (!(pci_pcie_type(dev) == PCI_EXP_TYPE_ROOT_PORT ||
	      pci_pcie_type(dev) == PCI_EXP_TYPE_DOWNSTREAM))
		dev = dev->bus->self;

	status = broadcast_error_message(dev,
			state,
			"error_detected",
			report_error_detected);

	if (status == PCI_ERS_RESULT_CAN_RECOVER)
		status = broadcast_error_message(dev,
				state,
				"mmio_enabled",
				report_mmio_enabled);

	if (status == PCI_ERS_RESULT_NEED_RESET) {
		/*
		 * TODO: Should call platform-specific
		 * functions to reset slot before calling
		 * drivers' slot_reset callbacks?
		 */
		status = broadcast_error_message(dev,
				state,
				"slot_reset",
				report_slot_reset);
	}

	if (status != PCI_ERS_RESULT_RECOVERED)
		goto failed;

	broadcast_error_message(dev,
				state,
				"resume",
				report_resume);

	pci_aer_clear_device_status(dev);
	pci_cleanup_aer_uncorrect_error_status(dev);
	pci_info(dev, "AER: Device recovery successful\n");
	return;

failed:
	pci_uevent_ers(dev, PCI_ERS_RESULT_DISCONNECT);

	/* TODO: Should kernel panic here? */
	pci_info(dev, "AER: Device recovery failed\n");
}
