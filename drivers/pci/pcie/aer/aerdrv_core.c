/*
 * drivers/pci/pcie/aer/aerdrv_core.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This file implements the core part of PCI-Express AER. When an pci-express
 * error is delivered, an error message will be collected and printed to
 * console, then, an error recovery procedure will be executed by following
 * the pci error recovery rules.
 *
 * Copyright (C) 2006 Intel Corp.
 *	Tom Long Nguyen (tom.l.nguyen@intel.com)
 *	Zhang Yanmin (yanmin.zhang@intel.com)
 *
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include "aerdrv.h"

static int forceload;
module_param(forceload, bool, 0);

int pci_enable_pcie_error_reporting(struct pci_dev *dev)
{
	u16 reg16 = 0;
	int pos;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (!pos)
		return -EIO;

	pos = pci_find_capability(dev, PCI_CAP_ID_EXP);
	if (!pos)
		return -EIO;

	pci_read_config_word(dev, pos+PCI_EXP_DEVCTL, &reg16);
	reg16 = reg16 |
		PCI_EXP_DEVCTL_CERE |
		PCI_EXP_DEVCTL_NFERE |
		PCI_EXP_DEVCTL_FERE |
		PCI_EXP_DEVCTL_URRE;
	pci_write_config_word(dev, pos+PCI_EXP_DEVCTL,
			reg16);
	return 0;
}

int pci_disable_pcie_error_reporting(struct pci_dev *dev)
{
	u16 reg16 = 0;
	int pos;

	pos = pci_find_capability(dev, PCI_CAP_ID_EXP);
	if (!pos)
		return -EIO;

	pci_read_config_word(dev, pos+PCI_EXP_DEVCTL, &reg16);
	reg16 = reg16 & ~(PCI_EXP_DEVCTL_CERE |
			PCI_EXP_DEVCTL_NFERE |
			PCI_EXP_DEVCTL_FERE |
			PCI_EXP_DEVCTL_URRE);
	pci_write_config_word(dev, pos+PCI_EXP_DEVCTL,
			reg16);
	return 0;
}

int pci_cleanup_aer_uncorrect_error_status(struct pci_dev *dev)
{
	int pos;
	u32 status, mask;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (!pos)
		return -EIO;

	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_STATUS, &status);
	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_SEVER, &mask);
	if (dev->error_state == pci_channel_io_normal)
		status &= ~mask; /* Clear corresponding nonfatal bits */
	else
		status &= mask; /* Clear corresponding fatal bits */
	pci_write_config_dword(dev, pos + PCI_ERR_UNCOR_STATUS, status);

	return 0;
}

#if 0
int pci_cleanup_aer_correct_error_status(struct pci_dev *dev)
{
	int pos;
	u32 status;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (!pos)
		return -EIO;

	pci_read_config_dword(dev, pos + PCI_ERR_COR_STATUS, &status);
	pci_write_config_dword(dev, pos + PCI_ERR_COR_STATUS, status);

	return 0;
}
#endif  /*  0  */


static void set_device_error_reporting(struct pci_dev *dev, void *data)
{
	bool enable = *((bool *)data);

	if (dev->pcie_type != PCIE_RC_PORT &&
	    dev->pcie_type != PCIE_SW_UPSTREAM_PORT &&
	    dev->pcie_type != PCIE_SW_DOWNSTREAM_PORT)
		return;

	if (enable)
		pci_enable_pcie_error_reporting(dev);
	else
		pci_disable_pcie_error_reporting(dev);
}

/**
 * set_downstream_devices_error_reporting - enable/disable the error reporting  bits on the root port and its downstream ports.
 * @dev: pointer to root port's pci_dev data structure
 * @enable: true = enable error reporting, false = disable error reporting.
 */
static void set_downstream_devices_error_reporting(struct pci_dev *dev,
						   bool enable)
{
	set_device_error_reporting(dev, &enable);

	if (!dev->subordinate)
		return;
	pci_walk_bus(dev->subordinate, set_device_error_reporting, &enable);
}

static int find_device_iter(struct device *device, void *data)
{
	struct pci_dev *dev;
	u16 id = *(unsigned long *)data;
	u8 secondary, subordinate, d_bus = id >> 8;

	if (device->bus == &pci_bus_type) {
		dev = to_pci_dev(device);
		if (id == ((dev->bus->number << 8) | dev->devfn)) {
			/*
			 * Device ID match
			 */
			*(unsigned long*)data = (unsigned long)device;
			return 1;
		}

		/*
		 * If device is P2P, check if it is an upstream?
		 */
		if (dev->hdr_type & PCI_HEADER_TYPE_BRIDGE) {
			pci_read_config_byte(dev, PCI_SECONDARY_BUS,
				&secondary);
			pci_read_config_byte(dev, PCI_SUBORDINATE_BUS,
				&subordinate);
			if (d_bus >= secondary && d_bus <= subordinate) {
				*(unsigned long*)data = (unsigned long)device;
				return 1;
			}
		}
	}

	return 0;
}

/**
 * find_source_device - search through device hierarchy for source device
 * @parent: pointer to Root Port pci_dev data structure
 * @id: device ID of agent who sends an error message to this Root Port
 *
 * Invoked when error is detected at the Root Port.
 */
static struct device* find_source_device(struct pci_dev *parent, u16 id)
{
	struct pci_dev *dev = parent;
	struct device *device;
	unsigned long device_addr;
	int status;

	/* Is Root Port an agent that sends error message? */
	if (id == ((dev->bus->number << 8) | dev->devfn))
		return &dev->dev;

	do {
		device_addr = id;
 		if ((status = device_for_each_child(&dev->dev,
			&device_addr, find_device_iter))) {
			device = (struct device*)device_addr;
			dev = to_pci_dev(device);
			if (id == ((dev->bus->number << 8) | dev->devfn))
				return device;
		}
 	}while (status);

	return NULL;
}

static void report_error_detected(struct pci_dev *dev, void *data)
{
	pci_ers_result_t vote;
	struct pci_error_handlers *err_handler;
	struct aer_broadcast_data *result_data;
	result_data = (struct aer_broadcast_data *) data;

	dev->error_state = result_data->state;

	if (!dev->driver ||
		!dev->driver->err_handler ||
		!dev->driver->err_handler->error_detected) {
		if (result_data->state == pci_channel_io_frozen &&
			!(dev->hdr_type & PCI_HEADER_TYPE_BRIDGE)) {
			/*
			 * In case of fatal recovery, if one of down-
			 * stream device has no driver. We might be
			 * unable to recover because a later insmod
			 * of a driver for this device is unaware of
			 * its hw state.
			 */
			dev_printk(KERN_DEBUG, &dev->dev, "device has %s\n",
				   dev->driver ?
				   "no AER-aware driver" : "no driver");
		}
		return;
	}

	err_handler = dev->driver->err_handler;
	vote = err_handler->error_detected(dev, result_data->state);
	result_data->result = merge_result(result_data->result, vote);
	return;
}

static void report_mmio_enabled(struct pci_dev *dev, void *data)
{
	pci_ers_result_t vote;
	struct pci_error_handlers *err_handler;
	struct aer_broadcast_data *result_data;
	result_data = (struct aer_broadcast_data *) data;

	if (!dev->driver ||
		!dev->driver->err_handler ||
		!dev->driver->err_handler->mmio_enabled)
		return;

	err_handler = dev->driver->err_handler;
	vote = err_handler->mmio_enabled(dev);
	result_data->result = merge_result(result_data->result, vote);
	return;
}

static void report_slot_reset(struct pci_dev *dev, void *data)
{
	pci_ers_result_t vote;
	struct pci_error_handlers *err_handler;
	struct aer_broadcast_data *result_data;
	result_data = (struct aer_broadcast_data *) data;

	if (!dev->driver ||
		!dev->driver->err_handler ||
		!dev->driver->err_handler->slot_reset)
		return;

	err_handler = dev->driver->err_handler;
	vote = err_handler->slot_reset(dev);
	result_data->result = merge_result(result_data->result, vote);
	return;
}

static void report_resume(struct pci_dev *dev, void *data)
{
	struct pci_error_handlers *err_handler;

	dev->error_state = pci_channel_io_normal;

	if (!dev->driver ||
		!dev->driver->err_handler ||
		!dev->driver->err_handler->resume)
		return;

	err_handler = dev->driver->err_handler;
	err_handler->resume(dev);
	return;
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
	void (*cb)(struct pci_dev *, void *))
{
	struct aer_broadcast_data result_data;

	dev_printk(KERN_DEBUG, &dev->dev, "broadcast %s message\n", error_mesg);
	result_data.state = state;
	if (cb == report_error_detected)
		result_data.result = PCI_ERS_RESULT_CAN_RECOVER;
	else
		result_data.result = PCI_ERS_RESULT_RECOVERED;

	if (dev->hdr_type & PCI_HEADER_TYPE_BRIDGE) {
		/*
		 * If the error is reported by a bridge, we think this error
		 * is related to the downstream link of the bridge, so we
		 * do error recovery on all subordinates of the bridge instead
		 * of the bridge and clear the error status of the bridge.
		 */
		if (cb == report_error_detected)
			dev->error_state = state;
		pci_walk_bus(dev->subordinate, cb, &result_data);
		if (cb == report_resume) {
			pci_cleanup_aer_uncorrect_error_status(dev);
			dev->error_state = pci_channel_io_normal;
		}
	}
	else {
		/*
		 * If the error is reported by an end point, we think this
		 * error is related to the upstream link of the end point.
		 */
		pci_walk_bus(dev->bus, cb, &result_data);
	}

	return result_data.result;
}

struct find_aer_service_data {
	struct pcie_port_service_driver *aer_driver;
	int is_downstream;
};

static int find_aer_service_iter(struct device *device, void *data)
{
	struct device_driver *driver;
	struct pcie_port_service_driver *service_driver;
	struct pcie_device *pcie_dev;
	struct find_aer_service_data *result;

	result = (struct find_aer_service_data *) data;

	if (device->bus == &pcie_port_bus_type) {
		pcie_dev = to_pcie_device(device);
		if (pcie_dev->id.port_type == PCIE_SW_DOWNSTREAM_PORT)
			result->is_downstream = 1;

		driver = device->driver;
		if (driver) {
			service_driver = to_service_driver(driver);
			if (service_driver->id_table->service_type ==
					PCIE_PORT_SERVICE_AER) {
				result->aer_driver = service_driver;
				return 1;
			}
		}
	}

	return 0;
}

static void find_aer_service(struct pci_dev *dev,
		struct find_aer_service_data *data)
{
	int retval;
	retval = device_for_each_child(&dev->dev, data, find_aer_service_iter);
}

static pci_ers_result_t reset_link(struct pcie_device *aerdev,
		struct pci_dev *dev)
{
	struct pci_dev *udev;
	pci_ers_result_t status;
	struct find_aer_service_data data;

	if (dev->hdr_type & PCI_HEADER_TYPE_BRIDGE)
		udev = dev;
	else
		udev= dev->bus->self;

	data.is_downstream = 0;
	data.aer_driver = NULL;
	find_aer_service(udev, &data);

	/*
	 * Use the aer driver of the error agent firstly.
	 * If it hasn't the aer driver, use the root port's
	 */
	if (!data.aer_driver || !data.aer_driver->reset_link) {
		if (data.is_downstream &&
			aerdev->device.driver &&
			to_service_driver(aerdev->device.driver)->reset_link) {
			data.aer_driver =
				to_service_driver(aerdev->device.driver);
		} else {
			dev_printk(KERN_DEBUG, &dev->dev, "no link-reset "
				   "support\n");
			return PCI_ERS_RESULT_DISCONNECT;
		}
	}

	status = data.aer_driver->reset_link(udev);
	if (status != PCI_ERS_RESULT_RECOVERED) {
		dev_printk(KERN_DEBUG, &dev->dev, "link reset at upstream "
			   "device %s failed\n", pci_name(udev));
		return PCI_ERS_RESULT_DISCONNECT;
	}

	return status;
}

/**
 * do_recovery - handle nonfatal/fatal error recovery process
 * @aerdev: pointer to a pcie_device data structure of root port
 * @dev: pointer to a pci_dev data structure of agent detecting an error
 * @severity: error severity type
 *
 * Invoked when an error is nonfatal/fatal. Once being invoked, broadcast
 * error detected message to all downstream drivers within a hierarchy in
 * question and return the returned code.
 */
static pci_ers_result_t do_recovery(struct pcie_device *aerdev,
		struct pci_dev *dev,
		int severity)
{
	pci_ers_result_t status, result = PCI_ERS_RESULT_RECOVERED;
	enum pci_channel_state state;

	if (severity == AER_FATAL)
		state = pci_channel_io_frozen;
	else
		state = pci_channel_io_normal;

	status = broadcast_error_message(dev,
			state,
			"error_detected",
			report_error_detected);

	if (severity == AER_FATAL) {
		result = reset_link(aerdev, dev);
		if (result != PCI_ERS_RESULT_RECOVERED) {
			/* TODO: Should panic here? */
			return result;
		}
	}

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

	if (status == PCI_ERS_RESULT_RECOVERED)
		broadcast_error_message(dev,
				state,
				"resume",
				report_resume);

	return status;
}

/**
 * handle_error_source - handle logging error into an event log
 * @aerdev: pointer to pcie_device data structure of the root port
 * @dev: pointer to pci_dev data structure of error source device
 * @info: comprehensive error information
 *
 * Invoked when an error being detected by Root Port.
 */
static void handle_error_source(struct pcie_device * aerdev,
	struct pci_dev *dev,
	struct aer_err_info info)
{
	pci_ers_result_t status = 0;
	int pos;

	if (info.severity == AER_CORRECTABLE) {
		/*
		 * Correctable error does not need software intevention.
		 * No need to go through error recovery process.
		 */
		pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
		if (pos)
			pci_write_config_dword(dev, pos + PCI_ERR_COR_STATUS,
					info.status);
	} else {
		status = do_recovery(aerdev, dev, info.severity);
		if (status == PCI_ERS_RESULT_RECOVERED) {
			dev_printk(KERN_DEBUG, &dev->dev, "AER driver "
				   "successfully recovered\n");
		} else {
			/* TODO: Should kernel panic here? */
			dev_printk(KERN_DEBUG, &dev->dev, "AER driver didn't "
				   "recover\n");
		}
	}
}

/**
 * aer_enable_rootport - enable Root Port's interrupts when receiving messages
 * @rpc: pointer to a Root Port data structure
 *
 * Invoked when PCIE bus loads AER service driver.
 */
void aer_enable_rootport(struct aer_rpc *rpc)
{
	struct pci_dev *pdev = rpc->rpd->port;
	int pos, aer_pos;
	u16 reg16;
	u32 reg32;

	pos = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	/* Clear PCIE Capability's Device Status */
	pci_read_config_word(pdev, pos+PCI_EXP_DEVSTA, &reg16);
	pci_write_config_word(pdev, pos+PCI_EXP_DEVSTA, reg16);

	/* Disable system error generation in response to error messages */
	pci_read_config_word(pdev, pos + PCI_EXP_RTCTL, &reg16);
	reg16 &= ~(SYSTEM_ERROR_INTR_ON_MESG_MASK);
	pci_write_config_word(pdev, pos + PCI_EXP_RTCTL, reg16);

	aer_pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_ERR);
	/* Clear error status */
	pci_read_config_dword(pdev, aer_pos + PCI_ERR_ROOT_STATUS, &reg32);
	pci_write_config_dword(pdev, aer_pos + PCI_ERR_ROOT_STATUS, reg32);
	pci_read_config_dword(pdev, aer_pos + PCI_ERR_COR_STATUS, &reg32);
	pci_write_config_dword(pdev, aer_pos + PCI_ERR_COR_STATUS, reg32);
	pci_read_config_dword(pdev, aer_pos + PCI_ERR_UNCOR_STATUS, &reg32);
	pci_write_config_dword(pdev, aer_pos + PCI_ERR_UNCOR_STATUS, reg32);

	/*
	 * Enable error reporting for the root port device and downstream port
	 * devices.
	 */
	set_downstream_devices_error_reporting(pdev, true);

	/* Enable Root Port's interrupt in response to error messages */
	pci_write_config_dword(pdev,
		aer_pos + PCI_ERR_ROOT_COMMAND,
		ROOT_PORT_INTR_ON_MESG_MASK);
}

/**
 * disable_root_aer - disable Root Port's interrupts when receiving messages
 * @rpc: pointer to a Root Port data structure
 *
 * Invoked when PCIE bus unloads AER service driver.
 */
static void disable_root_aer(struct aer_rpc *rpc)
{
	struct pci_dev *pdev = rpc->rpd->port;
	u32 reg32;
	int pos;

	/*
	 * Disable error reporting for the root port device and downstream port
	 * devices.
	 */
	set_downstream_devices_error_reporting(pdev, false);

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_ERR);
	/* Disable Root's interrupt in response to error messages */
	pci_write_config_dword(pdev, pos + PCI_ERR_ROOT_COMMAND, 0);

	/* Clear Root's error status reg */
	pci_read_config_dword(pdev, pos + PCI_ERR_ROOT_STATUS, &reg32);
	pci_write_config_dword(pdev, pos + PCI_ERR_ROOT_STATUS, reg32);
}

/**
 * get_e_source - retrieve an error source
 * @rpc: pointer to the root port which holds an error
 *
 * Invoked by DPC handler to consume an error.
 */
static struct aer_err_source* get_e_source(struct aer_rpc *rpc)
{
	struct aer_err_source *e_source;
	unsigned long flags;

	/* Lock access to Root error producer/consumer index */
	spin_lock_irqsave(&rpc->e_lock, flags);
	if (rpc->prod_idx == rpc->cons_idx) {
		spin_unlock_irqrestore(&rpc->e_lock, flags);
		return NULL;
	}
	e_source = &rpc->e_sources[rpc->cons_idx];
	rpc->cons_idx++;
	if (rpc->cons_idx == AER_ERROR_SOURCES_MAX)
		rpc->cons_idx = 0;
	spin_unlock_irqrestore(&rpc->e_lock, flags);

	return e_source;
}

static int get_device_error_info(struct pci_dev *dev, struct aer_err_info *info)
{
	int pos;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);

	/* The device might not support AER */
	if (!pos)
		return AER_SUCCESS;

	if (info->severity == AER_CORRECTABLE) {
		pci_read_config_dword(dev, pos + PCI_ERR_COR_STATUS,
			&info->status);
		if (!(info->status & ERR_CORRECTABLE_ERROR_MASK))
			return AER_UNSUCCESS;
	} else if (dev->hdr_type & PCI_HEADER_TYPE_BRIDGE ||
		info->severity == AER_NONFATAL) {

		/* Link is still healthy for IO reads */
		pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_STATUS,
			&info->status);
		if (!(info->status & ERR_UNCORRECTABLE_ERROR_MASK))
			return AER_UNSUCCESS;

		if (info->status & AER_LOG_TLP_MASKS) {
			info->flags |= AER_TLP_HEADER_VALID_FLAG;
			pci_read_config_dword(dev,
				pos + PCI_ERR_HEADER_LOG, &info->tlp.dw0);
			pci_read_config_dword(dev,
				pos + PCI_ERR_HEADER_LOG + 4, &info->tlp.dw1);
			pci_read_config_dword(dev,
				pos + PCI_ERR_HEADER_LOG + 8, &info->tlp.dw2);
			pci_read_config_dword(dev,
				pos + PCI_ERR_HEADER_LOG + 12, &info->tlp.dw3);
		}
	}

	return AER_SUCCESS;
}

/**
 * aer_isr_one_error - consume an error detected by root port
 * @p_device: pointer to error root port service device
 * @e_src: pointer to an error source
 */
static void aer_isr_one_error(struct pcie_device *p_device,
		struct aer_err_source *e_src)
{
	struct device *s_device;
	struct aer_err_info e_info = {0, 0, 0,};
	int i;
	u16 id;

	/*
	 * There is a possibility that both correctable error and
	 * uncorrectable error being logged. Report correctable error first.
	 */
	for (i = 1; i & ROOT_ERR_STATUS_MASKS ; i <<= 2) {
		if (i > 4)
			break;
		if (!(e_src->status & i))
			continue;

		/* Init comprehensive error information */
		if (i & PCI_ERR_ROOT_COR_RCV) {
			id = ERR_COR_ID(e_src->id);
			e_info.severity = AER_CORRECTABLE;
		} else {
			id = ERR_UNCOR_ID(e_src->id);
			e_info.severity = ((e_src->status >> 6) & 1);
		}
		if (e_src->status &
			(PCI_ERR_ROOT_MULTI_COR_RCV |
			 PCI_ERR_ROOT_MULTI_UNCOR_RCV))
			e_info.flags |= AER_MULTI_ERROR_VALID_FLAG;
		if (!(s_device = find_source_device(p_device->port, id))) {
			printk(KERN_DEBUG "%s->can't find device of ID%04x\n",
				__func__, id);
			continue;
		}
		if (get_device_error_info(to_pci_dev(s_device), &e_info) ==
				AER_SUCCESS) {
			aer_print_error(to_pci_dev(s_device), &e_info);
			handle_error_source(p_device,
				to_pci_dev(s_device),
				e_info);
		}
	}
}

/**
 * aer_isr - consume errors detected by root port
 * @work: definition of this work item
 *
 * Invoked, as DPC, when root port records new detected error
 */
void aer_isr(struct work_struct *work)
{
	struct aer_rpc *rpc = container_of(work, struct aer_rpc, dpc_handler);
	struct pcie_device *p_device = rpc->rpd;
	struct aer_err_source *e_src;

	mutex_lock(&rpc->rpc_mutex);
	e_src = get_e_source(rpc);
	while (e_src) {
		aer_isr_one_error(p_device, e_src);
		e_src = get_e_source(rpc);
	}
	mutex_unlock(&rpc->rpc_mutex);

	wake_up(&rpc->wait_release);
}

/**
 * aer_delete_rootport - disable root port aer and delete service data
 * @rpc: pointer to a root port device being deleted
 *
 * Invoked when AER service unloaded on a specific Root Port
 */
void aer_delete_rootport(struct aer_rpc *rpc)
{
	/* Disable root port AER itself */
	disable_root_aer(rpc);

	kfree(rpc);
}

/**
 * aer_init - provide AER initialization
 * @dev: pointer to AER pcie device
 *
 * Invoked when AER service driver is loaded.
 */
int aer_init(struct pcie_device *dev)
{
	if (aer_osc_setup(dev) && !forceload)
		return -ENXIO;

	return AER_SUCCESS;
}

EXPORT_SYMBOL_GPL(pci_enable_pcie_error_reporting);
EXPORT_SYMBOL_GPL(pci_disable_pcie_error_reporting);
EXPORT_SYMBOL_GPL(pci_cleanup_aer_uncorrect_error_status);

