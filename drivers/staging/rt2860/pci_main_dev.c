/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

    Module Name:
    pci_main_dev.c

    Abstract:
    Create and register network interface for PCI based chipsets in Linux platform.

    Revision History:
    Who         When            What
    --------    ----------      ----------------------------------------------
*/

#include "rt_config.h"
#include <linux/pci.h>
#include <linux/slab.h>

/* Following information will be show when you run 'modinfo' */
/* *** If you have a solution for the bug in current version of driver, please mail to me. */
/* Otherwise post to forum in ralinktech's web site(www.ralinktech.com) and let all users help you. *** */
MODULE_AUTHOR("Jett Chen <jett_chen@ralinktech.com>");
MODULE_DESCRIPTION("RT2860/RT3090 Wireless Lan Linux Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("rt3090sta");

/* */
/* Function declarations */
/* */
extern int rt28xx_close(IN struct net_device *net_dev);
extern int rt28xx_open(struct net_device *net_dev);

static void __devexit rt2860_remove_one(struct pci_dev *pci_dev);
static int __devinit rt2860_probe(struct pci_dev *pci_dev,
				  const struct pci_device_id *ent);
static void __exit rt2860_cleanup_module(void);
static int __init rt2860_init_module(void);

static void RTMPInitPCIeDevice(IN struct pci_dev *pci_dev,
			       struct rt_rtmp_adapter *pAd);

#ifdef CONFIG_PM
static int rt2860_suspend(struct pci_dev *pci_dev, pm_message_t state);
static int rt2860_resume(struct pci_dev *pci_dev);
#endif /* CONFIG_PM // */

/* */
/* Ralink PCI device table, include all supported chipsets */
/* */
static struct pci_device_id rt2860_pci_tbl[] __devinitdata = {
#ifdef RT2860
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC2860_PCI_DEVICE_ID)},	/*RT28602.4G */
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC2860_PCIe_DEVICE_ID)},
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC2760_PCI_DEVICE_ID)},
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC2790_PCIe_DEVICE_ID)},
	{PCI_DEVICE(VEN_AWT_PCI_VENDOR_ID, VEN_AWT_PCIe_DEVICE_ID)},
	{PCI_DEVICE(EDIMAX_PCI_VENDOR_ID, 0x7708)},
	{PCI_DEVICE(EDIMAX_PCI_VENDOR_ID, 0x7728)},
	{PCI_DEVICE(EDIMAX_PCI_VENDOR_ID, 0x7758)},
	{PCI_DEVICE(EDIMAX_PCI_VENDOR_ID, 0x7727)},
	{PCI_DEVICE(EDIMAX_PCI_VENDOR_ID, 0x7738)},
	{PCI_DEVICE(EDIMAX_PCI_VENDOR_ID, 0x7748)},
	{PCI_DEVICE(EDIMAX_PCI_VENDOR_ID, 0x7768)},
#endif
#ifdef RT3090
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC3090_PCIe_DEVICE_ID)},
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC3091_PCIe_DEVICE_ID)},
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC3092_PCIe_DEVICE_ID)},
#endif /* RT3090 // */
#ifdef RT3390
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC3390_PCIe_DEVICE_ID)},
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC3391_PCIe_DEVICE_ID)},
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC3392_PCIe_DEVICE_ID)},
#endif /* RT3390 // */
	{0,}			/* terminate list */
};

MODULE_DEVICE_TABLE(pci, rt2860_pci_tbl);
#ifdef MODULE_VERSION
MODULE_VERSION(STA_DRIVER_VERSION);
#endif

/* */
/* Our PCI driver structure */
/* */
static struct pci_driver rt2860_driver = {
name: "rt2860",
id_table : rt2860_pci_tbl,
probe : rt2860_probe,
remove : __devexit_p(rt2860_remove_one),
#ifdef CONFIG_PM
suspend : rt2860_suspend,
resume : rt2860_resume,
#endif
};

/***************************************************************************
 *
 *	PCI device initialization related procedures.
 *
 ***************************************************************************/
#ifdef CONFIG_PM

void RT2860RejectPendingPackets(struct rt_rtmp_adapter *pAd)
{
	/* clear PS packets */
	/* clear TxSw packets */
}

static int rt2860_suspend(struct pci_dev *pci_dev, pm_message_t state)
{
	struct net_device *net_dev = pci_get_drvdata(pci_dev);
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)NULL;
	int retval = 0;

	DBGPRINT(RT_DEBUG_TRACE, ("===> rt2860_suspend()\n"));

	if (net_dev == NULL) {
		DBGPRINT(RT_DEBUG_ERROR, ("net_dev == NULL!\n"));
	} else {
		GET_PAD_FROM_NET_DEV(pAd, net_dev);

		/* we can not use IFF_UP because ra0 down but ra1 up */
		/* and 1 suspend/resume function for 1 module, not for each interface */
		/* so Linux will call suspend/resume function once */
		if (VIRTUAL_IF_NUM(pAd) > 0) {
			/* avoid users do suspend after interface is down */

			/* stop interface */
			netif_carrier_off(net_dev);
			netif_stop_queue(net_dev);

			/* mark device as removed from system and therefore no longer available */
			netif_device_detach(net_dev);

			/* mark halt flag */
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);

			/* take down the device */
			rt28xx_close((struct net_device *)net_dev);

			RT_MOD_DEC_USE_COUNT();
		}
	}

	/* reference to http://vovo2000.com/type-lab/linux/kernel-api/linux-kernel-api.html */
	/* enable device to generate PME# when suspended */
	/* pci_choose_state(): Choose the power state of a PCI device to be suspended */
	retval = pci_enable_wake(pci_dev, pci_choose_state(pci_dev, state), 1);
	/* save the PCI configuration space of a device before suspending */
	pci_save_state(pci_dev);
	/* disable PCI device after use */
	pci_disable_device(pci_dev);

	retval = pci_set_power_state(pci_dev, pci_choose_state(pci_dev, state));

	DBGPRINT(RT_DEBUG_TRACE, ("<=== rt2860_suspend()\n"));
	return retval;
}

static int rt2860_resume(struct pci_dev *pci_dev)
{
	struct net_device *net_dev = pci_get_drvdata(pci_dev);
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)NULL;
	int retval;

	/* set the power state of a PCI device */
	/* PCI has 4 power states, DO (normal) ~ D3(less power) */
	/* in include/linux/pci.h, you can find that */
	/* #define PCI_D0          ((pci_power_t __force) 0) */
	/* #define PCI_D1          ((pci_power_t __force) 1) */
	/* #define PCI_D2          ((pci_power_t __force) 2) */
	/* #define PCI_D3hot       ((pci_power_t __force) 3) */
	/* #define PCI_D3cold      ((pci_power_t __force) 4) */
	/* #define PCI_UNKNOWN     ((pci_power_t __force) 5) */
	/* #define PCI_POWER_ERROR ((pci_power_t __force) -1) */
	retval = pci_set_power_state(pci_dev, PCI_D0);

	/* restore the saved state of a PCI device */
	pci_restore_state(pci_dev);

	/* initialize device before it's used by a driver */
	if (pci_enable_device(pci_dev)) {
		printk("pci enable fail!\n");
		return 0;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("===> rt2860_resume()\n"));

	if (net_dev == NULL)
		DBGPRINT(RT_DEBUG_ERROR, ("net_dev == NULL!\n"));
	else
		GET_PAD_FROM_NET_DEV(pAd, net_dev);

	if (pAd != NULL) {
		/* we can not use IFF_UP because ra0 down but ra1 up */
		/* and 1 suspend/resume function for 1 module, not for each interface */
		/* so Linux will call suspend/resume function once */
		if (VIRTUAL_IF_NUM(pAd) > 0) {
			/* mark device as attached from system and restart if needed */
			netif_device_attach(net_dev);

			if (rt28xx_open((struct net_device *)net_dev) != 0) {
				/* open fail */
				DBGPRINT(RT_DEBUG_TRACE,
					 ("<=== rt2860_resume()\n"));
				return 0;
			}
			/* increase MODULE use count */
			RT_MOD_INC_USE_COUNT();

			RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);
			RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);

			netif_start_queue(net_dev);
			netif_carrier_on(net_dev);
			netif_wake_queue(net_dev);
		}
	}

	DBGPRINT(RT_DEBUG_TRACE, ("<=== rt2860_resume()\n"));
	return 0;
}
#endif /* CONFIG_PM // */

static int __init rt2860_init_module(void)
{
	return pci_register_driver(&rt2860_driver);
}

/* */
/* Driver module unload function */
/* */
static void __exit rt2860_cleanup_module(void)
{
	pci_unregister_driver(&rt2860_driver);
}

module_init(rt2860_init_module);
module_exit(rt2860_cleanup_module);

/* */
/* PCI device probe & initialization function */
/* */
static int __devinit rt2860_probe(IN struct pci_dev *pci_dev,
				  IN const struct pci_device_id *pci_id)
{
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)NULL;
	struct net_device *net_dev;
	void *handle;
	char *print_name;
	unsigned long csr_addr;
	int rv = 0;
	struct rt_rtmp_os_netdev_op_hook netDevHook;

	DBGPRINT(RT_DEBUG_TRACE, ("===> rt2860_probe\n"));

/*PCIDevInit============================================== */
	/* wake up and enable device */
	rv = pci_enable_device(pci_dev);

	if (rv != 0) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("Enable PCI device failed, errno=%d!\n", rv));
		return rv;
	}

	print_name = (char *)pci_name(pci_dev);

	rv = pci_request_regions(pci_dev, print_name);

	if (rv != 0) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("Request PCI resource failed, errno=%d!\n", rv));
		goto err_out;
	}
	/* map physical address to virtual address for accessing register */
	csr_addr =
	    (unsigned long)ioremap(pci_resource_start(pci_dev, 0),
				   pci_resource_len(pci_dev, 0));
	if (!csr_addr) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("ioremap failed for device %s, region 0x%lX @ 0x%lX\n",
			  print_name, (unsigned long)pci_resource_len(pci_dev, 0),
			  (unsigned long)pci_resource_start(pci_dev, 0)));
		goto err_out_free_res;
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s: at 0x%lx, VA 0x%lx, IRQ %d. \n", print_name,
			  (unsigned long)pci_resource_start(pci_dev, 0),
			  (unsigned long)csr_addr, pci_dev->irq));
	}

	/* Set DMA master */
	pci_set_master(pci_dev);

/*RtmpDevInit============================================== */
	/* Allocate struct rt_rtmp_adapter adapter structure */
	handle = kmalloc(sizeof(struct os_cookie), GFP_KERNEL);
	if (handle == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s(): Allocate memory for os handle failed!\n",
			  __func__));
		goto err_out_iounmap;
	}

	((struct os_cookie *)handle)->pci_dev = pci_dev;

	rv = RTMPAllocAdapterBlock(handle, &pAd);	/*shiang: we may need the pci_dev for allocate structure of "struct rt_rtmp_adapter" */
	if (rv != NDIS_STATUS_SUCCESS)
		goto err_out_iounmap;
	/* Here are the struct rt_rtmp_adapter structure with pci-bus specific parameters. */
	pAd->CSRBaseAddress = (u8 *)csr_addr;
	DBGPRINT(RT_DEBUG_ERROR,
		 ("pAd->CSRBaseAddress =0x%lx, csr_addr=0x%lx!\n",
		  (unsigned long)pAd->CSRBaseAddress, csr_addr));
	RtmpRaDevCtrlInit(pAd, RTMP_DEV_INF_PCI);

/*NetDevInit============================================== */
	net_dev = RtmpPhyNetDevInit(pAd, &netDevHook);
	if (net_dev == NULL)
		goto err_out_free_radev;

	/* Here are the net_device structure with pci-bus specific parameters. */
	net_dev->irq = pci_dev->irq;	/* Interrupt IRQ number */
	net_dev->base_addr = csr_addr;	/* Save CSR virtual address and irq to device structure */
	pci_set_drvdata(pci_dev, net_dev);	/* Set driver data */

/* for supporting Network Manager */
	/* Set the sysfs physical device reference for the network logical device
	 * if set prior to registration will cause a symlink during initialization.
	 */
	SET_NETDEV_DEV(net_dev, &(pci_dev->dev));

/*All done, it's time to register the net device to linux kernel. */
	/* Register this device */
	rv = RtmpOSNetDevAttach(net_dev, &netDevHook);
	if (rv)
		goto err_out_free_netdev;

	pAd->StaCfg.OriDevType = net_dev->type;
	RTMPInitPCIeDevice(pci_dev, pAd);

	DBGPRINT(RT_DEBUG_TRACE, ("<=== rt2860_probe\n"));

	return 0;		/* probe ok */

	/* --------------------------- ERROR HANDLE --------------------------- */
err_out_free_netdev:
	RtmpOSNetDevFree(net_dev);

err_out_free_radev:
	/* free struct rt_rtmp_adapter strcuture and os_cookie */
	RTMPFreeAdapter(pAd);

err_out_iounmap:
	iounmap((void *)(csr_addr));
	release_mem_region(pci_resource_start(pci_dev, 0),
			   pci_resource_len(pci_dev, 0));

err_out_free_res:
	pci_release_regions(pci_dev);

err_out:
	pci_disable_device(pci_dev);

	DBGPRINT(RT_DEBUG_ERROR,
		 ("<=== rt2860_probe failed with rv = %d!\n", rv));

	return -ENODEV;		/* probe fail */
}

static void __devexit rt2860_remove_one(IN struct pci_dev *pci_dev)
{
	struct net_device *net_dev = pci_get_drvdata(pci_dev);
	struct rt_rtmp_adapter *pAd = NULL;
	unsigned long csr_addr = net_dev->base_addr;	/* pAd->CSRBaseAddress; */

	GET_PAD_FROM_NET_DEV(pAd, net_dev);

	DBGPRINT(RT_DEBUG_TRACE, ("===> rt2860_remove_one\n"));

	if (pAd != NULL) {
		/* Unregister/Free all allocated net_device. */
		RtmpPhyNetDevExit(pAd, net_dev);

		/* Unmap CSR base address */
		iounmap((char *)(csr_addr));

		/* release memory region */
		release_mem_region(pci_resource_start(pci_dev, 0),
				   pci_resource_len(pci_dev, 0));

		/* Free struct rt_rtmp_adapter related structures. */
		RtmpRaDevCtrlExit(pAd);

	} else {
		/* Unregister network device */
		RtmpOSNetDevDetach(net_dev);

		/* Unmap CSR base address */
		iounmap((char *)(net_dev->base_addr));

		/* release memory region */
		release_mem_region(pci_resource_start(pci_dev, 0),
				   pci_resource_len(pci_dev, 0));
	}

	/* Free the root net_device */
	RtmpOSNetDevFree(net_dev);

}

/*
========================================================================
Routine Description:
    Check the chipset vendor/product ID.

Arguments:
    _dev_p				Point to the PCI or USB device

Return Value:
    TRUE				Check ok
	FALSE				Check fail

Note:
========================================================================
*/
BOOLEAN RT28XXChipsetCheck(IN void *_dev_p)
{
	/* always TRUE */
	return TRUE;
}

/***************************************************************************
 *
 *	PCIe device initialization related procedures.
 *
 ***************************************************************************/
static void RTMPInitPCIeDevice(struct pci_dev *pci_dev, struct rt_rtmp_adapter *pAd)
{
	u16 device_id;
	struct os_cookie *pObj;

	pObj = (struct os_cookie *)pAd->OS_Cookie;
	pci_read_config_word(pci_dev, PCI_DEVICE_ID, &device_id);
	device_id = le2cpu16(device_id);
	pObj->DeviceID = device_id;
	if (
#ifdef RT2860
		   (device_id == NIC2860_PCIe_DEVICE_ID) ||
		   (device_id == NIC2790_PCIe_DEVICE_ID) ||
		   (device_id == VEN_AWT_PCIe_DEVICE_ID) ||
#endif
#ifdef RT3090
		   (device_id == NIC3090_PCIe_DEVICE_ID) ||
		   (device_id == NIC3091_PCIe_DEVICE_ID) ||
		   (device_id == NIC3092_PCIe_DEVICE_ID) ||
#endif /* RT3090 // */
		   0) {
		u32 MacCsr0 = 0, Index = 0;
		do {
			RTMP_IO_READ32(pAd, MAC_CSR0, &MacCsr0);

			if ((MacCsr0 != 0x00) && (MacCsr0 != 0xFFFFFFFF))
				break;

			RTMPusecDelay(10);
		} while (Index++ < 100);

		/* Support advanced power save after 2892/2790. */
		/* MAC version at offset 0x1000 is 0x2872XXXX/0x2870XXXX(PCIe, USB, SDIO). */
		if ((MacCsr0 & 0xffff0000) != 0x28600000)
			OPSTATUS_SET_FLAG(pAd, fOP_STATUS_PCIE_DEVICE);
	}
}

void RTMPInitPCIeLinkCtrlValue(struct rt_rtmp_adapter *pAd)
{
	int pos;
	u16 reg16, data2, PCIePowerSaveLevel, Configuration;
	u32 MacValue;
	BOOLEAN bFindIntel = FALSE;
	struct os_cookie *pObj;

	pObj = (struct os_cookie *)pAd->OS_Cookie;

	if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE))
		return;

	DBGPRINT(RT_DEBUG_TRACE, ("%s.===>\n", __func__));
	/* Init EEPROM, and save settings */
	if (!(IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))) {
		RT28xx_EEPROM_READ16(pAd, 0x22, PCIePowerSaveLevel);
		pAd->PCIePowerSaveLevel = PCIePowerSaveLevel & 0xff;

		pAd->LnkCtrlBitMask = 0;
		if ((PCIePowerSaveLevel & 0xff) == 0xff) {
			OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_PCIE_DEVICE);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("====> PCIePowerSaveLevel = 0x%x.\n",
				  PCIePowerSaveLevel));
			return;
		} else {
			PCIePowerSaveLevel &= 0x3;
			RT28xx_EEPROM_READ16(pAd, 0x24, data2);

			if (!
			    (((data2 & 0xff00) == 0x9200)
			     && ((data2 & 0x80) != 0))) {
				if (PCIePowerSaveLevel > 1)
					PCIePowerSaveLevel = 1;
			}

			DBGPRINT(RT_DEBUG_TRACE,
				 ("====> Write 0x83 = 0x%x.\n",
				  PCIePowerSaveLevel));
			AsicSendCommandToMcu(pAd, 0x83, 0xff,
					     (u8)PCIePowerSaveLevel, 0x00);
			RT28xx_EEPROM_READ16(pAd, 0x22, PCIePowerSaveLevel);
			PCIePowerSaveLevel &= 0xff;
			PCIePowerSaveLevel = PCIePowerSaveLevel >> 6;
			switch (PCIePowerSaveLevel) {
			case 0:	/* Only support L0 */
				pAd->LnkCtrlBitMask = 0;
				break;
			case 1:	/* Only enable L0s */
				pAd->LnkCtrlBitMask = 1;
				break;
			case 2:	/* enable L1, L0s */
				pAd->LnkCtrlBitMask = 3;
				break;
			case 3:	/* sync with host clk and enable L1, L0s */
				pAd->LnkCtrlBitMask = 0x103;
				break;
			}
			RT28xx_EEPROM_READ16(pAd, 0x24, data2);
			if ((PCIePowerSaveLevel & 0xff) != 0xff) {
				PCIePowerSaveLevel &= 0x3;

				if (!
				    (((data2 & 0xff00) == 0x9200)
				     && ((data2 & 0x80) != 0))) {
					if (PCIePowerSaveLevel > 1)
						PCIePowerSaveLevel = 1;
				}

				DBGPRINT(RT_DEBUG_TRACE,
					 ("====> rt28xx Write 0x83 Command = 0x%x.\n",
					  PCIePowerSaveLevel));

				AsicSendCommandToMcu(pAd, 0x83, 0xff,
						     (u8)PCIePowerSaveLevel,
						     0x00);
			}
			DBGPRINT(RT_DEBUG_TRACE,
				 ("====> LnkCtrlBitMask = 0x%x.\n",
				  pAd->LnkCtrlBitMask));
		}
	} else if (IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd)) {
		u8 LinkCtrlSetting = 0;

		/* Check 3090E special setting chip. */
		RT28xx_EEPROM_READ16(pAd, 0x24, data2);
		if ((data2 == 0x9280) && ((pAd->MACVersion & 0xffff) == 0x0211)) {
			pAd->b3090ESpecialChip = TRUE;
			DBGPRINT_RAW(RT_DEBUG_ERROR, ("Special 3090E chip \n"));
		}

		RTMP_IO_READ32(pAd, AUX_CTRL, &MacValue);
		/*enable WAKE_PCIE function, which forces to enable PCIE clock when mpu interrupt asserting. */
		/*Force PCIE 125MHz CLK to toggle */
		MacValue |= 0x402;
		RTMP_IO_WRITE32(pAd, AUX_CTRL, MacValue);
		DBGPRINT_RAW(RT_DEBUG_ERROR,
			     (" AUX_CTRL = 0x%32x\n", MacValue));

		/* for RT30xx F and after, PCIe infterface, and for power solution 3 */
		if ((IS_VERSION_AFTER_F(pAd))
		    && (pAd->StaCfg.PSControl.field.rt30xxPowerMode >= 2)
		    && (pAd->StaCfg.PSControl.field.rt30xxPowerMode <= 3)) {
			RTMP_IO_READ32(pAd, AUX_CTRL, &MacValue);
			DBGPRINT_RAW(RT_DEBUG_ERROR,
				     (" Read AUX_CTRL = 0x%x\n", MacValue));
			/* turn on bit 12. */
			/*enable 32KHz clock mode for power saving */
			MacValue |= 0x1000;
			if (MacValue != 0xffffffff) {
				RTMP_IO_WRITE32(pAd, AUX_CTRL, MacValue);
				DBGPRINT_RAW(RT_DEBUG_ERROR,
					     (" Write AUX_CTRL = 0x%x\n",
					      MacValue));
				/* 1. if use PCIePowerSetting is 2 or 3, need to program OSC_CTRL to 0x3ff11. */
				MacValue = 0x3ff11;
				RTMP_IO_WRITE32(pAd, OSC_CTRL, MacValue);
				DBGPRINT_RAW(RT_DEBUG_ERROR,
					     (" OSC_CTRL = 0x%x\n", MacValue));
				/* 2. Write PCI register Clk ref bit */
				RTMPrt3xSetPCIePowerLinkCtrl(pAd);
			} else {
				/* Error read Aux_Ctrl value.  Force to use solution 1 */
				DBGPRINT(RT_DEBUG_ERROR,
					 (" Error Value in AUX_CTRL = 0x%x\n",
					  MacValue));
				pAd->StaCfg.PSControl.field.rt30xxPowerMode = 1;
				DBGPRINT(RT_DEBUG_ERROR,
					 (" Force to use power solution1 \n"));
			}
		}
		/* 1. read setting from inf file. */

		PCIePowerSaveLevel =
		    (u16)pAd->StaCfg.PSControl.field.rt30xxPowerMode;
		DBGPRINT(RT_DEBUG_ERROR,
			 ("====> rt30xx Read PowerLevelMode =  0x%x.\n",
			  PCIePowerSaveLevel));
		/* 2. Check EnableNewPS. */
		if (pAd->StaCfg.PSControl.field.EnableNewPS == FALSE)
			PCIePowerSaveLevel = 1;

		if (IS_VERSION_BEFORE_F(pAd)
		    && (pAd->b3090ESpecialChip == FALSE)) {
			/* Chip Version E only allow 1, So force set 1. */
			PCIePowerSaveLevel &= 0x1;
			pAd->PCIePowerSaveLevel = (u16)PCIePowerSaveLevel;
			DBGPRINT(RT_DEBUG_TRACE,
				 ("====> rt30xx E Write 0x83 Command = 0x%x.\n",
				  PCIePowerSaveLevel));

			AsicSendCommandToMcu(pAd, 0x83, 0xff,
					     (u8)PCIePowerSaveLevel, 0x00);
		} else {
			/* Chip Version F and after only allow 1 or 2 or 3. This might be modified after new chip version come out. */
			if (!
			    ((PCIePowerSaveLevel == 1)
			     || (PCIePowerSaveLevel == 3)))
				PCIePowerSaveLevel = 1;
			DBGPRINT(RT_DEBUG_ERROR,
				 ("====> rt30xx F Write 0x83 Command = 0x%x.\n",
				  PCIePowerSaveLevel));
			pAd->PCIePowerSaveLevel = (u16)PCIePowerSaveLevel;
			/* for 3090F , we need to add high-byte arg for 0x83 command to indicate the link control setting in */
			/* PCI Configuration Space. Because firmware can't read PCI Configuration Space */
			if ((pAd->Rt3xxRalinkLinkCtrl & 0x2)
			    && (pAd->Rt3xxHostLinkCtrl & 0x2)) {
				LinkCtrlSetting = 1;
			}
			DBGPRINT(RT_DEBUG_TRACE,
				 ("====> rt30xxF LinkCtrlSetting = 0x%x.\n",
				  LinkCtrlSetting));
			AsicSendCommandToMcu(pAd, 0x83, 0xff,
					     (u8)PCIePowerSaveLevel,
					     LinkCtrlSetting);
		}
	}
	/* Find Ralink PCIe Device's Express Capability Offset */
	pos = pci_find_capability(pObj->pci_dev, PCI_CAP_ID_EXP);

	if (pos != 0) {
		/* Ralink PCIe Device's Link Control Register Offset */
		pAd->RLnkCtrlOffset = pos + PCI_EXP_LNKCTL;
		pci_read_config_word(pObj->pci_dev, pAd->RLnkCtrlOffset,
				     &reg16);
		Configuration = le2cpu16(reg16);
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Read (Ralink PCIe Link Control Register) offset 0x%x = 0x%x\n",
			  pAd->RLnkCtrlOffset, Configuration));
		pAd->RLnkCtrlConfiguration = (Configuration & 0x103);
		Configuration &= 0xfefc;
		Configuration |= (0x0);
#ifdef RT2860
		if ((pObj->DeviceID == NIC2860_PCIe_DEVICE_ID)
		    || (pObj->DeviceID == NIC2790_PCIe_DEVICE_ID)) {
			reg16 = cpu2le16(Configuration);
			pci_write_config_word(pObj->pci_dev,
					      pAd->RLnkCtrlOffset, reg16);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("Write (Ralink PCIe Link Control Register)  offset 0x%x = 0x%x\n",
				  pos + PCI_EXP_LNKCTL, Configuration));
		}
#endif /* RT2860 // */

		RTMPFindHostPCIDev(pAd);
		if (pObj->parent_pci_dev) {
			u16 vendor_id;

			pci_read_config_word(pObj->parent_pci_dev,
					     PCI_VENDOR_ID, &vendor_id);
			vendor_id = le2cpu16(vendor_id);
			if (vendor_id == PCIBUS_INTEL_VENDOR) {
				bFindIntel = TRUE;
				RTMP_SET_PSFLAG(pAd, fRTMP_PS_TOGGLE_L1);
			}
			/* Find PCI-to-PCI Bridge Express Capability Offset */
			pos =
			    pci_find_capability(pObj->parent_pci_dev,
						PCI_CAP_ID_EXP);

			if (pos != 0) {
				BOOLEAN bChange = FALSE;
				/* PCI-to-PCI Bridge Link Control Register Offset */
				pAd->HostLnkCtrlOffset = pos + PCI_EXP_LNKCTL;
				pci_read_config_word(pObj->parent_pci_dev,
						     pAd->HostLnkCtrlOffset,
						     &reg16);
				Configuration = le2cpu16(reg16);
				DBGPRINT(RT_DEBUG_TRACE,
					 ("Read (Host PCI-to-PCI Bridge Link Control Register) offset 0x%x = 0x%x\n",
					  pAd->HostLnkCtrlOffset,
					  Configuration));
				pAd->HostLnkCtrlConfiguration =
				    (Configuration & 0x103);
				Configuration &= 0xfefc;
				Configuration |= (0x0);

				switch (pObj->DeviceID) {
#ifdef RT2860
				case NIC2860_PCIe_DEVICE_ID:
				case NIC2790_PCIe_DEVICE_ID:
					bChange = TRUE;
					break;
#endif /* RT2860 // */
#ifdef RT3090
				case NIC3090_PCIe_DEVICE_ID:
				case NIC3091_PCIe_DEVICE_ID:
				case NIC3092_PCIe_DEVICE_ID:
					if (bFindIntel == FALSE)
						bChange = TRUE;
					break;
#endif /* RT3090 // */
				default:
					break;
				}

				if (bChange) {
					reg16 = cpu2le16(Configuration);
					pci_write_config_word(pObj->
							      parent_pci_dev,
							      pAd->
							      HostLnkCtrlOffset,
							      reg16);
					DBGPRINT(RT_DEBUG_TRACE,
						 ("Write (Host PCI-to-PCI Bridge Link Control Register) offset 0x%x = 0x%x\n",
						  pAd->HostLnkCtrlOffset,
						  Configuration));
				}
			} else {
				pAd->HostLnkCtrlOffset = 0;
				DBGPRINT(RT_DEBUG_ERROR,
					 ("%s: cannot find PCI-to-PCI Bridge PCI Express Capability!\n",
					  __func__));
			}
		}
	} else {
		pAd->RLnkCtrlOffset = 0;
		pAd->HostLnkCtrlOffset = 0;
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: cannot find Ralink PCIe Device's PCI Express Capability!\n",
			  __func__));
	}

	if (bFindIntel == FALSE) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Doesn't find Intel PCI host controller. \n"));
		/* Doesn't switch L0, L1, So set PCIePowerSaveLevel to 0xff */
		pAd->PCIePowerSaveLevel = 0xff;
		if ((pAd->RLnkCtrlOffset != 0)
#ifdef RT3090
		    && ((pObj->DeviceID == NIC3090_PCIe_DEVICE_ID)
			|| (pObj->DeviceID == NIC3091_PCIe_DEVICE_ID)
			|| (pObj->DeviceID == NIC3092_PCIe_DEVICE_ID))
#endif /* RT3090 // */
		    ) {
			pci_read_config_word(pObj->pci_dev, pAd->RLnkCtrlOffset,
					     &reg16);
			Configuration = le2cpu16(reg16);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("Read (Ralink 30xx PCIe Link Control Register) offset 0x%x = 0x%x\n",
				  pAd->RLnkCtrlOffset, Configuration));
			pAd->RLnkCtrlConfiguration = (Configuration & 0x103);
			Configuration &= 0xfefc;
			Configuration |= (0x0);
			reg16 = cpu2le16(Configuration);
			pci_write_config_word(pObj->pci_dev,
					      pAd->RLnkCtrlOffset, reg16);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("Write (Ralink PCIe Link Control Register)  offset 0x%x = 0x%x\n",
				  pos + PCI_EXP_LNKCTL, Configuration));
		}
	}
}

void RTMPFindHostPCIDev(struct rt_rtmp_adapter *pAd)
{
	u16 reg16;
	u8 reg8;
	u32 DevFn;
	struct pci_dev *pPci_dev;
	struct os_cookie *pObj;

	pObj = (struct os_cookie *)pAd->OS_Cookie;

	if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE))
		return;

	DBGPRINT(RT_DEBUG_TRACE, ("%s.===>\n", __func__));

	pObj->parent_pci_dev = NULL;
	if (pObj->pci_dev->bus->parent) {
		for (DevFn = 0; DevFn < 255; DevFn++) {
			pPci_dev =
			    pci_get_slot(pObj->pci_dev->bus->parent, DevFn);
			if (pPci_dev) {
				pci_read_config_word(pPci_dev, PCI_CLASS_DEVICE,
						     &reg16);
				reg16 = le2cpu16(reg16);
				pci_read_config_byte(pPci_dev, PCI_CB_CARD_BUS,
						     &reg8);
				if ((reg16 == PCI_CLASS_BRIDGE_PCI)
				    && (reg8 == pObj->pci_dev->bus->number)) {
					pObj->parent_pci_dev = pPci_dev;
				}
			}
		}
	}
}

/*
	========================================================================

	Routine Description:

	Arguments:
		Level = RESTORE_HALT : Restore PCI host and Ralink PCIe Link Control field to its default value.
		Level = Other Value : Restore from dot11 power save or radio off status. And force PCI host Link Control fields to 0x1

	========================================================================
*/
void RTMPPCIeLinkCtrlValueRestore(struct rt_rtmp_adapter *pAd, u8 Level)
{
	u16 PCIePowerSaveLevel, reg16;
	u16 Configuration;
	struct os_cookie *pObj;

	pObj = (struct os_cookie *)pAd->OS_Cookie;

	if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE))
		return;

#ifdef RT2860
	if (!((pObj->DeviceID == NIC2860_PCIe_DEVICE_ID)
	      || (pObj->DeviceID == NIC2790_PCIe_DEVICE_ID)))
		return;
#endif /* RT2860 // */
	/* Check PSControl Configuration */
	if (pAd->StaCfg.PSControl.field.EnableNewPS == FALSE)
		return;

	/*3090 will not execute the following codes. */
	/* Check interface : If not PCIe interface, return. */

#ifdef RT3090
	if ((pObj->DeviceID == NIC3090_PCIe_DEVICE_ID)
	    || (pObj->DeviceID == NIC3091_PCIe_DEVICE_ID)
	    || (pObj->DeviceID == NIC3092_PCIe_DEVICE_ID))
		return;
#endif /* RT3090 // */

	DBGPRINT(RT_DEBUG_TRACE, ("%s.===>\n", __func__));
	PCIePowerSaveLevel = pAd->PCIePowerSaveLevel;
	if ((PCIePowerSaveLevel & 0xff) == 0xff) {
		DBGPRINT(RT_DEBUG_TRACE, ("return  \n"));
		return;
	}

	if (pObj->parent_pci_dev && (pAd->HostLnkCtrlOffset != 0)) {
		PCI_REG_READ_WORD(pObj->parent_pci_dev, pAd->HostLnkCtrlOffset,
				  Configuration);
		if ((Configuration != 0) && (Configuration != 0xFFFF)) {
			Configuration &= 0xfefc;
			/* If call from interface down, restore to orginial setting. */
			if (Level == RESTORE_CLOSE)
				Configuration |= pAd->HostLnkCtrlConfiguration;
			else
				Configuration |= 0x0;
			PCI_REG_WIRTE_WORD(pObj->parent_pci_dev,
					   pAd->HostLnkCtrlOffset,
					   Configuration);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("Restore PCI host : offset 0x%x = 0x%x\n",
				  pAd->HostLnkCtrlOffset, Configuration));
		} else
			DBGPRINT(RT_DEBUG_ERROR,
				 ("Restore PCI host : PCI_REG_READ_WORD failed (Configuration = 0x%x)\n",
				  Configuration));
	}

	if (pObj->pci_dev && (pAd->RLnkCtrlOffset != 0)) {
		PCI_REG_READ_WORD(pObj->pci_dev, pAd->RLnkCtrlOffset,
				  Configuration);
		if ((Configuration != 0) && (Configuration != 0xFFFF)) {
			Configuration &= 0xfefc;
			/* If call from interface down, restore to orginial setting. */
			if (Level == RESTORE_CLOSE)
				Configuration |= pAd->RLnkCtrlConfiguration;
			else
				Configuration |= 0x0;
			PCI_REG_WIRTE_WORD(pObj->pci_dev, pAd->RLnkCtrlOffset,
					   Configuration);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("Restore Ralink : offset 0x%x = 0x%x\n",
				  pAd->RLnkCtrlOffset, Configuration));
		} else
			DBGPRINT(RT_DEBUG_ERROR,
				 ("Restore Ralink : PCI_REG_READ_WORD failed (Configuration = 0x%x)\n",
				  Configuration));
	}

	DBGPRINT(RT_DEBUG_TRACE, ("%s <===\n", __func__));
}

/*
	========================================================================

	Routine Description:

	Arguments:
		Max : limit Host PCI and Ralink PCIe device's LINK CONTROL field's value.
		Because now frequently set our device to mode 1 or mode 3 will cause problem.

	========================================================================
*/
void RTMPPCIeLinkCtrlSetting(struct rt_rtmp_adapter *pAd, u16 Max)
{
	u16 PCIePowerSaveLevel, reg16;
	u16 Configuration;
	struct os_cookie *pObj;

	pObj = (struct os_cookie *)pAd->OS_Cookie;

	if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE))
		return;

#ifdef RT2860
	if (!((pObj->DeviceID == NIC2860_PCIe_DEVICE_ID)
	      || (pObj->DeviceID == NIC2790_PCIe_DEVICE_ID)))
		return;
#endif /* RT2860 // */
	/* Check PSControl Configuration */
	if (pAd->StaCfg.PSControl.field.EnableNewPS == FALSE)
		return;

	/* Check interface : If not PCIe interface, return. */
	/*Block 3090 to enter the following function */

#ifdef RT3090
	if ((pObj->DeviceID == NIC3090_PCIe_DEVICE_ID)
	    || (pObj->DeviceID == NIC3091_PCIe_DEVICE_ID)
	    || (pObj->DeviceID == NIC3092_PCIe_DEVICE_ID))
		return;
#endif /* RT3090 // */
	if (!RTMP_TEST_PSFLAG(pAd, fRTMP_PS_CAN_GO_SLEEP)) {
		DBGPRINT(RT_DEBUG_INFO,
			 ("RTMPPCIePowerLinkCtrl return on fRTMP_PS_CAN_GO_SLEEP flag\n"));
		return;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("%s===>\n", __func__));
	PCIePowerSaveLevel = pAd->PCIePowerSaveLevel;
	if ((PCIePowerSaveLevel & 0xff) == 0xff) {
		DBGPRINT(RT_DEBUG_TRACE, ("return  \n"));
		return;
	}
	PCIePowerSaveLevel = PCIePowerSaveLevel >> 6;

	/* Skip non-exist deice right away */
	if (pObj->parent_pci_dev && (pAd->HostLnkCtrlOffset != 0)) {
		PCI_REG_READ_WORD(pObj->parent_pci_dev, pAd->HostLnkCtrlOffset,
				  Configuration);
		switch (PCIePowerSaveLevel) {
		case 0:
			/* Set b0 and b1 of LinkControl (both 2892 and PCIe bridge) to 00 */
			Configuration &= 0xfefc;
			break;
		case 1:
			/* Set b0 and b1 of LinkControl (both 2892 and PCIe bridge) to 01 */
			Configuration &= 0xfefc;
			Configuration |= 0x1;
			break;
		case 2:
			/*  Set b0 and b1 of LinkControl (both 2892 and PCIe bridge) to 11 */
			Configuration &= 0xfefc;
			Configuration |= 0x3;
			break;
		case 3:
			/* Set b0 and b1 of LinkControl (both 2892 and PCIe bridge) to 11 and bit 8 of LinkControl of 2892 to 1 */
			Configuration &= 0xfefc;
			Configuration |= 0x103;
			break;
		}
		PCI_REG_WIRTE_WORD(pObj->parent_pci_dev, pAd->HostLnkCtrlOffset,
				   Configuration);
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Write PCI host offset 0x%x = 0x%x\n",
			  pAd->HostLnkCtrlOffset, Configuration));
	}

	if (pObj->pci_dev && (pAd->RLnkCtrlOffset != 0)) {
		/* first 2892 chip not allow to frequently set mode 3. will cause hang problem. */
		if (PCIePowerSaveLevel > Max)
			PCIePowerSaveLevel = Max;

		PCI_REG_READ_WORD(pObj->pci_dev, pAd->RLnkCtrlOffset,
				  Configuration);
		switch (PCIePowerSaveLevel) {
		case 0:
			/* No PCI power safe */
			/* Set b0 and b1 of LinkControl (both 2892 and PCIe bridge) to 00 . */
			Configuration &= 0xfefc;
			break;
		case 1:
			/*  L0 */
			/* Set b0 and b1 of LinkControl (both 2892 and PCIe bridge) to 01 . */
			Configuration &= 0xfefc;
			Configuration |= 0x1;
			break;
		case 2:
			/* L0 and L1 */
			/*  Set b0 and b1 of LinkControl (both 2892 and PCIe bridge) to 11 */
			Configuration &= 0xfefc;
			Configuration |= 0x3;
			break;
		case 3:
			/* L0 , L1 and clock management. */
			/* Set b0 and b1 of LinkControl (both 2892 and PCIe bridge) to 11 and bit 8 of LinkControl of 2892 to 1 */
			Configuration &= 0xfefc;
			Configuration |= 0x103;
			pAd->bPCIclkOff = TRUE;
			break;
		}
		PCI_REG_WIRTE_WORD(pObj->pci_dev, pAd->RLnkCtrlOffset,
				   Configuration);
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Write Ralink device : offset 0x%x = 0x%x\n",
			  pAd->RLnkCtrlOffset, Configuration));
	}

	DBGPRINT(RT_DEBUG_TRACE, ("RTMPPCIePowerLinkCtrl <==============\n"));
}

/*
	========================================================================

	Routine Description:
		1. Write a PCI register for rt30xx power solution 3

	========================================================================
*/
void RTMPrt3xSetPCIePowerLinkCtrl(struct rt_rtmp_adapter *pAd)
{

	unsigned long HostConfiguration = 0;
	unsigned long Configuration;
	struct os_cookie *pObj;
	int pos;
	u16 reg16;

	pObj = (struct os_cookie *)pAd->OS_Cookie;

	DBGPRINT(RT_DEBUG_INFO,
		 ("RTMPrt3xSetPCIePowerLinkCtrl.===> %lx\n",
		  pAd->StaCfg.PSControl.word));

	/* Check PSControl Configuration */
	if (pAd->StaCfg.PSControl.field.EnableNewPS == FALSE)
		return;
	RTMPFindHostPCIDev(pAd);
	if (pObj->parent_pci_dev) {
		/* Find PCI-to-PCI Bridge Express Capability Offset */
		pos = pci_find_capability(pObj->parent_pci_dev, PCI_CAP_ID_EXP);

		if (pos != 0)
			pAd->HostLnkCtrlOffset = pos + PCI_EXP_LNKCTL;

		/* If configurared to turn on L1. */
		HostConfiguration = 0;
		if (pAd->StaCfg.PSControl.field.rt30xxForceASPMTest == 1) {
			DBGPRINT(RT_DEBUG_TRACE, ("Enter,PSM : Force ASPM\n"));

			/* Skip non-exist deice right away */
			if ((pAd->HostLnkCtrlOffset != 0)) {
				PCI_REG_READ_WORD(pObj->parent_pci_dev,
						  pAd->HostLnkCtrlOffset,
						  HostConfiguration);
				/* Prepare Configuration to write to Host */
				HostConfiguration |= 0x3;
				PCI_REG_WIRTE_WORD(pObj->parent_pci_dev,
						   pAd->HostLnkCtrlOffset,
						   HostConfiguration);
				pAd->Rt3xxHostLinkCtrl = HostConfiguration;
				/* Because in rt30xxForceASPMTest Mode, Force turn on L0s, L1. */
				/* Fix HostConfiguration bit0:1 = 0x3 for later use. */
				HostConfiguration = 0x3;
				DBGPRINT(RT_DEBUG_TRACE,
					 ("PSM : Force ASPM : "
					  "Host device L1/L0s Value =  0x%lx\n",
					  HostConfiguration));
			}
		} else if (pAd->StaCfg.PSControl.field.rt30xxFollowHostASPM ==
			   1) {

			/* Skip non-exist deice right away */
			if ((pAd->HostLnkCtrlOffset != 0)) {
				PCI_REG_READ_WORD(pObj->parent_pci_dev,
						  pAd->HostLnkCtrlOffset,
						  HostConfiguration);
				pAd->Rt3xxHostLinkCtrl = HostConfiguration;
				HostConfiguration &= 0x3;
				DBGPRINT(RT_DEBUG_TRACE,
					 ("PSM : Follow Host ASPM : "
					  "Host device L1/L0s Value =  0x%lx\n",
					  HostConfiguration));
			}
		}
	}
	/* Prepare to write Ralink setting. */
	/* Find Ralink PCIe Device's Express Capability Offset */
	pos = pci_find_capability(pObj->pci_dev, PCI_CAP_ID_EXP);

	if (pos != 0) {
		/* Ralink PCIe Device's Link Control Register Offset */
		pAd->RLnkCtrlOffset = pos + PCI_EXP_LNKCTL;
		pci_read_config_word(pObj->pci_dev, pAd->RLnkCtrlOffset,
				     &reg16);
		Configuration = le2cpu16(reg16);
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Read (Ralink PCIe Link Control Register) "
			  "offset 0x%x = 0x%lx\n",
			  pAd->RLnkCtrlOffset, Configuration));
		Configuration |= 0x100;
		if ((pAd->StaCfg.PSControl.field.rt30xxFollowHostASPM == 1)
		    || (pAd->StaCfg.PSControl.field.rt30xxForceASPMTest == 1)) {
			switch (HostConfiguration) {
			case 0:
				Configuration &= 0xffffffc;
				break;
			case 1:
				Configuration &= 0xffffffc;
				Configuration |= 0x1;
				break;
			case 2:
				Configuration &= 0xffffffc;
				Configuration |= 0x2;
				break;
			case 3:
				Configuration |= 0x3;
				break;
			}
		}
		reg16 = cpu2le16(Configuration);
		pci_write_config_word(pObj->pci_dev, pAd->RLnkCtrlOffset,
				      reg16);
		pAd->Rt3xxRalinkLinkCtrl = Configuration;
		DBGPRINT(RT_DEBUG_TRACE,
			 ("PSM :Write Ralink device L1/L0s Value =  0x%lx\n",
			  Configuration));
	}
	DBGPRINT(RT_DEBUG_INFO,
		 ("PSM :RTMPrt3xSetPCIePowerLinkCtrl <==============\n"));
}
