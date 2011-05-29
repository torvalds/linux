/*
   Power management interface routines.
   Written by Mariusz Matuszek.
   This code is currently just a placeholder for later work and
   does not do anything useful.

   This is part of rtl8180 OpenSource driver.
   Copyright (C) Andrea Merello 2004  <andreamrl@tiscali.it>
   Released under the terms of GPL (General Public Licence)
*/

#include "r8192E.h"
#include "r8192E_hw.h"
#include "r8192_pm.h"
#include "r8190_rtl8256.h"

int rtl8192E_save_state (struct pci_dev *dev, pm_message_t state)
{
        printk(KERN_NOTICE "r8192E save state call (state %u).\n", state.event);
	return -EAGAIN;
}


int rtl8192E_suspend (struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32	ulRegRead;

        RT_TRACE(COMP_POWER, "============> r8192E suspend call.\n");
	if (!netif_running(dev))
		goto out_pci_suspend;

	if (dev->netdev_ops->ndo_stop)
		dev->netdev_ops->ndo_stop(dev);

	// Call MgntActSet_RF_State instead to prevent RF config race condition.
	if(!priv->ieee80211->bSupportRemoteWakeUp) {
		MgntActSet_RF_State(priv, eRfOff, RF_CHANGE_BY_INIT);
		// 2006.11.30. System reset bit
		ulRegRead = read_nic_dword(priv, CPU_GEN);
		ulRegRead|=CPU_GEN_SYSTEM_RESET;
		write_nic_dword(priv, CPU_GEN, ulRegRead);
	} else {
		//2008.06.03 for WOL
		write_nic_dword(priv, WFCRC0, 0xffffffff);
		write_nic_dword(priv, WFCRC1, 0xffffffff);
		write_nic_dword(priv, WFCRC2, 0xffffffff);
		//Write PMR register
		write_nic_byte(priv, PMR, 0x5);
		//Disable tx, enanble rx
		write_nic_byte(priv, MacBlkCtrl, 0xa);
	}

out_pci_suspend:
	RT_TRACE(COMP_POWER, "r8192E support WOL call??????????????????????\n");
	if(priv->ieee80211->bSupportRemoteWakeUp) {
		RT_TRACE(COMP_POWER, "r8192E support WOL call!!!!!!!!!!!!!!!!!!.\n");
	}
	netif_device_detach(dev);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_enable_wake(pdev, pci_choose_state(pdev,state),
			priv->ieee80211->bSupportRemoteWakeUp?1:0);
	pci_set_power_state(pdev,pci_choose_state(pdev,state));

	return 0;
}

int rtl8192E_resume (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	int err;
	u32 val;

        RT_TRACE(COMP_POWER, "================>r8192E resume call.\n");

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
	if ((val & 0x0000ff00) != 0) {
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);
	}



	pci_enable_wake(pdev, PCI_D0, 0);

	if(!netif_running(dev))
		goto out;

	netif_device_attach(dev);

	if (dev->netdev_ops->ndo_open)
		dev->netdev_ops->ndo_open(dev);

out:
        RT_TRACE(COMP_POWER, "<================r8192E resume call.\n");
	return 0;
}


int rtl8192E_enable_wake (struct pci_dev *dev, pm_message_t state, int enable)
{
        printk(KERN_NOTICE "r8192E enable wake call (state %u, enable %d).\n",
	       state.event, enable);
	return -EAGAIN;
}
