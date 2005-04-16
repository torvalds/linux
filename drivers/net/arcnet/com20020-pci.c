/*
 * Linux ARCnet driver - COM20020 PCI support
 * Contemporary Controls PCI20 and SOHARD SH-ARC PCI
 * 
 * Written 1994-1999 by Avery Pennarun,
 *    based on an ISA version by David Woodhouse.
 * Written 1999-2000 by Martin Mares <mj@ucw.cz>.
 * Derived from skeleton.c by Donald Becker.
 *
 * Special thanks to Contemporary Controls, Inc. (www.ccontrols.com)
 *  for sponsoring the further development of this driver.
 *
 * **********************
 *
 * The original copyright of skeleton.c was as follows:
 *
 * skeleton.c Written 1993 by Donald Becker.
 * Copyright 1993 United States Government as represented by the
 * Director, National Security Agency.  This software may only be used
 * and distributed according to the terms of the GNU General Public License as
 * modified by SRC, incorporated herein by reference.
 *
 * **********************
 *
 * For more details, see drivers/net/arcnet.c
 *
 * **********************
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/arcdevice.h>
#include <linux/com20020.h>

#include <asm/io.h>


#define VERSION "arcnet: COM20020 PCI support\n"

/* Module parameters */

static int node;
static char device[9];		/* use eg. device="arc1" to change name */
static int timeout = 3;
static int backplane;
static int clockp;
static int clockm;

module_param(node, int, 0);
module_param_string(device, device, sizeof(device), 0);
module_param(timeout, int, 0);
module_param(backplane, int, 0);
module_param(clockp, int, 0);
module_param(clockm, int, 0);
MODULE_LICENSE("GPL");

static int __devinit com20020pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net_device *dev;
	struct arcnet_local *lp;
	int ioaddr, err;

	if (pci_enable_device(pdev))
		return -EIO;
	dev = alloc_arcdev(device);
	if (!dev)
		return -ENOMEM;
	lp = dev->priv;

	pci_set_drvdata(pdev, dev);

	// SOHARD needs PCI base addr 4
	if (pdev->vendor==0x10B5) {
		BUGMSG(D_NORMAL, "SOHARD\n");
		ioaddr = pci_resource_start(pdev, 4);
	}
	else {
		BUGMSG(D_NORMAL, "Contemporary Controls\n");
		ioaddr = pci_resource_start(pdev, 2);
	}

	if (!request_region(ioaddr, ARCNET_TOTAL_SIZE, "com20020-pci")) {
		BUGMSG(D_INIT, "IO region %xh-%xh already allocated.\n",
		       ioaddr, ioaddr + ARCNET_TOTAL_SIZE - 1);
		err = -EBUSY;
		goto out_dev;
	}

	// Dummy access after Reset
	// ARCNET controller needs this access to detect bustype
	outb(0x00,ioaddr+1);
	inb(ioaddr+1);

	dev->base_addr = ioaddr;
	dev->irq = pdev->irq;
	dev->dev_addr[0] = node;
	lp->card_name = "PCI COM20020";
	lp->card_flags = id->driver_data;
	lp->backplane = backplane;
	lp->clockp = clockp & 7;
	lp->clockm = clockm & 3;
	lp->timeout = timeout;
	lp->hw.owner = THIS_MODULE;

	if (ASTATUS() == 0xFF) {
		BUGMSG(D_NORMAL, "IO address %Xh was reported by PCI BIOS, "
		       "but seems empty!\n", ioaddr);
		err = -EIO;
		goto out_port;
	}
	if (com20020_check(dev)) {
		err = -EIO;
		goto out_port;
	}

	if ((err = com20020_found(dev, SA_SHIRQ)) != 0)
	        goto out_port;

	return 0;

out_port:
	release_region(ioaddr, ARCNET_TOTAL_SIZE);
out_dev:
	free_netdev(dev);
	return err;
}

static void __devexit com20020pci_remove(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	unregister_netdev(dev);
	free_irq(dev->irq, dev);
	release_region(dev->base_addr, ARCNET_TOTAL_SIZE);
	free_netdev(dev);
}

static struct pci_device_id com20020pci_id_table[] = {
	{ 0x1571, 0xa001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1571, 0xa002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1571, 0xa003, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1571, 0xa004, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1571, 0xa005, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1571, 0xa006, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1571, 0xa007, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1571, 0xa008, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1571, 0xa009, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ARC_IS_5MBIT },
	{ 0x1571, 0xa00a, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ARC_IS_5MBIT },
	{ 0x1571, 0xa00b, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ARC_IS_5MBIT },
	{ 0x1571, 0xa00c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ARC_IS_5MBIT },
	{ 0x1571, 0xa00d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ARC_IS_5MBIT },
	{ 0x1571, 0xa201, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ARC_CAN_10MBIT },
	{ 0x1571, 0xa202, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ARC_CAN_10MBIT },
	{ 0x1571, 0xa203, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ARC_CAN_10MBIT },
	{ 0x1571, 0xa204, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ARC_CAN_10MBIT },
	{ 0x1571, 0xa205, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ARC_CAN_10MBIT },
	{ 0x1571, 0xa206, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ARC_CAN_10MBIT },
	{ 0x10B5, 0x9050, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ARC_CAN_10MBIT },
	{0,}
};

MODULE_DEVICE_TABLE(pci, com20020pci_id_table);

static struct pci_driver com20020pci_driver = {
	.name		= "com20020",
	.id_table	= com20020pci_id_table,
	.probe		= com20020pci_probe,
	.remove		= __devexit_p(com20020pci_remove),
};

static int __init com20020pci_init(void)
{
	BUGLVL(D_NORMAL) printk(VERSION);
	return pci_module_init(&com20020pci_driver);
}

static void __exit com20020pci_cleanup(void)
{
	pci_unregister_driver(&com20020pci_driver);
}

module_init(com20020pci_init)
module_exit(com20020pci_cleanup)
