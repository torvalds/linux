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
*  @file    dmacHw_extra.c
*
*  @brief   Extra Low level DMA controller driver routines
*
*  @note
*
*   These routines provide basic DMA functionality only.
*/
/****************************************************************************/

/* ---- Include Files ---------------------------------------------------- */

#include <csp/stdint.h>
#include <stddef.h>

#include <csp/dmacHw.h>
#include <mach/csp/dmacHw_reg.h>
#include <mach/csp/dmacHw_priv.h>

extern dmacHw_CBLK_t dmacHw_gCblk[dmacHw_MAX_CHANNEL_COUNT];	/* Declared in dmacHw.c */

/* ---- External Function Prototypes ------------------------------------- */

/* ---- Internal Use Function Prototypes --------------------------------- */
/****************************************************************************/
/**
*  @brief   Overwrites data length in the descriptor
*
*  This function overwrites data length in the descriptor
*
*
*  @return   void
*
*  @note
*          This is only used for PCM channel
*/
/****************************************************************************/
void dmacHw_setDataLength(dmacHw_CONFIG_t *pConfig,	/*   [ IN ] Configuration settings */
			  void *pDescriptor,	/*   [ IN ] Descriptor buffer */
			  size_t dataLen	/*   [ IN ] Data length in bytes */
    );

/****************************************************************************/
/**
*  @brief   Helper function to display DMA registers
*
*  @return  void
*
*
*  @note
*     None
*/
/****************************************************************************/
static void DisplayRegisterContents(int module,	/*   [ IN ] DMA Controller unit  (0-1) */
				    int channel,	/*   [ IN ] DMA Channel          (0-7) / -1(all) */
				    int (*fpPrint) (const char *, ...)	/*   [ IN ] Callback to the print function */
    ) {
	int chan;

	(*fpPrint) ("Displaying register content \n\n");
	(*fpPrint) ("Module %d: Interrupt raw transfer              0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_RAW_TRAN(module)));
	(*fpPrint) ("Module %d: Interrupt raw block                 0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_RAW_BLOCK(module)));
	(*fpPrint) ("Module %d: Interrupt raw src transfer          0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_RAW_STRAN(module)));
	(*fpPrint) ("Module %d: Interrupt raw dst transfer          0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_RAW_DTRAN(module)));
	(*fpPrint) ("Module %d: Interrupt raw error                 0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_RAW_ERROR(module)));
	(*fpPrint) ("--------------------------------------------------\n");
	(*fpPrint) ("Module %d: Interrupt stat transfer             0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_STAT_TRAN(module)));
	(*fpPrint) ("Module %d: Interrupt stat block                0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_STAT_BLOCK(module)));
	(*fpPrint) ("Module %d: Interrupt stat src transfer         0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_STAT_STRAN(module)));
	(*fpPrint) ("Module %d: Interrupt stat dst transfer         0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_STAT_DTRAN(module)));
	(*fpPrint) ("Module %d: Interrupt stat error                0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_STAT_ERROR(module)));
	(*fpPrint) ("--------------------------------------------------\n");
	(*fpPrint) ("Module %d: Interrupt mask transfer             0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_MASK_TRAN(module)));
	(*fpPrint) ("Module %d: Interrupt mask block                0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_MASK_BLOCK(module)));
	(*fpPrint) ("Module %d: Interrupt mask src transfer         0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_MASK_STRAN(module)));
	(*fpPrint) ("Module %d: Interrupt mask dst transfer         0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_MASK_DTRAN(module)));
	(*fpPrint) ("Module %d: Interrupt mask error                0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_MASK_ERROR(module)));
	(*fpPrint) ("--------------------------------------------------\n");
	(*fpPrint) ("Module %d: Interrupt clear transfer            0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_CLEAR_TRAN(module)));
	(*fpPrint) ("Module %d: Interrupt clear block               0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_CLEAR_BLOCK(module)));
	(*fpPrint) ("Module %d: Interrupt clear src transfer        0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_CLEAR_STRAN(module)));
	(*fpPrint) ("Module %d: Interrupt clear dst transfer        0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_CLEAR_DTRAN(module)));
	(*fpPrint) ("Module %d: Interrupt clear error               0x%X\n",
		    module, (uint32_t) (dmacHw_REG_INT_CLEAR_ERROR(module)));
	(*fpPrint) ("--------------------------------------------------\n");
	(*fpPrint) ("Module %d: SW source req                       0x%X\n",
		    module, (uint32_t) (dmacHw_REG_SW_HS_SRC_REQ(module)));
	(*fpPrint) ("Module %d: SW dest req                         0x%X\n",
		    module, (uint32_t) (dmacHw_REG_SW_HS_DST_REQ(module)));
	(*fpPrint) ("Module %d: SW source signal                    0x%X\n",
		    module, (uint32_t) (dmacHw_REG_SW_HS_SRC_SGL_REQ(module)));
	(*fpPrint) ("Module %d: SW dest signal                      0x%X\n",
		    module, (uint32_t) (dmacHw_REG_SW_HS_DST_SGL_REQ(module)));
	(*fpPrint) ("Module %d: SW source last                      0x%X\n",
		    module, (uint32_t) (dmacHw_REG_SW_HS_SRC_LST_REQ(module)));
	(*fpPrint) ("Module %d: SW dest last                        0x%X\n",
		    module, (uint32_t) (dmacHw_REG_SW_HS_DST_LST_REQ(module)));
	(*fpPrint) ("--------------------------------------------------\n");
	(*fpPrint) ("Module %d: misc config                         0x%X\n",
		    module, (uint32_t) (dmacHw_REG_MISC_CFG(module)));
	(*fpPrint) ("Module %d: misc channel enable                 0x%X\n",
		    module, (uint32_t) (dmacHw_REG_MISC_CH_ENABLE(module)));
	(*fpPrint) ("Module %d: misc ID                             0x%X\n",
		    module, (uint32_t) (dmacHw_REG_MISC_ID(module)));
	(*fpPrint) ("Module %d: misc test                           0x%X\n",
		    module, (uint32_t) (dmacHw_REG_MISC_TEST(module)));

	if (channel == -1) {
		for (chan = 0; chan < 8; chan++) {
			(*fpPrint)
			    ("--------------------------------------------------\n");
			(*fpPrint)
			    ("Module %d: Channel %d Source                   0x%X\n",
			     module, chan,
			     (uint32_t) (dmacHw_REG_SAR(module, chan)));
			(*fpPrint)
			    ("Module %d: Channel %d Destination              0x%X\n",
			     module, chan,
			     (uint32_t) (dmacHw_REG_DAR(module, chan)));
			(*fpPrint)
			    ("Module %d: Channel %d LLP                      0x%X\n",
			     module, chan,
			     (uint32_t) (dmacHw_REG_LLP(module, chan)));
			(*fpPrint)
			    ("Module %d: Channel %d Control (LO)             0x%X\n",
			     module, chan,
			     (uint32_t) (dmacHw_REG_CTL_LO(module, chan)));
			(*fpPrint)
			    ("Module %d: Channel %d Control (HI)             0x%X\n",
			     module, chan,
			     (uint32_t) (dmacHw_REG_CTL_HI(module, chan)));
			(*fpPrint)
			    ("Module %d: Channel %d Source Stats             0x%X\n",
			     module, chan,
			     (uint32_t) (dmacHw_REG_SSTAT(module, chan)));
			(*fpPrint)
			    ("Module %d: Channel %d Dest Stats               0x%X\n",
			     module, chan,
			     (uint32_t) (dmacHw_REG_DSTAT(module, chan)));
			(*fpPrint)
			    ("Module %d: Channel %d Source Stats Addr        0x%X\n",
			     module, chan,
			     (uint32_t) (dmacHw_REG_SSTATAR(module, chan)));
			(*fpPrint)
			    ("Module %d: Channel %d Dest Stats Addr          0x%X\n",
			     module, chan,
			     (uint32_t) (dmacHw_REG_DSTATAR(module, chan)));
			(*fpPrint)
			    ("Module %d: Channel %d Config (LO)              0x%X\n",
			     module, chan,
			     (uint32_t) (dmacHw_REG_CFG_LO(module, chan)));
			(*fpPrint)
			    ("Module %d: Channel %d Config (HI)              0x%X\n",
			     module, chan,
			     (uint32_t) (dmacHw_REG_CFG_HI(module, chan)));
		}
	} else {
		chan = channel;
		(*fpPrint)
		    ("--------------------------------------------------\n");
		(*fpPrint)
		    ("Module %d: Channel %d Source                   0x%X\n",
		     module, chan, (uint32_t) (dmacHw_REG_SAR(module, chan)));
		(*fpPrint)
		    ("Module %d: Channel %d Destination              0x%X\n",
		     module, chan, (uint32_t) (dmacHw_REG_DAR(module, chan)));
		(*fpPrint)
		    ("Module %d: Channel %d LLP                      0x%X\n",
		     module, chan, (uint32_t) (dmacHw_REG_LLP(module, chan)));
		(*fpPrint)
		    ("Module %d: Channel %d Control (LO)             0x%X\n",
		     module, chan,
		     (uint32_t) (dmacHw_REG_CTL_LO(module, chan)));
		(*fpPrint)
		    ("Module %d: Channel %d Control (HI)             0x%X\n",
		     module, chan,
		     (uint32_t) (dmacHw_REG_CTL_HI(module, chan)));
		(*fpPrint)
		    ("Module %d: Channel %d Source Stats             0x%X\n",
		     module, chan, (uint32_t) (dmacHw_REG_SSTAT(module, chan)));
		(*fpPrint)
		    ("Module %d: Channel %d Dest Stats               0x%X\n",
		     module, chan, (uint32_t) (dmacHw_REG_DSTAT(module, chan)));
		(*fpPrint)
		    ("Module %d: Channel %d Source Stats Addr        0x%X\n",
		     module, chan,
		     (uint32_t) (dmacHw_REG_SSTATAR(module, chan)));
		(*fpPrint)
		    ("Module %d: Channel %d Dest Stats Addr          0x%X\n",
		     module, chan,
		     (uint32_t) (dmacHw_REG_DSTATAR(module, chan)));
		(*fpPrint)
		    ("Module %d: Channel %d Config (LO)              0x%X\n",
		     module, chan,
		     (uint32_t) (dmacHw_REG_CFG_LO(module, chan)));
		(*fpPrint)
		    ("Module %d: Channel %d Config (HI)              0x%X\n",
		     module, chan,
		     (uint32_t) (dmacHw_REG_CFG_HI(module, chan)));
	}
}

/****************************************************************************/
/**
*  @brief   Helper function to display descriptor ring
*
*  @return  void
*
*
*  @note
*     None
*/
/****************************************************************************/
static void DisplayDescRing(void *pDescriptor,	/*   [ IN ] Descriptor buffer */
			    int (*fpPrint) (const char *, ...)	/*   [ IN ] Callback to the print function */
    ) {
	dmacHw_DESC_RING_t *pRing = dmacHw_GET_DESC_RING(pDescriptor);
	dmacHw_DESC_t *pStart;

	if (pRing->pHead == NULL) {
		return;
	}

	pStart = pRing->pHead;

	while ((dmacHw_DESC_t *) pStart->llp != pRing->pHead) {
		if (pStart == pRing->pHead) {
			(*fpPrint) ("Head\n");
		}
		if (pStart == pRing->pTail) {
			(*fpPrint) ("Tail\n");
		}
		if (pStart == pRing->pProg) {
			(*fpPrint) ("Prog\n");
		}
		if (pStart == pRing->pEnd) {
			(*fpPrint) ("End\n");
		}
		if (pStart == pRing->pFree) {
			(*fpPrint) ("Free\n");
		}
		(*fpPrint) ("0x%X:\n", (uint32_t) pStart);
		(*fpPrint) ("sar    0x%0X\n", pStart->sar);
		(*fpPrint) ("dar    0x%0X\n", pStart->dar);
		(*fpPrint) ("llp    0x%0X\n", pStart->llp);
		(*fpPrint) ("ctl.lo 0x%0X\n", pStart->ctl.lo);
		(*fpPrint) ("ctl.hi 0x%0X\n", pStart->ctl.hi);
		(*fpPrint) ("sstat  0x%0X\n", pStart->sstat);
		(*fpPrint) ("dstat  0x%0X\n", pStart->dstat);
		(*fpPrint) ("devCtl 0x%0X\n", pStart->devCtl);

		pStart = (dmacHw_DESC_t *) pStart->llp;
	}
	if (pStart == pRing->pHead) {
		(*fpPrint) ("Head\n");
	}
	if (pStart == pRing->pTail) {
		(*fpPrint) ("Tail\n");
	}
	if (pStart == pRing->pProg) {
		(*fpPrint) ("Prog\n");
	}
	if (pStart == pRing->pEnd) {
		(*fpPrint) ("End\n");
	}
	if (pStart == pRing->pFree) {
		(*fpPrint) ("Free\n");
	}
	(*fpPrint) ("0x%X:\n", (uint32_t) pStart);
	(*fpPrint) ("sar    0x%0X\n", pStart->sar);
	(*fpPrint) ("dar    0x%0X\n", pStart->dar);
	(*fpPrint) ("llp    0x%0X\n", pStart->llp);
	(*fpPrint) ("ctl.lo 0x%0X\n", pStart->ctl.lo);
	(*fpPrint) ("ctl.hi 0x%0X\n", pStart->ctl.hi);
	(*fpPrint) ("sstat  0x%0X\n", pStart->sstat);
	(*fpPrint) ("dstat  0x%0X\n", pStart->dstat);
	(*fpPrint) ("devCtl 0x%0X\n", pStart->devCtl);
}

/****************************************************************************/
/**
*  @brief   Check if DMA channel is the flow controller
*
*  @return  1 : If DMA is a flow controller
*           0 : Peripheral is the flow controller
*
*  @note
*     None
*/
/****************************************************************************/
static inline int DmaIsFlowController(void *pDescriptor	/*   [ IN ] Descriptor buffer */
    ) {
	uint32_t ttfc =
	    (dmacHw_GET_DESC_RING(pDescriptor))->pTail->ctl.
	    lo & dmacHw_REG_CTL_TTFC_MASK;

	switch (ttfc) {
	case dmacHw_REG_CTL_TTFC_MM_DMAC:
	case dmacHw_REG_CTL_TTFC_MP_DMAC:
	case dmacHw_REG_CTL_TTFC_PM_DMAC:
	case dmacHw_REG_CTL_TTFC_PP_DMAC:
		return 1;
	}

	return 0;
}

/****************************************************************************/
/**
*  @brief   Overwrites data length in the descriptor
*
*  This function overwrites data length in the descriptor
*
*
*  @return   void
*
*  @note
*          This is only used for PCM channel
*/
/****************************************************************************/
void dmacHw_setDataLength(dmacHw_CONFIG_t *pConfig,	/*   [ IN ] Configuration settings */
			  void *pDescriptor,	/*   [ IN ] Descriptor buffer */
			  size_t dataLen	/*   [ IN ] Data length in bytes */
    ) {
	dmacHw_DESC_t *pProg;
	dmacHw_DESC_t *pHead;
	int srcTs = 0;
	int srcTrSize = 0;

	pHead = (dmacHw_GET_DESC_RING(pDescriptor))->pHead;
	pProg = pHead;

	srcTrSize = dmacHw_GetTrWidthInBytes(pConfig->srcMaxTransactionWidth);
	srcTs = dataLen / srcTrSize;
	do {
		pProg->ctl.hi = srcTs & dmacHw_REG_CTL_BLOCK_TS_MASK;
		pProg = (dmacHw_DESC_t *) pProg->llp;
	} while (pProg != pHead);
}

/****************************************************************************/
/**
*  @brief   Clears the interrupt
*
*  This function clears the DMA channel specific interrupt
*
*
*  @return   void
*
*  @note
*     Must be called under the context of ISR
*/
/****************************************************************************/
void dmacHw_clearInterrupt(dmacHw_HANDLE_t handle	/* [ IN ] DMA Channel handle */
    ) {
	dmacHw_CBLK_t *pCblk = dmacHw_HANDLE_TO_CBLK(handle);

	dmacHw_TRAN_INT_CLEAR(pCblk->module, pCblk->channel);
	dmacHw_BLOCK_INT_CLEAR(pCblk->module, pCblk->channel);
	dmacHw_ERROR_INT_CLEAR(pCblk->module, pCblk->channel);
}

/****************************************************************************/
/**
*  @brief   Returns the cause of channel specific DMA interrupt
*
*  This function returns the cause of interrupt
*
*  @return  Interrupt status, each bit representing a specific type of interrupt
*
*  @note
*     Should be called under the context of ISR
*/
/****************************************************************************/
dmacHw_INTERRUPT_STATUS_e dmacHw_getInterruptStatus(dmacHw_HANDLE_t handle	/* [ IN ] DMA Channel handle */
    ) {
	dmacHw_CBLK_t *pCblk = dmacHw_HANDLE_TO_CBLK(handle);
	dmacHw_INTERRUPT_STATUS_e status = dmacHw_INTERRUPT_STATUS_NONE;

	if (dmacHw_REG_INT_STAT_TRAN(pCblk->module) &
	    ((0x00000001 << pCblk->channel))) {
		status |= dmacHw_INTERRUPT_STATUS_TRANS;
	}
	if (dmacHw_REG_INT_STAT_BLOCK(pCblk->module) &
	    ((0x00000001 << pCblk->channel))) {
		status |= dmacHw_INTERRUPT_STATUS_BLOCK;
	}
	if (dmacHw_REG_INT_STAT_ERROR(pCblk->module) &
	    ((0x00000001 << pCblk->channel))) {
		status |= dmacHw_INTERRUPT_STATUS_ERROR;
	}

	return status;
}

/****************************************************************************/
/**
*  @brief   Indentifies a DMA channel causing interrupt
*
*  This functions returns a channel causing interrupt of type dmacHw_INTERRUPT_STATUS_e
*
*  @return  NULL   : No channel causing DMA interrupt
*           ! NULL : Handle to a channel causing DMA interrupt
*  @note
*     dmacHw_clearInterrupt() must be called with a valid handle after calling this function
*/
/****************************************************************************/
dmacHw_HANDLE_t dmacHw_getInterruptSource(void)
{
	uint32_t i;

	for (i = 0; i < dmaChannelCount_0 + dmaChannelCount_1; i++) {
		if ((dmacHw_REG_INT_STAT_TRAN(dmacHw_gCblk[i].module) &
		     ((0x00000001 << dmacHw_gCblk[i].channel)))
		    || (dmacHw_REG_INT_STAT_BLOCK(dmacHw_gCblk[i].module) &
			((0x00000001 << dmacHw_gCblk[i].channel)))
		    || (dmacHw_REG_INT_STAT_ERROR(dmacHw_gCblk[i].module) &
			((0x00000001 << dmacHw_gCblk[i].channel)))
		    ) {
			return dmacHw_CBLK_TO_HANDLE(&dmacHw_gCblk[i]);
		}
	}
	return dmacHw_CBLK_TO_HANDLE(NULL);
}

/****************************************************************************/
/**
*  @brief  Estimates number of descriptor needed to perform certain DMA transfer
*
*
*  @return  On failure : -1
*           On success : Number of descriptor count
*
*
*/
/****************************************************************************/
int dmacHw_calculateDescriptorCount(dmacHw_CONFIG_t *pConfig,	/*   [ IN ] Configuration settings */
				    void *pSrcAddr,	/*   [ IN ] Source (Peripheral/Memory) address */
				    void *pDstAddr,	/*   [ IN ] Destination (Peripheral/Memory) address */
				    size_t dataLen	/*   [ IN ] Data length in bytes */
    ) {
	int srcTs = 0;
	int oddSize = 0;
	int descCount = 0;
	int dstTrSize = 0;
	int srcTrSize = 0;
	uint32_t maxBlockSize = dmacHw_MAX_BLOCKSIZE;
	dmacHw_TRANSACTION_WIDTH_e dstTrWidth;
	dmacHw_TRANSACTION_WIDTH_e srcTrWidth;

	dstTrSize = dmacHw_GetTrWidthInBytes(pConfig->dstMaxTransactionWidth);
	srcTrSize = dmacHw_GetTrWidthInBytes(pConfig->srcMaxTransactionWidth);

	/* Skip Tx if buffer is NULL  or length is unknown */
	if ((pSrcAddr == NULL) || (pDstAddr == NULL) || (dataLen == 0)) {
		/* Do not initiate transfer */
		return -1;
	}

	/* Ensure scatter and gather are transaction aligned */
	if (pConfig->srcGatherWidth % srcTrSize
	    || pConfig->dstScatterWidth % dstTrSize) {
		return -1;
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

	return descCount;
}

/****************************************************************************/
/**
*  @brief   Check the existance of pending descriptor
*
*  This function confirmes if there is any pending descriptor in the chain
*  to program the channel
*
*  @return  1 : Channel need to be programmed with pending descriptor
*           0 : No more pending descriptor to programe the channel
*
*  @note
*     - This function should be called from ISR in case there are pending
*       descriptor to program the channel.
*
*     Example:
*
*     dmac_isr ()
*     {
*         ...
*         if (dmacHw_descriptorPending (handle))
*         {
*            dmacHw_initiateTransfer (handle);
*         }
*     }
*
*/
/****************************************************************************/
uint32_t dmacHw_descriptorPending(dmacHw_HANDLE_t handle,	/*   [ IN ] DMA Channel handle */
				  void *pDescriptor	/*   [ IN ] Descriptor buffer */
    ) {
	dmacHw_CBLK_t *pCblk = dmacHw_HANDLE_TO_CBLK(handle);
	dmacHw_DESC_RING_t *pRing = dmacHw_GET_DESC_RING(pDescriptor);

	/* Make sure channel is not busy */
	if (!CHANNEL_BUSY(pCblk->module, pCblk->channel)) {
		/* Check if pEnd is not processed */
		if (pRing->pEnd) {
			/* Something left for processing */
			return 1;
		}
	}
	return 0;
}

/****************************************************************************/
/**
*  @brief   Program channel register to stop transfer
*
*  Ensures the channel is not doing any transfer after calling this function
*
*  @return  void
*
*/
/****************************************************************************/
void dmacHw_stopTransfer(dmacHw_HANDLE_t handle	/*   [ IN ] DMA Channel handle */
    ) {
	dmacHw_CBLK_t *pCblk;

	pCblk = dmacHw_HANDLE_TO_CBLK(handle);

	/* Stop the channel */
	dmacHw_DMA_STOP(pCblk->module, pCblk->channel);
}

/****************************************************************************/
/**
*  @brief   Deallocates source or destination memory, allocated
*
*  This function can be called to deallocate data memory that was DMAed successfully
*
*  @return  On failure : -1
*           On success : Number of buffer freed
*
*  @note
*     This function will be called ONLY, when source OR destination address is pointing
*     to dynamic memory
*/
/****************************************************************************/
int dmacHw_freeMem(dmacHw_CONFIG_t *pConfig,	/*   [ IN ] Configuration settings */
		   void *pDescriptor,	/*   [ IN ] Descriptor buffer */
		   void (*fpFree) (void *)	/*   [ IN ] Function pointer to free data memory */
    ) {
	dmacHw_DESC_RING_t *pRing = dmacHw_GET_DESC_RING(pDescriptor);
	uint32_t count = 0;

	if (fpFree == NULL) {
		return -1;
	}

	while ((pRing->pFree != pRing->pTail)
	       && (pRing->pFree->ctl.lo & dmacHw_DESC_FREE)) {
		if (pRing->pFree->devCtl == dmacHw_FREE_USER_MEMORY) {
			/* Identify, which memory to free */
			if (dmacHw_DST_IS_MEMORY(pConfig->transferType)) {
				(*fpFree) ((void *)pRing->pFree->dar);
			} else {
				/* Destination was a peripheral */
				(*fpFree) ((void *)pRing->pFree->sar);
			}
			/* Unmark user memory to indicate it is freed */
			pRing->pFree->devCtl = ~dmacHw_FREE_USER_MEMORY;
		}
		dmacHw_NEXT_DESC(pRing, pFree);

		count++;
	}

	return count;
}

/****************************************************************************/
/**
*  @brief   Prepares descriptor ring, when source peripheral working as a flow controller
*
*  This function will update the discriptor ring by allocating buffers, when source peripheral
*  has to work as a flow controller to transfer data from:
*           - Peripheral to memory.
*
*  @return  On failure : -1
*           On success : Number of descriptor updated
*
*
*  @note
*     Channel must be configured for peripheral to memory transfer
*
*/
/****************************************************************************/
int dmacHw_setVariableDataDescriptor(dmacHw_HANDLE_t handle,	/*   [ IN ] DMA Channel handle */
				     dmacHw_CONFIG_t *pConfig,	/*   [ IN ] Configuration settings */
				     void *pDescriptor,	/*   [ IN ] Descriptor buffer */
				     uint32_t srcAddr,	/*   [ IN ] Source peripheral address */
				     void *(*fpAlloc) (int len),	/*   [ IN ] Function pointer  that provides destination memory */
				     int len,	/*   [ IN ] Number of bytes "fpAlloc" will allocate for destination */
				     int num	/*   [ IN ] Number of descriptor to set */
    ) {
	dmacHw_CBLK_t *pCblk = dmacHw_HANDLE_TO_CBLK(handle);
	dmacHw_DESC_t *pProg = NULL;
	dmacHw_DESC_t *pLast = NULL;
	dmacHw_DESC_RING_t *pRing = dmacHw_GET_DESC_RING(pDescriptor);
	uint32_t dstAddr;
	uint32_t controlParam;
	int i;

	dmacHw_ASSERT(pConfig->transferType ==
		      dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM);

	if (num > pRing->num) {
		return -1;
	}

	pLast = pRing->pEnd;	/* Last descriptor updated */
	pProg = pRing->pHead;	/* First descriptor in the new list */

	controlParam = pConfig->srcUpdate |
	    pConfig->dstUpdate |
	    pConfig->srcMaxTransactionWidth |
	    pConfig->dstMaxTransactionWidth |
	    pConfig->srcMasterInterface |
	    pConfig->dstMasterInterface |
	    pConfig->srcMaxBurstWidth |
	    pConfig->dstMaxBurstWidth |
	    dmacHw_REG_CTL_TTFC_PM_PERI |
	    dmacHw_REG_CTL_LLP_DST_EN |
	    dmacHw_REG_CTL_LLP_SRC_EN | dmacHw_REG_CTL_INT_EN;

	for (i = 0; i < num; i++) {
		/* Allocate Rx buffer only for idle descriptor */
		if (((pRing->pHead->ctl.hi & dmacHw_DESC_FREE) == 0) ||
		    ((dmacHw_DESC_t *) pRing->pHead->llp == pRing->pTail)
		    ) {
			/* Rx descriptor is not idle */
			break;
		}
		/* Set source address */
		pRing->pHead->sar = srcAddr;
		if (fpAlloc) {
			/* Allocate memory for buffer in descriptor */
			dstAddr = (uint32_t) (*fpAlloc) (len);
			/* Check the destination address */
			if (dstAddr == 0) {
				if (i == 0) {
					/* Not a single descriptor is available */
					return -1;
				}
				break;
			}
			/* Set destination address */
			pRing->pHead->dar = dstAddr;
		}
		/* Set control information */
		pRing->pHead->ctl.lo = controlParam;
		/* Use "devCtl" to mark the memory that need to be freed later */
		pRing->pHead->devCtl = dmacHw_FREE_USER_MEMORY;
		/* Descriptor is now owned by the channel */
		pRing->pHead->ctl.hi = 0;
		/* Remember the descriptor last updated */
		pRing->pEnd = pRing->pHead;
		/* Update next descriptor */
		dmacHw_NEXT_DESC(pRing, pHead);
	}

	/* Mark the end of the list */
	pRing->pEnd->ctl.lo &=
	    ~(dmacHw_REG_CTL_LLP_DST_EN | dmacHw_REG_CTL_LLP_SRC_EN);
	/* Connect the list */
	if (pLast != pProg) {
		pLast->ctl.lo |=
		    dmacHw_REG_CTL_LLP_DST_EN | dmacHw_REG_CTL_LLP_SRC_EN;
	}
	/* Mark the descriptors are updated */
	pCblk->descUpdated = 1;
	if (!pCblk->varDataStarted) {
		/* LLP must be pointing to the first descriptor */
		dmacHw_SET_LLP(pCblk->module, pCblk->channel,
			       (uint32_t) pProg - pRing->virt2PhyOffset);
		/* Channel, handling variable data started */
		pCblk->varDataStarted = 1;
	}

	return i;
}

/****************************************************************************/
/**
*  @brief   Read data DMAed to memory
*
*  This function will read data that has been DMAed to memory while transfering from:
*          - Memory to memory
*          - Peripheral to memory
*
*  @param    handle     -
*  @param    ppBbuf     -
*  @param    pLen       -
*
*  @return  0 - No more data is available to read
*           1 - More data might be available to read
*
*/
/****************************************************************************/
int dmacHw_readTransferredData(dmacHw_HANDLE_t handle,	/*  [ IN ] DMA Channel handle */
			       dmacHw_CONFIG_t *pConfig,	/*   [ IN ]  Configuration settings */
			       void *pDescriptor,	/*   [ IN ] Descriptor buffer */
			       void **ppBbuf,	/*   [ OUT ] Data received */
			       size_t *pLlen	/*   [ OUT ] Length of the data received */
    ) {
	dmacHw_DESC_RING_t *pRing = dmacHw_GET_DESC_RING(pDescriptor);

	(void)handle;

	if (pConfig->transferMode != dmacHw_TRANSFER_MODE_CONTINUOUS) {
		if (((pRing->pTail->ctl.hi & dmacHw_DESC_FREE) == 0) ||
		    (pRing->pTail == pRing->pHead)
		    ) {
			/* No receive data available */
			*ppBbuf = (char *)NULL;
			*pLlen = 0;

			return 0;
		}
	}

	/* Return read buffer and length */
	*ppBbuf = (char *)pRing->pTail->dar;

	/* Extract length of the received data */
	if (DmaIsFlowController(pDescriptor)) {
		uint32_t srcTrSize = 0;

		switch (pRing->pTail->ctl.lo & dmacHw_REG_CTL_SRC_TR_WIDTH_MASK) {
		case dmacHw_REG_CTL_SRC_TR_WIDTH_8:
			srcTrSize = 1;
			break;
		case dmacHw_REG_CTL_SRC_TR_WIDTH_16:
			srcTrSize = 2;
			break;
		case dmacHw_REG_CTL_SRC_TR_WIDTH_32:
			srcTrSize = 4;
			break;
		case dmacHw_REG_CTL_SRC_TR_WIDTH_64:
			srcTrSize = 8;
			break;
		default:
			dmacHw_ASSERT(0);
		}
		/* Calculate length from the block size */
		*pLlen =
		    (pRing->pTail->ctl.hi & dmacHw_REG_CTL_BLOCK_TS_MASK) *
		    srcTrSize;
	} else {
		/* Extract length from the source peripheral */
		*pLlen = pRing->pTail->sstat;
	}

	/* Advance tail to next descriptor */
	dmacHw_NEXT_DESC(pRing, pTail);

	return 1;
}

/****************************************************************************/
/**
*  @brief   Set descriptor carrying control information
*
*  This function will be used to send specific control information to the device
*  using the DMA channel
*
*
*  @return  -1 - On failure
*            0 - On success
*
*  @note
*     None
*/
/****************************************************************************/
int dmacHw_setControlDescriptor(dmacHw_CONFIG_t *pConfig,	/*   [ IN ] Configuration settings */
				void *pDescriptor,	/*   [ IN ] Descriptor buffer */
				uint32_t ctlAddress,	/*   [ IN ] Address of the device control register */
				uint32_t control	/*   [ IN ] Device control information */
    ) {
	dmacHw_DESC_RING_t *pRing = dmacHw_GET_DESC_RING(pDescriptor);

	if (ctlAddress == 0) {
		return -1;
	}

	/* Check the availability of descriptors in the ring */
	if ((pRing->pHead->ctl.hi & dmacHw_DESC_FREE) == 0) {
		return -1;
	}
	/* Set control information */
	pRing->pHead->devCtl = control;
	/* Set source and destination address */
	pRing->pHead->sar = (uint32_t) &pRing->pHead->devCtl;
	pRing->pHead->dar = ctlAddress;
	/* Set control parameters */
	if (pConfig->flowControler == dmacHw_FLOW_CONTROL_DMA) {
		pRing->pHead->ctl.lo = pConfig->transferType |
		    dmacHw_SRC_ADDRESS_UPDATE_MODE_INC |
		    dmacHw_DST_ADDRESS_UPDATE_MODE_INC |
		    dmacHw_SRC_TRANSACTION_WIDTH_32 |
		    pConfig->dstMaxTransactionWidth |
		    dmacHw_SRC_BURST_WIDTH_0 |
		    dmacHw_DST_BURST_WIDTH_0 |
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
		pRing->pHead->ctl.lo = transferType |
		    dmacHw_SRC_ADDRESS_UPDATE_MODE_INC |
		    dmacHw_DST_ADDRESS_UPDATE_MODE_INC |
		    dmacHw_SRC_TRANSACTION_WIDTH_32 |
		    pConfig->dstMaxTransactionWidth |
		    dmacHw_SRC_BURST_WIDTH_0 |
		    dmacHw_DST_BURST_WIDTH_0 |
		    pConfig->srcMasterInterface |
		    pConfig->dstMasterInterface |
		    pConfig->flowControler | dmacHw_REG_CTL_INT_EN;
	}

	/* Set block transaction size to one 32 bit transaction */
	pRing->pHead->ctl.hi = dmacHw_REG_CTL_BLOCK_TS_MASK & 1;

	/* Remember the descriptor to initialize the registers */
	if (pRing->pProg == dmacHw_DESC_INIT) {
		pRing->pProg = pRing->pHead;
	}
	pRing->pEnd = pRing->pHead;

	/* Advance the descriptor */
	dmacHw_NEXT_DESC(pRing, pHead);

	/* Update Tail pointer if destination is a peripheral */
	if (!dmacHw_DST_IS_MEMORY(pConfig->transferType)) {
		pRing->pTail = pRing->pHead;
	}
	return 0;
}

/****************************************************************************/
/**
*  @brief   Sets channel specific user data
*
*  This function associates user data to a specif DMA channel
*
*/
/****************************************************************************/
void dmacHw_setChannelUserData(dmacHw_HANDLE_t handle,	/*  [ IN ] DMA Channel handle */
			       void *userData	/*  [ IN ] User data */
    ) {
	dmacHw_CBLK_t *pCblk = dmacHw_HANDLE_TO_CBLK(handle);

	pCblk->userData = userData;
}

/****************************************************************************/
/**
*  @brief   Gets channel specific user data
*
*  This function returns user data specific to a DMA channel
*
*  @return   user data
*/
/****************************************************************************/
void *dmacHw_getChannelUserData(dmacHw_HANDLE_t handle	/*  [ IN ] DMA Channel handle */
    ) {
	dmacHw_CBLK_t *pCblk = dmacHw_HANDLE_TO_CBLK(handle);

	return pCblk->userData;
}

/****************************************************************************/
/**
*  @brief   Resets descriptor control information
*
*  @return  void
*/
/****************************************************************************/
void dmacHw_resetDescriptorControl(void *pDescriptor	/*   [ IN ] Descriptor buffer  */
    ) {
	int i;
	dmacHw_DESC_RING_t *pRing;
	dmacHw_DESC_t *pDesc;

	pRing = dmacHw_GET_DESC_RING(pDescriptor);
	pDesc = pRing->pHead;

	for (i = 0; i < pRing->num; i++) {
		/* Mark descriptor is ready to use */
		pDesc->ctl.hi = dmacHw_DESC_FREE;
		/* Look into next link list item */
		pDesc++;
	}
	pRing->pFree = pRing->pTail = pRing->pEnd = pRing->pHead;
	pRing->pProg = dmacHw_DESC_INIT;
}

/****************************************************************************/
/**
*  @brief   Displays channel specific registers and other control parameters
*
*  @return  void
*
*
*  @note
*     None
*/
/****************************************************************************/
void dmacHw_printDebugInfo(dmacHw_HANDLE_t handle,	/*  [ IN ] DMA Channel handle */
			   void *pDescriptor,	/*   [ IN ] Descriptor buffer */
			   int (*fpPrint) (const char *, ...)	/*  [ IN ] Print callback function */
    ) {
	dmacHw_CBLK_t *pCblk = dmacHw_HANDLE_TO_CBLK(handle);

	DisplayRegisterContents(pCblk->module, pCblk->channel, fpPrint);
	DisplayDescRing(pDescriptor, fpPrint);
}
