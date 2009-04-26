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
    2870_main_dev.c

    Abstract:
    Create and register network interface.

    Revision History:
    Who         When            What
    --------    ----------      ----------------------------------------------
*/

#include "rt_config.h"

extern INT __devinit rt28xx_probe(IN void *_dev_p, IN void *_dev_id_p,
									IN UINT argc, OUT PRTMP_ADAPTER *ppAd);

static void rx_done_tasklet(unsigned long data);
static void mgmt_dma_done_tasklet(unsigned long data);
static void ac0_dma_done_tasklet(unsigned long data);
static void ac1_dma_done_tasklet(unsigned long data);
static void ac2_dma_done_tasklet(unsigned long data);
static void ac3_dma_done_tasklet(unsigned long data);
static void hcca_dma_done_tasklet(unsigned long data);
static void fifo_statistic_full_tasklet(unsigned long data);


/*---------------------------------------------------------------------*/
/* Symbol & Macro Definitions                                          */
/*---------------------------------------------------------------------*/
#define RT2860_INT_RX_DLY				(1<<0)		// bit 0
#define RT2860_INT_TX_DLY				(1<<1)		// bit 1
#define RT2860_INT_RX_DONE				(1<<2)		// bit 2
#define RT2860_INT_AC0_DMA_DONE			(1<<3)		// bit 3
#define RT2860_INT_AC1_DMA_DONE			(1<<4)		// bit 4
#define RT2860_INT_AC2_DMA_DONE			(1<<5)		// bit 5
#define RT2860_INT_AC3_DMA_DONE			(1<<6)		// bit 6
#define RT2860_INT_HCCA_DMA_DONE		(1<<7)		// bit 7
#define RT2860_INT_MGMT_DONE			(1<<8)		// bit 8

#define INT_RX			RT2860_INT_RX_DONE

#define INT_AC0_DLY		(RT2860_INT_AC0_DMA_DONE) //| RT2860_INT_TX_DLY)
#define INT_AC1_DLY		(RT2860_INT_AC1_DMA_DONE) //| RT2860_INT_TX_DLY)
#define INT_AC2_DLY		(RT2860_INT_AC2_DMA_DONE) //| RT2860_INT_TX_DLY)
#define INT_AC3_DLY		(RT2860_INT_AC3_DMA_DONE) //| RT2860_INT_TX_DLY)
#define INT_HCCA_DLY 	(RT2860_INT_HCCA_DMA_DONE) //| RT2860_INT_TX_DLY)
#define INT_MGMT_DLY	RT2860_INT_MGMT_DONE

/*---------------------------------------------------------------------*/
/* Prototypes of Functions Used                                        */
/*---------------------------------------------------------------------*/
/* function declarations */
static INT __devinit rt2860_init_one (struct pci_dev *pci_dev, const struct pci_device_id  *ent);
static VOID __devexit rt2860_remove_one(struct pci_dev *pci_dev);
static INT __devinit rt2860_probe(struct pci_dev *pci_dev, const struct pci_device_id  *ent);
void init_thread_task(PRTMP_ADAPTER pAd);
static void __exit rt2860_cleanup_module(void);
static int __init rt2860_init_module(void);

#ifdef CONFIG_PM
static int rt2860_suspend(struct pci_dev *pci_dev, pm_message_t state);
static int rt2860_resume(struct pci_dev *pci_dev);
#endif // CONFIG_PM //


//
// Ralink PCI device table, include all supported chipsets
//
static struct pci_device_id rt2860_pci_tbl[] __devinitdata =
{
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC2860_PCI_DEVICE_ID)},		//RT28602.4G
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC2860_PCIe_DEVICE_ID)},
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC2760_PCI_DEVICE_ID)},
	{PCI_DEVICE(NIC_PCI_VENDOR_ID, NIC2790_PCIe_DEVICE_ID)},
	{PCI_DEVICE(VEN_AWT_PCI_VENDOR_ID, VEN_AWT_PCIe_DEVICE_ID)},
    {0,}		// terminate list
};

MODULE_DEVICE_TABLE(pci, rt2860_pci_tbl);
#ifdef CONFIG_STA_SUPPORT
MODULE_LICENSE("GPL");
#ifdef MODULE_VERSION
MODULE_VERSION(STA_DRIVER_VERSION);
#endif
#endif // CONFIG_STA_SUPPORT //


//
// Our PCI driver structure
//
static struct pci_driver rt2860_driver =
{
    name:       "rt2860",
    id_table:   rt2860_pci_tbl,
    probe:      rt2860_init_one,
    remove:     __devexit_p(rt2860_remove_one),

#ifdef CONFIG_PM
	suspend:	rt2860_suspend,
	resume:		rt2860_resume,
#endif
};


#ifdef CONFIG_PM

VOID RT2860RejectPendingPackets(
	IN	PRTMP_ADAPTER	pAd)
{
	// clear PS packets
	// clear TxSw packets
}

static int rt2860_suspend(
	struct pci_dev *pci_dev,
	pm_message_t state)
{
	struct net_device *net_dev = pci_get_drvdata(pci_dev);
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)NULL;
	INT32 retval;


	DBGPRINT(RT_DEBUG_TRACE, ("===> rt2860_suspend()\n"));

	if (net_dev == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("net_dev == NULL!\n"));
	}
	else
	{
		pAd = net_dev->ml_priv;

		/* we can not use IFF_UP because ra0 down but ra1 up */
		/* and 1 suspend/resume function for 1 module, not for each interface */
		/* so Linux will call suspend/resume function once */
		if (VIRTUAL_IF_NUM(pAd) > 0)
		{
			// avoid users do suspend after interface is down

			// stop interface
			netif_carrier_off(net_dev);
			netif_stop_queue(net_dev);

			// mark device as removed from system and therefore no longer available
			netif_device_detach(net_dev);

			// mark halt flag
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);

			// take down the device
			rt28xx_close((PNET_DEV)net_dev);

			RT_MOD_DEC_USE_COUNT();
		}
	}

	// reference to http://vovo2000.com/type-lab/linux/kernel-api/linux-kernel-api.html
	// enable device to generate PME# when suspended
	// pci_choose_state(): Choose the power state of a PCI device to be suspended
	retval = pci_enable_wake(pci_dev, pci_choose_state(pci_dev, state), 1);
	// save the PCI configuration space of a device before suspending
	pci_save_state(pci_dev);
	// disable PCI device after use
	pci_disable_device(pci_dev);

	retval = pci_set_power_state(pci_dev, pci_choose_state(pci_dev, state));

	DBGPRINT(RT_DEBUG_TRACE, ("<=== rt2860_suspend()\n"));
	return retval;
}

static int rt2860_resume(
	struct pci_dev *pci_dev)
{
	struct net_device *net_dev = pci_get_drvdata(pci_dev);
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)NULL;
	INT32 retval;


	// set the power state of a PCI device
	// PCI has 4 power states, DO (normal) ~ D3(less power)
	// in include/linux/pci.h, you can find that
	// #define PCI_D0          ((pci_power_t __force) 0)
	// #define PCI_D1          ((pci_power_t __force) 1)
	// #define PCI_D2          ((pci_power_t __force) 2)
	// #define PCI_D3hot       ((pci_power_t __force) 3)
	// #define PCI_D3cold      ((pci_power_t __force) 4)
	// #define PCI_UNKNOWN     ((pci_power_t __force) 5)
	// #define PCI_POWER_ERROR ((pci_power_t __force) -1)
	retval = pci_set_power_state(pci_dev, PCI_D0);

	// restore the saved state of a PCI device
	pci_restore_state(pci_dev);

	// initialize device before it's used by a driver
	if (pci_enable_device(pci_dev))
	{
		printk("pci enable fail!\n");
		return 0;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("===> rt2860_resume()\n"));

	if (net_dev == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("net_dev == NULL!\n"));
	}
	else
		pAd = net_dev->ml_priv;

	if (pAd != NULL)
	{
		/* we can not use IFF_UP because ra0 down but ra1 up */
		/* and 1 suspend/resume function for 1 module, not for each interface */
		/* so Linux will call suspend/resume function once */
		if (VIRTUAL_IF_NUM(pAd) > 0)
		{
			// mark device as attached from system and restart if needed
			netif_device_attach(net_dev);

			if (rt28xx_open((PNET_DEV)net_dev) != 0)
			{
				// open fail
				DBGPRINT(RT_DEBUG_TRACE, ("<=== rt2860_resume()\n"));
				return 0;
			}

			// increase MODULE use count
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
#endif // CONFIG_PM //


static INT __init rt2860_init_module(VOID)
{
	return pci_register_driver(&rt2860_driver);
}


//
// Driver module unload function
//
static VOID __exit rt2860_cleanup_module(VOID)
{
    pci_unregister_driver(&rt2860_driver);
}

module_init(rt2860_init_module);
module_exit(rt2860_cleanup_module);


static INT __devinit rt2860_init_one (
    IN  struct pci_dev              *pci_dev,
    IN  const struct pci_device_id  *ent)
{
    INT rc;

    DBGPRINT(RT_DEBUG_TRACE, ("===> rt2860_init_one\n"));

    // wake up and enable device
    if (pci_enable_device (pci_dev))
    {
        rc = -EIO;
    }
    else
    {
        rc = rt2860_probe(pci_dev, ent);
    }

    DBGPRINT(RT_DEBUG_TRACE, ("<=== rt2860_init_one\n"));
    return rc;
}


static VOID __devexit rt2860_remove_one(
    IN  struct pci_dev  *pci_dev)
{
    struct net_device   *net_dev = pci_get_drvdata(pci_dev);
    RTMP_ADAPTER        *pAd = net_dev->ml_priv;

    DBGPRINT(RT_DEBUG_TRACE, ("===> rt2860_remove_one\n"));

	if (pAd != NULL)
	{
		// Unregister network device
		unregister_netdev(net_dev);

		// Unmap CSR base address
		iounmap((char *)(net_dev->base_addr));

		RTMPFreeAdapter(pAd);

		// release memory region
		release_mem_region(pci_resource_start(pci_dev, 0), pci_resource_len(pci_dev, 0));
	}
	else
	{
		// Unregister network device
		unregister_netdev(net_dev);

		// Unmap CSR base address
		iounmap((char *)(net_dev->base_addr));

		// release memory region
		release_mem_region(pci_resource_start(pci_dev, 0), pci_resource_len(pci_dev, 0));
	}

	// Free pre-allocated net_device memory
	free_netdev(net_dev);
}

//
// PCI device probe & initialization function
//
static INT __devinit   rt2860_probe(
    IN  struct pci_dev              *pci_dev,
    IN  const struct pci_device_id  *ent)
{
	PRTMP_ADAPTER pAd;
    INT rv = 0;

    rv = (INT)rt28xx_probe((void *)pci_dev, (void *)ent, 0, &pAd);
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_ADVANCE_POWER_SAVE_PCIE_DEVICE);
	return rv;
}


void init_thread_task(IN PRTMP_ADAPTER pAd)
{
	POS_COOKIE pObj;

	pObj = (POS_COOKIE) pAd->OS_Cookie;

	tasklet_init(&pObj->rx_done_task, rx_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->mgmt_dma_done_task, mgmt_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->ac0_dma_done_task, ac0_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->ac1_dma_done_task, ac1_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->ac2_dma_done_task, ac2_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->ac3_dma_done_task, ac3_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->hcca_dma_done_task, hcca_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->tbtt_task, tbtt_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->fifo_statistic_full_task, fifo_statistic_full_tasklet, (unsigned long)pAd);
}

void kill_thread_task(IN PRTMP_ADAPTER pAd)
{
	POS_COOKIE pObj;

	pObj = (POS_COOKIE) pAd->OS_Cookie;

	tasklet_kill(&pObj->rx_done_task);
	tasklet_kill(&pObj->mgmt_dma_done_task);
	tasklet_kill(&pObj->ac0_dma_done_task);
	tasklet_kill(&pObj->ac1_dma_done_task);
	tasklet_kill(&pObj->ac2_dma_done_task);
	tasklet_kill(&pObj->ac3_dma_done_task);
	tasklet_kill(&pObj->hcca_dma_done_task);
	tasklet_kill(&pObj->tbtt_task);
	tasklet_kill(&pObj->fifo_statistic_full_task);
}


static void rt2860_int_enable(PRTMP_ADAPTER pAd, unsigned int mode)
{
	u32 regValue;

	pAd->int_disable_mask &= ~(mode);
	regValue = pAd->int_enable_reg & ~(pAd->int_disable_mask);
	RTMP_IO_WRITE32(pAd, INT_MASK_CSR, regValue);     // 1:enable

	if (regValue != 0)
		RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_ACTIVE);
}


static void rt2860_int_disable(PRTMP_ADAPTER pAd, unsigned int mode)
{
	u32 regValue;

	pAd->int_disable_mask |= mode;
	regValue = 	pAd->int_enable_reg & ~(pAd->int_disable_mask);
	RTMP_IO_WRITE32(pAd, INT_MASK_CSR, regValue);     // 0: disable

	if (regValue == 0)
	{
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_ACTIVE);
	}
}

static void mgmt_dma_done_tasklet(unsigned long data)
{
	unsigned long flags;
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER) data;
    INT_SOURCE_CSR_STRUC	IntSource;
	POS_COOKIE pObj;

	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

    pObj = (POS_COOKIE) pAd->OS_Cookie;

	IntSource.word = 0;
	IntSource.field.MgmtDmaDone = 1;
	pAd->int_pending &= ~INT_MGMT_DLY;

	RTMPHandleMgmtRingDmaDoneInterrupt(pAd);

	// if you use RTMP_SEM_LOCK, sometimes kernel will hang up, no any
	// bug report output
	RTMP_INT_LOCK(&pAd->irq_lock, flags);
	/*
	 * double check to avoid lose of interrupts
	 */
	if (pAd->int_pending & INT_MGMT_DLY)
	{
		tasklet_hi_schedule(&pObj->mgmt_dma_done_task);
		RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
		return;
	}

	/* enable TxDataInt again */
	rt2860_int_enable(pAd, INT_MGMT_DLY);
	RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
}

static void rx_done_tasklet(unsigned long data)
{
	unsigned long flags;
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER) data;
	BOOLEAN	bReschedule = 0;
	POS_COOKIE pObj;

	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

    pObj = (POS_COOKIE) pAd->OS_Cookie;

	pAd->int_pending &= ~(INT_RX);
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		bReschedule = STARxDoneInterruptHandle(pAd, 0);
#endif // CONFIG_STA_SUPPORT //

	RTMP_INT_LOCK(&pAd->irq_lock, flags);
	/*
	 * double check to avoid rotting packet
	 */
	if (pAd->int_pending & INT_RX || bReschedule)
	{
		tasklet_hi_schedule(&pObj->rx_done_task);
		RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
		return;
	}

	/* enable RxINT again */
	rt2860_int_enable(pAd, INT_RX);
	RTMP_INT_UNLOCK(&pAd->irq_lock, flags);

}

void fifo_statistic_full_tasklet(unsigned long data)
{
	unsigned long flags;
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER) data;
	POS_COOKIE pObj;

	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

    pObj = (POS_COOKIE) pAd->OS_Cookie;

	pAd->int_pending &= ~(FifoStaFullInt);
	NICUpdateFifoStaCounters(pAd);

	RTMP_INT_LOCK(&pAd->irq_lock, flags);
	/*
	 * double check to avoid rotting packet
	 */
	if (pAd->int_pending & FifoStaFullInt)
	{
		tasklet_hi_schedule(&pObj->fifo_statistic_full_task);
		RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
		return;
	}

	/* enable RxINT again */

	rt2860_int_enable(pAd, FifoStaFullInt);
	RTMP_INT_UNLOCK(&pAd->irq_lock, flags);

}

static void hcca_dma_done_tasklet(unsigned long data)
{
	unsigned long flags;
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER) data;
    INT_SOURCE_CSR_STRUC	IntSource;
	POS_COOKIE pObj;

	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

    pObj = (POS_COOKIE) pAd->OS_Cookie;


	IntSource.word = 0;
	IntSource.field.HccaDmaDone = 1;
	pAd->int_pending &= ~INT_HCCA_DLY;

	RTMPHandleTxRingDmaDoneInterrupt(pAd, IntSource);

	RTMP_INT_LOCK(&pAd->irq_lock, flags);
	/*
	 * double check to avoid lose of interrupts
	 */
	if (pAd->int_pending & INT_HCCA_DLY)
	{
		tasklet_hi_schedule(&pObj->hcca_dma_done_task);
		RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
		return;
	}

	/* enable TxDataInt again */
	rt2860_int_enable(pAd, INT_HCCA_DLY);
	RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
}

static void ac3_dma_done_tasklet(unsigned long data)
{
	unsigned long flags;
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER) data;
    INT_SOURCE_CSR_STRUC	IntSource;
	POS_COOKIE pObj;
	BOOLEAN bReschedule = 0;

	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

    pObj = (POS_COOKIE) pAd->OS_Cookie;

	IntSource.word = 0;
	IntSource.field.Ac3DmaDone = 1;
	pAd->int_pending &= ~INT_AC3_DLY;

	bReschedule = RTMPHandleTxRingDmaDoneInterrupt(pAd, IntSource);

	RTMP_INT_LOCK(&pAd->irq_lock, flags);
	/*
	 * double check to avoid lose of interrupts
	 */
	if ((pAd->int_pending & INT_AC3_DLY) || bReschedule)
	{
		tasklet_hi_schedule(&pObj->ac3_dma_done_task);
		RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
		return;
	}

	/* enable TxDataInt again */
	rt2860_int_enable(pAd, INT_AC3_DLY);
	RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
}

static void ac2_dma_done_tasklet(unsigned long data)
{
	unsigned long flags;
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER) data;
    INT_SOURCE_CSR_STRUC	IntSource;
	POS_COOKIE pObj;
	BOOLEAN bReschedule = 0;

	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

    pObj = (POS_COOKIE) pAd->OS_Cookie;

	IntSource.word = 0;
	IntSource.field.Ac2DmaDone = 1;
	pAd->int_pending &= ~INT_AC2_DLY;

	bReschedule = RTMPHandleTxRingDmaDoneInterrupt(pAd, IntSource);

	RTMP_INT_LOCK(&pAd->irq_lock, flags);

	/*
	 * double check to avoid lose of interrupts
	 */
	if ((pAd->int_pending & INT_AC2_DLY) || bReschedule)
	{
		tasklet_hi_schedule(&pObj->ac2_dma_done_task);
		RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
		return;
	}

	/* enable TxDataInt again */
	rt2860_int_enable(pAd, INT_AC2_DLY);
	RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
}

static void ac1_dma_done_tasklet(unsigned long data)
{
	unsigned long flags;
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER) data;
    INT_SOURCE_CSR_STRUC	IntSource;
	POS_COOKIE pObj;
	BOOLEAN bReschedule = 0;

	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

    pObj = (POS_COOKIE) pAd->OS_Cookie;

	IntSource.word = 0;
	IntSource.field.Ac1DmaDone = 1;
	pAd->int_pending &= ~INT_AC1_DLY;

	bReschedule = RTMPHandleTxRingDmaDoneInterrupt(pAd, IntSource);

	RTMP_INT_LOCK(&pAd->irq_lock, flags);
	/*
	 * double check to avoid lose of interrupts
	 */
	if ((pAd->int_pending & INT_AC1_DLY) || bReschedule)
	{
		tasklet_hi_schedule(&pObj->ac1_dma_done_task);
		RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
		return;
	}

	/* enable TxDataInt again */
	rt2860_int_enable(pAd, INT_AC1_DLY);
	RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
}

static void ac0_dma_done_tasklet(unsigned long data)
{
	unsigned long flags;
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER) data;
    INT_SOURCE_CSR_STRUC	IntSource;
	POS_COOKIE pObj;
	BOOLEAN bReschedule = 0;

	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return;

    pObj = (POS_COOKIE) pAd->OS_Cookie;

	IntSource.word = 0;
	IntSource.field.Ac0DmaDone = 1;
	pAd->int_pending &= ~INT_AC0_DLY;

	bReschedule = RTMPHandleTxRingDmaDoneInterrupt(pAd, IntSource);

	RTMP_INT_LOCK(&pAd->irq_lock, flags);
	/*
	 * double check to avoid lose of interrupts
	 */
	if ((pAd->int_pending & INT_AC0_DLY) || bReschedule)
	{
		tasklet_hi_schedule(&pObj->ac0_dma_done_task);
		RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
		return;
	}

	/* enable TxDataInt again */
	rt2860_int_enable(pAd, INT_AC0_DLY);
	RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
}


int print_int_count;

IRQ_HANDLE_TYPE
rt2860_interrupt(int irq, void *dev_instance)
{
	struct net_device *net_dev = (struct net_device *) dev_instance;
	PRTMP_ADAPTER pAd = net_dev->ml_priv;
	INT_SOURCE_CSR_STRUC	IntSource;
	POS_COOKIE pObj;
	BOOLEAN	bOldValue;

	pObj = (POS_COOKIE) pAd->OS_Cookie;


	/* Note 03312008: we can not return here before
		RTMP_IO_READ32(pAd, INT_SOURCE_CSR, &IntSource.word);
		RTMP_IO_WRITE32(pAd, INT_SOURCE_CSR, IntSource.word);
		Or kernel will panic after ifconfig ra0 down sometimes */


	//
	// Inital the Interrupt source.
	//
	IntSource.word = 0x00000000L;
//	McuIntSource.word = 0x00000000L;

	//
	// Get the interrupt sources & saved to local variable
	//
	//RTMP_IO_READ32(pAd, where, &McuIntSource.word);
	//RTMP_IO_WRITE32(pAd, , McuIntSource.word);

	//
	// Flag fOP_STATUS_DOZE On, means ASIC put to sleep, elase means ASICK WakeUp
	// And at the same time, clock maybe turned off that say there is no DMA service.
	// when ASIC get to sleep.
	// To prevent system hang on power saving.
	// We need to check it before handle the INT_SOURCE_CSR, ASIC must be wake up.
	//
	// RT2661 => when ASIC is sleeping, MAC register cannot be read and written.
	// RT2860 => when ASIC is sleeping, MAC register can be read and written.

	bOldValue = pAd->bPCIclkOff;
	pAd->bPCIclkOff = FALSE;
	{
		RTMP_IO_READ32(pAd, INT_SOURCE_CSR, &IntSource.word);
		RTMP_IO_WRITE32(pAd, INT_SOURCE_CSR, IntSource.word); // write 1 to clear
	}
	pAd->bPCIclkOff = bOldValue;

	// Do nothing if Reset in progress
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS) ||
		RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))
	{
		return IRQ_HANDLED;
	}

	//
	// Handle interrupt, walk through all bits
	// Should start from highest priority interrupt
	// The priority can be adjust by altering processing if statement
	//

	// If required spinlock, each interrupt service routine has to acquire
	// and release itself.
	//

	// Do nothing if NIC doesn't exist
	if (IntSource.word == 0xffffffff)
	{
		RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST | fRTMP_ADAPTER_HALT_IN_PROGRESS);
		printk("snowpin - IntSource.word == 0xffffffff\n");
		return IRQ_HANDLED;
	}

	if (IntSource.word & TxCoherent)
	{
		DBGPRINT(RT_DEBUG_ERROR, (">>>TxCoherent<<<\n"));
		RTMPHandleRxCoherentInterrupt(pAd);
	}

	if (IntSource.word & RxCoherent)
	{
		DBGPRINT(RT_DEBUG_ERROR, (">>>RxCoherent<<<\n"));
		RTMPHandleRxCoherentInterrupt(pAd);
	}

	if (IntSource.word & FifoStaFullInt)
	{
#if 1
		if ((pAd->int_disable_mask & FifoStaFullInt) == 0)
		{
			/* mask FifoStaFullInt */
			rt2860_int_disable(pAd, FifoStaFullInt);
			tasklet_hi_schedule(&pObj->fifo_statistic_full_task);
		}
		pAd->int_pending |= FifoStaFullInt;
#else
		NICUpdateFifoStaCounters(pAd);
#endif
	}

	if (IntSource.word & INT_MGMT_DLY)
	{
		if ((pAd->int_disable_mask & INT_MGMT_DLY) ==0 )
		{
			rt2860_int_disable(pAd, INT_MGMT_DLY);
			tasklet_hi_schedule(&pObj->mgmt_dma_done_task);
		}
		pAd->int_pending |= INT_MGMT_DLY ;
	}

	if (IntSource.word & INT_RX)
	{
		if ((pAd->int_disable_mask & INT_RX) == 0)
		{
			/* mask RxINT */
			rt2860_int_disable(pAd, INT_RX);
			tasklet_hi_schedule(&pObj->rx_done_task);
		}
		pAd->int_pending |= INT_RX;
	}

	if (IntSource.word & INT_HCCA_DLY)
	{

		if ((pAd->int_disable_mask & INT_HCCA_DLY) == 0)
		{
			/* mask TxDataInt */
			rt2860_int_disable(pAd, INT_HCCA_DLY);
			tasklet_hi_schedule(&pObj->hcca_dma_done_task);
		}
		pAd->int_pending |= INT_HCCA_DLY;
	}

	if (IntSource.word & INT_AC3_DLY)
	{

		if ((pAd->int_disable_mask & INT_AC3_DLY) == 0)
		{
			/* mask TxDataInt */
			rt2860_int_disable(pAd, INT_AC3_DLY);
			tasklet_hi_schedule(&pObj->ac3_dma_done_task);
		}
		pAd->int_pending |= INT_AC3_DLY;
	}

	if (IntSource.word & INT_AC2_DLY)
	{

		if ((pAd->int_disable_mask & INT_AC2_DLY) == 0)
		{
			/* mask TxDataInt */
			rt2860_int_disable(pAd, INT_AC2_DLY);
			tasklet_hi_schedule(&pObj->ac2_dma_done_task);
		}
		pAd->int_pending |= INT_AC2_DLY;
	}

	if (IntSource.word & INT_AC1_DLY)
	{

		pAd->int_pending |= INT_AC1_DLY;

		if ((pAd->int_disable_mask & INT_AC1_DLY) == 0)
		{
			/* mask TxDataInt */
			rt2860_int_disable(pAd, INT_AC1_DLY);
			tasklet_hi_schedule(&pObj->ac1_dma_done_task);
		}

	}

	if (IntSource.word & INT_AC0_DLY)
	{
		pAd->int_pending |= INT_AC0_DLY;

		if ((pAd->int_disable_mask & INT_AC0_DLY) == 0)
		{
			/* mask TxDataInt */
			rt2860_int_disable(pAd, INT_AC0_DLY);
			tasklet_hi_schedule(&pObj->ac0_dma_done_task);
		}

	}

    if (IntSource.word & PreTBTTInt)
	{
		RTMPHandlePreTBTTInterrupt(pAd);
	}

	if (IntSource.word & TBTTInt)
	{
		RTMPHandleTBTTInterrupt(pAd);
	}



#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		if (IntSource.word & AutoWakeupInt)
			RTMPHandleTwakeupInterrupt(pAd);
	}
#endif // CONFIG_STA_SUPPORT //

    return  IRQ_HANDLED;
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
BOOLEAN RT28XXChipsetCheck(
	IN void *_dev_p)
{
	/* always TRUE */
	return TRUE;
}


/*
========================================================================
Routine Description:
    Init net device structure.

Arguments:
    _dev_p				Point to the PCI or USB device
    *net_dev			Point to the net device
	*pAd				the raxx interface data pointer

Return Value:
    TRUE				Init ok
	FALSE				Init fail

Note:
========================================================================
*/
BOOLEAN RT28XXNetDevInit(
	IN void 				*_dev_p,
	IN struct  net_device	*net_dev,
	IN RTMP_ADAPTER 		*pAd)
{
	struct pci_dev *pci_dev = (struct pci_dev *)_dev_p;
    const CHAR	*print_name;
    ULONG	csr_addr;


	print_name = pci_dev ? pci_name(pci_dev) : "rt2860";

	net_dev->base_addr = 0;
	net_dev->irq = 0;

    if (pci_request_regions(pci_dev, print_name))
        goto err_out_free_netdev;

    // interrupt IRQ number
    net_dev->irq = pci_dev->irq;

    // map physical address to virtual address for accessing register
    csr_addr = (unsigned long) ioremap(pci_resource_start(pci_dev, 0),
										pci_resource_len(pci_dev, 0));

    if (!csr_addr)
    {
        DBGPRINT(RT_DEBUG_ERROR,
				("ioremap failed for device %s, region 0x%lX @ 0x%lX\n",
				print_name, (ULONG)pci_resource_len(pci_dev, 0),
				(ULONG)pci_resource_start(pci_dev, 0)));
        goto err_out_free_res;
    }

    // Save CSR virtual address and irq to device structure
    net_dev->base_addr = csr_addr;
    pAd->CSRBaseAddress = (PUCHAR)net_dev->base_addr;

    // Set DMA master
    pci_set_master(pci_dev);

    net_dev->priv_flags = INT_MAIN;

    DBGPRINT(RT_DEBUG_TRACE, ("%s: at 0x%lx, VA 0x%lx, IRQ %d. \n",
        	net_dev->name, (ULONG)pci_resource_start(pci_dev, 0),
			(ULONG)csr_addr, pci_dev->irq));
	return TRUE;


	/* --------------------------- ERROR HANDLE --------------------------- */
err_out_free_res:
    pci_release_regions(pci_dev);
err_out_free_netdev:
	/* free netdev in caller, not here */
	return FALSE;
}


/*
========================================================================
Routine Description:
    Init net device structure.

Arguments:
    _dev_p				Point to the PCI or USB device
	*pAd				the raxx interface data pointer

Return Value:
    TRUE				Config ok
	FALSE				Config fail

Note:
========================================================================
*/
BOOLEAN RT28XXProbePostConfig(
	IN void 				*_dev_p,
	IN RTMP_ADAPTER 		*pAd,
	IN INT32				argc)
{
	/* no use */
	return TRUE;
}


/*
========================================================================
Routine Description:
    Disable DMA.

Arguments:
	*pAd				the raxx interface data pointer

Return Value:
	None

Note:
========================================================================
*/
VOID RT28XXDMADisable(
	IN RTMP_ADAPTER 		*pAd)
{
	WPDMA_GLO_CFG_STRUC     GloCfg;


	RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);
	GloCfg.word &= 0xff0;
	GloCfg.field.EnTXWriteBackDDONE =1;
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);
}


/*
========================================================================
Routine Description:
    Enable DMA.

Arguments:
	*pAd				the raxx interface data pointer

Return Value:
	None

Note:
========================================================================
*/
VOID RT28XXDMAEnable(
	IN RTMP_ADAPTER 		*pAd)
{
	WPDMA_GLO_CFG_STRUC	GloCfg;
	int i = 0;

	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x4);
	do
	{
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);
		if ((GloCfg.field.TxDMABusy == 0)  && (GloCfg.field.RxDMABusy == 0))
			break;

		DBGPRINT(RT_DEBUG_TRACE, ("==>  DMABusy\n"));
		RTMPusecDelay(1000);
		i++;
	}while ( i <200);

	RTMPusecDelay(50);

	GloCfg.field.EnTXWriteBackDDONE = 1;
	GloCfg.field.WPDMABurstSIZE = 2;
	GloCfg.field.EnableRxDMA = 1;
	GloCfg.field.EnableTxDMA = 1;

	DBGPRINT(RT_DEBUG_TRACE, ("<== WRITE DMA offset 0x208 = 0x%x\n", GloCfg.word));
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);

}

/*
========================================================================
Routine Description:
    Write Beacon buffer to Asic.

Arguments:
	*pAd				the raxx interface data pointer

Return Value:
	None

Note:
========================================================================
*/
VOID RT28xx_UpdateBeaconToAsic(
	IN RTMP_ADAPTER		*pAd,
	IN INT				apidx,
	IN ULONG			FrameLen,
	IN ULONG			UpdatePos)
{
	ULONG				CapInfoPos = 0;
	UCHAR  			*ptr, *ptr_update, *ptr_capinfo;
	UINT  			i;
	BOOLEAN			bBcnReq = FALSE;
	UCHAR			bcn_idx = 0;

	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s() : No valid Interface be found.\n", __func__));
		return;
	}

	if (bBcnReq == FALSE)
	{
		/* when the ra interface is down, do not send its beacon frame */
		/* clear all zero */
		for(i=0; i<TXWI_SIZE; i+=4)
			RTMP_IO_WRITE32(pAd, pAd->BeaconOffset[bcn_idx] + i, 0x00);
	}
	else
	{
		ptr = (PUCHAR)&pAd->BeaconTxWI;

		for (i=0; i<TXWI_SIZE; i+=4)  // 16-byte TXWI field
		{
			UINT32 longptr =  *ptr + (*(ptr+1)<<8) + (*(ptr+2)<<16) + (*(ptr+3)<<24);
			RTMP_IO_WRITE32(pAd, pAd->BeaconOffset[bcn_idx] + i, longptr);
			ptr += 4;
		}

		// Update CapabilityInfo in Beacon
		for (i = CapInfoPos; i < (CapInfoPos+2); i++)
		{
			RTMP_IO_WRITE8(pAd, pAd->BeaconOffset[bcn_idx] + TXWI_SIZE + i, *ptr_capinfo);
			ptr_capinfo ++;
		}

		if (FrameLen > UpdatePos)
		{
			for (i= UpdatePos; i< (FrameLen); i++)
			{
				RTMP_IO_WRITE8(pAd, pAd->BeaconOffset[bcn_idx] + TXWI_SIZE + i, *ptr_update);
				ptr_update ++;
			}
		}

	}

}

#ifdef CONFIG_STA_SUPPORT
VOID RTMPInitPCIeLinkCtrlValue(
	IN	PRTMP_ADAPTER	pAd)
{
}

VOID RTMPFindHostPCIDev(
    IN	PRTMP_ADAPTER	pAd)
{
}

/*
	========================================================================

	Routine Description:

	Arguments:
		Level = RESTORE_HALT : Restore PCI host and Ralink PCIe Link Control field to its default value.
		Level = Other Value : Restore from dot11 power save or radio off status. And force PCI host Link Control fields to 0x1

	========================================================================
*/
VOID RTMPPCIeLinkCtrlValueRestore(
	IN	PRTMP_ADAPTER	pAd,
	IN   UCHAR		Level)
{
}

/*
	========================================================================

	Routine Description:

	Arguments:
		Max : limit Host PCI and Ralink PCIe device's LINK CONTROL field's value.
		Because now frequently set our device to mode 1 or mode 3 will cause problem.

	========================================================================
*/
VOID RTMPPCIeLinkCtrlSetting(
	IN	PRTMP_ADAPTER	pAd,
	IN 	USHORT		Max)
{
}
#endif // CONFIG_STA_SUPPORT //

VOID rt2860_stop(struct net_device *net_dev)
{
    PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)NULL;
    if (net_dev == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("net_dev == NULL!\n"));
	}
	else
		pAd = net_dev->ml_priv;

	if (pAd != NULL)
	{
	    // stop interface
		netif_carrier_off(net_dev);
		netif_stop_queue(net_dev);

		// mark device as removed from system and therefore no longer available
		netif_device_detach(net_dev);

		// mark halt flag
		RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);
		RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);

		// take down the device
		rt28xx_close((PNET_DEV)net_dev);
		RT_MOD_DEC_USE_COUNT();
	}
    return;
}

/*
 * invaild or writeback cache
 * and convert virtual address to physical address
 */
dma_addr_t linux_pci_map_single(void *handle, void *ptr, size_t size, int sd_idx, int direction)
{
	PRTMP_ADAPTER pAd;
	POS_COOKIE pObj;

	/*
		------ Porting Information ------
		> For Tx Alloc:
			mgmt packets => sd_idx = 0
			SwIdx: pAd->MgmtRing.TxCpuIdx
			pTxD : pAd->MgmtRing.Cell[SwIdx].AllocVa;

			data packets => sd_idx = 1
	 		TxIdx : pAd->TxRing[pTxBlk->QueIdx].TxCpuIdx
	 		QueIdx: pTxBlk->QueIdx
	 		pTxD  : pAd->TxRing[pTxBlk->QueIdx].Cell[TxIdx].AllocVa;

	 	> For Rx Alloc:
	 		sd_idx = -1
	*/

	pAd = (PRTMP_ADAPTER)handle;
	pObj = (POS_COOKIE)pAd->OS_Cookie;

	if (sd_idx == 1)
	{
		PTX_BLK		pTxBlk;
		pTxBlk = (PTX_BLK)ptr;
		return pci_map_single(pObj->pci_dev, pTxBlk->pSrcBufData, pTxBlk->SrcBufLen, direction);
	}
	else
	{
		return pci_map_single(pObj->pci_dev, ptr, size, direction);
	}

}

void linux_pci_unmap_single(void *handle, dma_addr_t dma_addr, size_t size, int direction)
{
	PRTMP_ADAPTER pAd;
	POS_COOKIE pObj;

	pAd=(PRTMP_ADAPTER)handle;
	pObj = (POS_COOKIE)pAd->OS_Cookie;

	pci_unmap_single(pObj->pci_dev, dma_addr, size, direction);

}

