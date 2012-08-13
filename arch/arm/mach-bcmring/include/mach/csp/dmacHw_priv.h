/*****************************************************************************
* Copyright 2004 - 2008 Broadcom Corporation.  All rights reserved.
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
*  @file    dmacHw_priv.h
*
*  @brief   Private Definitions for low level DMA driver
*
*/
/****************************************************************************/

#ifndef _DMACHW_PRIV_H
#define _DMACHW_PRIV_H

#include <linux/types.h>

/* Data type for DMA Link List Item */
typedef struct {
	uint32_t sar;		/* Source Address Register.
				   Address must be aligned to CTLx.SRC_TR_WIDTH.             */
	uint32_t dar;		/* Destination Address Register.
				   Address must be aligned to CTLx.DST_TR_WIDTH.             */
	uint32_t llpPhy;	/* LLP contains the physical address of the next descriptor for block chaining using linked lists.
				   Address MUST be aligned to a 32-bit boundary.             */
	dmacHw_REG64_t ctl;	/* Control Register. 64 bits */
	uint32_t sstat;		/* Source Status Register */
	uint32_t dstat;		/* Destination Status Register */
	uint32_t devCtl;	/* Device specific control information */
	uint32_t llp;		/* LLP contains the virtual address of the next descriptor for block chaining using linked lists. */
} dmacHw_DESC_t;

/*
 *  Descriptor ring pointers
 */
typedef struct {
	int num;		/* Number of link items */
	dmacHw_DESC_t *pHead;	/* Head of descriptor ring (for writing) */
	dmacHw_DESC_t *pTail;	/* Tail of descriptor ring (for reading) */
	dmacHw_DESC_t *pProg;	/* Descriptor to program the channel (for programming the channel register) */
	dmacHw_DESC_t *pEnd;	/* End of current descriptor chain */
	dmacHw_DESC_t *pFree;	/* Descriptor to free memory (freeing dynamic memory) */
	uint32_t virt2PhyOffset;	/* Virtual to physical address offset for the descriptor ring */
} dmacHw_DESC_RING_t;

/*
 *  DMA channel control block
 */
typedef struct {
	uint32_t module;	/* DMA controller module (0-1) */
	uint32_t channel;	/* DMA channel (0-7) */
	volatile uint32_t varDataStarted;	/* Flag indicating variable data channel is enabled */
	volatile uint32_t descUpdated;	/* Flag to indicate descriptor update is complete */
	void *userData;		/* Channel specifc user data */
} dmacHw_CBLK_t;

#define dmacHw_ASSERT(a)                  if (!(a)) while (1)
#define dmacHw_MAX_CHANNEL_COUNT          16
#define dmacHw_FREE_USER_MEMORY           0xFFFFFFFF
#define dmacHw_DESC_FREE                  dmacHw_REG_CTL_DONE
#define dmacHw_DESC_INIT                  ((dmacHw_DESC_t *) 0xFFFFFFFF)
#define dmacHw_MAX_BLOCKSIZE              4064
#define dmacHw_GET_DESC_RING(addr)        (dmacHw_DESC_RING_t *)(addr)
#define dmacHw_ADDRESS_MASK(byte)         ((byte) - 1)
#define dmacHw_NEXT_DESC(rp, dp)           ((rp)->dp = (dmacHw_DESC_t *)(rp)->dp->llp)
#define dmacHw_HANDLE_TO_CBLK(handle)     ((dmacHw_CBLK_t *) (handle))
#define dmacHw_CBLK_TO_HANDLE(cblkp)      ((dmacHw_HANDLE_t) (cblkp))
#define dmacHw_DST_IS_MEMORY(tt)          (((tt) ==  dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM) || ((tt) == dmacHw_TRANSFER_TYPE_MEM_TO_MEM)) ? 1 : 0

/****************************************************************************/
/**
*  @brief   Get next available transaction width
*
*
*  @return  On success  : Next available transaction width
*           On failure : dmacHw_TRANSACTION_WIDTH_8
*
*  @note
*     None
*/
/****************************************************************************/
static inline dmacHw_TRANSACTION_WIDTH_e dmacHw_GetNextTrWidth(dmacHw_TRANSACTION_WIDTH_e tw	/*   [ IN ] Current transaction width */
    ) {
	if (tw & dmacHw_REG_CTL_SRC_TR_WIDTH_MASK) {
		return ((tw >> dmacHw_REG_CTL_SRC_TR_WIDTH_SHIFT) -
			 1) << dmacHw_REG_CTL_SRC_TR_WIDTH_SHIFT;
	} else if (tw & dmacHw_REG_CTL_DST_TR_WIDTH_MASK) {
		return ((tw >> dmacHw_REG_CTL_DST_TR_WIDTH_SHIFT) -
			 1) << dmacHw_REG_CTL_DST_TR_WIDTH_SHIFT;
	}

	/* Default return  */
	return dmacHw_SRC_TRANSACTION_WIDTH_8;
}

/****************************************************************************/
/**
*  @brief   Get number of bytes per transaction
*
*  @return  Number of bytes per transaction
*
*
*  @note
*     None
*/
/****************************************************************************/
static inline int dmacHw_GetTrWidthInBytes(dmacHw_TRANSACTION_WIDTH_e tw	/*   [ IN ]  Transaction width */
    ) {
	int width = 1;
	switch (tw) {
	case dmacHw_SRC_TRANSACTION_WIDTH_8:
		width = 1;
		break;
	case dmacHw_SRC_TRANSACTION_WIDTH_16:
	case dmacHw_DST_TRANSACTION_WIDTH_16:
		width = 2;
		break;
	case dmacHw_SRC_TRANSACTION_WIDTH_32:
	case dmacHw_DST_TRANSACTION_WIDTH_32:
		width = 4;
		break;
	case dmacHw_SRC_TRANSACTION_WIDTH_64:
	case dmacHw_DST_TRANSACTION_WIDTH_64:
		width = 8;
		break;
	default:
		dmacHw_ASSERT(0);
	}

	/* Default transaction width */
	return width;
}

#endif /* _DMACHW_PRIV_H */
