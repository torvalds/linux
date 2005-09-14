/*
 * File:	portdrv_pci.c
 * Purpose:	PCI Express Port Bus Driver
 *
 * Copyright (C) 2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/pcieport_if.h>

#include "portdrv.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.0"
#define DRIVER_AUTHOR "tom.l.nguyen@intel.com"
#define DRIVER_DESC "PCIE Port Bus Driver"
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/* global data */
static const char device_name[] = "pcieport-driver";

static void pci_save_msi_state(struct pci_dev *dev)
{
	struct pcie_port_device_ext *p_ext = pci_get_drvdata(dev);
	int i = 0, pos;
	u16 control;

   	if ((pos = pci_find_capability(dev, PCI_CAP_ID_MSI)) <= 0)
		return;

	pci_read_config_dword(dev, pos, &p_ext->saved_msi_config_space[i++]);
	control = p_ext->saved_msi_config_space[0] >> 16;
	pci_read_config_dword(dev, pos + PCI_MSI_ADDRESS_LO,
		&p_ext->saved_msi_config_space[i++]);
	if (control & PCI_MSI_FLAGS_64BIT) {
		pci_read_config_dword(dev, pos + PCI_MSI_ADDRESS_HI,
			&p_ext->saved_msi_config_space[i++]);
		pci_read_config_dword(dev, pos + PCI_MSI_DATA_64,
			&p_ext->saved_msi_config_space[i++]);
	} else
		pci_read_config_dword(dev, pos + PCI_MSI_DATA_32,
			&p_ext->saved_msi_config_space[i++]);
	if (control & PCI_MSI_FLAGS_MASKBIT)
		pci_read_config_dword(dev, pos + PCI_MSI_MASK_BIT,
			&p_ext->saved_msi_config_space[i++]);
}

static void pci_restore_msi_state(struct pci_dev *dev)
{
	struct pcie_port_device_ext *p_ext = pci_get_drvdata(dev);
	int i = 0, pos;
	u16 control;

   	if ((pos = pci_find_capability(dev, PCI_CAP_ID_MSI)) <= 0)
		return;

	control = p_ext->saved_msi_config_space[i++] >> 16;
	pci_write_config_word(dev, pos + PCI_MSI_FLAGS, control);
	pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_LO,
		p_ext->saved_msi_config_space[i++]);
	if (control & PCI_MSI_FLAGS_64BIT) {
		pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_HI,
			p_ext->saved_msi_config_space[i++]);
		pci_write_config_dword(dev, pos + PCI_MSI_DATA_64,
			p_ext->saved_msi_config_space[i++]);
	} else
		pci_write_config_dword(dev, pos + PCI_MSI_DATA_32,
			p_ext->saved_msi_config_space[i++]);
	if (control & PCI_MSI_FLAGS_MASKBIT)
		pci_write_config_dword(dev, pos + PCI_MSI_MASK_BIT,
			p_ext->saved_msi_config_space[i++]);
}

static void pcie_portdrv_save_config(struct pci_dev *dev)
{
	struct pcie_port_device_ext *p_ext = pci_get_drvdata(dev);

	pci_save_state(dev);
	if (p_ext->interrupt_mode == PCIE_PORT_MSI_MODE)
		pci_save_msi_state(dev);
}

static int pcie_portdrv_restore_config(struct pci_dev *dev)
{
	struct pcie_port_device_ext *p_ext = pci_get_drvdata(dev);
	int retval;

	pci_restore_state(dev);
	if (p_ext->interrupt_mode == PCIE_PORT_MSI_MODE)
		pci_restore_msi_state(dev);
	retval = pci_enable_device(dev);
	if (retval)
		return retval;
	pci_set_master(dev);
	return 0;
}

/*
 * pcie_portdrv_probe - Probe PCI-Express port devices
 * @dev: PCI-Express port device being probed
 *
 * If detected invokes the pcie_port_device_register() method for 
 * this port device.
 *
 */
static int __devinit pcie_portdrv_probe (struct pci_dev *dev, 
				const struct pci_device_id *id )
{
	int			status;

	status = pcie_port_device_probe(dev);
	if (status)
		return status;

	if (pci_enable_device(dev) < 0) 
		return -ENODEV;
	
	pci_set_master(dev);
        if (!dev->irq) {
		printk(KERN_WARNING 
		"%s->Dev[%04x:%04x] has invalid IRQ. Check vendor BIOS\n", 
		__FUNCTION__, dev->device, dev->vendor);
	}
	if (pcie_port_device_register(dev)) 
		return -ENOMEM;

	return 0;
}

static void pcie_portdrv_remove (struct pci_dev *dev)
{
	pcie_port_device_remove(dev);
	kfree(pci_get_drvdata(dev));
}

#ifdef CONFIG_PM
static int pcie_portdrv_suspend (struct pci_dev *dev, pm_message_t state)
{
	int ret = pcie_port_device_suspend(dev, state);

	pcie_portdrv_save_config(dev);
	return ret;
}

static int pcie_portdrv_resume (struct pci_dev *dev)
{
	pcie_portdrv_restore_config(dev);
	return pcie_port_device_resume(dev);
}
#endif

/*
 * LINUX Device Driver Model
 */
static const struct pci_device_id port_pci_ids[] = { {
	/* handle any PCI-Express port */
	PCI_DEVICE_CLASS(((PCI_CLASS_BRIDGE_PCI << 8) | 0x00), ~0),
	}, { /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, port_pci_ids);

static struct pci_driver pcie_portdrv = {
	.name		= (char *)device_name,
	.id_table	= &port_pci_ids[0],

	.probe		= pcie_portdrv_probe,
	.remove		= pcie_portdrv_remove,

#ifdef	CONFIG_PM
	.suspend	= pcie_portdrv_suspend,
	.resume		= pcie_portdrv_resume,
#endif	/* PM */
};

static int __init pcie_portdrv_init(void)
{
	int retval = 0;

	pcie_port_bus_register();
	retval = pci_register_driver(&pcie_portdrv);
	if (retval)
		pcie_port_bus_unregister();
	return retval;
}

static void __exit pcie_portdrv_exit(void) 
{
	pci_unregister_driver(&pcie_portdrv);
	pcie_port_bus_unregister();
}

module_init(pcie_portdrv_init);
module_exit(pcie_portdrv_exit);
