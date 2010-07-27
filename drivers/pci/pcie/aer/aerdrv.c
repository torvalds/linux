/*
 * drivers/pci/pcie/aer/aerdrv.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This file implements the AER root port service driver. The driver will
 * register an irq handler. When root port triggers an AER interrupt, the irq
 * handler will collect root port status and schedule a work.
 *
 * Copyright (C) 2006 Intel Corp.
 *	Tom Long Nguyen (tom.l.nguyen@intel.com)
 *	Zhang Yanmin (yanmin.zhang@intel.com)
 *
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pcieport_if.h>
#include <linux/slab.h>

#include "aerdrv.h"
#include "../../pci.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.0"
#define DRIVER_AUTHOR "tom.l.nguyen@intel.com"
#define DRIVER_DESC "Root Port Advanced Error Reporting Driver"
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

static int __devinit aer_probe(struct pcie_device *dev);
static void aer_remove(struct pcie_device *dev);
static pci_ers_result_t aer_error_detected(struct pci_dev *dev,
	enum pci_channel_state error);
static void aer_error_resume(struct pci_dev *dev);
static pci_ers_result_t aer_root_reset(struct pci_dev *dev);

static struct pci_error_handlers aer_error_handlers = {
	.error_detected = aer_error_detected,
	.resume		= aer_error_resume,
};

static struct pcie_port_service_driver aerdriver = {
	.name		= "aer",
	.port_type	= PCI_EXP_TYPE_ROOT_PORT,
	.service	= PCIE_PORT_SERVICE_AER,

	.probe		= aer_probe,
	.remove		= aer_remove,

	.err_handler	= &aer_error_handlers,

	.reset_link	= aer_root_reset,
};

static int pcie_aer_disable;

void pci_no_aer(void)
{
	pcie_aer_disable = 1;	/* has priority over 'forceload' */
}

static int set_device_error_reporting(struct pci_dev *dev, void *data)
{
	bool enable = *((bool *)data);

	if ((dev->pcie_type == PCI_EXP_TYPE_ROOT_PORT) ||
	    (dev->pcie_type == PCI_EXP_TYPE_UPSTREAM) ||
	    (dev->pcie_type == PCI_EXP_TYPE_DOWNSTREAM)) {
		if (enable)
			pci_enable_pcie_error_reporting(dev);
		else
			pci_disable_pcie_error_reporting(dev);
	}

	if (enable)
		pcie_set_ecrc_checking(dev);

	return 0;
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

/**
 * aer_enable_rootport - enable Root Port's interrupts when receiving messages
 * @rpc: pointer to a Root Port data structure
 *
 * Invoked when PCIe bus loads AER service driver.
 */
static void aer_enable_rootport(struct aer_rpc *rpc)
{
	struct pci_dev *pdev = rpc->rpd->port;
	int pos, aer_pos;
	u16 reg16;
	u32 reg32;

	pos = pci_pcie_cap(pdev);
	/* Clear PCIe Capability's Device Status */
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
	pci_read_config_dword(pdev, aer_pos + PCI_ERR_ROOT_COMMAND, &reg32);
	reg32 |= ROOT_PORT_INTR_ON_MESG_MASK;
	pci_write_config_dword(pdev, aer_pos + PCI_ERR_ROOT_COMMAND, reg32);
}

/**
 * aer_disable_rootport - disable Root Port's interrupts when receiving messages
 * @rpc: pointer to a Root Port data structure
 *
 * Invoked when PCIe bus unloads AER service driver.
 */
static void aer_disable_rootport(struct aer_rpc *rpc)
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
	pci_read_config_dword(pdev, pos + PCI_ERR_ROOT_COMMAND, &reg32);
	reg32 &= ~ROOT_PORT_INTR_ON_MESG_MASK;
	pci_write_config_dword(pdev, pos + PCI_ERR_ROOT_COMMAND, reg32);

	/* Clear Root's error status reg */
	pci_read_config_dword(pdev, pos + PCI_ERR_ROOT_STATUS, &reg32);
	pci_write_config_dword(pdev, pos + PCI_ERR_ROOT_STATUS, reg32);
}

/**
 * aer_irq - Root Port's ISR
 * @irq: IRQ assigned to Root Port
 * @context: pointer to Root Port data structure
 *
 * Invoked when Root Port detects AER messages.
 */
irqreturn_t aer_irq(int irq, void *context)
{
	unsigned int status, id;
	struct pcie_device *pdev = (struct pcie_device *)context;
	struct aer_rpc *rpc = get_service_data(pdev);
	int next_prod_idx;
	unsigned long flags;
	int pos;

	pos = pci_find_ext_capability(pdev->port, PCI_EXT_CAP_ID_ERR);
	/*
	 * Must lock access to Root Error Status Reg, Root Error ID Reg,
	 * and Root error producer/consumer index
	 */
	spin_lock_irqsave(&rpc->e_lock, flags);

	/* Read error status */
	pci_read_config_dword(pdev->port, pos + PCI_ERR_ROOT_STATUS, &status);
	if (!(status & (PCI_ERR_ROOT_UNCOR_RCV|PCI_ERR_ROOT_COR_RCV))) {
		spin_unlock_irqrestore(&rpc->e_lock, flags);
		return IRQ_NONE;
	}

	/* Read error source and clear error status */
	pci_read_config_dword(pdev->port, pos + PCI_ERR_ROOT_ERR_SRC, &id);
	pci_write_config_dword(pdev->port, pos + PCI_ERR_ROOT_STATUS, status);

	/* Store error source for later DPC handler */
	next_prod_idx = rpc->prod_idx + 1;
	if (next_prod_idx == AER_ERROR_SOURCES_MAX)
		next_prod_idx = 0;
	if (next_prod_idx == rpc->cons_idx) {
		/*
		 * Error Storm Condition - possibly the same error occurred.
		 * Drop the error.
		 */
		spin_unlock_irqrestore(&rpc->e_lock, flags);
		return IRQ_HANDLED;
	}
	rpc->e_sources[rpc->prod_idx].status =  status;
	rpc->e_sources[rpc->prod_idx].id = id;
	rpc->prod_idx = next_prod_idx;
	spin_unlock_irqrestore(&rpc->e_lock, flags);

	/*  Invoke DPC handler */
	schedule_work(&rpc->dpc_handler);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(aer_irq);

/**
 * aer_alloc_rpc - allocate Root Port data structure
 * @dev: pointer to the pcie_dev data structure
 *
 * Invoked when Root Port's AER service is loaded.
 */
static struct aer_rpc *aer_alloc_rpc(struct pcie_device *dev)
{
	struct aer_rpc *rpc;

	rpc = kzalloc(sizeof(struct aer_rpc), GFP_KERNEL);
	if (!rpc)
		return NULL;

	/* Initialize Root lock access, e_lock, to Root Error Status Reg */
	spin_lock_init(&rpc->e_lock);

	rpc->rpd = dev;
	INIT_WORK(&rpc->dpc_handler, aer_isr);
	mutex_init(&rpc->rpc_mutex);
	init_waitqueue_head(&rpc->wait_release);

	/* Use PCIe bus function to store rpc into PCIe device */
	set_service_data(dev, rpc);

	return rpc;
}

/**
 * aer_remove - clean up resources
 * @dev: pointer to the pcie_dev data structure
 *
 * Invoked when PCI Express bus unloads or AER probe fails.
 */
static void aer_remove(struct pcie_device *dev)
{
	struct aer_rpc *rpc = get_service_data(dev);

	if (rpc) {
		/* If register interrupt service, it must be free. */
		if (rpc->isr)
			free_irq(dev->irq, dev);

		wait_event(rpc->wait_release, rpc->prod_idx == rpc->cons_idx);

		aer_disable_rootport(rpc);
		kfree(rpc);
		set_service_data(dev, NULL);
	}
}

/**
 * aer_probe - initialize resources
 * @dev: pointer to the pcie_dev data structure
 * @id: pointer to the service id data structure
 *
 * Invoked when PCI Express bus loads AER service driver.
 */
static int __devinit aer_probe(struct pcie_device *dev)
{
	int status;
	struct aer_rpc *rpc;
	struct device *device = &dev->device;

	/* Init */
	status = aer_init(dev);
	if (status)
		return status;

	/* Alloc rpc data structure */
	rpc = aer_alloc_rpc(dev);
	if (!rpc) {
		dev_printk(KERN_DEBUG, device, "alloc rpc failed\n");
		aer_remove(dev);
		return -ENOMEM;
	}

	/* Request IRQ ISR */
	status = request_irq(dev->irq, aer_irq, IRQF_SHARED, "aerdrv", dev);
	if (status) {
		dev_printk(KERN_DEBUG, device, "request IRQ failed\n");
		aer_remove(dev);
		return status;
	}

	rpc->isr = 1;

	aer_enable_rootport(rpc);

	return status;
}

/**
 * aer_root_reset - reset link on Root Port
 * @dev: pointer to Root Port's pci_dev data structure
 *
 * Invoked by Port Bus driver when performing link reset at Root Port.
 */
static pci_ers_result_t aer_root_reset(struct pci_dev *dev)
{
	u32 reg32;
	int pos;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);

	/* Disable Root's interrupt in response to error messages */
	pci_read_config_dword(dev, pos + PCI_ERR_ROOT_COMMAND, &reg32);
	reg32 &= ~ROOT_PORT_INTR_ON_MESG_MASK;
	pci_write_config_dword(dev, pos + PCI_ERR_ROOT_COMMAND, reg32);

	aer_do_secondary_bus_reset(dev);
	dev_printk(KERN_DEBUG, &dev->dev, "Root Port link has been reset\n");

	/* Clear Root Error Status */
	pci_read_config_dword(dev, pos + PCI_ERR_ROOT_STATUS, &reg32);
	pci_write_config_dword(dev, pos + PCI_ERR_ROOT_STATUS, reg32);

	/* Enable Root Port's interrupt in response to error messages */
	pci_read_config_dword(dev, pos + PCI_ERR_ROOT_COMMAND, &reg32);
	reg32 |= ROOT_PORT_INTR_ON_MESG_MASK;
	pci_write_config_dword(dev, pos + PCI_ERR_ROOT_COMMAND, reg32);

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * aer_error_detected - update severity status
 * @dev: pointer to Root Port's pci_dev data structure
 * @error: error severity being notified by port bus
 *
 * Invoked by Port Bus driver during error recovery.
 */
static pci_ers_result_t aer_error_detected(struct pci_dev *dev,
			enum pci_channel_state error)
{
	/* Root Port has no impact. Always recovers. */
	return PCI_ERS_RESULT_CAN_RECOVER;
}

/**
 * aer_error_resume - clean up corresponding error status bits
 * @dev: pointer to Root Port's pci_dev data structure
 *
 * Invoked by Port Bus driver during nonfatal recovery.
 */
static void aer_error_resume(struct pci_dev *dev)
{
	int pos;
	u32 status, mask;
	u16 reg16;

	/* Clean up Root device status */
	pos = pci_pcie_cap(dev);
	pci_read_config_word(dev, pos + PCI_EXP_DEVSTA, &reg16);
	pci_write_config_word(dev, pos + PCI_EXP_DEVSTA, reg16);

	/* Clean AER Root Error Status */
	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_STATUS, &status);
	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_SEVER, &mask);
	if (dev->error_state == pci_channel_io_normal)
		status &= ~mask; /* Clear corresponding nonfatal bits */
	else
		status &= mask; /* Clear corresponding fatal bits */
	pci_write_config_dword(dev, pos + PCI_ERR_UNCOR_STATUS, status);
}

/**
 * aer_service_init - register AER root service driver
 *
 * Invoked when AER root service driver is loaded.
 */
static int __init aer_service_init(void)
{
	if (pcie_aer_disable)
		return -ENXIO;
	if (!pci_msi_enabled())
		return -ENXIO;
	return pcie_port_service_register(&aerdriver);
}

/**
 * aer_service_exit - unregister AER root service driver
 *
 * Invoked when AER root service driver is unloaded.
 */
static void __exit aer_service_exit(void)
{
	pcie_port_service_unregister(&aerdriver);
}

module_init(aer_service_init);
module_exit(aer_service_exit);
