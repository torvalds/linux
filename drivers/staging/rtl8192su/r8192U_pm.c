/*
   Power management interface routines.
   Written by Mariusz Matuszek.
   This code is currently just a placeholder for later work and
   does not do anything useful.

   This is part of rtl8180 OpenSource driver.
   Copyright (C) Andrea Merello 2004  <andreamrl@tiscali.it>
   Released under the terms of GPL (General Public Licence)
*/

#include "r8192U.h"
#include "r8192U_pm.h"

/*****************************************************************************/
int rtl8192U_save_state (struct pci_dev *dev, u32 state)
{
	printk(KERN_NOTICE "r8192U save state call (state %u).\n", state);
	return(-EAGAIN);
}

int rtl8192U_suspend(struct usb_interface *intf, pm_message_t state)
{
	struct net_device *dev = usb_get_intfdata(intf);

	RT_TRACE(COMP_POWER, "============> r8192U suspend call.\n");

	if(dev) {
		 if (!netif_running(dev)) {
		      printk(KERN_WARNING "netif not running, go out suspend function\n");
		      return 0;
		 }

		if (dev->netdev_ops->ndo_stop)
			dev->netdev_ops->ndo_stop(dev);

		mdelay(10);

		netif_device_detach(dev);
	}

	return 0;
}

int rtl8192U_resume (struct usb_interface *intf)
{
	struct net_device *dev = usb_get_intfdata(intf);

	RT_TRACE(COMP_POWER, "================>r8192U resume call.");

	if(dev) {
		if (!netif_running(dev)){
			printk(KERN_WARNING "netif not running, go out resume function\n");
			return 0;
		}

		netif_device_attach(dev);

		if (dev->netdev_ops->ndo_open)
			dev->netdev_ops->ndo_open(dev);
	}

        return 0;
}

int rtl8192U_enable_wake (struct pci_dev *dev, u32 state, int enable)
{
	printk(KERN_NOTICE "r8192U enable wake call (state %u, enable %d).\n",
			state, enable);
	return(-EAGAIN);
}

