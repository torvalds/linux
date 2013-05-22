/*
 *  Linux MegaRAID driver for SAS based RAID controllers
 *
 *  Copyright (c) 2009-2012  LSI Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *  FILE: megaraid_sas_fp.c
 *
 *  Authors: LSI Corporation
 *           Sumant Patro
 *           Varad Talamacki
 *           Manoj Jose
 *
 *  Send feedback to: <megaraidlinux@lsi.com>
 *
 *  Mail to: LSI Corporation, 1621 Barber Lane, Milpitas, CA 95035
 *     ATTN: Linuxraid
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/uio.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/compat.h>
#include <linux/blkdev.h>
#include <linux/poll.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "megaraid_sas_fusion.h"
#include "megaraid_sas.h"
#include <asm/div64.h>

#define ABS_DIFF(a, b)   (((a) > (b)) ? ((a) - (b)) : ((b) - (a)))
#define MR_LD_STATE_OPTIMAL 3
#define FALSE 0
#define TRUE 1

/* Prototypes */
void
mr_update_load_balance_params(struct MR_FW_RAID_MAP_ALL *map,
			      struct LD_LOAD_BALANCE_INFO *lbInfo);

u32 mega_mod64(u64 dividend, u32 divisor)
{
	u64 d;
	u32 remainder;

	if (!divisor)
		printk(KERN_ERR "megasas : DIVISOR is zero, in div fn\n");
	d = dividend;
	remainder = do_div(d, divisor);
	return remainder;
}

/**
 * @param dividend    : Dividend
 * @param divisor    : Divisor
 *
 * @return quotient
 **/
u64 mega_div64_32(uint64_t dividend, uint32_t divisor)
{
	u32 remainder;
	u64 d;

	if (!divisor)
		printk(KERN_ERR "megasas : DIVISOR is zero in mod fn\n");

	d = dividend;
	remainder = do_div(d, divisor);

	return d;
}

struct MR_LD_RAID *MR_LdRaidGet(u32 ld, struct MR_FW_RAID_MAP_ALL *map)
{
	return &map->raidMap.ldSpanMap[ld].ldRaid;
}

static struct MR_SPAN_BLOCK_INFO *MR_LdSpanInfoGet(u32 ld,
						   struct MR_FW_RAID_MAP_ALL
						   *map)
{
	return &map->raidMap.ldSpanMap[ld].spanBlock[0];
}

static u8 MR_LdDataArmGet(u32 ld, u32 armIdx, struct MR_FW_RAID_MAP_ALL *map)
{
	return map->raidMap.ldSpanMap[ld].dataArmMap[armIdx];
}

static u16 MR_ArPdGet(u32 ar, u32 arm, struct MR_FW_RAID_MAP_ALL *map)
{
	return map->raidMap.arMapInfo[ar].pd[arm];
}

static u16 MR_LdSpanArrayGet(u32 ld, u32 span, struct MR_FW_RAID_MAP_ALL *map)
{
	return map->raidMap.ldSpanMap[ld].spanBlock[span].span.arrayRef;
}

static u16 MR_PdDevHandleGet(u32 pd, struct MR_FW_RAID_MAP_ALL *map)
{
	return map->raidMap.devHndlInfo[pd].curDevHdl;
}

u16 MR_GetLDTgtId(u32 ld, struct MR_FW_RAID_MAP_ALL *map)
{
	return map->raidMap.ldSpanMap[ld].ldRaid.targetId;
}

u16 MR_TargetIdToLdGet(u32 ldTgtId, struct MR_FW_RAID_MAP_ALL *map)
{
	return map->raidMap.ldTgtIdToLd[ldTgtId];
}

static struct MR_LD_SPAN *MR_LdSpanPtrGet(u32 ld, u32 span,
					  struct MR_FW_RAID_MAP_ALL *map)
{
	return &map->raidMap.ldSpanMap[ld].spanBlock[span].span;
}

/*
 * This function will validate Map info data provided by FW
 */
u8 MR_ValidateMapInfo(struct MR_FW_RAID_MAP_ALL *map,
		      struct LD_LOAD_BALANCE_INFO *lbInfo)
{
	struct MR_FW_RAID_MAP *pFwRaidMap = &map->raidMap;

	if (pFwRaidMap->totalSize !=
	    (sizeof(struct MR_FW_RAID_MAP) -sizeof(struct MR_LD_SPAN_MAP) +
	     (sizeof(struct MR_LD_SPAN_MAP) *pFwRaidMap->ldCount))) {
		printk(KERN_ERR "megasas: map info structure size 0x%x is not matching with ld count\n",
		       (unsigned int)((sizeof(struct MR_FW_RAID_MAP) -
				       sizeof(struct MR_LD_SPAN_MAP)) +
				      (sizeof(struct MR_LD_SPAN_MAP) *
				       pFwRaidMap->ldCount)));
		printk(KERN_ERR "megasas: span map %x, pFwRaidMap->totalSize "
		       ": %x\n", (unsigned int)sizeof(struct MR_LD_SPAN_MAP),
		       pFwRaidMap->totalSize);
		return 0;
	}

	mr_update_load_balance_params(map, lbInfo);

	return 1;
}

u32 MR_GetSpanBlock(u32 ld, u64 row, u64 *span_blk,
		    struct MR_FW_RAID_MAP_ALL *map, int *div_error)
{
	struct MR_SPAN_BLOCK_INFO *pSpanBlock = MR_LdSpanInfoGet(ld, map);
	struct MR_QUAD_ELEMENT    *quad;
	struct MR_LD_RAID         *raid = MR_LdRaidGet(ld, map);
	u32                span, j;

	for (span = 0; span < raid->spanDepth; span++, pSpanBlock++) {

		for (j = 0; j < pSpanBlock->block_span_info.noElements; j++) {
			quad = &pSpanBlock->block_span_info.quad[j];

			if (quad->diff == 0) {
				*div_error = 1;
				return span;
			}
			if (quad->logStart <= row  &&  row <= quad->logEnd  &&
			    (mega_mod64(row-quad->logStart, quad->diff)) == 0) {
				if (span_blk != NULL) {
					u64  blk, debugBlk;
					blk =
						mega_div64_32(
							(row-quad->logStart),
							quad->diff);
					debugBlk = blk;

					blk = (blk + quad->offsetInSpan) <<
						raid->stripeShift;
					*span_blk = blk;
				}
				return span;
			}
		}
	}
	return span;
}

/*
******************************************************************************
*
* This routine calculates the arm, span and block for the specified stripe and
* reference in stripe.
*
* Inputs :
*
*    ld   - Logical drive number
*    stripRow        - Stripe number
*    stripRef    - Reference in stripe
*
* Outputs :
*
*    span          - Span number
*    block         - Absolute Block number in the physical disk
*/
u8 MR_GetPhyParams(struct megasas_instance *instance, u32 ld, u64 stripRow,
		   u16 stripRef, u64 *pdBlock, u16 *pDevHandle,
		   struct RAID_CONTEXT *pRAID_Context,
		   struct MR_FW_RAID_MAP_ALL *map)
{
	struct MR_LD_RAID  *raid = MR_LdRaidGet(ld, map);
	u32         pd, arRef;
	u8          physArm, span;
	u64         row;
	u8	    retval = TRUE;
	int	    error_code = 0;
	u8          do_invader = 0;

	if ((instance->pdev->device == PCI_DEVICE_ID_LSI_INVADER ||
		instance->pdev->device == PCI_DEVICE_ID_LSI_FURY))
		do_invader = 1;

	row =  mega_div64_32(stripRow, raid->rowDataSize);

	if (raid->level == 6) {
		/* logical arm within row */
		u32 logArm =  mega_mod64(stripRow, raid->rowDataSize);
		u32 rowMod, armQ, arm;

		if (raid->rowSize == 0)
			return FALSE;
		/* get logical row mod */
		rowMod = mega_mod64(row, raid->rowSize);
		armQ = raid->rowSize-1-rowMod; /* index of Q drive */
		arm = armQ+1+logArm; /* data always logically follows Q */
		if (arm >= raid->rowSize) /* handle wrap condition */
			arm -= raid->rowSize;
		physArm = (u8)arm;
	} else  {
		if (raid->modFactor == 0)
			return FALSE;
		physArm = MR_LdDataArmGet(ld,  mega_mod64(stripRow,
							  raid->modFactor),
					  map);
	}

	if (raid->spanDepth == 1) {
		span = 0;
		*pdBlock = row << raid->stripeShift;
	} else {
		span = (u8)MR_GetSpanBlock(ld, row, pdBlock, map, &error_code);
		if (error_code == 1)
			return FALSE;
	}

	/* Get the array on which this span is present */
	arRef       = MR_LdSpanArrayGet(ld, span, map);
	pd          = MR_ArPdGet(arRef, physArm, map); /* Get the pd */

	if (pd != MR_PD_INVALID)
		/* Get dev handle from Pd. */
		*pDevHandle = MR_PdDevHandleGet(pd, map);
	else {
		*pDevHandle = MR_PD_INVALID; /* set dev handle as invalid. */
		if ((raid->level >= 5) &&
			(!do_invader  || (do_invader &&
			(raid->regTypeReqOnRead != REGION_TYPE_UNUSED))))
			pRAID_Context->regLockFlags = REGION_TYPE_EXCLUSIVE;
		else if (raid->level == 1) {
			/* Get alternate Pd. */
			pd = MR_ArPdGet(arRef, physArm + 1, map);
			if (pd != MR_PD_INVALID)
				/* Get dev handle from Pd */
				*pDevHandle = MR_PdDevHandleGet(pd, map);
		}
	}

	*pdBlock += stripRef + MR_LdSpanPtrGet(ld, span, map)->startBlk;
	pRAID_Context->spanArm = (span << RAID_CTX_SPANARM_SPAN_SHIFT) |
		physArm;
	return retval;
}

/*
******************************************************************************
*
* MR_BuildRaidContext function
*
* This function will initiate command processing.  The start/end row and strip
* information is calculated then the lock is acquired.
* This function will return 0 if region lock was acquired OR return num strips
*/
u8
MR_BuildRaidContext(struct megasas_instance *instance,
		    struct IO_REQUEST_INFO *io_info,
		    struct RAID_CONTEXT *pRAID_Context,
		    struct MR_FW_RAID_MAP_ALL *map)
{
	struct MR_LD_RAID  *raid;
	u32         ld, stripSize, stripe_mask;
	u64         endLba, endStrip, endRow, start_row, start_strip;
	u64         regStart;
	u32         regSize;
	u8          num_strips, numRows;
	u16         ref_in_start_stripe, ref_in_end_stripe;
	u64         ldStartBlock;
	u32         numBlocks, ldTgtId;
	u8          isRead;
	u8	    retval = 0;

	ldStartBlock = io_info->ldStartBlock;
	numBlocks = io_info->numBlocks;
	ldTgtId = io_info->ldTgtId;
	isRead = io_info->isRead;

	ld = MR_TargetIdToLdGet(ldTgtId, map);
	raid = MR_LdRaidGet(ld, map);

	stripSize = 1 << raid->stripeShift;
	stripe_mask = stripSize-1;
	/*
	 * calculate starting row and stripe, and number of strips and rows
	 */
	start_strip         = ldStartBlock >> raid->stripeShift;
	ref_in_start_stripe = (u16)(ldStartBlock & stripe_mask);
	endLba              = ldStartBlock + numBlocks - 1;
	ref_in_end_stripe   = (u16)(endLba & stripe_mask);
	endStrip            = endLba >> raid->stripeShift;
	num_strips          = (u8)(endStrip - start_strip + 1); /* End strip */
	if (raid->rowDataSize == 0)
		return FALSE;
	start_row           =  mega_div64_32(start_strip, raid->rowDataSize);
	endRow              =  mega_div64_32(endStrip, raid->rowDataSize);
	numRows             = (u8)(endRow - start_row + 1);

	/*
	 * calculate region info.
	 */

	/* assume region is at the start of the first row */
	regStart            = start_row << raid->stripeShift;
	/* assume this IO needs the full row - we'll adjust if not true */
	regSize             = stripSize;

	/* Check if we can send this I/O via FastPath */
	if (raid->capability.fpCapable) {
		if (isRead)
			io_info->fpOkForIo = (raid->capability.fpReadCapable &&
					      ((num_strips == 1) ||
					       raid->capability.
					       fpReadAcrossStripe));
		else
			io_info->fpOkForIo = (raid->capability.fpWriteCapable &&
					      ((num_strips == 1) ||
					       raid->capability.
					       fpWriteAcrossStripe));
	} else
		io_info->fpOkForIo = FALSE;

	if (numRows == 1) {
		/* single-strip IOs can always lock only the data needed */
		if (num_strips == 1) {
			regStart += ref_in_start_stripe;
			regSize = numBlocks;
		}
		/* multi-strip IOs always need to full stripe locked */
	} else {
		if (start_strip == (start_row + 1) * raid->rowDataSize - 1) {
			/* If the start strip is the last in the start row */
			regStart += ref_in_start_stripe;
			regSize = stripSize - ref_in_start_stripe;
			/* initialize count to sectors from startref to end
			   of strip */
		}

		if (numRows > 2)
			/* Add complete rows in the middle of the transfer */
			regSize += (numRows-2) << raid->stripeShift;

		/* if IO ends within first strip of last row */
		if (endStrip == endRow*raid->rowDataSize)
			regSize += ref_in_end_stripe+1;
		else
			regSize += stripSize;
	}

	pRAID_Context->timeoutValue     = map->raidMap.fpPdIoTimeoutSec;
	if ((instance->pdev->device == PCI_DEVICE_ID_LSI_INVADER) ||
		(instance->pdev->device == PCI_DEVICE_ID_LSI_FURY))
		pRAID_Context->regLockFlags = (isRead) ?
			raid->regTypeReqOnRead : raid->regTypeReqOnWrite;
	else
		pRAID_Context->regLockFlags = (isRead) ?
			REGION_TYPE_SHARED_READ : raid->regTypeReqOnWrite;
	pRAID_Context->VirtualDiskTgtId = raid->targetId;
	pRAID_Context->regLockRowLBA    = regStart;
	pRAID_Context->regLockLength    = regSize;
	pRAID_Context->configSeqNum	= raid->seqNum;

	/*Get Phy Params only if FP capable, or else leave it to MR firmware
	  to do the calculation.*/
	if (io_info->fpOkForIo) {
		retval = MR_GetPhyParams(instance, ld, start_strip,
					 ref_in_start_stripe,
					 &io_info->pdBlock,
					 &io_info->devHandle, pRAID_Context,
					 map);
		/* If IO on an invalid Pd, then FP i snot possible */
		if (io_info->devHandle == MR_PD_INVALID)
			io_info->fpOkForIo = FALSE;
		return retval;
	} else if (isRead) {
		uint stripIdx;
		for (stripIdx = 0; stripIdx < num_strips; stripIdx++) {
			if (!MR_GetPhyParams(instance, ld,
					     start_strip + stripIdx,
					     ref_in_start_stripe,
					     &io_info->pdBlock,
					     &io_info->devHandle,
					     pRAID_Context, map))
				return TRUE;
		}
	}
	return TRUE;
}

void
mr_update_load_balance_params(struct MR_FW_RAID_MAP_ALL *map,
			      struct LD_LOAD_BALANCE_INFO *lbInfo)
{
	int ldCount;
	u16 ld;
	struct MR_LD_RAID *raid;

	for (ldCount = 0; ldCount < MAX_LOGICAL_DRIVES; ldCount++) {
		ld = MR_TargetIdToLdGet(ldCount, map);
		if (ld >= MAX_LOGICAL_DRIVES) {
			lbInfo[ldCount].loadBalanceFlag = 0;
			continue;
		}

		raid = MR_LdRaidGet(ld, map);

		/* Two drive Optimal RAID 1 */
		if ((raid->level == 1)  &&  (raid->rowSize == 2) &&
		    (raid->spanDepth == 1) && raid->ldState ==
		    MR_LD_STATE_OPTIMAL) {
			u32 pd, arRef;

			lbInfo[ldCount].loadBalanceFlag = 1;

			/* Get the array on which this span is present */
			arRef = MR_LdSpanArrayGet(ld, 0, map);

			/* Get the Pd */
			pd = MR_ArPdGet(arRef, 0, map);
			/* Get dev handle from Pd */
			lbInfo[ldCount].raid1DevHandle[0] =
				MR_PdDevHandleGet(pd, map);
			/* Get the Pd */
			pd = MR_ArPdGet(arRef, 1, map);

			/* Get the dev handle from Pd */
			lbInfo[ldCount].raid1DevHandle[1] =
				MR_PdDevHandleGet(pd, map);
		} else
			lbInfo[ldCount].loadBalanceFlag = 0;
	}
}

u8 megasas_get_best_arm(struct LD_LOAD_BALANCE_INFO *lbInfo, u8 arm, u64 block,
			u32 count)
{
	u16     pend0, pend1;
	u64     diff0, diff1;
	u8      bestArm;

	/* get the pending cmds for the data and mirror arms */
	pend0 = atomic_read(&lbInfo->scsi_pending_cmds[0]);
	pend1 = atomic_read(&lbInfo->scsi_pending_cmds[1]);

	/* Determine the disk whose head is nearer to the req. block */
	diff0 = ABS_DIFF(block, lbInfo->last_accessed_block[0]);
	diff1 = ABS_DIFF(block, lbInfo->last_accessed_block[1]);
	bestArm = (diff0 <= diff1 ? 0 : 1);

	/*Make balance count from 16 to 4 to keep driver in sync with Firmware*/
	if ((bestArm == arm && pend0 > pend1 + 4)  ||
	    (bestArm != arm && pend1 > pend0 + 4))
		bestArm ^= 1;

	/* Update the last accessed block on the correct pd */
	lbInfo->last_accessed_block[bestArm] = block + count - 1;

	return bestArm;
}

u16 get_updated_dev_handle(struct LD_LOAD_BALANCE_INFO *lbInfo,
			   struct IO_REQUEST_INFO *io_info)
{
	u8 arm, old_arm;
	u16 devHandle;

	old_arm = lbInfo->raid1DevHandle[0] == io_info->devHandle ? 0 : 1;

	/* get best new arm */
	arm  = megasas_get_best_arm(lbInfo, old_arm, io_info->ldStartBlock,
				    io_info->numBlocks);
	devHandle = lbInfo->raid1DevHandle[arm];
	atomic_inc(&lbInfo->scsi_pending_cmds[arm]);

	return devHandle;
}
