/*
 * drivers/block/sunxi_nand/src/include/nand_physic.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __NAND_PHYSIC_H__
#define __NAND_PHYSIC_H__

#include "nand_type.h"

//===========================================
extern struct __NandStorageInfo_t  NandStorageInfo;
extern struct __NandPageCachePool_t PageCachePool;
//===========================================



//==============================================================================
//  define the physical archictecture export parameter
//==============================================================================

//define the Ecc Mode
#define ECC_MODE           (NandStorageInfo.EccMode)

//define the DDR tyep
#define DDR_TYPE           (NandStorageInfo.DDRType)

//define the sector count of a single physical page
#define SECTOR_CNT_OF_SINGLE_PAGE           (NandStorageInfo.SectorCntPerPage)

//define the sector count of a super physical page, the super page may be based on multi-plane
#define SECTOR_CNT_OF_SUPER_PAGE            (NandStorageInfo.SectorCntPerPage * NandStorageInfo.PlaneCntPerDie)

//define the sector bitmap for a single page
#define FULL_BITMAP_OF_SINGLE_PAGE          ((__u32)((1<<SECTOR_CNT_OF_SINGLE_PAGE) - 1))

//define the sector bitmap for a super page, the sector count of a super page may be equal to 32
#define FULL_BITMAP_OF_SUPER_PAGE           ((__u32)((1<<(SECTOR_CNT_OF_SUPER_PAGE - 1)) | ((1<<(SECTOR_CNT_OF_SUPER_PAGE - 1)) - 1)))

//define the block number offset for the multi-plane operation
#define MULTI_PLANE_BLOCK_OFFSET            (NandStorageInfo.OptPhyOpPar.MultiPlaneBlockOffset)

//define the position of the bad block flag in a physical block
#define BAD_BLOCK_FLAG_PST                  (NandStorageInfo.OptPhyOpPar.BadBlockFlagPosition)

//define if the nand flash can support cache read operation
#define SUPPORT_CACHE_READ                  (NAND_CACHE_READ & NandStorageInfo.OperationOpt)

//define if the nand flash can support cache program operation
#define SUPPORT_CACHE_PROGRAM               (NAND_CACHE_PROGRAM & NandStorageInfo.OperationOpt)

//define if the nand flash can support multi-plane read operation
#define SUPPORT_MULTI_READ                  (NAND_MULTI_READ & NandStorageInfo.OperationOpt)

//define if the nand flash can support multi-plane program operation
#define SUPPORT_MULTI_PROGRAM               (NAND_MULTI_PROGRAM & NandStorageInfo.OperationOpt)

//define if the nand flash can support page copy-back with command operation
#define SUPPORT_PAGE_COPYBACK               (NAND_PAGE_COPYBACK & NandStorageInfo.OperationOpt)

//define if the nand flash can support __s32ernal __s32er-leave operation
#define SUPPORT_INT_INTERLEAVE              (NAND_INT_INTERLEAVE & NandStorageInfo.OperationOpt)

//define if the nand flash system can support external __s32er-leave operation
#define SUPPORT_EXT_INTERLEAVE              (NAND_EXT_INTERLEAVE & NandStorageInfo.OperationOpt)

//define if the nand flash system can support randomizer
#define SUPPORT_RANDOM                      (NAND_RANDOM & NandStorageInfo.OperationOpt)

//define if the nand flash system can support read retry
#define SUPPORT_READ_RETRY                  (NAND_READ_RETRY & NandStorageInfo.OperationOpt)

//define if the nand flash system can support read unique id
#define SUPPORT_READ_UNIQUE_ID              (NAND_READ_UNIQUE_ID & NandStorageInfo.OperationOpt)

//define if the nand flash system can support bank align
#define SUPPORT_ALIGN_NAND_BNK              (!(NAND_PAGE_ADR_NO_SKIP & NandStorageInfo.OperationOpt))

//define the count of the nand flash DIE in a nand flash chip
#define DIE_CNT_OF_CHIP                     (NandStorageInfo.DieCntPerChip)

//define the count of the nand flash bank in a nand flas hchip
#define BNK_CNT_OF_CHIP                     (NandStorageInfo.BankCntPerChip)

//define the Rb connect Mode
#define RB_CONNECT_MODE                    (NandStorageInfo.RbConnectMode)

//define the count of the total nand flash bank in the nand flash storage system
#define TOTAL_BANK_CNT                      (NandStorageInfo.BankCntPerChip * NandStorageInfo.ChipCnt)

//define the count of the physical block in a nand flash DIE
#define BLOCK_CNT_OF_DIE                    (NandStorageInfo.BlkCntPerDie)

//define the count of the nand flash plane in a nand flash DIE
#define PLANE_CNT_OF_DIE                    (NandStorageInfo.PlaneCntPerDie)

//define the count of the physical page in a physical block
#define PAGE_CNT_OF_PHY_BLK                 (NandStorageInfo.PageCntPerPhyBlk)

//define the information of the nand chip connect in the nand storage system
#define CHIP_CONNECT_INFO                   (NandStorageInfo.ChipConnectInfo)

//define the ReadRetryType of the nand chip connect in the nand storage system
#define READ_RETRY_TYPE                   (NandStorageInfo.ReadRetryType)

//define the ReadRetryType of the nand chip connect in the nand storage system
#define READ_RETRY_MODE                   ((READ_RETRY_TYPE>>16)&0xff)

//define the ReadRetryType of the nand chip connect in the nand storage system
#define READ_RETRY_CYCLE                   ((READ_RETRY_TYPE>>8)&0xff)

//define the ReadRetryType of the nand chip connect in the nand storage system
#define READ_RETRY_REG_CNT                   ((READ_RETRY_TYPE>>0)&0xff)

//define the nand flash access frequence parameter
#define NAND_ACCESS_FREQUENCE               (NandStorageInfo.FrequencePar)

#define BAD_BLK_FLAG_PST                    (NandStorageInfo.OptPhyOpPar.BadBlockFlagPosition)

//sync bank with chip mode, need wait whole chip true ready
#define SYNC_CHIP_MODE                      0x00

//sync bank with bank mode, only check the status of the bank to wait bank ready
#define SYNC_BANK_MODE                      0x01

//define the page cache for physical module processing page data
#define PHY_TMP_PAGE_CACHE                  (PageCachePool.PageCache0)

//define the spare data cache for physical module processing spare area data
#define PHY_TMP_SPARE_CACHE                 (PageCachePool.SpareCache)
//==============================================================================
//  define the functions __s32erface for the physic operation module
//==============================================================================

/*
************************************************************************************************************************
*                       INIT NAND FLASH DRIVER PHYSICAL MODULE
*
* Description: init nand flash driver physical module.
*
* Aguments   : none
*
* Returns    : the resutl of initial.
*                   = 0     initiate successful;
*                   = -1    initiate failed.
************************************************************************************************************************
*/
__s32 PHY_Init(void);
__s32 PHY_ChangeMode(__u8 serial_mode);

/*
************************************************************************************************************************
*                       NAND FLASH DRIVER PHYSICAL MODULE EXIT
*
* Description: nand flash driver physical module exit.
*
* Aguments   : none
*
* Returns    : the resutl of exit.
*                   = 0     exit successful;
*                   = -1    exit failed.
************************************************************************************************************************
*/
__s32 PHY_Exit(void);


/*
************************************************************************************************************************
*                       RESET ONE NAND FLASH CHIP
*
*Description: Reset the given nand chip;
*
*Arguments  : nChip     the chip select number, which need be reset.
*
*Return     : the result of chip reset;
*               = 0     reset nand chip successful;
*               = -1    reset nand chip failed.
************************************************************************************************************************
*/
__s32 PHY_ResetChip(__u32 nChip);


/*
************************************************************************************************************************
*                       READ NAND FLASH ID
*
*Description: Read nand flash ID from the given nand chip.
*
*Arguments  : nChip         the chip number whoes ID need be read;
*             pChipID       the po__s32er to the chip ID buffer.
*
*Return     : read nand chip ID result;
*               = 0     read chip ID successful, the chip ID has been stored in given buffer;
*               = -1    read chip ID failed.
************************************************************************************************************************
*/
__s32 PHY_ReadNandId(__s32 nChip, void *pChipID);
__s32 PHY_ReadNandUniqueId(__s32 bank, void *pChipID);


/*
************************************************************************************************************************
*                       CHECK WRITE PROTECT STATUS
*
*Description: check the status of write protect.
*
*Arguments  : nChip     the number of chip, which nand chip need be checked.
*
*Return     : the result of status check;
*             = 0       the nand flash is not write proteced;
*             = 1       the nand flash is write proteced;
*             = -1      check status failed.
************************************************************************************************************************
*/
__s32 PHY_CheckWp(__u32 nChip);


/*
************************************************************************************************************************
*                           PHYSICAL BLOCK ERASE
*
*Description: Erase one nand flash physical block.
*
*Arguments  : pBlkAdr   the parameter of the physical block which need be erased.
*
*Return     : the result of the block erase;
*               = 0     erase physical block successful;
*               = -1    erase physical block failed.
************************************************************************************************************************
*/
__s32 PHY_BlockErase(struct __PhysicOpPara_t *pBlkAdr);


/*
************************************************************************************************************************
*                       READ NAND FLASH PHYSICAL PAGE DATA
*
*Description: Read a page from a nand flash physical page to buffer.
*
*Arguments  : pPageAdr      the po__s32er to the accessed page parameter.
*
*Return     : the result of physical page read;
*               = 0     read physical page successful;
*               > 0     read physical page successful, but need do some process;
*               < 0     read physical page failed.
************************************************************************************************************************
*/
__s32 PHY_PageRead(struct __PhysicOpPara_t *pPageAdr);


__s32  PHY_PageReadSpare(struct __PhysicOpPara_t *pPageAdr);

/*
************************************************************************************************************************
*                       WRITE NAND FLASH PHYSICAL PAGE DATA
*
*Description: Write a page from buffer to a nand flash physical page.
*
*Arguments  : pPageAdr      the po__s32er to the accessed page parameter.
*
*Return     : The result of the page write;
*               = 0     page write successful;
*               > 0     page write successful, but need do some process;
*               < 0     page write failed.
************************************************************************************************************************
*/
__s32 PHY_PageWrite(struct __PhysicOpPara_t  *pPageAdr);


/*
************************************************************************************************************************
*                           PHYSIC PAGE COPY-BACK
*
*Description: copy one physical page from one physical block to another physical block.
*
*Arguments  : pSrcPage      the parameter of the source page which need be copied;
*             pDstPage      the parameter of the destination page which copied to.
*
*Return     : the result of the page copy-back;
*               = 0         page copy-back successful;
*               = -1        page copy-back failed.
************************************************************************************************************************
*/
__s32 PHY_PageCopyback(struct __PhysicOpPara_t *pSrcPage, struct __PhysicOpPara_t *pDstPage);


/*
************************************************************************************************************************
*                       SYNCH NAND FLASH PHYSIC OPERATION
*
*Description: Synch nand flash operation, check nand flash program/erase operation status.
*
*Arguments  : nBank     the number of the bank which need be synchronized;
*             bMode     the type of synch,
*                       = 0     synch the chip which the bank belonged to, wait the whole chip
*                               to be ready, and report status. if the chip support cacheprogram,
*                               need check if the chip is true ready;
*                       = 1     only synch the the bank, wait the bank ready and report the status,
*                               if the chip support cache program, need not check if the cache is
*                               true ready.
*
*Return     : the result of synch;
*               = 0     synch nand flash successful, nand operation ok;
*               = -1    synch nand flash failed.
************************************************************************************************************************
*/
__s32 PHY_SynchBank(__u32 nBank, __u32 bMode);


__s32 PHY_GetDefaultParam(__u32 bank);
__s32 PHY_SetDefaultParam(__u32 bank);

__s32 PHY_ScanDDRParam(void);

#endif  //ifnedf __NAND_PHYSIC_H__




