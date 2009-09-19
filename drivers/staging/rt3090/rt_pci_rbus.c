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
    rt_pci_rbus.c

    Abstract:
    Create and register network interface.

    Revision History:
    Who         When            What
    --------    ----------      ----------------------------------------------
*/

#include "rt_config.h"
#include <linux/pci.h>


IRQ_HANDLE_TYPE
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19))
rt2860_interrupt(int irq, void *dev_instance);
#else
rt2860_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
#endif


static void rx_done_tasklet(unsigned long data);
static void mgmt_dma_done_tasklet(unsigned long data);
static void ac0_dma_done_tasklet(unsigned long data);
static void ac1_dma_done_tasklet(unsigned long data);
static void ac2_dma_done_tasklet(unsigned long data);
static void ac3_dma_done_tasklet(unsigned long data);
/*static void hcca_dma_done_tasklet(unsigned long data);*/
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
#ifdef TONE_RADAR_DETECT_SUPPORT
#define RT2860_INT_TONE_RADAR			(1<<20)		// bit 20
#endif // TONE_RADAR_DETECT_SUPPORT //

#define INT_RX			RT2860_INT_RX_DONE

#define INT_AC0_DLY		(RT2860_INT_AC0_DMA_DONE) //| RT2860_INT_TX_DLY)
#define INT_AC1_DLY		(RT2860_INT_AC1_DMA_DONE) //| RT2860_INT_TX_DLY)
#define INT_AC2_DLY		(RT2860_INT_AC2_DMA_DONE) //| RT2860_INT_TX_DLY)
#define INT_AC3_DLY		(RT2860_INT_AC3_DMA_DONE) //| RT2860_INT_TX_DLY)
#define INT_HCCA_DLY	(RT2860_INT_HCCA_DMA_DONE) //| RT2860_INT_TX_DLY)
#define INT_MGMT_DLY	RT2860_INT_MGMT_DONE
#ifdef TONE_RADAR_DETECT_SUPPORT
#define INT_TONE_RADAR	(RT2860_INT_TONE_RADAR)
#endif // TONE_RADAR_DETECT_SUPPORT //


/***************************************************************************
  *
  *	Interface-depended memory allocation/Free related procedures.
  *		Mainly for Hardware TxDesc/RxDesc/MgmtDesc, DMA Memory for TxData/RxData, etc.,
  *
  **************************************************************************/
// Function for TxDesc Memory allocation.
void RTMP_AllocateTxDescMemory(
	IN	PRTMP_ADAPTER pAd,
	IN	UINT	Index,
	IN	ULONG	Length,
	IN	BOOLEAN	Cached,
	OUT	PVOID	*VirtualAddress,
	OUT	PNDIS_PHYSICAL_ADDRESS PhysicalAddress)
{
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;

	*VirtualAddress = (PVOID)pci_alloc_consistent(pObj->pci_dev,sizeof(char)*Length, PhysicalAddress);

}


// Function for MgmtDesc Memory allocation.
void RTMP_AllocateMgmtDescMemory(
	IN	PRTMP_ADAPTER pAd,
	IN	ULONG	Length,
	IN	BOOLEAN	Cached,
	OUT	PVOID	*VirtualAddress,
	OUT	PNDIS_PHYSICAL_ADDRESS PhysicalAddress)
{
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;

	*VirtualAddress = (PVOID)pci_alloc_consistent(pObj->pci_dev,sizeof(char)*Length, PhysicalAddress);

}


// Function for RxDesc Memory allocation.
void RTMP_AllocateRxDescMemory(
	IN	PRTMP_ADAPTER pAd,
	IN	ULONG	Length,
	IN	BOOLEAN	Cached,
	OUT	PVOID	*VirtualAddress,
	OUT	PNDIS_PHYSICAL_ADDRESS PhysicalAddress)
{
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;

	*VirtualAddress = (PVOID)pci_alloc_consistent(pObj->pci_dev,sizeof(char)*Length, PhysicalAddress);

}


// Function for free allocated Desc Memory.
void RTMP_FreeDescMemory(
	IN	PRTMP_ADAPTER pAd,
	IN	ULONG	Length,
	IN	PVOID	VirtualAddress,
	IN	NDIS_PHYSICAL_ADDRESS PhysicalAddress)
{
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;

	pci_free_consistent(pObj->pci_dev, Length, VirtualAddress, PhysicalAddress);
}


// Function for TxData DMA Memory allocation.
void RTMP_AllocateFirstTxBuffer(
	IN	PRTMP_ADAPTER pAd,
	IN	UINT	Index,
	IN	ULONG	Length,
	IN	BOOLEAN	Cached,
	OUT	PVOID	*VirtualAddress,
	OUT	PNDIS_PHYSICAL_ADDRESS PhysicalAddress)
{
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;

	*VirtualAddress = (PVOID)pci_alloc_consistent(pObj->pci_dev,sizeof(char)*Length, PhysicalAddress);
}


void RTMP_FreeFirstTxBuffer(
	IN	PRTMP_ADAPTER pAd,
	IN	ULONG	Length,
	IN	BOOLEAN	Cached,
	IN	PVOID	VirtualAddress,
	IN	NDIS_PHYSICAL_ADDRESS PhysicalAddress)
{
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;

	pci_free_consistent(pObj->pci_dev, Length, VirtualAddress, PhysicalAddress);
}


/*
 * FUNCTION: Allocate a common buffer for DMA
 * ARGUMENTS:
 *     AdapterHandle:  AdapterHandle
 *     Length:  Number of bytes to allocate
 *     Cached:  Whether or not the memory can be cached
 *     VirtualAddress:  Pointer to memory is returned here
 *     PhysicalAddress:  Physical address corresponding to virtual address
 */
void RTMP_AllocateSharedMemory(
	IN	PRTMP_ADAPTER pAd,
	IN	ULONG	Length,
	IN	BOOLEAN	Cached,
	OUT	PVOID	*VirtualAddress,
	OUT	PNDIS_PHYSICAL_ADDRESS PhysicalAddress)
{
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;

	*VirtualAddress = (PVOID)pci_alloc_consistent(pObj->pci_dev,sizeof(char)*Length, PhysicalAddress);
}


/*
 * FUNCTION: Allocate a packet buffer for DMA
 * ARGUMENTS:
 *     AdapterHandle:  AdapterHandle
 *     Length:  Number of bytes to allocate
 *     Cached:  Whether or not the memory can be cached
 *     VirtualAddress:  Pointer to memory is returned here
 *     PhysicalAddress:  Physical address corresponding to virtual address
 * Notes:
 *     Cached is ignored: always cached memory
 */
PNDIS_PACKET RTMP_AllocateRxPacketBuffer(
	IN	PRTMP_ADAPTER pAd,
	IN	ULONG	Length,
	IN	BOOLEAN	Cached,
	OUT	PVOID	*VirtualAddress,
	OUT	PNDIS_PHYSICAL_ADDRESS PhysicalAddress)
{
	struct sk_buff *pkt;

	pkt = dev_alloc_skb(Length);

	if (pkt == NULL) {
		DBGPRINT(RT_DEBUG_ERROR, ("can't allocate rx %ld size packet\n",Length));
	}

	if (pkt) {
		RTMP_SET_PACKET_SOURCE(OSPKT_TO_RTPKT(pkt), PKTSRC_NDIS);
		*VirtualAddress = (PVOID) pkt->data;
//#ifdef CONFIG_5VT_ENHANCE
//		*PhysicalAddress = PCI_MAP_SINGLE(pAd, *VirtualAddress, 1600, PCI_DMA_FROMDEVICE);
//#else
		*PhysicalAddress = PCI_MAP_SINGLE(pAd, *VirtualAddress, Length,  -1, PCI_DMA_FROMDEVICE);
//#endif
	} else {
		*VirtualAddress = (PVOID) NULL;
		*PhysicalAddress = (NDIS_PHYSICAL_ADDRESS) NULL;
	}

	return (PNDIS_PACKET) pkt;
}


VOID Invalid_Remaining_Packet(
	IN	PRTMP_ADAPTER pAd,
	IN	 ULONG VirtualAddress)
{
	NDIS_PHYSICAL_ADDRESS PhysicalAddress;

	PhysicalAddress = PCI_MAP_SINGLE(pAd, (void *)(VirtualAddress+1600), RX_BUFFER_NORMSIZE-1600, -1, PCI_DMA_FROMDEVICE);
}


int RtmpOSIRQRequest(IN struct net_device *net_dev)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)(RTMP_OS_NETDEV_GET_PRIV(net_dev));
	int retval = 0;

	ASSERT(pAd);

	if (pAd->infType != RTMP_DEV_INF_RBUS)
	{
		POS_COOKIE _pObj = (POS_COOKIE)(pAd->OS_Cookie);
		RTMP_MSI_ENABLE(pAd);
		retval = request_irq(_pObj->pci_dev->irq,  rt2860_interrupt, SA_SHIRQ, (net_dev)->name, (net_dev));
		if (retval != 0)
			printk("RT2860: request_irq  ERROR(%d)\n", retval);
	}
	else
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
		if ((retval = request_irq(net_dev->irq, rt2860_interrupt, IRQF_SHARED, net_dev->name ,net_dev)))
#else
		if ((retval = request_irq(net_dev->irq,rt2860_interrupt, SA_INTERRUPT, net_dev->name ,net_dev)))
#endif
		{
			printk("RT2860: request_irq  ERROR(%d)\n", retval);
		}
	}

	return retval;

}


int RtmpOSIRQRelease(IN struct net_device *net_dev)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)(RTMP_OS_NETDEV_GET_PRIV(net_dev));

	ASSERT(pAd);
	if (pAd->infType != RTMP_DEV_INF_RBUS)
	{
		POS_COOKIE pObj = (POS_COOKIE)(pAd->OS_Cookie);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
		synchronize_irq(pObj->pci_dev->irq);
#endif
		free_irq(pObj->pci_dev->irq, (net_dev));
		RTMP_MSI_DISABLE(pAd);
	}
	else
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
		synchronize_irq(net_dev->irq);
#endif
		free_irq(net_dev->irq, (net_dev));
	}

	return 0;
}


NDIS_STATUS RtmpNetTaskInit(IN RTMP_ADAPTER *pAd)
{
	POS_COOKIE pObj;

	pObj = (POS_COOKIE) pAd->OS_Cookie;

	tasklet_init(&pObj->rx_done_task, rx_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->mgmt_dma_done_task, mgmt_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->ac0_dma_done_task, ac0_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->ac1_dma_done_task, ac1_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->ac2_dma_done_task, ac2_dma_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->ac3_dma_done_task, ac3_dma_done_tasklet, (unsigned long)pAd);
	/*tasklet_init(&pObj->hcca_dma_done_task, hcca_dma_done_tasklet, (unsigned long)pAd);*/
	tasklet_init(&pObj->tbtt_task, tbtt_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->fifo_statistic_full_task, fifo_statistic_full_tasklet, (unsigned long)pAd);

	return NDIS_STATUS_SUCCESS;
}


void RtmpNetTaskExit(IN RTMP_ADAPTER *pAd)
{
	POS_COOKIE pObj;

	pObj = (POS_COOKIE) pAd->OS_Cookie;

	tasklet_kill(&pObj->rx_done_task);
	tasklet_kill(&pObj->mgmt_dma_done_task);
	tasklet_kill(&pObj->ac0_dma_done_task);
	tasklet_kill(&pObj->ac1_dma_done_task);
	tasklet_kill(&pObj->ac2_dma_done_task);
	tasklet_kill(&pObj->ac3_dma_done_task);
	/*tasklet_kill(&pObj->hcca_dma_done_task);*/
	tasklet_kill(&pObj->tbtt_task);
	tasklet_kill(&pObj->fifo_statistic_full_task);
}


NDIS_STATUS RtmpMgmtTaskInit(IN RTMP_ADAPTER *pAd)
{


	return NDIS_STATUS_SUCCESS;
}


/*
========================================================================
Routine Description:
    Close kernel threads.

Arguments:
	*pAd				the raxx interface data pointer

Return Value:
    NONE

Note:
========================================================================
*/
VOID RtmpMgmtTaskExit(
	IN RTMP_ADAPTER *pAd)
{


	return;
}


static inline void rt2860_int_enable(PRTMP_ADAPTER pAd, unsigned int mode)
{
	u32 regValue;

	pAd->int_disable_mask &= ~(mode);
	regValue = pAd->int_enable_reg & ~(pAd->int_disable_mask);
	//if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
	{
		RTMP_IO_WRITE32(pAd, INT_MASK_CSR, regValue);     // 1:enable
	}
	//else
	//	DBGPRINT(RT_DEBUG_TRACE, ("fOP_STATUS_DOZE !\n"));

	if (regValue != 0)
		RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_ACTIVE);
}


static inline void rt2860_int_disable(PRTMP_ADAPTER pAd, unsigned int mode)
{
	u32 regValue;

	pAd->int_disable_mask |= mode;
	regValue =	pAd->int_enable_reg & ~(pAd->int_disable_mask);
	RTMP_IO_WRITE32(pAd, INT_MASK_CSR, regValue);     // 0: disable

	if (regValue == 0)
	{
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_ACTIVE);
	}
}


/***************************************************************************
  *
  *	tasklet related procedures.
  *
  **************************************************************************/
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

//	printk("mgmt_dma_done_process\n");
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

#ifdef UAPSD_AP_SUPPORT
	UAPSD_TIMING_RECORD(pAd, UAPSD_TIMING_RECORD_TASKLET);
#endif // UAPSD_AP_SUPPORT //

    pObj = (POS_COOKIE) pAd->OS_Cookie;

	pAd->int_pending &= ~(INT_RX);
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		bReschedule = STARxDoneInterruptHandle(pAd, 0);
#endif // CONFIG_STA_SUPPORT //

#ifdef UAPSD_AP_SUPPORT
	UAPSD_TIMING_RECORD_STOP();
#endif // UAPSD_AP_SUPPORT //

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

//	printk("ac0_dma_done_process\n");
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

//	printk("ac0_dma_done_process\n");
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

//	printk("ac0_dma_done_process\n");
	IntSource.word = 0;
	IntSource.field.Ac0DmaDone = 1;
	pAd->int_pending &= ~INT_AC0_DLY;

//	RTMPHandleMgmtRingDmaDoneInterrupt(pAd);
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




/***************************************************************************
  *
  *	interrupt handler related procedures.
  *
  **************************************************************************/
int print_int_count;

IRQ_HANDLE_TYPE
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19))
rt2860_interrupt(int irq, void *dev_instance)
#else
rt2860_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
#endif
{
	struct net_device *net_dev = (struct net_device *) dev_instance;
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER) RTMP_OS_NETDEV_GET_PRIV(net_dev);
	INT_SOURCE_CSR_STRUC	IntSource;
	POS_COOKIE pObj;

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
//	if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
	{
		RTMP_IO_READ32(pAd, INT_SOURCE_CSR, &IntSource.word);
		RTMP_IO_WRITE32(pAd, INT_SOURCE_CSR, IntSource.word); // write 1 to clear
	}
//	else
//		DBGPRINT(RT_DEBUG_TRACE, (">>>fOP_STATUS_DOZE<<<\n"));

//	RTMP_IO_READ32(pAd, INT_SOURCE_CSR, &IsrAfterClear);
//	RTMP_IO_READ32(pAd, MCU_INT_SOURCE_CSR, &McuIsrAfterClear);
//	DBGPRINT(RT_DEBUG_INFO, ("====> RTMPHandleInterrupt(ISR=%08x,Mcu ISR=%08x, After clear ISR=%08x, MCU ISR=%08x)\n",
//			IntSource.word, McuIntSource.word, IsrAfterClear, McuIsrAfterClear));

	// Do nothing if Reset in progress
	if (RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |fRTMP_ADAPTER_HALT_IN_PROGRESS)))
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
        return  IRQ_HANDLED;
#else
        return;
#endif
	}

	//
	// Handle interrupt, walk through all bits
	// Should start from highest priority interrupt
	// The priority can be adjust by altering processing if statement
	//

#ifdef DBG

#endif


	pAd->bPCIclkOff = FALSE;

	// If required spinlock, each interrupt service routine has to acquire
	// and release itself.
	//

	// Do nothing if NIC doesn't exist
	if (IntSource.word == 0xffffffff)
	{
		RTMP_SET_FLAG(pAd, (fRTMP_ADAPTER_NIC_NOT_EXIST | fRTMP_ADAPTER_HALT_IN_PROGRESS));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
        return  IRQ_HANDLED;
#else
        return;
#endif
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
		if ((pAd->int_disable_mask & FifoStaFullInt) == 0)
		{
			/* mask FifoStaFullInt */
			rt2860_int_disable(pAd, FifoStaFullInt);
			tasklet_hi_schedule(&pObj->fifo_statistic_full_task);
		}
		pAd->int_pending |= FifoStaFullInt;
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

/*
		if (IntSource.word & 0x2) {
			u32 reg;
			RTMP_IO_READ32(pAd, DELAY_INT_CFG, &reg);
			printk("IntSource.word = %08x, DELAY_REG = %08x\n", IntSource.word, reg);
		}
*/
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	return  IRQ_HANDLED;
#endif

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
