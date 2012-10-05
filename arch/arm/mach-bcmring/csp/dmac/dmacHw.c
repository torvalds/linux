/*****************************************************************************
* Copyright 2003 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/****************************************************************************/
/**
*  @file    dmacHw.c
*
*  @brief   Low level DMA controller driver routines
*
*  @note
*
*   These routines provide basic DMA functionality only.
*/
/****************************************************************************/

/* ---- Include Files ---------------------------------------------------- */
#include <linux/types.h>
#include <linux/string.h>
#include <linux/stddef.h>

#include <mach/csp/dmacHw.h>
#include <mach/csp/dmacHw_reg.h>
#include <mach/csp/dmacHw_priv.h>
#include <mach/csp/chipcHw_inline.h>

/* ---- External Function Prototypes ------------------------------------- */

/* Allocate DMA control blocks */
dmacHw_CBLK_t dmacHw_gCblk[dmacHw_MAX_CHANNEL_COUNT];

uint32_t dmaChannelCount_0 = dmacHw_MAX_CHANNEL_COUNT / 2;
uint32_t dmaChannelCount_1 = dmacHw_MAX_CHANNEL_COUNT / 2;

/****************************************************************************/
/**
*  @brief   Get maximum FIFO for a DMA channel
*
*  @return  Maximum allowable FIFO size
*
*
*/
/****************************************************************************/
static uint32_t GetFifoSize(dmacHw_HANDLE_t handle	/*   [ IN ] DMA Channel handle */
    ) {
	uint32_t val = 0;
	dmacHw_CBLK_t *pCblk = dmacHw_HANDLE_TO_CBLK(handle);
	dmacHw_MISC_t __iomem *pMiscReg = (void __iomem *)dmacHw_REG_MISC_BASE(pCblk->module);

	switch (pCblk->channel) {
	case 0:
		val = (readl(&pMiscReg->CompParm2.lo) & 0x70000000) >> 28;
		break;
	case 1:
		val = (readl(&pMiscReg->CompParm3.hi) & 0x70000000) >> 28;
		break;
	case 2:
		val = (readl(&pMiscReg->CompParm3.lo) & 0x70000000) >> 28;
		break;
	case 3:
		val = (readl(&pMiscReg->CompParm4.hi) & 0x70000000) >> 28;
		break;
	case 4:
		val = (readl(&pMiscReg->CompParm4.lo) & 0x70000000) >> 28;
		break;
	case 5:
		val = (readl(&pMiscReg->CompParm5.hi) & 0x70000000) >> 28;
		break;
	case 6:
		val = (readl(&pMiscReg->CompParm5.lo) & 0x70000000) >> 28;
		break;
	case 7:
		val = (readl(&pMiscReg->CompParm6.hi) & 0x70000000) >> 28;
		break;
	}

	if (val <= 0x4) {
		return 8 << val;
	} else {
		dmacHw_ASSERT(0);
	}
	return 0;
}

/****************************************************************************/
/**
*  @brief   Program channel register to initiate transfer
*
*  @return  void
*
*
*  @note
*     - Descriptor buffer MUST ALWAYS be flushed before calling this function
*     - This function should also be called from ISR to program the channel with
*       pending descriptors
*/
/****************************************************************************/
void dmacHw_initiateTransfer(dmacHw_HANDLE_t handle,	/*   [ IN ] DMA Channel handle */
			     dmacHw_CONFIG_t *pConfig,	/*   [ IN ] Configuration settings */
			     void *pDescriptor	/*   [ IN ] Descriptor buffer */
    ) {
	dmacHw_DESC_RING_t *pRing;
	dmacHw_DESC_t *pProg;
	dmacHw_CBLK_t *pCblk;

	pCblk = dmacHw_HANDLE_TO_CBLK(handle);
	pRing = dmacHw_GET_DESC_RING(pDescriptor);

	if (CHANNEL_BUSY(pCblk->module, pCblk->channel)) {
		/* Not safe yet to program the channel */
		return;
	}

	if (pCblk->varDataStarted) {
		if (pCblk->descUpdated) {
			pCblk->descUpdated = 0;
			pProg =
			    (dmacHw_DESC_t *) ((uint32_t)
					       dmacHw_REG_LLP(pCblk->module,
							      pCblk->channel) +
					       pRing->virt2PhyOffset);

			/* Load descriptor if not loaded */
			if (!(pProg->ctl.hi & dmacHw_REG_CTL_DONE)) {
				dmacHw_SET_SAR(pCblk->module, pCblk->channel,
					       pProg->sar);
				dmacHw_SET_DAR(pCblk->module, pCblk->channel,
					       pProg->dar);
				dmacHw_REG_CTL_LO(pCblk->module,
						  pCblk->channel) =
				    pProg->ctl.lo;
				dmacHw_REG_CTL_HI(pCblk->module,
						  pCblk->channel) =
				    pProg->ctl.hi;
			} else if (pProg == (dmacHw_DESC_t *) pRing->pEnd->llp) {
				/* Return as end descriptor is processed */
				return;
			} else {
				dmacHw_ASSERT(0);
			}
		} else {
			return;
		}
	} else {
		if (pConfig->transferMode == dmacHw_TRANSFER_MODE_PERIODIC) {
			/* Do not make a single chain, rather process one descriptor at a time */
			pProg = pRing->pHead;
			/* Point to the next descriptor for next iteration */
			dmacHw_NEXT_DESC(pRing, pHead);
		} else {
			/* Return if no more pending descriptor */
			if (pRing->pEnd == NULL) {
				return;
			}

			pProg = pRing->pProg;
			if (pConfig->transferMode ==
			    dmacHw_TRANSFER_MODE_CONTINUOUS) {
				/* Make sure a complete ring can be formed */
				dmacHw_ASSERT((dmacHw_DESC_t *) pRing->pEnd->
					      llp == pRing->pProg);
				/* Make sure pProg pointing to the pHead */
				dmacHw_ASSERT((dmacHw_DESC_t *) pRing->pProg ==
					      pRing->pHead);
				/* Make a complete ring */
				do {
					pRing->pProg->ctl.lo |=
					    (dmacHw_REG_CTL_LLP_DST_EN |
					     dmacHw_REG_CTL_LLP_SRC_EN);
					pRing->pProg =
					    (dmacHw_DESC_t *) pRing->pProg->llp;
				} while (pRing->pProg != pRing->pHead);
			} else {
				/* Make a single long chain */
				while (pRing->pProg != pRing->pEnd) {
					pRing->pProg->ctl.lo |=
					    (dmacHw_REG_CTL_LLP_DST_EN |
					     dmacHw_REG_CTL_LLP_SRC_EN);
					pRing->pProg =
					    (dmacHw_DESC_t *) pRing->pProg->llp;
				}
			}
		}

		/* Program the channel registers */
		dmacHw_SET_SAR(pCblk->module, pCblk->channel, pProg->sar);
		dmacHw_SET_DAR(pCblk->module, pCblk->channel, pProg->dar);
		dmacHw_SET_LLP(pCblk->module, pCblk->channel,
			       (uint32_t) pProg - pRing->virt2PhyOffset);
		dmacHw_REG_CTL_LO(pCblk->module, pCblk->channel) =
		    pProg->ctl.lo;
		dmacHw_REG_CTL_HI(pCblk->module, pCblk->channel) =
		    pProg->ctl.hi;
		if (pRing->pEnd) {
			/* Remember the descriptor to use next */
			pRing->pProg = (dmacHw_DESC_t *) pRing->pEnd->llp;
		}
		/* Indicate no more pending descriptor  */
		pRing->pEnd = (dmacHw_DESC_t *) NULL;
	}
	/* Start DMA operation */
	dmacHw_DMA_START(pCblk->module, pCblk->channel);
}

/****************************************************************************/
/**
*  @brief   Initializes DMA
*
*  This function initializes DMA CSP driver
*
*  @note
*     Must be called before using any DMA channel
*/
/****************************************************************************/
void dmacHw_initDma(void)
{

	uint32_t i = 0;

	dmaChannelCount_0 = dmacHw_GET_NUM_CHANNEL(0);
	dmaChannelCount_1 = dmacHw_GET_NUM_CHANNEL(1);

	/* Enable access to the DMA block */
	chipcHw_busInterfaceClockEnable(chipcHw_REG_BUS_CLOCK_DMAC0);
	chipcHw_busInterfaceClockEnable(chipcHw_REG_BUS_CLOCK_DMAC1);

	if ((dmaChannelCount_0 + dmaChannelCount_1) > dmacHw_MAX_CHANNEL_COUNT) {
		dmacHw_ASSERT(0);
	}

	memset((void *)dmacHw_gCblk, 0,
	       sizeof(dmacHw_CBLK_t) * (dmaChannelCount_0 + dmaChannelCount_1));
	for (i = 0; i < dmaChannelCount_0; i++) {
		dmacHw_gCblk[i].module = 0;
		dmacHw_gCblk[i].channel = i;
	}
	for (i = 0; i < dmaChannelCount_1; i++) {
		dmacHw_gCblk[i + dmaChannelCount_0].module = 1;
		dmacHw_gCblk[i + dmaChannelCount_0].channel = i;
	}
}

/****************************************************************************/
/**
*  @brief   Exit function for  DMA
*
*  This function isolates DMA from the system
*
*/
/****************************************************************************/
void dmacHw_exitDma(void)
{
	/* Disable access to the DMA block */
	chipcHw_busInterfaceClockDisable(chipcHw_REG_BUS_CLOCK_DMAC0);
	chipcHw_busInterfaceClockDisable(chipcHw_REG_BUS_CLOCK_DMAC1);
}

/****************************************************************************/
/**
*  @brief   Gets a handle to a DMA channel
*
*  This function returns a handle, representing a control block of a particular DMA channel
*
*  @return  -1       - On Failure
*            handle  - On Success, representing a channel control block
*
*  @note
*     None  Channel ID must be created using "dmacHw_MAKE_CHANNEL_ID" macro
*/
/****************************************************************************/
dmacHw_HANDLE_t dmacHw_getChannelHandle(dmacHw_ID_t channelId	/* [ IN ] DMA Channel Id */
    ) {
	int idx;

	switch ((channelId >> 8)) {
	case 0:
		dmacHw_ASSERT((channelId & 0xff) < dmaChannelCount_0);
		idx = (channelId & 0xff);
		break;
	case 1:
		dmacHw_ASSERT((channelId & 0xff) < dmaChannelCount_1);
		idx = dmaChannelCount_0 + (channelId & 0xff);
		break;
	default:
		dmacHw_ASSERT(0);
		return (dmacHw_HANDLE_t) -1;
	}

	return dmacHw_CBLK_TO_HANDLE(&dmacHw_gCblk[idx]);
}

/****************************************************************************/
/**
*  @brief   Initializes a DMA channel for use
*
*  This function initializes and resets a DMA channel for use
*
*  @return  -1     - On Failure
*            0     - On Success
*
*  @note
*     None
*/
/****************************************************************************/
int dmacHw_initChannel(dmacHw_HANDLE_t handle	/*   [ IN ] DMA Channel handle */
    ) {
	dmacHw_CBLK_t *pCblk = dmacHw_HANDLE_TO_CBLK(handle);
	int module = pCblk->module;
	int channel = pCblk->channel;

	/* Reinitialize the control block */
	memset((void *)pCblk, 0, sizeof(dmacHw_CBLK_t));
	pCblk->module = module;
	pCblk->channel = channel;

	/* Enable DMA controller */
	dmacHw_DMA_ENABLE(pCblk->module);
	/* Reset DMA channel */
	dmacHw_RESET_CONTROL_LO(pCblk->module, pCblk->channel);
	dmacHw_RESET_CONTROL_HI(pCblk->module, pCblk->channel);
	dmacHw_RESET_CONFIG_LO(pCblk->module, pCblk->channel);
	dmacHw_RESET_CONFIG_HI(pCblk->module, pCblk->channel);

	/* Clear all raw interrupt status */
	dmacHw_TRAN_INT_CLEAR(pCblk->module, pCblk->channel);
	dmacHw_BLOCK_INT_CLEAR(pCblk->module, pCblk->channel);
	dmacHw_ERROR_INT_CLEAR(pCblk->module, pCblk->channel);

	/* Mask event specific interrupts */
	dmacHw_TRAN_INT_DISABLE(pCblk->module, pCblk->channel);
	dmacHw_BLOCK_INT_DISABLE(pCblk->module, pCblk->channel);
	dmacHw_STRAN_INT_DISABLE(pCblk->module, pCblk->channel);
	dmacHw_DTRAN_INT_DISABLE(pCblk->module, pCblk->channel);
	dmacHw_ERROR_INT_DISABLE(pCblk->module, pCblk->channel);

	return 0;
}

/****************************************************************************/
/**
*  @brief  Finds amount of memory required to form a descriptor ring
*
*
*  @return   Number of bytes required to form a descriptor ring
*
*
*/
/****************************************************************************/
uint32_t dmacHw_descriptorLen(uint32_t descCnt	/* [ IN ] Number of descriptor in the ring */
    ) {
	/* Need extra 4 byte to ensure 32 bit alignment  */
	return (descCnt * sizeof(dmacHw_DESC_t)) + sizeof(dmacHw_DESC_RING_t) +
		sizeof(uint32_t);
}

/****************************************************************************/
/**
*  @brief   Initializes descriptor ring
*
*  This function will initializes the descriptor ring of a DMA channel
*
*
*  @return   -1 - On failure
*             0 - On success
*  @note
*     - "len" parameter should be obtained from "dmacHw_descriptorLen"
*     - Descriptor buffer MUST be 32 bit aligned and uncached as it is
*       accessed by ARM and DMA
*/
/****************************************************************************/
int dmacHw_initDescriptor(void *pDescriptorVirt,	/*  [ IN ] Virtual address of uncahced buffer allocated to form descriptor ring */
			  uint32_t descriptorPhyAddr,	/*  [ IN ] Physical address of pDescriptorVirt (descriptor buffer) */
			  uint32_t len,	/*  [ IN ] Size of the pBuf */
			  uint32_t num	/*  [ IN ] Number of descriptor in the ring */
    ) {
	uint32_t i;
	dmacHw_DESC_RING_t *pRing;
	dmacHw_DESC_t *pDesc;

	/* Check the alignment of the descriptor */
	if ((uint32_t) pDescriptorVirt & 0x00000003) {
		dmacHw_ASSERT(0);
		return -1;
	}

	/* Check if enough space has been allocated for descriptor ring */
	if (len < dmacHw_descriptorLen(num)) {
		return -1;
	}

	pRing = dmacHw_GET_DESC_RING(pDescriptorVirt);
	pRing->pHead =
	    (dmacHw_DESC_t *) ((uint32_t) pRing + sizeof(dmacHw_DESC_RING_t));
	pRing->pFree = pRing->pTail = pRing->pEnd = pRing->pHead;
	pRing->pProg = dmacHw_DESC_INIT;
	/* Initialize link item chain, starting from the head */
	pDesc = pRing->pHead;
	/* Find the offset between virtual to physical address */
	pRing->virt2PhyOffset = (uint32_t) pDescriptorVirt - descriptorPhyAddr;

	/* Form the descriptor ring */
	for (i = 0; i < num - 1; i++) {
		/* Clear link list item */
		memset((void *)pDesc, 0, sizeof(dmacHw_DESC_t));
		/* Point to the next item in the physical address */
		pDesc->llpPhy = (uint32_t) (pDesc + 1) - pRing->virt2PhyOffset;
		/* Point to the next item in the virtual address */
		pDesc->llp = (uint32_t) (pDesc + 1);
		/* Mark descriptor is ready to use */
		pDesc->ctl.hi = dmacHw_DESC_FREE;
		/* Look into next link list item */
		pDesc++;
	}

	/* Clear last link list item */
	memset((void *)pDesc, 0, sizeof(dmacHw_DESC_t));
	/* Last item pointing to the first item in the
	   physical address to complete the ring */
	pDesc->llpPhy = (uint32_t) pRing->pHead - pRing->virt2PhyOffset;
	/* Last item pointing to the first item in the
	   virtual address to complete the ring
	 */
	pDesc->llp = (uint32_t) pRing->pHead;
	/* Mark descriptor is ready to use */
	pDesc->ctl.hi = dmacHw_DESC_FREE;
	/* Set the number of descriptors in the ring */
	pRing->num = num;
	return 0;
}

/****************************************************************************/
/**
*  @brief   Configure DMA channel
*
*  @return  0  : On success
*           -1 : On failure
*/
/****************************************************************************/
int dmacHw_configChannel(dmacHw_HANDLE_t handle,	/*   [ IN ] DMA Channel handle */
			 dmacHw_CONFIG_t *pConfig	/*   [ IN ] Configuration settings */
    ) {
	dmacHw_CBLK_t *pCblk = dmacHw_HANDLE_TO_CBLK(handle);
	uint32_t cfgHigh = 0;
	int srcTrSize;
	int dstTrSize;

	pCblk->varDataStarted = 0;
	pCblk->userData = NULL;

	/* Configure
	   - Burst transaction when enough data in available in FIFO
	   - AHB Access protection 1
	   - Source and destination peripheral ports
	 */
	cfgHigh =
	    dmacHw_REG_CFG_HI_FIFO_ENOUGH | dmacHw_REG_CFG_HI_AHB_HPROT_1 |
	    dmacHw_SRC_PERI_INTF(pConfig->
				 srcPeripheralPort) |
	    dmacHw_DST_PERI_INTF(pConfig->dstPeripheralPort);
	/* Set priority */
	dmacHw_SET_CHANNEL_PRIORITY(pCblk->module, pCblk->channel,
				    pConfig->channelPriority);

	if (pConfig->dstStatusRegisterAddress != 0) {
		/* Destination status update enable */
		cfgHigh |= dmacHw_REG_CFG_HI_UPDATE_DST_STAT;
		/* Configure status registers */
		dmacHw_SET_DSTATAR(pCblk->module, pCblk->channel,
				   pConfig->dstStatusRegisterAddress);
	}

	if (pConfig->srcStatusRegisterAddress != 0) {
		/* Source status update enable */
		cfgHigh |= dmacHw_REG_CFG_HI_UPDATE_SRC_STAT;
		/* Source status update enable */
		dmacHw_SET_SSTATAR(pCblk->module, pCblk->channel,
				   pConfig->srcStatusRegisterAddress);
	}
	/* Configure the config high register */
	dmacHw_GET_CONFIG_HI(pCblk->module, pCblk->channel) = cfgHigh;

	/* Clear all raw interrupt status */
	dmacHw_TRAN_INT_CLEAR(pCblk->module, pCblk->channel);
	dmacHw_BLOCK_INT_CLEAR(pCblk->module, pCblk->channel);
	dmacHw_ERROR_INT_CLEAR(pCblk->module, pCblk->channel);

	/* Configure block interrupt */
	if (pConfig->blockTransferInterrupt == dmacHw_INTERRUPT_ENABLE) {
		dmacHw_BLOCK_INT_ENABLE(pCblk->module, pCblk->channel);
	} else {
		dmacHw_BLOCK_INT_DISABLE(pCblk->module, pCblk->channel);
	}
	/* Configure complete transfer interrupt */
	if (pConfig->completeTransferInterrupt == dmacHw_INTERRUPT_ENABLE) {
		dmacHw_TRAN_INT_ENABLE(pCblk->module, pCblk->channel);
	} else {
		dmacHw_TRAN_INT_DISABLE(pCblk->module, pCblk->channel);
	}
	/* Configure error interrupt */
	if (pConfig->errorInterrupt == dmacHw_INTERRUPT_ENABLE) {
		dmacHw_ERROR_INT_ENABLE(pCblk->module, pCblk->channel);
	} else {
		dmacHw_ERROR_INT_DISABLE(pCblk->module, pCblk->channel);
	}
	/* Configure gather register */
	if (pConfig->srcGatherWidth) {
		srcTrSize =
		    dmacHw_GetTrWidthInBytes(pConfig->srcMaxTransactionWidth);
		if (!
		    ((pConfig->srcGatherWidth % srcTrSize)
		     && (pConfig->srcGatherJump % srcTrSize))) {
			dmacHw_REG_SGR_LO(pCblk->module, pCblk->channel) =
			    ((pConfig->srcGatherWidth /
			      srcTrSize) << 20) | (pConfig->srcGatherJump /
						   srcTrSize);
		} else {
			return -1;
		}
	}
	/* Configure scatter register */
	if (pConfig->dstScatterWidth) {
		dstTrSize =
		    dmacHw_GetTrWidthInBytes(pConfig->dstMaxTransactionWidth);
		if (!
		    ((pConfig->dstScatterWidth % dstTrSize)
		     && (pConfig->dstScatterJump % dstTrSize))) {
			dmacHw_REG_DSR_LO(pCblk->module, pCblk->channel) =
			    ((pConfig->dstScatterWidth /
			      dstTrSize) << 20) | (pConfig->dstScatterJump /
						   dstTrSize);
		} else {
			return -1;
		}
	}
	return 0;
}

/****************************************************************************/
/**
*  @brief   Indicates whether DMA transfer is in progress or completed
*
*  @return   DMA transfer status
*          dmacHw_TRANSFER_STATUS_BUSY:         DMA Transfer ongoing
*          dmacHw_TRANSFER_STATUS_DONE:         DMA Transfer completed
*          dmacHw_TRANSFER_STATUS_ERROR:        DMA Transfer error
*
*/
/****************************************************************************/
dmacHw_TRANSFER_STATUS_e dmacHw_transferCompleted(dmacHw_HANDLE_t handle	/*   [ IN ] DMA Channel handle */
    ) {
	dmacHw_CBLK_t *pCblk = dmacHw_HANDLE_TO_CBLK(handle);

	if (CHANNEL_BUSY(pCblk->module, pCblk->channel)) {
		return dmacHw_TRANSFER_STATUS_BUSY;
	} else if (dmacHw_REG_INT_RAW_ERROR(pCblk->module) &
		   (0x00000001 << pCblk->channel)) {
		return dmacHw_TRANSFER_STATUS_ERROR;
	}

	return dmacHw_TRANSFER_STATUS_DONE;
}

/****************************************************************************/
/**
*  @brief   Set descriptors for known data length
*
*  When DMA has to work as a flow controller, this function prepares the
*  descriptor chain to transfer data
*
*  from:
*          - Memory to memory
*          - Peripheral to memory
*          - Memory to Peripheral
*          - Peripheral to Peripheral
*
*  @return   -1 - On failure
*             0 - On success
*
*/
/****************************************************************************/
int dmacHw_setDataDescriptor(dmacHw_CONFIG_t *pConfig,	/*   [ IN ] Configuration settings */
			     void *pDescriptor,	/*   [ IN ] Descriptor buffer */
			     void *pSrcAddr,	/*   [ IN ] Source (Peripheral/Memory) address */
			     void *pDstAddr,	/*   [ IN ] Destination (Peripheral/Memory) address */
			     size_t dataLen	/*   [ IN ] Data length in bytes */
    ) {
	dmacHw_TRANSACTION_WIDTH_e dstTrWidth;
	dmacHw_TRANSACTION_WIDTH_e srcTrWidth;
	dmacHw_DESC_RING_t *pRing = dmacHw_GET_DESC_RING(pDescriptor);
	dmacHw_DESC_t *pStart;
	dmacHw_DESC_t *pProg;
	int srcTs = 0;
	int blkTs = 0;
	int oddSize = 0;
	int descCount = 0;
	int count = 0;
	int dstTrSize = 0;
	int srcTrSize = 0;
	uint32_t maxBlockSize = dmacHw_MAX_BLOCKSIZE;

	dstTrSize = dmacHw_GetTrWidthInBytes(pConfig->dstMaxTransactionWidth);
	srcTrSize = dmacHw_GetTrWidthInBytes(pConfig->srcMaxTransactionWidth);

	/* Skip Tx if buffer is NULL  or length is unknown */
	if ((pSrcAddr == NULL) || (pDstAddr == NULL) || (dataLen == 0)) {
		/* Do not initiate transfer */
		return -1;
	}

	/* Ensure scatter and gather are transaction aligned */
	if ((pConfig->srcGatherWidth % srcTrSize)
	    || (pConfig->dstScatterWidth % dstTrSize)) {
		return -2;
	}

	/*
	   Background 1: DMAC can not perform DMA if source and destination addresses are
	   not properly aligned with the channel's transaction width. So, for successful
	   DMA transfer, transaction width must be set according to the alignment of the
	   source and destination address.
	 */

	/* Adjust destination transaction width if destination address is not aligned properly */
	dstTrWidth = pConfig->dstMaxTransactionWidth;
	while (dmacHw_ADDRESS_MASK(dstTrSize) & (uint32_t) pDstAddr) {
		dstTrWidth = dmacHw_GetNextTrWidth(dstTrWidth);
		dstTrSize = dmacHw_GetTrWidthInBytes(dstTrWidth);
	}

	/* Adjust source transaction width if source address is not aligned properly */
	srcTrWidth = pConfig->srcMaxTransactionWidth;
	while (dmacHw_ADDRESS_MASK(srcTrSize) & (uint32_t) pSrcAddr) {
		srcTrWidth = dmacHw_GetNextTrWidth(srcTrWidth);
		srcTrSize = dmacHw_GetTrWidthInBytes(srcTrWidth);
	}

	/* Find the maximum transaction per descriptor */
	if (pConfig->maxDataPerBlock
	    && ((pConfig->maxDataPerBlock / srcTrSize) <
		dmacHw_MAX_BLOCKSIZE)) {
		maxBlockSize = pConfig->maxDataPerBlock / srcTrSize;
	}

	/* Find number of source transactions needed to complete the DMA transfer */
	srcTs = dataLen / srcTrSize;
	/* Find the odd number of bytes that need to be transferred as single byte transaction width */
	if (srcTs && (dstTrSize > srcTrSize)) {
		oddSize = dataLen % dstTrSize;
		/* Adjust source transaction count due to "oddSize" */
		srcTs = srcTs - (oddSize / srcTrSize);
	} else {
		oddSize = dataLen % srcTrSize;
	}
	/* Adjust "descCount" due to "oddSize" */
	if (oddSize) {
		descCount++;
	}
	/* Find the number of descriptor needed for total "srcTs" */
	if (srcTs) {
		descCount += ((srcTs - 1) / maxBlockSize) + 1;
	}

	/* Check the availability of "descCount" discriptors in the ring */
	pProg = pRing->pHead;
	for (count = 0; (descCount <= pRing->num) && (count < descCount);
	     count++) {
		if ((pProg->ctl.hi & dmacHw_DESC_FREE) == 0) {
			/* Sufficient descriptors are not available */
			return -3;
		}
		pProg = (dmacHw_DESC_t *) pProg->llp;
	}

	/* Remember the link list item to program the channel registers */
	pStart = pProg = pRing->pHead;
	/* Make a link list with "descCount(=count)" number of descriptors */
	while (count) {
		/* Reset channel control information */
		pProg->ctl.lo = 0;
		/* Enable source gather if configured */
		if (pConfig->srcGatherWidth) {
			pProg->ctl.lo |= dmacHw_REG_CTL_SG_ENABLE;
		}
		/* Enable destination scatter if configured */
		if (pConfig->dstScatterWidth) {
			pProg->ctl.lo |= dmacHw_REG_CTL_DS_ENABLE;
		}
		/* Set source and destination address */
		pProg->sar = (uint32_t) pSrcAddr;
		pProg->dar = (uint32_t) pDstAddr;
		/* Use "devCtl" to mark that user memory need to be freed later if needed */
		if (pProg == pRing->pHead) {
			pProg->devCtl = dmacHw_FREE_USER_MEMORY;
		} else {
			pProg->devCtl = 0;
		}

		blkTs = srcTs;

		/* Special treatmeant for last descriptor */
		if (count == 1) {
			/* Mark the last descriptor */
			pProg->ctl.lo &=
			    ~(dmacHw_REG_CTL_LLP_DST_EN |
			      dmacHw_REG_CTL_LLP_SRC_EN);
			/* Treatment for odd data bytes */
			if (oddSize) {
				/* Adjust for single byte transaction width */
				switch (pConfig->transferType) {
				case dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM:
					dstTrWidth =
					    dmacHw_DST_TRANSACTION_WIDTH_8;
					blkTs =
					    (oddSize / srcTrSize) +
					    ((oddSize % srcTrSize) ? 1 : 0);
					break;
				case dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL:
					srcTrWidth =
					    dmacHw_SRC_TRANSACTION_WIDTH_8;
					blkTs = oddSize;
					break;
				case dmacHw_TRANSFER_TYPE_MEM_TO_MEM:
					srcTrWidth =
					    dmacHw_SRC_TRANSACTION_WIDTH_8;
					dstTrWidth =
					    dmacHw_DST_TRANSACTION_WIDTH_8;
					blkTs = oddSize;
					break;
				case dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_PERIPHERAL:
					/* Do not adjust the transaction width  */
					break;
				}
			} else {
				srcTs -= blkTs;
			}
		} else {
			if (srcTs / maxBlockSize) {
				blkTs = maxBlockSize;
			}
			/* Remaining source transactions for next iteration */
			srcTs -= blkTs;
		}
		/* Must have a valid source transactions */
		dmacHw_ASSERT(blkTs > 0);
		/* Set control information */
		if (pConfig->flowControler == dmacHw_FLOW_CONTROL_DMA) {
			pProg->ctl.lo |= pConfig->transferType |
			    pConfig->srcUpdate |
			    pConfig->dstUpdate |
			    srcTrWidth |
			    dstTrWidth |
			    pConfig->srcMaxBurstWidth |
			    pConfig->dstMaxBurstWidth |
			    pConfig->srcMasterInterface |
			    pConfig->dstMasterInterface | dmacHw_REG_CTL_INT_EN;
		} else {
			uint32_t transferType = 0;
			switch (pConfig->transferType) {
			case dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM:
				transferType = dmacHw_REG_CTL_TTFC_PM_PERI;
				break;
			case dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL:
				transferType = dmacHw_REG_CTL_TTFC_MP_PERI;
				break;
			default:
				dmacHw_ASSERT(0);
			}
			pProg->ctl.lo |= transferType |
			    pConfig->srcUpdate |
			    pConfig->dstUpdate |
			    srcTrWidth |
			    dstTrWidth |
			    pConfig->srcMaxBurstWidth |
			    pConfig->dstMaxBurstWidth |
			    pConfig->srcMasterInterface |
			    pConfig->dstMasterInterface | dmacHw_REG_CTL_INT_EN;
		}

		/* Set block transaction size */
		pProg->ctl.hi = blkTs & dmacHw_REG_CTL_BLOCK_TS_MASK;
		/* Look for next descriptor */
		if (count > 1) {
			/* Point to the next descriptor */
			pProg = (dmacHw_DESC_t *) pProg->llp;

			/* Update source and destination address for next iteration */
			switch (pConfig->transferType) {
			case dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM:
				if (pConfig->dstScatterWidth) {
					pDstAddr =
					    (char *)pDstAddr +
					    blkTs * srcTrSize +
					    (((blkTs * srcTrSize) /
					      pConfig->dstScatterWidth) *
					     pConfig->dstScatterJump);
				} else {
					pDstAddr =
					    (char *)pDstAddr +
					    blkTs * srcTrSize;
				}
				break;
			case dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL:
				if (pConfig->srcGatherWidth) {
					pSrcAddr =
					    (char *)pDstAddr +
					    blkTs * srcTrSize +
					    (((blkTs * srcTrSize) /
					      pConfig->srcGatherWidth) *
					     pConfig->srcGatherJump);
				} else {
					pSrcAddr =
					    (char *)pSrcAddr +
					    blkTs * srcTrSize;
				}
				break;
			case dmacHw_TRANSFER_TYPE_MEM_TO_MEM:
				if (pConfig->dstScatterWidth) {
					pDstAddr =
					    (char *)pDstAddr +
					    blkTs * srcTrSize +
					    (((blkTs * srcTrSize) /
					      pConfig->dstScatterWidth) *
					     pConfig->dstScatterJump);
				} else {
					pDstAddr =
					    (char *)pDstAddr +
					    blkTs * srcTrSize;
				}

				if (pConfig->srcGatherWidth) {
					pSrcAddr =
					    (char *)pDstAddr +
					    blkTs * srcTrSize +
					    (((blkTs * srcTrSize) /
					      pConfig->srcGatherWidth) *
					     pConfig->srcGatherJump);
				} else {
					pSrcAddr =
					    (char *)pSrcAddr +
					    blkTs * srcTrSize;
				}
				break;
			case dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_PERIPHERAL:
				/* Do not adjust the address */
				break;
			default:
				dmacHw_ASSERT(0);
			}
		} else {
			/* At the end of transfer "srcTs" must be zero */
			dmacHw_ASSERT(srcTs == 0);
		}
		count--;
	}

	/* Remember the descriptor to initialize the registers */
	if (pRing->pProg == dmacHw_DESC_INIT) {
		pRing->pProg = pStart;
	}
	/* Indicate that the descriptor is updated */
	pRing->pEnd = pProg;
	/* Head pointing to the next descriptor */
	pRing->pHead = (dmacHw_DESC_t *) pProg->llp;
	/* Update Tail pointer if destination is a peripheral,
	   because no one is going to read from the pTail
	 */
	if (!dmacHw_DST_IS_MEMORY(pConfig->transferType)) {
		pRing->pTail = pRing->pHead;
	}
	return 0;
}

/****************************************************************************/
/**
*  @brief   Provides DMA controller attributes
*
*
*  @return  DMA controller attributes
*
*  @note
*     None
*/
/****************************************************************************/
uint32_t dmacHw_getDmaControllerAttribute(dmacHw_HANDLE_t handle,	/*  [ IN ]  DMA Channel handle */
					  dmacHw_CONTROLLER_ATTRIB_e attr	/*  [ IN ]  DMA Controller attribute of type  dmacHw_CONTROLLER_ATTRIB_e */
    ) {
	dmacHw_CBLK_t *pCblk = dmacHw_HANDLE_TO_CBLK(handle);

	switch (attr) {
	case dmacHw_CONTROLLER_ATTRIB_CHANNEL_NUM:
		return dmacHw_GET_NUM_CHANNEL(pCblk->module);
	case dmacHw_CONTROLLER_ATTRIB_CHANNEL_MAX_BLOCK_SIZE:
		return (1 <<
			 (dmacHw_GET_MAX_BLOCK_SIZE
			  (pCblk->module, pCblk->module) + 2)) - 8;
	case dmacHw_CONTROLLER_ATTRIB_MASTER_INTF_NUM:
		return dmacHw_GET_NUM_INTERFACE(pCblk->module);
	case dmacHw_CONTROLLER_ATTRIB_CHANNEL_BUS_WIDTH:
		return 32 << dmacHw_GET_CHANNEL_DATA_WIDTH(pCblk->module,
							   pCblk->channel);
	case dmacHw_CONTROLLER_ATTRIB_CHANNEL_FIFO_SIZE:
		return GetFifoSize(handle);
	}
	dmacHw_ASSERT(0);
	return 0;
}
