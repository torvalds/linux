/*
   Power management interface routines.
   Written by Mariusz Matuszek.
   This code is currently just a placeholder for later work and
   does not do anything useful.

   This is part of rtl8180 OpenSource driver.
   Copyright (C) Andrea Merello 2004  <andreamrl@tiscali.it>
   Released under the terms of GPL (General Public Licence)
*/

#ifdef CONFIG_RTL8180_PM


#include "r8180_hw.h"
#include "r8180_pm.h"
#include "r8180.h"

int rtl8180_save_state (struct pci_dev *dev, u32 state)
{
        printk(KERN_NOTICE "r8180 save state call (state %u).\n", state);
	return(-EAGAIN);
}

int rtl8180_suspend (struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
//	struct r8180_priv *priv = ieee80211_priv(dev);

	if (!netif_running(dev))
		goto out_pci_suspend;

	dev->stop(dev);

	netif_device_detach(dev);

out_pci_suspend:
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev,pci_choose_state(pdev,state));
	return 0;
}

int rtl8180_resume (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
//	struct r8180_priv *priv = ieee80211_priv(dev);
	int err;
	u32 val;

	pci_set_power_state(pdev, PCI_D0);

	err = pci_enable_device(pdev);
	if(err) {
		printk(KERN_ERR "%s: pci_enable_device failed on resume\n",
				dev->name);

		return err;
	}
	pci_restore_state(pdev);
	/*
	 * Suspend/Resume resets the PCI configuration space, so we have to
	 * re-disable the RETRY_TIMEOUT register (0x41) to keep PCI Tx retries
	 * from interfering with C3 CPU state. pci_restore_state won't help
	 * here since it only restores the first 64 bytes pci config header.
	 */
	pci_read_config_dword(pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);

	if(!netif_running(dev))
		goto out;

	dev->open(dev);
	netif_device_attach(dev);
out:
	return 0;
}


int rtl8180_enable_wake (struct pci_dev *dev, u32 state, int enable)
{
        printk(KERN_NOTICE "r8180 enable wake call (state %u, enable %d).\n",
	       state, enable);
	return(-EAGAIN);
}



#endif //CONFIG_RTL8180_PM
