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
 */

#ifndef __RT2860_H__
#define __RT2860_H__

#define RT28xx_CHIP_NAME	"RT2860"

#define TXINFO_SIZE               0
#define TXPADDING_SIZE      	  0

/* ----------------- EEPROM Related MACRO ----------------- */
#define RT28xx_EEPROM_READ16(pAd, offset, var)		\
	var = RTMP_EEPROM_READ16(pAd, offset)

#define RT28xx_EEPROM_WRITE16(pAd, offset, var)		\
	RTMP_EEPROM_WRITE16(pAd, offset, var)

/* ----------------- TASK/THREAD Related MACRO ----------------- */
#define RT28XX_TASK_THREAD_INIT(pAd, Status)		\
	init_thread_task(pAd); NICInitTxRxRingAndBacklogQueue(pAd);	\
	Status = NDIS_STATUS_SUCCESS;

/* function declarations */
#define IRQ_HANDLE_TYPE  irqreturn_t

IRQ_HANDLE_TYPE
rt2860_interrupt(int irq, void *dev_instance);

/* ----------------- Frimware Related MACRO ----------------- */
#define RT28XX_WRITE_FIRMWARE(_pAd, _pFwImage, _FwLen)				\
	do{																\
		ULONG	_i, _firm;											\
		RTMP_IO_WRITE32(_pAd, PBF_SYS_CTRL, 0x10000);				\
																	\
		for(_i=0; _i<_FwLen; _i+=4)									\
		{															\
			_firm = _pFwImage[_i] +									\
			   (_pFwImage[_i+3] << 24) +							\
			   (_pFwImage[_i+2] << 16) +							\
			   (_pFwImage[_i+1] << 8);								\
			RTMP_IO_WRITE32(_pAd, FIRMWARE_IMAGE_BASE + _i, _firm);	\
		}															\
		RTMP_IO_WRITE32(_pAd, PBF_SYS_CTRL, 0x00000);				\
		RTMP_IO_WRITE32(_pAd, PBF_SYS_CTRL, 0x00001);				\
																	\
		/* initialize BBP R/W access agent */						\
		RTMP_IO_WRITE32(_pAd, H2M_BBP_AGENT, 0);					\
		RTMP_IO_WRITE32(_pAd, H2M_MAILBOX_CSR, 0);					\
	}while(0)

/* ----------------- TX Related MACRO ----------------- */
#define RT28XX_START_DEQUEUE(pAd, QueIdx, irqFlags)		do{}while(0)
#define RT28XX_STOP_DEQUEUE(pAd, QueIdx, irqFlags)		do{}while(0)


#define RT28XX_HAS_ENOUGH_FREE_DESC(pAd, pTxBlk, freeNum, pPacket) \
		((freeNum) >= (ULONG)(pTxBlk->TotalFragNum + RTMP_GET_PACKET_FRAGMENTS(pPacket) + 3)) /* rough estimate we will use 3 more descriptor. */
#define RT28XX_RELEASE_DESC_RESOURCE(pAd, QueIdx)					\
		do{}while(0)

#define NEED_QUEUE_BACK_FOR_AGG(pAd, QueIdx, freeNum, _TxFrameType) \
		(((freeNum != (TX_RING_SIZE-1)) && (pAd->TxSwQueue[QueIdx].Number == 0)) || (freeNum<3))
		//(((freeNum) != (TX_RING_SIZE-1)) && (pAd->TxSwQueue[QueIdx].Number == 1 /*0*/))


#define HAL_KickOutMgmtTx(_pAd, _QueIdx, _pPacket, _pSrcBufVA, _SrcBufLen)	\
			RtmpPCIMgmtKickOut(_pAd, _QueIdx, _pPacket, _pSrcBufVA, _SrcBufLen)

#define RTMP_PKT_TAIL_PADDING 0

#define fRTMP_ADAPTER_NEED_STOP_TX	0

#define HAL_WriteSubTxResource(pAd, pTxBlk, bIsLast, pFreeNumber)	\
		/* RtmpPCI_WriteSubTxResource(pAd, pTxBlk, bIsLast, pFreeNumber)*/

#define HAL_WriteTxResource(pAd, pTxBlk,bIsLast, pFreeNumber)	\
			RtmpPCI_WriteSingleTxResource(pAd, pTxBlk, bIsLast, pFreeNumber)

#define HAL_WriteFragTxResource(pAd, pTxBlk, fragNum, pFreeNumber) \
			RtmpPCI_WriteFragTxResource(pAd, pTxBlk, fragNum, pFreeNumber)

#define HAL_WriteMultiTxResource(pAd, pTxBlk,frameNum, pFreeNumber)	\
			RtmpPCI_WriteMultiTxResource(pAd, pTxBlk, frameNum, pFreeNumber)

#define HAL_FinalWriteTxResource(_pAd, _pTxBlk, _TotalMPDUSize, _FirstTxIdx)	\
			RtmpPCI_FinalWriteTxResource(_pAd, _pTxBlk, _TotalMPDUSize, _FirstTxIdx)

#define HAL_LastTxIdx(_pAd, _QueIdx,_LastTxIdx) \
			/*RtmpPCIDataLastTxIdx(_pAd, _QueIdx,_LastTxIdx)*/

#define HAL_KickOutTx(_pAd, _pTxBlk, _QueIdx)	\
			RTMP_IO_WRITE32((_pAd), TX_CTX_IDX0+((_QueIdx)*0x10), (_pAd)->TxRing[(_QueIdx)].TxCpuIdx)
/*			RtmpPCIDataKickOut(_pAd, _pTxBlk, _QueIdx)*/

#define HAL_KickOutNullFrameTx(_pAd, _QueIdx, _pNullFrame, _frameLen)	\
			MiniportMMRequest(_pAd, _QueIdx, _pNullFrame, _frameLen)

#define GET_TXRING_FREENO(_pAd, _QueIdx) \
	(_pAd->TxRing[_QueIdx].TxSwFreeIdx > _pAd->TxRing[_QueIdx].TxCpuIdx)	? \
			(_pAd->TxRing[_QueIdx].TxSwFreeIdx - _pAd->TxRing[_QueIdx].TxCpuIdx - 1) \
			 :	\
			(_pAd->TxRing[_QueIdx].TxSwFreeIdx + TX_RING_SIZE - _pAd->TxRing[_QueIdx].TxCpuIdx - 1);


#define GET_MGMTRING_FREENO(_pAd) \
	(_pAd->MgmtRing.TxSwFreeIdx > _pAd->MgmtRing.TxCpuIdx)	? \
			(_pAd->MgmtRing.TxSwFreeIdx - _pAd->MgmtRing.TxCpuIdx - 1) \
			 :	\
			(_pAd->MgmtRing.TxSwFreeIdx + MGMT_RING_SIZE - _pAd->MgmtRing.TxCpuIdx - 1);


/* ----------------- RX Related MACRO ----------------- */

// no use
#define RT28XX_RCV_PKT_GET_INIT(pAd)
#define RT28XX_RV_A_BUF_END
//#define RT28XX_RV_ALL_BUF_END


/* ----------------- ASIC Related MACRO ----------------- */
// no use
#define RT28XX_DMA_POST_WRITE(pAd)

// reset MAC of a station entry to 0x000000000000
#define RT28XX_STA_ENTRY_MAC_RESET(pAd, Wcid)						\
	AsicDelWcidTab(pAd, Wcid);

// add this entry into ASIC RX WCID search table
#define RT28XX_STA_ENTRY_ADD(pAd, pEntry)							\
	AsicUpdateRxWCIDTable(pAd, pEntry->Aid, pEntry->Addr);

// remove Pair-wise key material from ASIC
#define RT28XX_STA_ENTRY_KEY_DEL(pAd, BssIdx, Wcid)					\
	AsicRemovePairwiseKeyEntry(pAd, BssIdx, (UCHAR)Wcid);

// add Client security information into ASIC WCID table and IVEIV table
#define RT28XX_STA_SECURITY_INFO_ADD(pAd, apidx, KeyID, pEntry)		\
	RTMPAddWcidAttributeEntry(pAd, apidx, KeyID, 					\
							pAd->SharedKey[apidx][KeyID].CipherAlg, pEntry);

#define RT28XX_SECURITY_KEY_ADD(pAd, apidx, KeyID, pEntry)				\
	{	/* update pairwise key information to ASIC Shared Key Table */	\
		AsicAddSharedKeyEntry(pAd, apidx, KeyID,						\
						  pAd->SharedKey[apidx][KeyID].CipherAlg,		\
						  pAd->SharedKey[apidx][KeyID].Key,				\
						  pAd->SharedKey[apidx][KeyID].TxMic,			\
						  pAd->SharedKey[apidx][KeyID].RxMic);			\
		/* update ASIC WCID attribute table and IVEIV table */			\
		RTMPAddWcidAttributeEntry(pAd, apidx, KeyID,					\
						  pAd->SharedKey[apidx][KeyID].CipherAlg,		\
						  pEntry); }


// Insert the BA bitmap to ASIC for the Wcid entry
#define RT28XX_ADD_BA_SESSION_TO_ASIC(_pAd, _Aid, _TID)					\
		do{																\
			UINT32	_Value = 0, _Offset;									\
			_Offset = MAC_WCID_BASE + (_Aid) * HW_WCID_ENTRY_SIZE + 4;	\
			RTMP_IO_READ32((_pAd), _Offset, &_Value);					\
			_Value |= (0x10000<<(_TID));								\
			RTMP_IO_WRITE32((_pAd), _Offset, _Value);					\
		}while(0)


// Remove the BA bitmap from ASIC for the Wcid entry
//		bitmap field starts at 0x10000 in ASIC WCID table
#define RT28XX_DEL_BA_SESSION_FROM_ASIC(_pAd, _Wcid, _TID)				\
		do{																\
			UINT32	_Value = 0, _Offset;									\
			_Offset = MAC_WCID_BASE + (_Wcid) * HW_WCID_ENTRY_SIZE + 4;	\
			RTMP_IO_READ32((_pAd), _Offset, &_Value);					\
			_Value &= (~(0x10000 << (_TID)));							\
			RTMP_IO_WRITE32((_pAd), _Offset, _Value);			\
		}while(0)


/* ----------------- PCI/USB Related MACRO ----------------- */

#define RT28XX_HANDLE_DEV_ASSIGN(handle, dev_p)				\
	((POS_COOKIE)handle)->pci_dev = dev_p;

// set driver data
#define RT28XX_DRVDATA_SET(_a)			pci_set_drvdata(_a, net_dev);

#define RT28XX_UNMAP()										\
{	if (net_dev->base_addr)	{								\
		iounmap((void *)(net_dev->base_addr));				\
		release_mem_region(pci_resource_start(dev_p, 0),	\
							pci_resource_len(dev_p, 0)); }	\
	if (net_dev->irq) pci_release_regions(dev_p); }

#ifdef PCI_MSI_SUPPORT
#define RTMP_MSI_ENABLE(_pAd) \
{ 	POS_COOKIE _pObj = (POS_COOKIE)(_pAd->OS_Cookie); \
	(_pAd)->HaveMsi =	pci_enable_msi(_pObj->pci_dev) == 0 ? TRUE : FALSE; }

#define RTMP_MSI_DISABLE(_pAd) \
{ 	POS_COOKIE _pObj = (POS_COOKIE)(_pAd->OS_Cookie); \
	if (_pAd->HaveMsi == TRUE) \
		pci_disable_msi(_pObj->pci_dev); \
	_pAd->HaveMsi = FALSE;	}
#else
#define RTMP_MSI_ENABLE(_pAd)
#define RTMP_MSI_DISABLE(_pAd)
#endif // PCI_MSI_SUPPORT //

#define SA_SHIRQ IRQF_SHARED

#define RT28XX_IRQ_REQUEST(net_dev)							\
{	PRTMP_ADAPTER _pAd = (PRTMP_ADAPTER)((net_dev)->ml_priv);	\
	POS_COOKIE _pObj = (POS_COOKIE)(_pAd->OS_Cookie);		\
	RTMP_MSI_ENABLE(_pAd);									\
	if ((retval = request_irq(_pObj->pci_dev->irq, 		\
							rt2860_interrupt, SA_SHIRQ,		\
							(net_dev)->name, (net_dev)))) {	\
		printk("RT2860: request_irq  ERROR(%d)\n", retval);	\
	return retval; } }

#define RT28XX_IRQ_RELEASE(net_dev)								\
{	PRTMP_ADAPTER _pAd = (PRTMP_ADAPTER)((net_dev)->ml_priv);		\
	POS_COOKIE _pObj = (POS_COOKIE)(_pAd->OS_Cookie);			\
	synchronize_irq(_pObj->pci_dev->irq);						\
	free_irq(_pObj->pci_dev->irq, (net_dev));					\
	RTMP_MSI_DISABLE(_pAd); }

#define RT28XX_IRQ_INIT(pAd)										\
	{	pAd->int_enable_reg = ((DELAYINTMASK) |						\
							(RxINT|TxDataInt|TxMgmtInt)) & ~(0x03);	\
		pAd->int_disable_mask = 0;									\
		pAd->int_pending = 0; }

#define RT28XX_IRQ_ENABLE(pAd)									\
	{	/* clear garbage ints */								\
		RTMP_IO_WRITE32(pAd, INT_SOURCE_CSR, 0xffffffff);		\
		NICEnableInterrupt(pAd); }

#define RT28XX_PUT_DEVICE(dev_p)


/* ----------------- MLME Related MACRO ----------------- */
#define RT28XX_MLME_HANDLER(pAd)			MlmeHandler(pAd)

#define RT28XX_MLME_PRE_SANITY_CHECK(pAd)

#define RT28XX_MLME_STA_QUICK_RSP_WAKE_UP(pAd)	\
	RTMPSetTimer(&pAd->StaCfg.StaQuickResponeForRateUpTimer, 100);

#define RT28XX_MLME_RESET_STATE_MACHINE(pAd)	\
		MlmeRestartStateMachine(pAd)

#define RT28XX_HANDLE_COUNTER_MEASURE(_pAd, _pEntry)		\
		HandleCounterMeasure(_pAd, _pEntry)

/* ----------------- Power Save Related MACRO ----------------- */
#define RT28XX_PS_POLL_ENQUEUE(pAd)				EnqueuePsPoll(pAd)

//
// Device ID & Vendor ID, these values should match EEPROM value
//
#define NIC2860_PCI_DEVICE_ID   0x0601
#define NIC2860_PCIe_DEVICE_ID  0x0681
#define NIC2760_PCI_DEVICE_ID   0x0701		// 1T/2R Cardbus ???
#define NIC2790_PCIe_DEVICE_ID  0x0781	    // 1T/2R miniCard

#define NIC_PCI_VENDOR_ID       0x1814

#define VEN_AWT_PCIe_DEVICE_ID	0x1059
#define VEN_AWT_PCI_VENDOR_ID	0x1A3B

// For RTMPPCIePowerLinkCtrlRestore () function
#define RESTORE_HALT		    1
#define RESTORE_WAKEUP		    2
#define RESTORE_CLOSE           3

#define PowerSafeCID		1
#define PowerRadioOffCID		2
#define PowerWakeCID		3
#define CID0MASK		0x000000ff
#define CID1MASK		0x0000ff00
#define CID2MASK		0x00ff0000
#define CID3MASK		0xff000000

#define PCI_REG_READ_WORD(pci_dev, offset, Configuration)   \
    if (pci_read_config_word(pci_dev, offset, &reg16) == 0)     \
        Configuration = le2cpu16(reg16);                        \
    else                                                        \
        Configuration = 0;

#define PCI_REG_WIRTE_WORD(pci_dev, offset, Configuration)  \
    reg16 = cpu2le16(Configuration);                        \
    pci_write_config_word(pci_dev, offset, reg16);          \

#define RT28XX_STA_FORCE_WAKEUP(pAd, Level) \
    RT28xxPciStaAsicForceWakeup(pAd, Level);

#define RT28XX_STA_SLEEP_THEN_AUTO_WAKEUP(pAd, TbttNumToNextWakeUp) \
    RT28xxPciStaAsicSleepThenAutoWakeup(pAd, TbttNumToNextWakeUp);

#define RT28XX_MLME_RADIO_ON(pAd) \
    RT28xxPciMlmeRadioOn(pAd);

#define RT28XX_MLME_RADIO_OFF(pAd) \
    RT28xxPciMlmeRadioOFF(pAd);

#endif //__RT2860_H__

