/*
 *  Copyright (C) 2002 Intersil Americas Inc.
 *  Copyright (C) 2003 Herbert Valerio Riedel <hvr@gnu.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h> /* For __init, __exit */
#include <linux/dma-mapping.h>

#include "prismcompat.h"
#include "islpci_dev.h"
#include "islpci_mgt.h"		/* for pc_debug */
#include "isl_oid.h"

MODULE_AUTHOR("[Intersil] R.Bastings and W.Termorshuizen, The prism54.org Development Team <prism54-devel@prism54.org>");
MODULE_DESCRIPTION("The Prism54 802.11 Wireless LAN adapter");
MODULE_LICENSE("GPL");

static int	init_pcitm = 0;
module_param(init_pcitm, int, 0);

/* In this order: vendor, device, subvendor, subdevice, class, class_mask,
 * driver_data
 * If you have an update for this please contact prism54-devel@prism54.org
 * The latest list can be found at http://wireless.kernel.org/en/users/Drivers/p54 */
static DEFINE_PCI_DEVICE_TABLE(prism54_id_tbl) = {
	/* Intersil PRISM Duette/Prism GT Wireless LAN adapter */
	{
	 0x1260, 0x3890,
	 PCI_ANY_ID, PCI_ANY_ID,
	 0, 0, 0
	},

	/* 3COM 3CRWE154G72 Wireless LAN adapter */
	{
	 PCI_VDEVICE(3COM, 0x6001), 0
	},

	/* Intersil PRISM Indigo Wireless LAN adapter */
	{
	 0x1260, 0x3877,
	 PCI_ANY_ID, PCI_ANY_ID,
	 0, 0, 0
	},

	/* Intersil PRISM Javelin/Xbow Wireless LAN adapter */
	{
	 0x1260, 0x3886,
	 PCI_ANY_ID, PCI_ANY_ID,
	 0, 0, 0
	},

	/* End of list */
	{0,0,0,0,0,0,0}
};

/* register the device with the Hotplug facilities of the kernel */
MODULE_DEVICE_TABLE(pci, prism54_id_tbl);

static int prism54_probe(struct pci_dev *, const struct pci_device_id *);
static void prism54_remove(struct pci_dev *);
static int prism54_suspend(struct pci_dev *, pm_message_t state);
static int prism54_resume(struct pci_dev *);

static struct pci_driver prism54_driver = {
	.name = DRV_NAME,
	.id_table = prism54_id_tbl,
	.probe = prism54_probe,
	.remove = prism54_remove,
	.suspend = prism54_suspend,
	.resume = prism54_resume,
};

/******************************************************************************
    Module initialization functions
******************************************************************************/

static int
prism54_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net_device *ndev;
	u8 latency_tmr;
	u32 mem_addr;
	islpci_private *priv;
	int rvalue;

	/* Enable the pci device */
	if (pci_enable_device(pdev)) {
		printk(KERN_ERR "%s: pci_enable_device() failed.\n", DRV_NAME);
		return -ENODEV;
	}

	/* check whether the latency timer is set correctly */
	pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &latency_tmr);
#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_TRACING, "latency timer: %x\n", latency_tmr);
#endif
	if (latency_tmr < PCIDEVICE_LATENCY_TIMER_MIN) {
		/* set the latency timer */
		pci_write_config_byte(pdev, PCI_LATENCY_TIMER,
				      PCIDEVICE_LATENCY_TIMER_VAL);
	}

	/* enable PCI DMA */
	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
		printk(KERN_ERR "%s: 32-bit PCI DMA not supported", DRV_NAME);
		goto do_pci_disable_device;
        }

	/* 0x40 is the programmable timer to configure the response timeout (TRDY_TIMEOUT)
	 * 0x41 is the programmable timer to configure the retry timeout (RETRY_TIMEOUT)
	 *	The RETRY_TIMEOUT is used to set the number of retries that the core, as a
	 *	Master, will perform before abandoning a cycle. The default value for
	 *	RETRY_TIMEOUT is 0x80, which far exceeds the PCI 2.1 requirement for new
	 *	devices. A write of zero to the RETRY_TIMEOUT register disables this
	 *	function to allow use with any non-compliant legacy devices that may
	 *	execute more retries.
	 *
	 *	Writing zero to both these two registers will disable both timeouts and
	 *	*can* solve problems caused by devices that are slow to respond.
	 *	Make this configurable - MSW
	 */
	if ( init_pcitm >= 0 ) {
		pci_write_config_byte(pdev, 0x40, (u8)init_pcitm);
		pci_write_config_byte(pdev, 0x41, (u8)init_pcitm);
	} else {
		printk(KERN_INFO "PCI TRDY/RETRY unchanged\n");
	}

	/* request the pci device I/O regions */
	rvalue = pci_request_regions(pdev, DRV_NAME);
	if (rvalue) {
		printk(KERN_ERR "%s: pci_request_regions failure (rc=%d)\n",
		       DRV_NAME, rvalue);
		goto do_pci_disable_device;
	}

	/* check if the memory window is indeed set */
	rvalue = pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0, &mem_addr);
	if (rvalue || !mem_addr) {
		printk(KERN_ERR "%s: PCI device memory region not configured; fix your BIOS or CardBus bridge/drivers\n",
		       DRV_NAME);
		goto do_pci_release_regions;
	}

	/* enable PCI bus-mastering */
	DEBUG(SHOW_TRACING, "%s: pci_set_master(pdev)\n", DRV_NAME);
	pci_set_master(pdev);

	/* enable MWI */
	pci_try_set_mwi(pdev);

	/* setup the network device interface and its structure */
	if (!(ndev = islpci_setup(pdev))) {
		/* error configuring the driver as a network device */
		printk(KERN_ERR "%s: could not configure network device\n",
		       DRV_NAME);
		goto do_pci_clear_mwi;
	}

	priv = netdev_priv(ndev);
	islpci_set_state(priv, PRV_STATE_PREBOOT); /* we are attempting to boot */

	/* card is in unknown state yet, might have some interrupts pending */
	isl38xx_disable_interrupts(priv->device_base);

	/* request for the interrupt before uploading the firmware */
	rvalue = request_irq(pdev->irq, islpci_interrupt,
			     IRQF_SHARED, ndev->name, priv);

	if (rvalue) {
		/* error, could not hook the handler to the irq */
		printk(KERN_ERR "%s: could not install IRQ handler\n",
		       ndev->name);
		goto do_unregister_netdev;
	}

	/* firmware upload is triggered in islpci_open */

	return 0;

      do_unregister_netdev:
	unregister_netdev(ndev);
	islpci_free_memory(priv);
	free_netdev(ndev);
	priv = NULL;
      do_pci_clear_mwi:
	pci_clear_mwi(pdev);
      do_pci_release_regions:
	pci_release_regions(pdev);
      do_pci_disable_device:
	pci_disable_device(pdev);
	return -EIO;
}

/* set by cleanup_module */
static volatile int __in_cleanup_module = 0;

/* this one removes one(!!) instance only */
static void
prism54_remove(struct pci_dev *pdev)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	islpci_private *priv = ndev ? netdev_priv(ndev) : NULL;
	BUG_ON(!priv);

	if (!__in_cleanup_module) {
		printk(KERN_DEBUG "%s: hot unplug detected\n", ndev->name);
		islpci_set_state(priv, PRV_STATE_OFF);
	}

	printk(KERN_DEBUG "%s: removing device\n", ndev->name);

	unregister_netdev(ndev);

	/* free the interrupt request */

	if (islpci_get_state(priv) != PRV_STATE_OFF) {
		isl38xx_disable_interrupts(priv->device_base);
		islpci_set_state(priv, PRV_STATE_OFF);
		/* This bellow causes a lockup at rmmod time. It might be
		 * because some interrupts still linger after rmmod time,
		 * see bug #17 */
		/* pci_set_power_state(pdev, 3);*/	/* try to power-off */
	}

	free_irq(pdev->irq, priv);

	/* free the PCI memory and unmap the remapped page */
	islpci_free_memory(priv);

	free_netdev(ndev);
	priv = NULL;

	pci_clear_mwi(pdev);

	pci_release_regions(pdev);

	pci_disable_device(pdev);
}

static int
prism54_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	islpci_private *priv = ndev ? netdev_priv(ndev) : NULL;
	BUG_ON(!priv);


	pci_save_state(pdev);

	/* tell the device not to trigger interrupts for now... */
	isl38xx_disable_interrupts(priv->device_base);

	/* from now on assume the hardware was already powered down
	   and don't touch it anymore */
	islpci_set_state(priv, PRV_STATE_OFF);

	netif_stop_queue(ndev);
	netif_device_detach(ndev);

	return 0;
}

static int
prism54_resume(struct pci_dev *pdev)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	islpci_private *priv = ndev ? netdev_priv(ndev) : NULL;
	int err;

	BUG_ON(!priv);

	printk(KERN_NOTICE "%s: got resume request\n", ndev->name);

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "%s: pci_enable_device failed on resume\n",
		       ndev->name);
		return err;
	}

	pci_restore_state(pdev);

	/* alright let's go into the PREBOOT state */
	islpci_reset(priv, 1);

	netif_device_attach(ndev);
	netif_start_queue(ndev);

	return 0;
}

static int __init
prism54_module_init(void)
{
	printk(KERN_INFO "Loaded %s driver, version %s\n",
	       DRV_NAME, DRV_VERSION);

	__bug_on_wrong_struct_sizes ();

	return pci_register_driver(&prism54_driver);
}

/* by the time prism54_module_exit() terminates, as a postcondition
 * all instances will have been destroyed by calls to
 * prism54_remove() */
static void __exit
prism54_module_exit(void)
{
	__in_cleanup_module = 1;

	pci_unregister_driver(&prism54_driver);

	printk(KERN_INFO "Unloaded %s driver\n", DRV_NAME);

	__in_cleanup_module = 0;
}

/* register entry points */
module_init(prism54_module_init);
module_exit(prism54_module_exit);
/* EOF */
