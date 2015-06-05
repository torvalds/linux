/*
 *  Linux MegaRAID driver for SAS based RAID controllers
 *
 *  Copyright (c) 2009-2013  LSI Corporation
 *  Copyright (c) 2013-2014  Avago Technologies
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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  FILE: megaraid_sas_fp.c
 *
 *  Authors: Avago Technologies
 *           Sumant Patro
 *           Varad Talamacki
 *           Manoj Jose
 *           Kashyap Desai <kashyap.desai@avagotech.com>
 *           Sumit Saxena <sumit.saxena@avagotech.com>
 *
 *  Send feedback to: megaraidlinux.pdl@avagotech.com
 *
 *  Mail to: Avago Technologies, 350 West Trimble Road, Building 90,
 *  San Jose, California 95131
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

#define LB_PENDING_CMDS_DEFAULT 4
static unsigned int lb_pending_cmds = LB_PENDING_CMDS_DEFAULT;
module_param(lb_pending_cmds, int, S_IRUGO);
MODULE_PARM_DESC(lb_pending_cmds, "Change raid-1 load balancing outstanding "
	"threshold. Valid Values are 1-128. Default: 4");


#define ABS_DIFF(a, b)   (((a) > (b)) ? ((a) - (b)) : ((b) - (a)))
#define MR_LD_STATE_OPTIMAL 3
#define FALSE 0
#define TRUE 1

#define SPAN_DEBUG 0
#define SPAN_ROW_SIZE(map, ld, index_) (MR_LdSpanPtrGet(ld, index_, map)->spanRowSize)
#define SPAN_ROW_DATA_SIZE(map_, ld, index_)   (MR_LdSpanPtrGet(ld, index_, map)->spanRowDataSize)
#define SPAN_INVALID  0xff

/* Prototypes */
static void mr_update_span_set(struct MR_DRV_RAID_MAP_ALL *map,
	PLD_SPAN_INFO ldSpanInfo);
static u8 mr_spanset_get_phy_params(struct megasas_instance *instance, u32 ld,
	u64 stripRow, u16 stripRef, struct IO_REQUEST_INFO *io_info,
	struct RAID_CONTEXT *pRAID_Context, struct MR_DRV_RAID_MAP_ALL *map);
static u64 get_row_from_strip(struct megasas_instance *instance, u32 ld,
	u64 strip, struct MR_DRV_RAID_MAP_ALL *map);

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

struct MR_LD_RAID *MR_LdRaidGet(u32 ld, struct MR_DRV_RAID_MAP_ALL *map)
{
	return &map->raidMap.ldSpanMap[ld].ldRaid;
}

static struct MR_SPAN_BLOCK_INFO *MR_LdSpanInfoGet(u32 ld,
						   struct MR_DRV_RAID_MAP_ALL
						   *map)
{
	return &map->raidMap.ldSpanMap[ld].spanBlock[0];
}

static u8 MR_LdDataArmGet(u32 ld, u32 armIdx, struct MR_DRV_RAID_MAP_ALL *map)
{
	return map->raidMap.ldSpanMap[ld].dataArmMap[armIdx];
}

u16 MR_ArPdGet(u32 ar, u32 arm, struct MR_DRV_RAID_MAP_ALL *map)
{
	return le16_to_cpu(map->raidMap.arMapInfo[ar].pd[arm]);
}

u16 MR_LdSpanArrayGet(u32 ld, u32 span, struct MR_DRV_RAID_MAP_ALL *map)
{
	return le16_to_cpu(map->raidMap.ldSpanMap[ld].spanBlock[span].span.arrayRef);
}

u16 MR_PdDevHandleGet(u32 pd, struct MR_DRV_RAID_MAP_ALL *map)
{
	return map->raidMap.devHndlInfo[pd].curDevHdl;
}

u16 MR_GetLDTgtId(u32 ld, struct MR_DRV_RAID_MAP_ALL *map)
{
	return le16_to_cpu(map->raidMap.ldSpanMap[ld].ldRaid.targetId);
}

u8 MR_TargetIdToLdGet(u32 ldTgtId, struct MR_DRV_RAID_MAP_ALL *map)
{
	return map->raidMap.ldTgtIdToLd[ldTgtId];
}

static struct MR_LD_SPAN *MR_LdSpanPtrGet(u32 ld, u32 span,
					  struct MR_DRV_RAID_MAP_ALL *map)
{
	return &map->raidMap.ldSpanMap[ld].spanBlock[span].span;
}

/*
 * This function will Populate Driver Map using firmware raid map
 */
void MR_PopulateDrvRaidMap(struct megasas_instance *instance)
{
	struct fusion_context *fusion = instance->ctrl_context;
	struct MR_FW_RAID_MAP_ALL     *fw_map_old    = NULL;
	struct MR_FW_RAID_MAP         *pFwRaidMap    = NULL;
	int i;
	u16 ld_count;


	struct MR_DRV_RAID_MAP_ALL *drv_map =
			fusion->ld_drv_map[(instance->map_id & 1)];
	struct MR_DRV_RAID_MAP *pDrvRaidMap = &drv_map->raidMap;

	if (instance->supportmax256vd) {
		memcpy(fusion->ld_drv_map[instance->map_id & 1],
			fusion->ld_map[instance->map_id & 1],
			fusion->current_map_sz);
		/* New Raid map will not set totalSize, so keep expected value
		 * for legacy code in ValidateMapInfo
		 */
		pDrvRaidMap->totalSize =
			cpu_to_le32(sizeof(struct MR_FW_RAID_MAP_EXT));
	} else {
		fw_map_old = (struct MR_FW_RAID_MAP_ALL *)
			fusion->ld_map[(instance->map_id & 1)];
		pFwRaidMap = &fw_map_old->raidMap;
		ld_count = (u16)le32_to_cpu(pFwRaidMap->ldCount);

#if VD_EXT_DEBUG
		for (i = 0; i < ld_count; i++) {
			dev_dbg(&instance->pdev->dev, "(%d) :Index 0x%x "
				"Target Id 0x%x Seq Num 0x%x Size 0/%llx\n",
				instance->unique_id, i,
				fw_map_old->raidMap.ldSpanMap[i].ldRaid.targetId,
				fw_map_old->raidMap.ldSpanMap[i].ldRaid.seqNum,
				fw_map_old->raidMap.ldSpanMap[i].ldRaid.size);
		}
#endif

		memset(drv_map, 0, fusion->drv_map_sz);
		pDrvRaidMap->totalSize = pFwRaidMap->totalSize;
		pDrvRaidMap->ldCount = (__le16)cpu_to_le16(ld_count);
		pDrvRaidMap->fpPdIoTimeoutSec = pFwRaidMap->fpPdIoTimeoutSec;
		for (i = 0; i < MAX_RAIDMAP_LOGICAL_DRIVES + MAX_RAIDMAP_VIEWS; i++)
			pDrvRaidMap->ldTgtIdToLd[i] =
				(u8)pFwRaidMap->ldTgtIdToLd[i];
		for (i = (MAX_RAIDMAP_LOGICAL_DRIVES + MAX_RAIDMAP_VIEWS);
			i < MAX_LOGICAL_DRIVES_EXT; i++)
			pDrvRaidMap->ldTgtIdToLd[i] = 0xff;
		for (i = 0; i < ld_count; i++) {
			pDrvRaidMap->ldSpanMap[i] = pFwRaidMap->ldSpanMap[i];
#if VD_EXT_DEBUG
			dev_dbg(&instance->pdev->dev,
				"pFwRaidMap->ldSpanMap[%d].ldRaid.targetId 0x%x "
				"pFwRaidMap->ldSpanMap[%d].ldRaid.seqNum 0x%x "
				"size 0x%x\n", i, i,
				pFwRaidMap->ldSpanMap[i].ldRaid.targetId,
				pFwRaidMap->ldSpanMap[i].ldRaid.seqNum,
				(u32)pFwRaidMap->ldSpanMap[i].ldRaid.rowSize);
			dev_dbg(&instance->pdev->dev,
				"pDrvRaidMap->ldSpanMap[%d].ldRaid.targetId 0x%x "
				"pDrvRaidMap->ldSpanMap[%d].ldRaid.seqNum 0x%x "
				"size 0x%x\n", i, i,
				pDrvRaidMap->ldSpanMap[i].ldRaid.targetId,
				pDrvRaidMap->ldSpanMap[i].ldRaid.seqNum,
				(u32)pDrvRaidMap->ldSpanMap[i].ldRaid.rowSize);
			dev_dbg(&instance->pdev->dev, "Driver raid map all %p "
				"raid map %p LD RAID MAP %p/%p\n", drv_map,
				pDrvRaidMap, &pFwRaidMap->ldSpanMap[i].ldRaid,
				&pDrvRaidMap->ldSpanMap[i].ldRaid);
#endif
		}
		memcpy(pDrvRaidMap->arMapInfo, pFwRaidMap->arMapInfo,
			sizeof(struct MR_ARRAY_INFO) * MAX_RAIDMAP_ARRAYS);
		memcpy(pDrvRaidMap->devHndlInfo, pFwRaidMap->devHndlInfo,
			sizeof(struct MR_DEV_HANDLE_INFO) *
			MAX_RAIDMAP_PHYSICAL_DEVICES);
	}
}

/*
 * This function will validate Map info data provided by FW
 */
u8 MR_ValidateMapInfo(struct megasas_instance *instance)
{
	struct fusion_context *fusion;
	struct MR_DRV_RAID_MAP_ALL *drv_map;
	struct MR_DRV_RAID_MAP *pDrvRaidMap;
	struct LD_LOAD_BALANCE_INFO *lbInfo;
	PLD_SPAN_INFO ldSpanInfo;
	struct MR_LD_RAID         *raid;
	u16 ldCount, num_lds;
	u16 ld;
	u32 expected_size;


	MR_PopulateDrvRaidMap(instance);

	fusion = instance->ctrl_context;
	drv_map = fusion->ld_drv_map[(instance->map_id & 1)];
	pDrvRaidMap = &drv_map->raidMap;

	lbInfo = fusion->load_balance_info;
	ldSpanInfo = fusion->log_to_span;

	if (instance->supportmax256vd)
		expected_size = sizeof(struct MR_FW_RAID_MAP_EXT);
	else
		expected_size =
			(sizeof(struct MR_FW_RAID_MAP) - sizeof(struct MR_LD_SPAN_MAP) +
			(sizeof(struct MR_LD_SPAN_MAP) * le16_to_cpu(pDrvRaidMap->ldCount)));

	if (le32_to_cpu(pDrvRaidMap->totalSize) != expected_size) {
		dev_err(&instance->pdev->dev, "map info structure size 0x%x is not matching with ld count\n",
		       (unsigned int) expected_size);
		dev_err(&instance->pdev->dev, "megasas: span map %x, pDrvRaidMap->totalSize : %x\n",
			(unsigned int)sizeof(struct MR_LD_SPAN_MAP),
			le32_to_cpu(pDrvRaidMap->totalSize));
		return 0;
	}

	if (instance->UnevenSpanSupport)
		mr_update_span_set(drv_map, ldSpanInfo);

	mr_update_load_balance_params(drv_map, lbInfo);

	num_lds = le16_to_cpu(drv_map->raidMap.ldCount);

	/*Convert Raid capability values to CPU arch */
	for (ldCount = 0; ldCount < num_lds; ldCount++) {
		ld = MR_TargetIdToLdGet(ldCount, drv_map);
		raid = MR_LdRaidGet(ld, drv_map);
		le32_to_cpus((u32 *)&raid->capability);
	}

	return 1;
}

u32 MR_GetSpanBlock(u32 ld, u64 row, u64 *span_blk,
		    struct MR_DRV_RAID_MAP_ALL *map)
{
	struct MR_SPAN_BLOCK_INFO *pSpanBlock = MR_LdSpanInfoGet(ld, map);
	struct MR_QUAD_ELEMENT    *quad;
	struct MR_LD_RAID         *raid = MR_LdRaidGet(ld, map);
	u32                span, j;

	for (span = 0; span < raid->spanDepth; span++, pSpanBlock++) {

		for (j = 0; j < le32_to_cpu(pSpanBlock->block_span_info.noElements); j++) {
			quad = &pSpanBlock->block_span_info.quad[j];

			if (le32_to_cpu(quad->diff) == 0)
				return SPAN_INVALID;
			if (le64_to_cpu(quad->logStart) <= row && row <=
				le64_to_cpu(quad->logEnd) && (mega_mod64(row - le64_to_cpu(quad->logStart),
				le32_to_cpu(quad->diff))) == 0) {
				if (span_blk != NULL) {
					u64  blk, debugBlk;
					blk =  mega_div64_32((row-le64_to_cpu(quad->logStart)), le32_to_cpu(quad->diff));
					debugBlk = blk;

					blk = (blk + le64_to_cpu(quad->offsetInSpan)) << raid->stripeShift;
					*span_blk = blk;
				}
				return span;
			}
		}
	}
	return SPAN_INVALID;
}

/*
******************************************************************************
*
* Function to print info about span set created in driver from FW raid map
*
* Inputs :
* map    - LD map
* ldSpanInfo - ldSpanInfo per HBA instance
*/
#if SPAN_DEBUG
static int getSpanInfo(struct MR_DRV_RAID_MAP_ALL *map,
	PLD_SPAN_INFO ldSpanInfo)
{

	u8   span;
	u32    element;
	struct MR_LD_RAID *raid;
	LD_SPAN_SET *span_set;
	struct MR_QUAD_ELEMENT    *quad;
	int ldCount;
	u16 ld;

	for (ldCount = 0; ldCount < MAX_LOGICAL_DRIVES_EXT; ldCount++) {
		ld = MR_TargetIdToLdGet(ldCount, map);
			if (ld >= (MAX_LOGICAL_DRIVES_EXT - 1))
				continue;
		raid = MR_LdRaidGet(ld, map);
		dev_dbg(&instance->pdev->dev, "LD %x: span_depth=%x\n",
			ld, raid->spanDepth);
		for (span = 0; span < raid->spanDepth; span++)
			dev_dbg(&instance->pdev->dev, "Span=%x,"
			" number of quads=%x\n", span,
			le32_to_cpu(map->raidMap.ldSpanMap[ld].spanBlock[span].
			block_span_info.noElements));
		for (element = 0; element < MAX_QUAD_DEPTH; element++) {
			span_set = &(ldSpanInfo[ld].span_set[element]);
			if (span_set->span_row_data_width == 0)
				break;

			dev_dbg(&instance->pdev->dev, "Span Set %x:"
				"width=%x, diff=%x\n", element,
				(unsigned int)span_set->span_row_data_width,
				(unsigned int)span_set->diff);
			dev_dbg(&instance->pdev->dev, "logical LBA"
				"start=0x%08lx, end=0x%08lx\n",
				(long unsigned int)span_set->log_start_lba,
				(long unsigned int)span_set->log_end_lba);
			dev_dbg(&instance->pdev->dev, "span row start=0x%08lx,"
				" end=0x%08lx\n",
				(long unsigned int)span_set->span_row_start,
				(long unsigned int)span_set->span_row_end);
			dev_dbg(&instance->pdev->dev, "data row start=0x%08lx,"
				" end=0x%08lx\n",
				(long unsigned int)span_set->data_row_start,
				(long unsigned int)span_set->data_row_end);
			dev_dbg(&instance->pdev->dev, "data strip start=0x%08lx,"
				" end=0x%08lx\n",
				(long unsigned int)span_set->data_strip_start,
				(long unsigned int)span_set->data_strip_end);

			for (span = 0; span < raid->spanDepth; span++) {
				if (le32_to_cpu(map->raidMap.ldSpanMap[ld].spanBlock[span].
					block_span_info.noElements) >=
					element + 1) {
					quad = &map->raidMap.ldSpanMap[ld].
						spanBlock[span].block_span_info.
						quad[element];
				dev_dbg(&instance->pdev->dev, "Span=%x,"
					"Quad=%x, diff=%x\n", span,
					element, le32_to_cpu(quad->diff));
				dev_dbg(&instance->pdev->dev,
					"offset_in_span=0x%08lx\n",
					(long unsigned int)le64_to_cpu(quad->offsetInSpan));
				dev_dbg(&instance->pdev->dev,
					"logical start=0x%08lx, end=0x%08lx\n",
					(long unsigned int)le64_to_cpu(quad->logStart),
					(long unsigned int)le64_to_cpu(quad->logEnd));
				}
			}
		}
	}
	return 0;
}
#endif

/*
******************************************************************************
*
* This routine calculates the Span block for given row using spanset.
*
* Inputs :
*    instance - HBA instance
*    ld   - Logical drive number
*    row        - Row number
*    map    - LD map
*
* Outputs :
*
*    span          - Span number
*    block         - Absolute Block number in the physical disk
*    div_error	   - Devide error code.
*/

u32 mr_spanset_get_span_block(struct megasas_instance *instance,
		u32 ld, u64 row, u64 *span_blk, struct MR_DRV_RAID_MAP_ALL *map)
{
	struct fusion_context *fusion = instance->ctrl_context;
	struct MR_LD_RAID         *raid = MR_LdRaidGet(ld, map);
	LD_SPAN_SET *span_set;
	struct MR_QUAD_ELEMENT    *quad;
	u32    span, info;
	PLD_SPAN_INFO ldSpanInfo = fusion->log_to_span;

	for (info = 0; info < MAX_QUAD_DEPTH; info++) {
		span_set = &(ldSpanInfo[ld].span_set[info]);

		if (span_set->span_row_data_width == 0)
			break;

		if (row > span_set->data_row_end)
			continue;

		for (span = 0; span < raid->spanDepth; span++)
			if (le32_to_cpu(map->raidMap.ldSpanMap[ld].spanBlock[span].
				block_span_info.noElements) >= info+1) {
				quad = &map->raidMap.ldSpanMap[ld].
					spanBlock[span].
					block_span_info.quad[info];
				if (le32_to_cpu(quad->diff) == 0)
					return SPAN_INVALID;
				if (le64_to_cpu(quad->logStart) <= row  &&
					row <= le64_to_cpu(quad->logEnd)  &&
					(mega_mod64(row - le64_to_cpu(quad->logStart),
						le32_to_cpu(quad->diff))) == 0) {
					if (span_blk != NULL) {
						u64  blk;
						blk = mega_div64_32
						    ((row - le64_to_cpu(quad->logStart)),
						    le32_to_cpu(quad->diff));
						blk = (blk + le64_to_cpu(quad->offsetInSpan))
							 << raid->stripeShift;
						*span_blk = blk;
					}
					return span;
				}
			}
	}
	return SPAN_INVALID;
}

/*
******************************************************************************
*
* This routine calculates the row for given strip using spanset.
*
* Inputs :
*    instance - HBA instance
*    ld   - Logical drive number
*    Strip        - Strip
*    map    - LD map
*
* Outputs :
*
*    row         - row associated with strip
*/

static u64  get_row_from_strip(struct megasas_instance *instance,
	u32 ld, u64 strip, struct MR_DRV_RAID_MAP_ALL *map)
{
	struct fusion_context *fusion = instance->ctrl_context;
	struct MR_LD_RAID	*raid = MR_LdRaidGet(ld, map);
	LD_SPAN_SET	*span_set;
	PLD_SPAN_INFO	ldSpanInfo = fusion->log_to_span;
	u32		info, strip_offset, span, span_offset;
	u64		span_set_Strip, span_set_Row, retval;

	for (info = 0; info < MAX_QUAD_DEPTH; info++) {
		span_set = &(ldSpanInfo[ld].span_set[info]);

		if (span_set->span_row_data_width == 0)
			break;
		if (strip > span_set->data_strip_end)
			continue;

		span_set_Strip = strip - span_set->data_strip_start;
		strip_offset = mega_mod64(span_set_Strip,
				span_set->span_row_data_width);
		span_set_Row = mega_div64_32(span_set_Strip,
				span_set->span_row_data_width) * span_set->diff;
		for (span = 0, span_offset = 0; span < raid->spanDepth; span++)
			if (le32_to_cpu(map->raidMap.ldSpanMap[ld].spanBlock[span].
				block_span_info.noElements) >= info+1) {
				if (strip_offset >=
					span_set->strip_offset[span])
					span_offset++;
				else
					break;
			}
#if SPAN_DEBUG
		dev_info(&instance->pdev->dev, "Strip 0x%llx,"
			"span_set_Strip 0x%llx, span_set_Row 0x%llx"
			"data width 0x%llx span offset 0x%x\n", strip,
			(unsigned long long)span_set_Strip,
			(unsigned long long)span_set_Row,
			(unsigned long long)span_set->span_row_data_width,
			span_offset);
		dev_info(&instance->pdev->dev, "For strip 0x%llx"
			"row is 0x%llx\n", strip,
			(unsigned long long) span_set->data_row_start +
			(unsigned long long) span_set_Row + (span_offset - 1));
#endif
		retval = (span_set->data_row_start + span_set_Row +
				(span_offset - 1));
		return retval;
	}
	return -1LLU;
}


/*
******************************************************************************
*
* This routine calculates the Start Strip for given row using spanset.
*
* Inputs :
*    instance - HBA instance
*    ld   - Logical drive number
*    row        - Row number
*    map    - LD map
*
* Outputs :
*
*    Strip         - Start strip associated with row
*/

static u64 get_strip_from_row(struct megasas_instance *instance,
		u32 ld, u64 row, struct MR_DRV_RAID_MAP_ALL *map)
{
	struct fusion_context *fusion = instance->ctrl_context;
	struct MR_LD_RAID         *raid = MR_LdRaidGet(ld, map);
	LD_SPAN_SET *span_set;
	struct MR_QUAD_ELEMENT    *quad;
	PLD_SPAN_INFO ldSpanInfo = fusion->log_to_span;
	u32    span, info;
	u64  strip;

	for (info = 0; info < MAX_QUAD_DEPTH; info++) {
		span_set = &(ldSpanInfo[ld].span_set[info]);

		if (span_set->span_row_data_width == 0)
			break;
		if (row > span_set->data_row_end)
			continue;

		for (span = 0; span < raid->spanDepth; span++)
			if (le32_to_cpu(map->raidMap.ldSpanMap[ld].spanBlock[span].
				block_span_info.noElements) >= info+1) {
				quad = &map->raidMap.ldSpanMap[ld].
					spanBlock[span].block_span_info.quad[info];
				if (le64_to_cpu(quad->logStart) <= row  &&
					row <= le64_to_cpu(quad->logEnd)  &&
					mega_mod64((row - le64_to_cpu(quad->logStart)),
					le32_to_cpu(quad->diff)) == 0) {
					strip = mega_div64_32
						(((row - span_set->data_row_start)
							- le64_to_cpu(quad->logStart)),
							le32_to_cpu(quad->diff));
					strip *= span_set->span_row_data_width;
					strip += span_set->data_strip_start;
					strip += span_set->strip_offset[span];
					return strip;
				}
			}
	}
	dev_err(&instance->pdev->dev, "get_strip_from_row"
		"returns invalid strip for ld=%x, row=%lx\n",
		ld, (long unsigned int)row);
	return -1;
}

/*
******************************************************************************
*
* This routine calculates the Physical Arm for given strip using spanset.
*
* Inputs :
*    instance - HBA instance
*    ld   - Logical drive number
*    strip      - Strip
*    map    - LD map
*
* Outputs :
*
*    Phys Arm         - Phys Arm associated with strip
*/

static u32 get_arm_from_strip(struct megasas_instance *instance,
	u32 ld, u64 strip, struct MR_DRV_RAID_MAP_ALL *map)
{
	struct fusion_context *fusion = instance->ctrl_context;
	struct MR_LD_RAID         *raid = MR_LdRaidGet(ld, map);
	LD_SPAN_SET *span_set;
	PLD_SPAN_INFO ldSpanInfo = fusion->log_to_span;
	u32    info, strip_offset, span, span_offset, retval;

	for (info = 0 ; info < MAX_QUAD_DEPTH; info++) {
		span_set = &(ldSpanInfo[ld].span_set[info]);

		if (span_set->span_row_data_width == 0)
			break;
		if (strip > span_set->data_strip_end)
			continue;

		strip_offset = (uint)mega_mod64
				((strip - span_set->data_strip_start),
				span_set->span_row_data_width);

		for (span = 0, span_offset = 0; span < raid->spanDepth; span++)
			if (le32_to_cpu(map->raidMap.ldSpanMap[ld].spanBlock[span].
				block_span_info.noElements) >= info+1) {
				if (strip_offset >=
					span_set->strip_offset[span])
					span_offset =
						span_set->strip_offset[span];
				else
					break;
			}
#if SPAN_DEBUG
		dev_info(&instance->pdev->dev, "get_arm_from_strip:"
			"for ld=0x%x strip=0x%lx arm is  0x%x\n", ld,
			(long unsigned int)strip, (strip_offset - span_offset));
#endif
		retval = (strip_offset - span_offset);
		return retval;
	}

	dev_err(&instance->pdev->dev, "get_arm_from_strip"
		"returns invalid arm for ld=%x strip=%lx\n",
		ld, (long unsigned int)strip);

	return -1;
}

/* This Function will return Phys arm */
u8 get_arm(struct megasas_instance *instance, u32 ld, u8 span, u64 stripe,
		struct MR_DRV_RAID_MAP_ALL *map)
{
	struct MR_LD_RAID  *raid = MR_LdRaidGet(ld, map);
	/* Need to check correct default value */
	u32    arm = 0;

	switch (raid->level) {
	case 0:
	case 5:
	case 6:
		arm = mega_mod64(stripe, SPAN_ROW_SIZE(map, ld, span));
		break;
	case 1:
		/* start with logical arm */
		arm = get_arm_from_strip(instance, ld, stripe, map);
		if (arm != -1U)
			arm *= 2;
		break;
	}

	return arm;
}


/*
******************************************************************************
*
* This routine calculates the arm, span and block for the specified stripe and
* reference in stripe using spanset
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
static u8 mr_spanset_get_phy_params(struct megasas_instance *instance, u32 ld,
		u64 stripRow, u16 stripRef, struct IO_REQUEST_INFO *io_info,
		struct RAID_CONTEXT *pRAID_Context,
		struct MR_DRV_RAID_MAP_ALL *map)
{
	struct MR_LD_RAID  *raid = MR_LdRaidGet(ld, map);
	u32     pd, arRef;
	u8      physArm, span;
	u64     row;
	u8	retval = TRUE;
	u8	do_invader = 0;
	u64	*pdBlock = &io_info->pdBlock;
	u16	*pDevHandle = &io_info->devHandle;
	u32	logArm, rowMod, armQ, arm;

	if ((instance->pdev->device == PCI_DEVICE_ID_LSI_INVADER ||
		instance->pdev->device == PCI_DEVICE_ID_LSI_FURY))
		do_invader = 1;

	/*Get row and span from io_info for Uneven Span IO.*/
	row	    = io_info->start_row;
	span	    = io_info->start_span;


	if (raid->level == 6) {
		logArm = get_arm_from_strip(instance, ld, stripRow, map);
		if (logArm == -1U)
			return FALSE;
		rowMod = mega_mod64(row, SPAN_ROW_SIZE(map, ld, span));
		armQ = SPAN_ROW_SIZE(map, ld, span) - 1 - rowMod;
		arm = armQ + 1 + logArm;
		if (arm >= SPAN_ROW_SIZE(map, ld, span))
			arm -= SPAN_ROW_SIZE(map, ld, span);
		physArm = (u8)arm;
	} else
		/* Calculate the arm */
		physArm = get_arm(instance, ld, span, stripRow, map);
	if (physArm == 0xFF)
		return FALSE;

	arRef       = MR_LdSpanArrayGet(ld, span, map);
	pd          = MR_ArPdGet(arRef, physArm, map);

	if (pd != MR_PD_INVALID)
		*pDevHandle = MR_PdDevHandleGet(pd, map);
	else {
		*pDevHandle = MR_PD_INVALID;
		if ((raid->level >= 5) &&
			(!do_invader  || (do_invader &&
			(raid->regTypeReqOnRead != REGION_TYPE_UNUSED))))
			pRAID_Context->regLockFlags = REGION_TYPE_EXCLUSIVE;
		else if (raid->level == 1) {
			pd = MR_ArPdGet(arRef, physArm + 1, map);
			if (pd != MR_PD_INVALID)
				*pDevHandle = MR_PdDevHandleGet(pd, map);
		}
	}

	*pdBlock += stripRef + le64_to_cpu(MR_LdSpanPtrGet(ld, span, map)->startBlk);
	pRAID_Context->spanArm = (span << RAID_CTX_SPANARM_SPAN_SHIFT) |
					physArm;
	io_info->span_arm = pRAID_Context->spanArm;
	return retval;
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
		u16 stripRef, struct IO_REQUEST_INFO *io_info,
		struct RAID_CONTEXT *pRAID_Context,
		struct MR_DRV_RAID_MAP_ALL *map)
{
	struct MR_LD_RAID  *raid = MR_LdRaidGet(ld, map);
	u32         pd, arRef;
	u8          physArm, span;
	u64         row;
	u8	    retval = TRUE;
	u8          do_invader = 0;
	u64	    *pdBlock = &io_info->pdBlock;
	u16	    *pDevHandle = &io_info->devHandle;

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
		span = (u8)MR_GetSpanBlock(ld, row, pdBlock, map);
		if (span == SPAN_INVALID)
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

	*pdBlock += stripRef + le64_to_cpu(MR_LdSpanPtrGet(ld, span, map)->startBlk);
	pRAID_Context->spanArm = (span << RAID_CTX_SPANARM_SPAN_SHIFT) |
		physArm;
	io_info->span_arm = pRAID_Context->spanArm;
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
		    struct MR_DRV_RAID_MAP_ALL *map, u8 **raidLUN)
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
	u8	    startlba_span = SPAN_INVALID;
	u64 *pdBlock = &io_info->pdBlock;

	ldStartBlock = io_info->ldStartBlock;
	numBlocks = io_info->numBlocks;
	ldTgtId = io_info->ldTgtId;
	isRead = io_info->isRead;
	io_info->IoforUnevenSpan = 0;
	io_info->start_span	= SPAN_INVALID;

	ld = MR_TargetIdToLdGet(ldTgtId, map);
	raid = MR_LdRaidGet(ld, map);

	/*
	 * if rowDataSize @RAID map and spanRowDataSize @SPAN INFO are zero
	 * return FALSE
	 */
	if (raid->rowDataSize == 0) {
		if (MR_LdSpanPtrGet(ld, 0, map)->spanRowDataSize == 0)
			return FALSE;
		else if (instance->UnevenSpanSupport) {
			io_info->IoforUnevenSpan = 1;
		} else {
			dev_info(&instance->pdev->dev,
				"raid->rowDataSize is 0, but has SPAN[0]"
				"rowDataSize = 0x%0x,"
				"but there is _NO_ UnevenSpanSupport\n",
				MR_LdSpanPtrGet(ld, 0, map)->spanRowDataSize);
			return FALSE;
		}
	}

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

	if (io_info->IoforUnevenSpan) {
		start_row = get_row_from_strip(instance, ld, start_strip, map);
		endRow	  = get_row_from_strip(instance, ld, endStrip, map);
		if (start_row == -1ULL || endRow == -1ULL) {
			dev_info(&instance->pdev->dev, "return from %s %d."
				"Send IO w/o region lock.\n",
				__func__, __LINE__);
			return FALSE;
		}

		if (raid->spanDepth == 1) {
			startlba_span = 0;
			*pdBlock = start_row << raid->stripeShift;
		} else
			startlba_span = (u8)mr_spanset_get_span_block(instance,
						ld, start_row, pdBlock, map);
		if (startlba_span == SPAN_INVALID) {
			dev_info(&instance->pdev->dev, "return from %s %d"
				"for row 0x%llx,start strip %llx"
				"endSrip %llx\n", __func__, __LINE__,
				(unsigned long long)start_row,
				(unsigned long long)start_strip,
				(unsigned long long)endStrip);
			return FALSE;
		}
		io_info->start_span	= startlba_span;
		io_info->start_row	= start_row;
#if SPAN_DEBUG
		dev_dbg(&instance->pdev->dev, "Check Span number from %s %d"
			"for row 0x%llx, start strip 0x%llx end strip 0x%llx"
			" span 0x%x\n", __func__, __LINE__,
			(unsigned long long)start_row,
			(unsigned long long)start_strip,
			(unsigned long long)endStrip, startlba_span);
		dev_dbg(&instance->pdev->dev, "start_row 0x%llx endRow 0x%llx"
			"Start span 0x%x\n", (unsigned long long)start_row,
			(unsigned long long)endRow, startlba_span);
#endif
	} else {
		start_row = mega_div64_32(start_strip, raid->rowDataSize);
		endRow    = mega_div64_32(endStrip, raid->rowDataSize);
	}
	numRows = (u8)(endRow - start_row + 1);

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
	} else if (io_info->IoforUnevenSpan == 0) {
		/*
		 * For Even span region lock optimization.
		 * If the start strip is the last in the start row
		 */
		if (start_strip == (start_row + 1) * raid->rowDataSize - 1) {
			regStart += ref_in_start_stripe;
			/* initialize count to sectors from startref to end
			   of strip */
			regSize = stripSize - ref_in_start_stripe;
		}

		/* add complete rows in the middle of the transfer */
		if (numRows > 2)
			regSize += (numRows-2) << raid->stripeShift;

		/* if IO ends within first strip of last row*/
		if (endStrip == endRow*raid->rowDataSize)
			regSize += ref_in_end_stripe+1;
		else
			regSize += stripSize;
	} else {
		/*
		 * For Uneven span region lock optimization.
		 * If the start strip is the last in the start row
		 */
		if (start_strip == (get_strip_from_row(instance, ld, start_row, map) +
				SPAN_ROW_DATA_SIZE(map, ld, startlba_span) - 1)) {
			regStart += ref_in_start_stripe;
			/* initialize count to sectors from
			 * startRef to end of strip
			 */
			regSize = stripSize - ref_in_start_stripe;
		}
		/* Add complete rows in the middle of the transfer*/

		if (numRows > 2)
			/* Add complete rows in the middle of the transfer*/
			regSize += (numRows-2) << raid->stripeShift;

		/* if IO ends within first strip of last row */
		if (endStrip == get_strip_from_row(instance, ld, endRow, map))
			regSize += ref_in_end_stripe + 1;
		else
			regSize += stripSize;
	}

	pRAID_Context->timeoutValue =
		cpu_to_le16(raid->fpIoTimeoutForLd ?
			    raid->fpIoTimeoutForLd :
			    map->raidMap.fpPdIoTimeoutSec);
	if ((instance->pdev->device == PCI_DEVICE_ID_LSI_INVADER) ||
		(instance->pdev->device == PCI_DEVICE_ID_LSI_FURY))
		pRAID_Context->regLockFlags = (isRead) ?
			raid->regTypeReqOnRead : raid->regTypeReqOnWrite;
	else
		pRAID_Context->regLockFlags = (isRead) ?
			REGION_TYPE_SHARED_READ : raid->regTypeReqOnWrite;
	pRAID_Context->VirtualDiskTgtId = raid->targetId;
	pRAID_Context->regLockRowLBA    = cpu_to_le64(regStart);
	pRAID_Context->regLockLength    = cpu_to_le32(regSize);
	pRAID_Context->configSeqNum	= raid->seqNum;
	/* save pointer to raid->LUN array */
	*raidLUN = raid->LUN;


	/*Get Phy Params only if FP capable, or else leave it to MR firmware
	  to do the calculation.*/
	if (io_info->fpOkForIo) {
		retval = io_info->IoforUnevenSpan ?
				mr_spanset_get_phy_params(instance, ld,
					start_strip, ref_in_start_stripe,
					io_info, pRAID_Context, map) :
				MR_GetPhyParams(instance, ld, start_strip,
					ref_in_start_stripe, io_info,
					pRAID_Context, map);
		/* If IO on an invalid Pd, then FP is not possible.*/
		if (io_info->devHandle == MR_PD_INVALID)
			io_info->fpOkForIo = FALSE;
		return retval;
	} else if (isRead) {
		uint stripIdx;
		for (stripIdx = 0; stripIdx < num_strips; stripIdx++) {
			retval = io_info->IoforUnevenSpan ?
				mr_spanset_get_phy_params(instance, ld,
				    start_strip + stripIdx,
				    ref_in_start_stripe, io_info,
				    pRAID_Context, map) :
				MR_GetPhyParams(instance, ld,
				    start_strip + stripIdx, ref_in_start_stripe,
				    io_info, pRAID_Context, map);
			if (!retval)
				return TRUE;
		}
	}

#if SPAN_DEBUG
	/* Just for testing what arm we get for strip.*/
	if (io_info->IoforUnevenSpan)
		get_arm_from_strip(instance, ld, start_strip, map);
#endif
	return TRUE;
}

/*
******************************************************************************
*
* This routine pepare spanset info from Valid Raid map and store it into
* local copy of ldSpanInfo per instance data structure.
*
* Inputs :
* map    - LD map
* ldSpanInfo - ldSpanInfo per HBA instance
*
*/
void mr_update_span_set(struct MR_DRV_RAID_MAP_ALL *map,
	PLD_SPAN_INFO ldSpanInfo)
{
	u8   span, count;
	u32  element, span_row_width;
	u64  span_row;
	struct MR_LD_RAID *raid;
	LD_SPAN_SET *span_set, *span_set_prev;
	struct MR_QUAD_ELEMENT    *quad;
	int ldCount;
	u16 ld;


	for (ldCount = 0; ldCount < MAX_LOGICAL_DRIVES_EXT; ldCount++) {
		ld = MR_TargetIdToLdGet(ldCount, map);
		if (ld >= (MAX_LOGICAL_DRIVES_EXT - 1))
			continue;
		raid = MR_LdRaidGet(ld, map);
		for (element = 0; element < MAX_QUAD_DEPTH; element++) {
			for (span = 0; span < raid->spanDepth; span++) {
				if (le32_to_cpu(map->raidMap.ldSpanMap[ld].spanBlock[span].
					block_span_info.noElements) <
					element + 1)
					continue;
				span_set = &(ldSpanInfo[ld].span_set[element]);
				quad = &map->raidMap.ldSpanMap[ld].
					spanBlock[span].block_span_info.
					quad[element];

				span_set->diff = le32_to_cpu(quad->diff);

				for (count = 0, span_row_width = 0;
					count < raid->spanDepth; count++) {
					if (le32_to_cpu(map->raidMap.ldSpanMap[ld].
						spanBlock[count].
						block_span_info.
						noElements) >= element + 1) {
						span_set->strip_offset[count] =
							span_row_width;
						span_row_width +=
							MR_LdSpanPtrGet
							(ld, count, map)->spanRowDataSize;
						printk(KERN_INFO "megasas:"
							"span %x rowDataSize %x\n",
							count, MR_LdSpanPtrGet
							(ld, count, map)->spanRowDataSize);
					}
				}

				span_set->span_row_data_width = span_row_width;
				span_row = mega_div64_32(((le64_to_cpu(quad->logEnd) -
					le64_to_cpu(quad->logStart)) + le32_to_cpu(quad->diff)),
					le32_to_cpu(quad->diff));

				if (element == 0) {
					span_set->log_start_lba = 0;
					span_set->log_end_lba =
						((span_row << raid->stripeShift)
						* span_row_width) - 1;

					span_set->span_row_start = 0;
					span_set->span_row_end = span_row - 1;

					span_set->data_strip_start = 0;
					span_set->data_strip_end =
						(span_row * span_row_width) - 1;

					span_set->data_row_start = 0;
					span_set->data_row_end =
						(span_row * le32_to_cpu(quad->diff)) - 1;
				} else {
					span_set_prev = &(ldSpanInfo[ld].
							span_set[element - 1]);
					span_set->log_start_lba =
						span_set_prev->log_end_lba + 1;
					span_set->log_end_lba =
						span_set->log_start_lba +
						((span_row << raid->stripeShift)
						* span_row_width) - 1;

					span_set->span_row_start =
						span_set_prev->span_row_end + 1;
					span_set->span_row_end =
					span_set->span_row_start + span_row - 1;

					span_set->data_strip_start =
					span_set_prev->data_strip_end + 1;
					span_set->data_strip_end =
						span_set->data_strip_start +
						(span_row * span_row_width) - 1;

					span_set->data_row_start =
						span_set_prev->data_row_end + 1;
					span_set->data_row_end =
						span_set->data_row_start +
						(span_row * le32_to_cpu(quad->diff)) - 1;
				}
				break;
		}
		if (span == raid->spanDepth)
			break;
	    }
	}
#if SPAN_DEBUG
	getSpanInfo(map, ldSpanInfo);
#endif

}

void mr_update_load_balance_params(struct MR_DRV_RAID_MAP_ALL *drv_map,
	struct LD_LOAD_BALANCE_INFO *lbInfo)
{
	int ldCount;
	u16 ld;
	struct MR_LD_RAID *raid;

	if (lb_pending_cmds > 128 || lb_pending_cmds < 1)
		lb_pending_cmds = LB_PENDING_CMDS_DEFAULT;

	for (ldCount = 0; ldCount < MAX_LOGICAL_DRIVES_EXT; ldCount++) {
		ld = MR_TargetIdToLdGet(ldCount, drv_map);
		if (ld >= MAX_LOGICAL_DRIVES_EXT) {
			lbInfo[ldCount].loadBalanceFlag = 0;
			continue;
		}

		raid = MR_LdRaidGet(ld, drv_map);
		if ((raid->level != 1) ||
			(raid->ldState != MR_LD_STATE_OPTIMAL)) {
			lbInfo[ldCount].loadBalanceFlag = 0;
			continue;
		}
		lbInfo[ldCount].loadBalanceFlag = 1;
	}
}

u8 megasas_get_best_arm_pd(struct megasas_instance *instance,
	struct LD_LOAD_BALANCE_INFO *lbInfo, struct IO_REQUEST_INFO *io_info)
{
	struct fusion_context *fusion;
	struct MR_LD_RAID  *raid;
	struct MR_DRV_RAID_MAP_ALL *drv_map;
	u16     pend0, pend1, ld;
	u64     diff0, diff1;
	u8      bestArm, pd0, pd1, span, arm;
	u32     arRef, span_row_size;

	u64 block = io_info->ldStartBlock;
	u32 count = io_info->numBlocks;

	span = ((io_info->span_arm & RAID_CTX_SPANARM_SPAN_MASK)
			>> RAID_CTX_SPANARM_SPAN_SHIFT);
	arm = (io_info->span_arm & RAID_CTX_SPANARM_ARM_MASK);


	fusion = instance->ctrl_context;
	drv_map = fusion->ld_drv_map[(instance->map_id & 1)];
	ld = MR_TargetIdToLdGet(io_info->ldTgtId, drv_map);
	raid = MR_LdRaidGet(ld, drv_map);
	span_row_size = instance->UnevenSpanSupport ?
			SPAN_ROW_SIZE(drv_map, ld, span) : raid->rowSize;

	arRef = MR_LdSpanArrayGet(ld, span, drv_map);
	pd0 = MR_ArPdGet(arRef, arm, drv_map);
	pd1 = MR_ArPdGet(arRef, (arm + 1) >= span_row_size ?
		(arm + 1 - span_row_size) : arm + 1, drv_map);

	/* get the pending cmds for the data and mirror arms */
	pend0 = atomic_read(&lbInfo->scsi_pending_cmds[pd0]);
	pend1 = atomic_read(&lbInfo->scsi_pending_cmds[pd1]);

	/* Determine the disk whose head is nearer to the req. block */
	diff0 = ABS_DIFF(block, lbInfo->last_accessed_block[pd0]);
	diff1 = ABS_DIFF(block, lbInfo->last_accessed_block[pd1]);
	bestArm = (diff0 <= diff1 ? arm : arm ^ 1);

	if ((bestArm == arm && pend0 > pend1 + lb_pending_cmds)  ||
			(bestArm != arm && pend1 > pend0 + lb_pending_cmds))
		bestArm ^= 1;

	/* Update the last accessed block on the correct pd */
	io_info->pd_after_lb = (bestArm == arm) ? pd0 : pd1;
	lbInfo->last_accessed_block[io_info->pd_after_lb] = block + count - 1;
	io_info->span_arm = (span << RAID_CTX_SPANARM_SPAN_SHIFT) | bestArm;
#if SPAN_DEBUG
	if (arm != bestArm)
		dev_dbg(&instance->pdev->dev, "LSI Debug R1 Load balance "
			"occur - span 0x%x arm 0x%x bestArm 0x%x "
			"io_info->span_arm 0x%x\n",
			span, arm, bestArm, io_info->span_arm);
#endif
	return io_info->pd_after_lb;
}

u16 get_updated_dev_handle(struct megasas_instance *instance,
	struct LD_LOAD_BALANCE_INFO *lbInfo, struct IO_REQUEST_INFO *io_info)
{
	u8 arm_pd;
	u16 devHandle;
	struct fusion_context *fusion;
	struct MR_DRV_RAID_MAP_ALL *drv_map;

	fusion = instance->ctrl_context;
	drv_map = fusion->ld_drv_map[(instance->map_id & 1)];

	/* get best new arm (PD ID) */
	arm_pd  = megasas_get_best_arm_pd(instance, lbInfo, io_info);
	devHandle = MR_PdDevHandleGet(arm_pd, drv_map);
	atomic_inc(&lbInfo->scsi_pending_cmds[arm_pd]);
	return devHandle;
}
