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
#ifdef RTL8190P
	u8	ucRegRead;
#endif
	u32	ulRegRead;

        RT_TRACE(COMP_POWER, "============> r8192E suspend call.\n");
	if (!netif_running(dev))
		goto out_pci_suspend;

	if (dev->netdev_ops->ndo_stop)
		dev->netdev_ops->ndo_stop(dev);
//	dev->stop(dev);
#if 0

	netif_carrier_off(dev);

	ieee80211_softmac_stop_protocol(priv->ieee80211);

	write_nic_byte(dev,MSR,(read_nic_byte(dev,MSR)&0xfc)|MSR_LINK_NONE);
	if(!priv->ieee80211->bSupportRemoteWakeUp) {
		/* disable tx/rx. In 8185 we write 0x10 (Reset bit),
		 * but here we make reference to WMAC and wirte 0x0.
		 * 2006.11.21 Emily
		 */
		write_nic_byte(dev, CMDR, 0);
	}
	//disable interrupt
	write_nic_dword(dev,INTA_MASK,0);
	priv->irq_enabled = 0;
        write_nic_dword(dev,ISR,read_nic_dword(dev, ISR));

	/* need to  free DM related functions */
	cancel_work_sync(&priv->reset_wq);
	del_timer_sync(&priv->fsync_timer);
	del_timer_sync(&priv->watch_dog_timer);
	cancel_delayed_work(&priv->watch_dog_wq);
	cancel_delayed_work(&priv->update_beacon_wq);
	cancel_work_sync(&priv->qos_activate);

	/* TODO
#if ((DEV_BUS_TYPE == PCI_INTERFACE) && (HAL_CODE_BASE == RTL8192))
pHalData->bHwRfOffAction = 2;
#endif
*/
#endif
	// Call MgntActSet_RF_State instead to prevent RF config race condition.
	// By Bruce, 2008-01-17.
	//
	if(!priv->ieee80211->bSupportRemoteWakeUp) {
		MgntActSet_RF_State(dev, eRfOff, RF_CHANGE_BY_INIT);
		// 2006.11.30. System reset bit
		ulRegRead = read_nic_dword(dev, CPU_GEN);
		ulRegRead|=CPU_GEN_SYSTEM_RESET;
		write_nic_dword(dev, CPU_GEN, ulRegRead);
	} else {
		//2008.06.03 for WOL
		write_nic_dword(dev, WFCRC0, 0xffffffff);
		write_nic_dword(dev, WFCRC1, 0xffffffff);
		write_nic_dword(dev, WFCRC2, 0xffffffff);
#ifdef RTL8190P
		//GPIO 0 = TRUE
		ucRegRead = read_nic_byte(dev, GPO);
		ucRegRead |= BIT0;
		write_nic_byte(dev, GPO, ucRegRead);
#endif
		//Write PMR register
		write_nic_byte(dev, PMR, 0x5);
		//Disable tx, enanble rx
		write_nic_byte(dev, MacBlkCtrl, 0xa);
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
	//struct r8192_priv *priv = ieee80211_priv(dev);
	//union iwreq_data wrqu;
	int err;
	u32 val;

        RT_TRACE(COMP_POWER, "================>r8192E resume call.");

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

//	dev->open(dev);
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
