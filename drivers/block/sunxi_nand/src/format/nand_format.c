/*
 * drivers/block/sunxi_nand/src/format/nand_format.c
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

#include "../include/nand_format.h"
#include "../include/nand_logic.h"

#define DBG_DUMP_DIE_INFO       (1)

extern  struct __NandDriverGlobal_t     NandDriverInfo;
extern  struct __LogicArchitecture_t    LogicArchiPar;
extern  struct __ZoneTblPstInfo_t       ZoneTblPstInfo[MAX_ZONE_CNT];

extern  struct __NandStorageInfo_t  NandStorageInfo;
extern  struct __NandPageCachePool_t    PageCachePool;


//define some local variable
static __u32 DieCntOfNand = 0;          //the count of dies in a nand chip
static __u32 SuperBlkCntOfDie = 0;      //the count of the super blocks in a Die

blk_for_boot1_t blks_array[ ] = {
	{ 16,  4, 16 },
	{ 32,  4, 8 },
	{ 128, 4, 3 },
	{ 256, 4, 3 },
	{ 512, 4, 3 },
	{ 0xffffffff,   4, 3 },
};
__u32 DIE0_FIRST_BLK_NUM;

/*
************************************************************************************************************************
*                       CALCULATE PHYSICAL OPERATION PARAMETER
*
*Description: Calculate the paramter for physical operation with the number of zone, number of
*             super block and number of page in the super block.
*
*Arguments  : pPhyPar   the pointer to the physical operation parameter;
*             nZone     the number of the zone which the super block blonged to;
*             nBlock    the number of the super block;
*             nPage     the number of the super page in the super block.
*
*Return     : calculate parameter result;
*               = 0     calculate parameter successful;
*               < 0     calcualte parameter failed.
************************************************************************************************************************
*/
#if(0)
__s32 _CalculatePhyOpPar(struct __PhysicOpPara_t *pPhyPar, __u32 nZone, __u32 nBlock, __u32 nPage)
{


    __u32   tmpDieNum, tmpBnkNum, tmpBlkNum, tmpPageNum;

//    FORMAT_DBG("[FORMAT_DBG] Calculate the physical operation parameters.\n"
//               "             ZoneNum:0x%x, BlockNum:0x%x, PageNum: 0x%x\n", nZone, nBlock, nPage);

    //calcualte the Die number by the zone number
    tmpDieNum = nZone / ZONE_CNT_OF_DIE;

    if(SUPPORT_INT_INTERLEAVE && SUPPORT_EXT_INTERLEAVE)
    {
        //nand flash support internal inter-leave and external iner-leave, the block number is
        //same as the virtual block number in the die, the bank number is the virtual page number
        //model the inter-leave bank count, and the page number is the virtual page number
        //divide the inter-leave bank count
        tmpBnkNum = nPage % INTERLEAVE_BANK_CNT;
        tmpBlkNum = nBlock;
        tmpPageNum = nPage / INTERLEAVE_BANK_CNT;
    }

    else if(SUPPORT_INT_INTERLEAVE && !SUPPORT_EXT_INTERLEAVE)
    {
        //nand flash support internal inter-leave but don't support external inter-leave, the block
        //number if same as the vitual block number, the bank number is virtual page number model
        //inter-leave count and add the chip bank base, the page number is the virtual page number
        //divide the inter-leave bank count
        tmpBnkNum = (nPage % INTERLEAVE_BANK_CNT) + (tmpDieNum * INTERLEAVE_BANK_CNT);
        tmpBlkNum = nBlock;
        tmpPageNum = nPage / INTERLEAVE_BANK_CNT;
    }

    else if(!SUPPORT_INT_INTERLEAVE && SUPPORT_EXT_INTERLEAVE)
    {
        //nand flash support external inter-leave but don't support internal inter-leave, the block
        //number is virtual block number add the die block base, the bank number is the page number
        //model the inter-leave bank count, the page number is vitual page number divide the inter-leave
        //bank count
        tmpBnkNum = nPage % INTERLEAVE_BANK_CNT;
        tmpBlkNum = nBlock + (tmpDieNum * (BLOCK_CNT_OF_DIE / PLANE_CNT_OF_DIE));
        tmpPageNum = nPage / INTERLEAVE_BANK_CNT;
    }

    else//if(!SUPPORT_INT_INTERLEAVE && !SUPPORT_EXT_INTERLEAVE)
    {
        //nand flash don't internal inter-leave and extern inter-leave either, the bank number is the
        //die number divide the die count of chip, the block number is the virtual block number add
        //the die block base in the chip, the page number is same as the virtual page number
        tmpBnkNum = tmpDieNum / DIE_CNT_OF_CHIP;
        tmpBlkNum = nBlock + (tmpDieNum % DIE_CNT_OF_CHIP) * (BLOCK_CNT_OF_DIE / PLANE_CNT_OF_DIE);
        tmpPageNum = nPage;
    }

    //set the physical operation paramter by the bank number, block number and page number
    pPhyPar->BankNum = tmpBnkNum;
    pPhyPar->PageNum = tmpPageNum;
    pPhyPar->BlkNum = tmpBlkNum;

//    FORMAT_DBG("         Calculate Result: BankNum 0x%x, BlkNum 0x%x, PageNum 0x%x\n", tmpBnkNum, tmpBlkNum, tmpPageNum);

    //calculate physical operation parameter successful
    return 0;
}
#elif(1)
__s32 _CalculatePhyOpPar(struct __PhysicOpPara_t *pPhyPar, __u32 nZone, __u32 nBlock, __u32 nPage)
{


    __u32   tmpDieNum, tmpBnkNum, tmpBlkNum, tmpPageNum;

//    FORMAT_DBG("[FORMAT_DBG] Calculate the physical operation parameters.\n"
//               "             ZoneNum:0x%x, BlockNum:0x%x, PageNum: 0x%x\n", nZone, nBlock, nPage);

    //calcualte the Die number by the zone number
    tmpDieNum = nZone / ZONE_CNT_OF_DIE;

    if(SUPPORT_INT_INTERLEAVE && SUPPORT_EXT_INTERLEAVE)
    {
        //nand flash support internal inter-leave and external iner-leave, the block number is
        //same as the virtual block number in the die, the bank number is the virtual page number
        //model the inter-leave bank count, and the page number is the virtual page number
        //divide the inter-leave bank count
        tmpBnkNum = (nPage % INTERLEAVE_BANK_CNT) + (tmpDieNum * INTERLEAVE_BANK_CNT);
        tmpBlkNum = nBlock ;
        tmpPageNum = nPage / INTERLEAVE_BANK_CNT;
    }

    else if(SUPPORT_INT_INTERLEAVE && !SUPPORT_EXT_INTERLEAVE)
    {
        //nand flash support internal inter-leave but don't support external inter-leave, the block
        //number if same as the vitual block number, the bank number is virtual page number model
        //inter-leave count and add the chip bank base, the page number is the virtual page number
        //divide the inter-leave bank count
        tmpBnkNum = (nPage % INTERLEAVE_BANK_CNT) + (tmpDieNum * INTERLEAVE_BANK_CNT);
        tmpBlkNum = nBlock;
        tmpPageNum = nPage / INTERLEAVE_BANK_CNT;
    }

    else if(!SUPPORT_INT_INTERLEAVE && SUPPORT_EXT_INTERLEAVE)
    {
        //nand flash support external inter-leave but don't support internal inter-leave, the block
        //number is virtual block number add the die block base, the bank number is the page number
        //model the inter-leave bank count, the page number is vitual page number divide the inter-leave
        //bank count
        //tmpBnkNum =  (nPage % INTERLEAVE_BANK_CNT) + (tmpDieNum * INTERLEAVE_BANK_CNT);
        tmpBnkNum =  (nPage % INTERLEAVE_BANK_CNT) + (tmpDieNum/DIE_CNT_OF_CHIP )*INTERLEAVE_BANK_CNT;
        tmpBlkNum = nBlock + ((tmpDieNum % DIE_CNT_OF_CHIP) * (BLOCK_CNT_OF_DIE / PLANE_CNT_OF_DIE));
        tmpPageNum = nPage / INTERLEAVE_BANK_CNT;
    }

    else//if(!SUPPORT_INT_INTERLEAVE && !SUPPORT_EXT_INTERLEAVE)
    {
        //nand flash don't internal inter-leave and extern inter-leave either, the bank number is the
        //die number divide the die count of chip, the block number is the virtual block number add
        //the die block base in the chip, the page number is same as the virtual page number
        tmpBnkNum = tmpDieNum / DIE_CNT_OF_CHIP;
        tmpBlkNum = nBlock + (tmpDieNum % DIE_CNT_OF_CHIP) * (BLOCK_CNT_OF_DIE / PLANE_CNT_OF_DIE);
        tmpPageNum = nPage;
    }

    //set the physical operation paramter by the bank number, block number and page number
    pPhyPar->BankNum = tmpBnkNum;
    pPhyPar->PageNum = tmpPageNum;
    pPhyPar->BlkNum = tmpBlkNum;

//    FORMAT_DBG("         Calculate Result: BankNum 0x%x, BlkNum 0x%x, PageNum 0x%x\n", tmpBnkNum, tmpBlkNum, tmpPageNum);

    //calculate physical operation parameter successful
    return 0;
}

#endif


/*
************************************************************************************************************************
*                       READ PAGE DATA FROM VIRTUAL BLOCK
*
*Description: Read page data from virtual block, the block is composed by several physical block.
*             It is named super block too.
*
*Arguments  : nDieNum   the number of the DIE, which the page is belonged to;
*             nBlkNum   the number of the virtual block in the die;
*             nPage     the number of the page in the virtual block;
*             Bitmap    the bitmap of the sectors need access in the page;
*             pBuf      the pointer to the page data buffer;
*             pSpare    the pointer to the spare data buffer.
*
*Return     : read result;
*               = 0     read page data successful;
*               < 0     read page data failed.
************************************************************************************************************************
*/
static __s32 _VirtualPageRead(__u32 nDieNum, __u32 nBlkNum, __u32 nPage, __u32 SectBitmap, void *pBuf, void *pSpare)
{
    __s32 i, result;
    __u8  *tmpSrcData, *tmpDstData, *tmpSrcPtr[4], *tmpDstPtr[4];
    struct __PhysicOpPara_t tmpPhyPage;

    //calculate the physical operation parameter by te die number, block number and page number
    _CalculatePhyOpPar(&tmpPhyPage, nDieNum * ZONE_CNT_OF_DIE, nBlkNum, nPage);

    //set the sector bitmap in the page, the main data buffer and the spare data buffer
    tmpPhyPage.SectBitmap = SectBitmap;
    tmpPhyPage.MDataPtr = pBuf;
    if(pSpare)
    {
        tmpPhyPage.SDataPtr = FORMAT_SPARE_BUF;

        //process the pointer to spare area data
        for(i=0; i<2; i++)
        {
            if(SectBitmap & (1<<i))
            {
                tmpSrcPtr[i] = FORMAT_SPARE_BUF + 4 * i;
                tmpDstPtr[i] = (__u8 *)pSpare + 4 * i;
            }
            else
            {
                tmpDstPtr[i] = NULL;
            }
        }

        for(i=0; i<2; i++)
        {
            if(SectBitmap & (1<<(i + SECTOR_CNT_OF_SINGLE_PAGE)))
            {
                tmpSrcPtr[i+2] = LML_SPARE_BUF + 4 * (i + SECTOR_CNT_OF_SINGLE_PAGE);
                tmpDstPtr[i+2] = (__u8 *)pSpare + 8 + 4 * i;
            }
            else
            {
                tmpDstPtr[i+2] = NULL;
            }
        }
#if(1)
		result = PHY_PageReadSpare(&tmpPhyPage);
#else
		result = PHY_PageRead(&tmpPhyPage);
#endif
    }
    else
    {
        tmpPhyPage.SDataPtr = NULL;
		result = PHY_PageRead(&tmpPhyPage);
    }



    //process spare area data
    if(pSpare)
    {
        //get the spare area data
        for(i=0; i<4; i++)
        {
            if(tmpDstPtr[i] != NULL)
            {
                tmpSrcData = tmpSrcPtr[i];
                tmpDstData = tmpDstPtr[i];

                *tmpDstData++ = *tmpSrcData++;
                *tmpDstData++ = *tmpSrcData++;
                *tmpDstData++ = *tmpSrcData++;
                *tmpDstData++ = *tmpSrcData++;
            }
        }
    }

	return result;

    #if 0
    if(result < 0)
    {
        //some error happen when read the virtual page data, report the error type
        return result;
    }
    else
    {
        return 0;
    }
	#endif
}


/*
************************************************************************************************************************
*                       WRITE PAGE DATA TO VIRTUAL BLOCK
*
*Description: Write page data to virtual block, the block is composed by several physical block.
*             It is named super block too.
*
*Arguments  : nDieNum   the number of the DIE, which the page is belonged to;
*             nBlkNum   the number of the virtual block in the die;
*             nPage     the number of the page in the virtual block;
*             Bitmap    the bitmap of the sectors need access in the page;
*             pBuf      the pointer to the page data buffer;
*             pSpare    the pointer to the spare data buffer.
*
*Return     : write result;
*               = 0     write page data successful;
*               < 0     write page data failed.
************************************************************************************************************************
*/
static __s32 _VirtualPageWrite(__u32 nDieNum, __u32 nBlkNum, __u32 nPage, __u32 SectBitmap, void *pBuf, void *pSpare)
{
    __s32 i, result;
    __u8  *tmpSrcData, *tmpDstData, *tmpSrcPtr[4], *tmpDstPtr[4];
    struct __PhysicOpPara_t tmpPhyPage;

    //calculate the physical operation parameter by te die number, block number and page number
    _CalculatePhyOpPar(&tmpPhyPage, nDieNum * ZONE_CNT_OF_DIE, nBlkNum, nPage);

    //set the sector bitmap in the page, the main data buffer and the spare data buffer
    tmpPhyPage.SectBitmap = SectBitmap;
    tmpPhyPage.MDataPtr = pBuf;
    tmpPhyPage.SDataPtr = FORMAT_SPARE_BUF;

    //process spare area data
    if(pSpare)
    {
        //process the pointer to spare area data
        for(i=0; i<2; i++)
        {
            if(SectBitmap & (1<<i))
            {
                tmpSrcPtr[i] = (__u8 *)pSpare + 4 * i;
                tmpDstPtr[i] = FORMAT_SPARE_BUF + 4 * i;
            }
            else
            {
                tmpDstPtr[i] = NULL;
            }
        }

        for(i=0; i<2; i++)
        {
            if(SectBitmap & (1<<(i + SECTOR_CNT_OF_SINGLE_PAGE)))
            {
                tmpSrcPtr[i+2] = (__u8 *)pSpare + 8 + 4 * i;
                tmpDstPtr[i+2] = LML_SPARE_BUF + 4 * (i + SECTOR_CNT_OF_SINGLE_PAGE);
            }
            else
            {
                tmpDstPtr[i+2] = NULL;
            }
        }

        MEMSET(FORMAT_SPARE_BUF, 0xff, SECTOR_CNT_OF_SUPER_PAGE * 4);

        for(i=0; i<4; i++)
        {
            tmpSrcData = tmpSrcPtr[i];
            tmpDstData = tmpDstPtr[i];

            if(tmpDstData != NULL)
            {
                *tmpDstData++ = *tmpSrcData++;
                *tmpDstData++ = *tmpSrcData++;
                *tmpDstData++ = *tmpSrcData++;
                *tmpDstData++ = *tmpSrcData++;
            }
        }
    }
    else
    {
        MEMSET(FORMAT_SPARE_BUF, 0xff, SECTOR_CNT_OF_SUPER_PAGE * 4);
    }

    PHY_PageWrite(&tmpPhyPage);
    //physical page write module is successful, synch the operation result to check if write successful true
    result = PHY_SynchBank(tmpPhyPage.BankNum, SYNC_CHIP_MODE);
    if(result < 0)
    {
        //some error happens when synch the write operation, report the error type
        return result;
    }
    else
    {
        return 0;
    }
}


/*
************************************************************************************************************************
*                       ERASE VIRTUAL BLOCK
*
*Description: Erase a virtual blcok.
*
*Arguments  : nDieNum   the number of the DIE, which the block is belonged to;
*             nBlkNum   the number of the virtual block in the die.
*
*Return     : erase result;
*               = 0     virtual block erase successful;
*               < 0     virtual block erase failed.
************************************************************************************************************************
*/
static __s32 _VirtualBlockErase(__u32 nDieNum, __u32 nBlkNum)
{
    __s32 i, result = 0;
    struct __PhysicOpPara_t tmpPhyBlk;

    //erase every block belonged to different banks
    for(i=0; i<INTERLEAVE_BANK_CNT; i++)
    {
        //calculate the physical operation parameter by te die number, block number and page number
        _CalculatePhyOpPar(&tmpPhyBlk, nDieNum * ZONE_CNT_OF_DIE, nBlkNum, i);

        PHY_BlockErase(&tmpPhyBlk);
    }

    //check the result of the block erase
    for(i=0; i<INTERLEAVE_BANK_CNT; i++)
    {
        result = PHY_SynchBank(i, SYNC_CHIP_MODE);
        if(result < 0)
        {
            return -1;
        }
    }

    return 0;
}


/*
************************************************************************************************************************
*                       COPY ONE VIRTUAL BLOCK TO ANOTERH BLOCK
*
*Description: Copy the whole block data from one virtual block to another virtual, the
*             source block and the destination block should be in the same DIE.
*
*Arguments  : nDieNum   the number of the DIE, which the blocks belonged to;
*             nSrcBlk   the number of the source virtual block;
*             nDstBlk   the number of the destination virtual block.
*
*Return     : block copy result;
*               = 0     copy block data successful;
*               < 0     copy block data failed.
************************************************************************************************************************
*/
static __s32 _VirtualBlockCopy(__u32 nDieNum, __u32 nSrcBlk, __u32 nDstBlk)
{
    __s32 i, result = 0;
    struct __PhysicOpPara_t tmpSrcBlk, tmpDstBlk;

    //copy every page from source block to destination block
    for(i=0; i<PAGE_CNT_OF_SUPER_BLK; i++)
    {
        //calculate the physical operation paramter for the source page and destination page
        _CalculatePhyOpPar(&tmpSrcBlk, nDieNum * ZONE_CNT_OF_DIE, nSrcBlk, i);
        _CalculatePhyOpPar(&tmpDstBlk, nDieNum * ZONE_CNT_OF_DIE, nDstBlk, i);

        PHY_PageCopyback(&tmpSrcBlk, &tmpDstBlk);

        result = PHY_SynchBank(tmpDstBlk.BankNum, SYNC_CHIP_MODE);
        if(result < 0)
        {
            FORMAT_ERR("[FORMAT_ERR] Copy page write failed!\n");

            return -1;
        }
    }

    return 0;
}


/*
************************************************************************************************************************
*                       WRITE THE BAD BLOCK FLAG TO A VIRTUAL BLOCK
*
*Description: Write bad block flag to a virtual block, because there is some error happen when write
*             erase the block, because we don't know which single physical block in the virtual block
*             is bad, so, should write the bad block flag to every single physical block in the virtual
*             block, the bad block will be kicked out anyway.
*
*Arguments  : nDieNum   the number of the DIE, which the virtual block belonged to;
*             nBlock    the number of the virtual block in the die;
*
*Return     : write result;
*             return 0 always.
************************************************************************************************************************
*/
static __s32 _WriteBadBlkFlag(__u32 nDieNum, __u32 nBlock)
{
    __s32   i;
    struct __NandUserData_t tmpSpare[2];

    //set bad block flag to the spare data write to nand flash
    tmpSpare[0].BadBlkFlag = 0x00;
    tmpSpare[1].BadBlkFlag = 0x00;
    tmpSpare[0].LogicInfo = 0x00;
    tmpSpare[1].LogicInfo = 0x00;
    tmpSpare[0].LogicPageNum = 0x00;
    tmpSpare[1].LogicPageNum = 0x00;
    tmpSpare[0].PageStatus = 0x00;
    tmpSpare[1].PageStatus = 0x00;

    for(i=0; i<INTERLEAVE_BANK_CNT; i++)
    {
        //write the bad block flag on the first page
        _VirtualPageWrite(nDieNum, nBlock, i, FULL_BITMAP_OF_SUPER_PAGE, FORMAT_PAGE_BUF, (void *)&tmpSpare);

        //write the bad block flag on the last page
        _VirtualPageWrite(nDieNum, nBlock, PAGE_CNT_OF_SUPER_BLK - INTERLEAVE_BANK_CNT + i, \
                        FULL_BITMAP_OF_SUPER_PAGE, FORMAT_PAGE_BUF, (void *)&tmpSpare);
    }

    return 0;
}


#if DBG_DUMP_DIE_INFO

/*
************************************************************************************************************************
*                       DUMP DIE INFORMATION FOR DEBUG
*
*Description: Dump die information for debug nand format.
*
*Arguments  : pDieInfo   the pointer to the die information.
*
*Return     : none
************************************************************************************************************************
*/
static void _DumpDieInfo(struct __ScanDieInfo_t *pDieInfo)
{
    int tmpZone, tmpLog;
	struct __ScanZoneInfo_t *tmpZoneInfo;
	struct __LogBlkType_t   *tmpLogBlk;

    FORMAT_DBG("\n");
    FORMAT_DBG("[FORMAT_DBG] ================== Die information ================\n");
    FORMAT_DBG("[FORMAT_DBG]    Die number:         0x%x\n", pDieInfo->nDie);
    FORMAT_DBG("[FORMAT_DBG]    Super block count:  0x%x\n", SuperBlkCntOfDie);
    FORMAT_DBG("[FORMAT_DBG]    Free block count:   0x%x\n", pDieInfo->nFreeCnt);
    FORMAT_DBG("[FORMAT_DBG]    Bad block count:    0x%x\n", pDieInfo->nBadCnt);

    for(tmpZone=0; tmpZone<ZONE_CNT_OF_DIE; tmpZone++)
    {
        tmpZoneInfo = &pDieInfo->ZoneInfo[tmpZone];
        FORMAT_DBG("[FORMAT_DBG] ---------------------------------------------------\n");
        FORMAT_DBG("[FORMAT_DBG] ZoneNum:    0x%x\n", tmpZone);
        FORMAT_DBG("[FORMAT_DBG]    Data block Count:    0x%x\n", tmpZoneInfo->nDataBlkCnt);
        FORMAT_DBG("[FORMAT_DBG]    Free block Count:    0x%x\n", tmpZoneInfo->nFreeBlkCnt);
        FORMAT_DBG("[FORMAT_DBG]    Log block table: \n");
		FORMAT_DBG("       [Index]             [LogicalBlk]         [LogBlk]        [DataBlk]\n");
        for(tmpLog=0; tmpLog<MAX_LOG_BLK_CNT; tmpLog++)
        {
            tmpLogBlk = tmpLogBlk;
            tmpLogBlk = &tmpZoneInfo->LogBlkTbl[tmpLog];
            FORMAT_DBG("      %x           %x          %x        %x\n", tmpLog, tmpLogBlk->LogicBlkNum,
    		    tmpLogBlk->PhyBlk.PhyBlkNum,
    		    (tmpLogBlk->LogicBlkNum == 0xffff)? 0xffff : tmpZoneInfo->ZoneTbl[tmpLogBlk->LogicBlkNum].PhyBlkNum);
        }
    }

    FORMAT_DBG("[FORMAT_DBG] ===================================================\n");
}

#endif


/*
************************************************************************************************************************
*                           GET FREE BLOCK FROM DIE INFORMATION
*
*Description: Get free block from the die information.
*
*Arguments  : pDieInfo  the pointer to the die information;
*
*Return     : the free block number;
*               >= 0    get free block successful, return the block number;
*               < 0     get free block failed.
************************************************************************************************************************
*/
static __s32 _GetFreeBlkFromDieInfo(struct __ScanDieInfo_t *pDieInfo)
{
    __s32   i, tmpFreeBlk;

    tmpFreeBlk = -1;

    //scan the logical information array, look for a free block
    for(i=pDieInfo->nFreeIndex; i<SuperBlkCntOfDie; i++)
    {
        if(pDieInfo->pPhyBlk[i] == FREE_BLOCK_INFO)
        {
            tmpFreeBlk = i;

            pDieInfo->pPhyBlk[i] = NULL_BLOCK_INFO;

            pDieInfo->nFreeIndex = i;

            pDieInfo->nFreeCnt--;

            break;
        }
    }

    return tmpFreeBlk;
}


/*
************************************************************************************************************************
*                           GET THE ZONE NUMBEB WHICH HAS LEAST BLOCK
*
*Description: Get the number of zone, which has the least count blocks, include data block, free
*             block and log block; assure the blocks is proportioned in every block mapping table.
*
*Arguments  : pDieInfo  the pointer to the die information;
*
*Return     : the number of the zone which has least blocks;
************************************************************************************************************************
*/
static __s32 _LeastBlkCntZone(struct __ScanDieInfo_t *pDieInfo)
{
    __s32 i, tmpZone, tmpBlkCnt, tmpLeastCntBlkZone = 0, tmpLeastBlkCnt = 0xffff;
    struct __LogBlkType_t *tmpLogBlkTbl;

    for(tmpZone=0; tmpZone<ZONE_CNT_OF_DIE; tmpZone++)
    {
        //skip valid block mapping table
        if(pDieInfo->TblBitmap & (1 << tmpZone))
        {
            continue;
        }

        tmpBlkCnt = DATA_BLK_CNT_OF_ZONE + pDieInfo->ZoneInfo[tmpZone].nFreeBlkCnt;

        //check the log block count of the zone
        tmpLogBlkTbl = pDieInfo->ZoneInfo[tmpZone].LogBlkTbl;
        for(i=0; i<MAX_LOG_BLK_CNT; i++)
        {
            if(tmpLogBlkTbl[i].LogicBlkNum != 0xffff)
            {
                tmpBlkCnt++;
            }
        }

        if(tmpBlkCnt < tmpLeastBlkCnt)
        {
            tmpLeastBlkCnt = tmpBlkCnt;
            tmpLeastCntBlkZone = tmpZone;
        }
    }

    return tmpLeastCntBlkZone;
}


/*
************************************************************************************************************************
*                           MERGE DATA BLOCK TO FREE BLOCK
*
*Description: Merge a data block to a free block, for block replace.
*
*Arguments  : pDieInfo  the pointer to the die information;
*             nDataBlk  the number of the data block;
*             nFreeBlk  the number of the free block.
*
*Return     : merge result;
*               = 0     merge successful;
*               < 0     merge failed.
************************************************************************************************************************
*/
static __s32 _MergeDataBlkToFreeBlk(struct __ScanDieInfo_t *pDieInfo, __u32 nDataBlk, __u32 nFreeBlk)
{
    __s32 result;

    //copy the data in the data block to the free block
    result = _VirtualBlockCopy(pDieInfo->nDie, nDataBlk, nFreeBlk);
    if(result < 0)
    {
        //copy the data failed, the free block may be a bad block
        _WriteBadBlkFlag(pDieInfo->nDie, nFreeBlk);

        return -1;
    }

    //modify the logical information of the free block in the die information
    pDieInfo->pPhyBlk[nFreeBlk] = pDieInfo->pPhyBlk[nDataBlk];
    pDieInfo->ZoneInfo[GET_LOGIC_INFO_ZONE(pDieInfo-> \
            pPhyBlk[nDataBlk])].ZoneTbl[GET_LOGIC_INFO_BLK(pDieInfo->pPhyBlk[nDataBlk])].PhyBlkNum = nFreeBlk;

    //erase the data block for saving block mapping table
    result = _VirtualBlockErase(pDieInfo->nDie, nDataBlk);
    if(result < 0)
    {
        //erase the data block failed, the block may be a bad block
        _WriteBadBlkFlag(pDieInfo->nDie, nDataBlk);

        return -1;
    }

    return 0;
}


/*
************************************************************************************************************************
*                       LOOK FOR THE BLOCK MAPPING TABLE POSITION
*
*Description: Look for the block-mapping-table position in the virtual block.
*
*Arguments  : nDieNum   the number of the DIE, which the virtual block belonged to;
*             nBlock    the number of the virtual block in the die;
*
*Return     : the page number which is the block-mapping-table position;
*               >=0     find the last group;
*               < 0     look for the last group failed.
************************************************************************************************************************
*/
static __u32 _SearchBlkTblPst(__u32 nDieNum, __u32 nBlock)
{
    struct __NandUserData_t tmpSpare;

    __s32   tmpLowPage, tmpHighPage, tmpMidPage;
    __u32   tmpPage;

    //use bisearch algorithm to look for the last group of the block mapping table in the super block
    tmpLowPage = 0;
    tmpHighPage = PAGE_CNT_OF_SUPER_BLK / PAGE_CNT_OF_TBL_GROUP - 1;
    while(tmpLowPage <= tmpHighPage)
    {
        //calcualte the number of the page which need be read currently
        tmpMidPage = (tmpLowPage + tmpHighPage) / 2;
        tmpPage = tmpMidPage * PAGE_CNT_OF_TBL_GROUP;

        //get the spare data of the page to check if the page has been used
        _VirtualPageRead(nDieNum, nBlock, tmpPage, 0x3, FORMAT_PAGE_BUF, (void *)&tmpSpare);

        if(tmpSpare.PageStatus == FREE_PAGE_MARK)
        {
            //look for the last table group in the front pages
            tmpHighPage = tmpMidPage - 1;
        }
        else
        {
            //look for the last table group in the hind pages
            tmpLowPage = tmpMidPage + 1;
        }
    }

    //calculate the number of the page which is the first page in the last table page group
    tmpPage = ((tmpLowPage + tmpHighPage) / 2) * PAGE_CNT_OF_TBL_GROUP;

    return tmpPage;
}


/*
************************************************************************************************************************
*                       LOOK FOR THE LAST USED PAGE IN A SUPER BLOCK
*
*Description: Look for the last used page, which is the last page page in the used group.
*
*Arguments  : nDieNum   the number of the DIE, which the virtual block belonged to;
*             nBlock    the number of the virtual block in the die;
*
*Return     : the page number of the last page in the used group;
*               >= 0    find the last used page;
*               <  0    look for the last used page failed.
************************************************************************************************************************
*/
static __s32 _GetLastUsedPage(__u32 nDieNum, __u32 nBlock)
{
    __s32   tmpLowPage, tmpHighPage, tmpMidPage, tmpPage, tmpUsedPage = 0;
    struct __NandUserData_t tmpSpare;

    //use bisearch algorithm to look for the last page in the used page group

    if(SUPPORT_ALIGN_NAND_BNK)
    {
        __u32   tmpBnkNum;
        __u8    tmpPageStatus;

        tmpLowPage = 0;
        tmpHighPage = PAGE_CNT_OF_PHY_BLK - 1;

        while(tmpLowPage <= tmpHighPage)
        {
            tmpPageStatus = FREE_PAGE_MARK;
            tmpMidPage = (tmpLowPage + tmpHighPage) / 2;

            //if support bank align, there may be some free pages in the used page group
            for(tmpBnkNum=0; tmpBnkNum<INTERLEAVE_BANK_CNT; tmpBnkNum++)
            {
                //read pages to check if the page is free
                tmpPage = tmpMidPage * INTERLEAVE_BANK_CNT + tmpBnkNum;
                _VirtualPageRead(nDieNum, nBlock, tmpPage, 0x3, FORMAT_PAGE_BUF, (void *)&tmpSpare);

                if((tmpSpare.PageStatus == FREE_PAGE_MARK) && (tmpSpare.LogicPageNum == 0xffff))
                {
                    //current page is a free page
                    continue;
                }

                tmpPageStatus &= DATA_PAGE_MARK;
                tmpUsedPage = tmpPage;
            }

            if(tmpPageStatus == FREE_PAGE_MARK)
            {
                //look for the last table group in the front pages
                tmpHighPage = tmpMidPage - 1;
            }
            else
            {
                //look for the last table group in the hind pages
                tmpLowPage = tmpMidPage + 1;
            }
        }

    }
    else
    {

        tmpLowPage = 0;
        tmpHighPage = PAGE_CNT_OF_SUPER_BLK - 1;

        while(tmpLowPage <= tmpHighPage)
        {
            tmpMidPage = (tmpLowPage + tmpHighPage) / 2;

            //get the spare area data of the page to check if the page is free
            _VirtualPageRead(nDieNum, nBlock, tmpMidPage, 0x3, FORMAT_PAGE_BUF, (void *)&tmpSpare);
            tmpUsedPage = tmpMidPage;

            if((tmpSpare.PageStatus == FREE_PAGE_MARK) && (tmpSpare.LogicPageNum == 0xffff))
            {
     		    //look for the last table group in the front pages
                tmpHighPage = tmpMidPage - 1;
                //tmpUsedPage = tmpMidPage;
            }
            else
            {
                //look for the last table group in the hind pages
                tmpLowPage = tmpMidPage + 1;
            }
        }

    }

    return tmpUsedPage;
}


/*
************************************************************************************************************************
*                       CALCULATE THE CHECKSUM FOR A MAPPING TABLE
*
*Description: Calculate the checksum for a mapping table, based on word.
*
*Arguments  : pTblBuf   the pointer to the table data buffer;
*             nLength   the size of the table data, based on word.
*
*Return     : table checksum;
************************************************************************************************************************
*/
static __u32 _CalCheckSum(__u32 *pTblBuf, __u32 nLength)
{
    __s32 i;
    __u32 tmpCheckSum = 0;
    __u32 *tmpItem = pTblBuf;

    for(i=0; i<nLength; i++)
    {
        tmpCheckSum += *tmpItem;
        tmpItem++;
    }

    return tmpCheckSum;
}


/*
************************************************************************************************************************
*                       GET THE LOGICAL INFORMATION OF PHYSICAL BLOCKS
*
*Description: Get the logical information of every physial block of the die.
*
*Arguments  : pDieInfo   the pointer to the die information whose logical block information need be got.
*
*Return     : get logical information result;
*               = 0     get logical information successful;
*               < 0     get logical information failed.
************************************************************************************************************************
*/
static __s32 _GetBlkLogicInfo(struct __ScanDieInfo_t *pDieInfo)
{
    __u32   tmpBlkNum, tmpBnkNum, tmpPage, tmpBadFlag;
    __s32   i;
    __s16   tmpPageNum[4];
    __u16   tmpLogicInfo;
    __u32   spare_bitmap;
    struct  __NandUserData_t tmpSpare[2];


    //initiate the number of the pages which need be read, the first page is read always, because the
    //the logical information is stored in the first page, other pages is read for check bad block flag
    tmpPageNum[0] = 0;
    tmpPageNum[1] = -1;
    tmpPageNum[2] = -1;
    tmpPageNum[3] = -1;

	tmpLogicInfo = 0xffff;

    //analyze the number of pages which need be read
    switch(BAD_BLK_FLAG_PST & 0x03)
    {
        case 0x00:
            //the bad block flag is in the first page, same as the logical information, just read 1 page is ok
            break;

        case 0x01:
            //the bad block flag is in the first page or the second page, need read the first page and the second page
            tmpPageNum[1] = 1;
            break;

        case 0x02:
            //the bad block flag is in the last page, need read the first page and the last page
            tmpPageNum[1] = PAGE_CNT_OF_PHY_BLK - 1;
            break;

        case 0x03:
            //the bad block flag is in the last 2 page, so, need read the first page, the last page and the last-1 page
            tmpPageNum[1] = PAGE_CNT_OF_PHY_BLK - 1;
            tmpPageNum[2] = PAGE_CNT_OF_PHY_BLK - 2;
            break;
    }

    //read every super block to get the logical information and the bad block flag
    for(tmpBlkNum=0; tmpBlkNum<SuperBlkCntOfDie; tmpBlkNum++)
    {
        //initiate the bad block flag
        tmpBadFlag = 0;

        //the super block is composed of several physical blocks in several banks
        for(tmpBnkNum=0; tmpBnkNum<INTERLEAVE_BANK_CNT; tmpBnkNum++)
        {
            for(i=3; i>=0; i--)
            {
                if(tmpPageNum[i] == -1)
                {
                    //need not check page
                    continue;
                }

                //calculate the number of the page in the super block to get spare data
                tmpPage = tmpPageNum[i] * INTERLEAVE_BANK_CNT + tmpBnkNum;
                //_VirtualPageRead(pDieInfo->nDie, tmpBlkNum, tmpPage, LOGIC_INFO_BITMAP, FORMAT_PAGE_BUF, (void *)&tmpSpare);
                spare_bitmap = (SUPPORT_MULTI_PROGRAM ? (0x3 | (0x3 << SECTOR_CNT_OF_SINGLE_PAGE)) : 0x3);
                _VirtualPageRead(pDieInfo->nDie, tmpBlkNum, tmpPage, spare_bitmap, FORMAT_PAGE_BUF, (void *)&tmpSpare);

				//check if the block is a bad block
                if((tmpSpare[0].BadBlkFlag != 0xff) || (SUPPORT_MULTI_PROGRAM && (tmpSpare[1].BadBlkFlag != 0xff)))
                {
                    //set the bad flag of the physical block
                    tmpBadFlag = 1;
                }

                if(tmpPage == 0)
                {
                    //get the logical information of the physical block
                    tmpLogicInfo = tmpSpare[0].LogicInfo;
                }
            }
        }

        if(tmpBadFlag == 1)
        {
            //the physical block is a bad block, set bad block flag in the logical information buffer
            FORMAT_DBG("[FORMAT_DBG] Find a bad block (NO. 0x%x) in the Die 0x%x\n", tmpBlkNum, pDieInfo->nDie);
            pDieInfo->pPhyBlk[tmpBlkNum] = BAD_BLOCK_INFO;

            continue;
        }

        //set the logical information for the valid physical block
        pDieInfo->pPhyBlk[tmpBlkNum] = tmpLogicInfo;
    }

    return 0;
}


/*
************************************************************************************************************************
*                       GET LOG AGE FROM PHYSICAL BLOCK
*
*Description: Get log age from physical block. the log age is stored in the spare area of the physical block.
*
*Arguments  : nDie      the number of the die which the physical block is belonged to;
*             nPhyBlk   the number of the physical block whose log age need be get.
*
*Return     : the log age of the physical block;
************************************************************************************************************************
*/
static __u8 _GetLogAge(__u32 nDie, __u16 nPhyBlk)
{
    __u8    tmpLogAge;
    struct __NandUserData_t tmpSpareData;

    //read the first page of the super block to get spare area data
    _VirtualPageRead(nDie, nPhyBlk, 0, 0x3, FORMAT_PAGE_BUF, (void *)&tmpSpareData);

    //the log age area is same as the page status area
    tmpLogAge = tmpSpareData.PageStatus;

    return tmpLogAge;
}


/*
************************************************************************************************************************
*               FILL A PHYSCIAL BLOCK TO THE BLOCK MAPPING TABLE
*
*Description: Fill a physical block to the block mapping table, one logical block may be related with
*             one data block and one log block at most. if the logical block contain a data and a log
*             block, check the age value to decide which block is the data block and which is the log block.
*
*Arguments  : pDieInfo      the pointer to the die information the physical block is belonged to;
*             pLogicInfo    the logical information of the super block;
*             nPhyBlk       the number of the physical block in the die;
*             pEraseBlk     the pointer to the block which need be erased;
*
*Return     : fill result;
*               = 0     fill block successful;
*               < 0     fill block failed.
************************************************************************************************************************
*/
static __s32 _FillBlkToZoneTbl(struct __ScanDieInfo_t *pDieInfo, __u16 nLogicInfo, __u16 nPhyBlk, __u32 *pEraseBlk)
{
    __s32   i, tmpLogPst;
    __u32   tmpZone, tmpLogicBlk;
    __u16   tmpDataBlk, tmpLogBlk, tmpNewBlk;
    //__u8    tmpAgeData, tmpAgeLog, tmpAgeNew;
    __u8    tmpAgeData, tmpAgeNew;
    __s32   tmpLastPageOfData, tmpLastPageOfLog, tmpLastPageOfNew;
    struct __SuperPhyBlkType_t *tmpSuperBlk;

    tmpZone = GET_LOGIC_INFO_ZONE(nLogicInfo);
    tmpLogicBlk = GET_LOGIC_INFO_BLK(nLogicInfo);
    tmpSuperBlk = (struct __SuperPhyBlkType_t *)&pDieInfo->ZoneInfo[tmpZone].ZoneTbl[tmpLogicBlk];

    *pEraseBlk = 0xffff;

    //check if there is a data block in the data block table already
    if(tmpSuperBlk->PhyBlkNum == 0xffff)
    {
        //the block is the first physical block related to the logical block, we consider it is a data block
        tmpSuperBlk->PhyBlkNum = nPhyBlk;
        tmpSuperBlk->BlkEraseCnt = 0;
        pDieInfo->ZoneInfo[tmpZone].nDataBlkCnt++;

        return 0;
    }

    tmpDataBlk = tmpSuperBlk->PhyBlkNum;
    tmpNewBlk = nPhyBlk;
    tmpLogBlk = 0xffff;
    //get the log age from the data block and the new block
    tmpAgeData = _GetLogAge(pDieInfo->nDie, tmpDataBlk);
    tmpAgeNew = _GetLogAge(pDieInfo->nDie, tmpNewBlk);
    tmpLogPst = -1;

    //there is a data block in the data block table already, check if the logical block contain a log block
    for(i=0; i<MAX_LOG_BLK_CNT; i++)
    {
        if(pDieInfo->ZoneInfo[tmpZone].LogBlkTbl[i].LogicBlkNum == tmpLogicBlk)
        {
            //find the item in the log block table
            tmpLogBlk = pDieInfo->ZoneInfo[tmpZone].LogBlkTbl[i].PhyBlk.PhyBlkNum;
            tmpLogPst = i;
            break;
        }
    }
    //if(tmpLogPst != -1)
    //{
        //get the log age from the log block
    //    tmpAgeLog = _GetLogAge(pDieInfo->nDie, tmpLogBlk);
    //}

    //compare the log age of current block with the data block
    if(COMPARE_AGE(tmpAgeNew, tmpAgeData) == 0)
    {
        //the log age of current block is same as the data block
        tmpLastPageOfData = _GetLastUsedPage(pDieInfo->nDie, tmpDataBlk);
        tmpLastPageOfNew = _GetLastUsedPage(pDieInfo->nDie, tmpNewBlk);

        if(tmpLastPageOfNew > tmpLastPageOfData)
        {
            //replace the the data block with the new block, because the new page has more used pages
            tmpSuperBlk->PhyBlkNum = tmpNewBlk;
            *pEraseBlk = tmpDataBlk;

            return 0;
        }
        else
        {
            //the new block has less pages than the data block
            *pEraseBlk = tmpNewBlk;

            return 0;
        }
    }
    else if(COMPARE_AGE(tmpAgeNew, tmpAgeData) > 0)
    {
        //the log age of current block is larger than the data block
        if(tmpAgeNew == ((tmpAgeData+1) & 0xff))
        {
            //the log age of the new block is sequential with the data block, need check the log block
            if(tmpLogPst != -1)
            {
                //there is a log block already, need check which block need be erased
                tmpLastPageOfNew = _GetLastUsedPage(pDieInfo->nDie, tmpNewBlk);
                tmpLastPageOfLog = _GetLastUsedPage(pDieInfo->nDie, tmpLogBlk);

                if(tmpLastPageOfNew > tmpLastPageOfLog)
                {
                    //the new block has more used page than the log block, replace it
                    pDieInfo->ZoneInfo[tmpZone].LogBlkTbl[i].PhyBlk.PhyBlkNum = tmpNewBlk;
                    *pEraseBlk = tmpLogBlk;

                    return 0;
                }
                else
                {
                    //the new block need be erased
                    *pEraseBlk = tmpNewBlk;

                    return 0;
                }
            }
            else
            {
                //the new block should be the log block, look for a empty item in the log block table
                for(i=0; i<MAX_LOG_BLK_CNT; i++)
                {
                    if(pDieInfo->ZoneInfo[tmpZone].LogBlkTbl[i].LogicBlkNum == 0xffff)
                    {
                        //find and empty item in the log block table
                        tmpLogPst = i;
                        break;
                    }
                }

                if(tmpLogPst != -1)
                {
                    //add the new bock to the log block table
                    pDieInfo->ZoneInfo[tmpZone].LogBlkTbl[tmpLogPst].LogicBlkNum = GET_LOGIC_INFO_BLK(nLogicInfo);
                    pDieInfo->ZoneInfo[tmpZone].LogBlkTbl[tmpLogPst].PhyBlk.PhyBlkNum = tmpNewBlk;

                    return 0;
                }
                else
                {
                    //there is no free item in the log block table, add the log block failed, erase the block
                    *pEraseBlk = tmpNewBlk;

                    return 0;
                }
            }
        }
        else
        {
            FORMAT_DBG("[FORMAT_DBG] The log age of block(logicInfo:0x%x) are not sequential, "
                "age is:0x%x, 0x%x\n", nLogicInfo, tmpAgeData, tmpAgeNew);

            //the new block need be erased
            *pEraseBlk = tmpNewBlk;

            return 0;
        }
    }
    else
    {
        //the log age of the new block is smaller than the data block
        if(tmpAgeData == ((tmpAgeNew+1) & 0xff))
        {
            //the new block should be the data block
            tmpSuperBlk->PhyBlkNum = tmpNewBlk;
            if(tmpLogPst != -1)
            {
                //the log block should be erased
                *pEraseBlk = tmpLogBlk;
            }
            else
            {
                //look for a free empty item in the log block table
                for(i=0; i<MAX_LOG_BLK_CNT; i++)
                {
                    if(pDieInfo->ZoneInfo[tmpZone].LogBlkTbl[i].LogicBlkNum == 0xffff)
                    {
                        //find and empty item in the log block table
                        tmpLogPst = i;
                        break;
                    }
                }

                if(tmpLogPst == -1)
                {
                    //look for free item in the log block table failed, erase the new block
                    *pEraseBlk = tmpDataBlk;

                    return 0;
                }

                //make a new log block item in the log block table
                pDieInfo->ZoneInfo[tmpZone].LogBlkTbl[tmpLogPst].LogicBlkNum = GET_LOGIC_INFO_BLK(nLogicInfo);
            }

            //the data block should be log block
            pDieInfo->ZoneInfo[tmpZone].LogBlkTbl[tmpLogPst].PhyBlk.PhyBlkNum = tmpDataBlk;

            return 0;
        }
        else
        {
            FORMAT_DBG("[FORMAT_DBG] The log age of block(logicInfo:0x%x) are not sequential, "
                "age is:0x%x, 0x%x\n", nLogicInfo, tmpAgeNew, tmpAgeData);

            //replace the data block with the new block, because the new block has a lower log age
            tmpSuperBlk->PhyBlkNum = tmpNewBlk;
            //the data block need be erased
            *pEraseBlk = tmpDataBlk;

            return 0;
        }
    }

    //return 0;
}


/*
************************************************************************************************************************
*                       GET A NEW PHYSICAL BLOCK FOR SAVING TABLE
*
*Description: Get a new physical block for saving block mapping table, the block should be in the
*             table block area, if can't find block in the block table area anyway, then, get a
*             free block not in the block area is ok, in this case, it will need rebuild the block
*             mapping table when installed the nand flash driver every time.
*
*Arguments  : pDieInfo  the pointer to the die information;
*             nZone     the number of the zone in the die.
*
*Return     : the block number.
*               >=0     get block successful, the return value is the number of the block.
*               < 0     get block failed.
************************************************************************************************************************
*/
static __s32 _GetNewTableBlk(struct __ScanDieInfo_t *pDieInfo, __u32 nZone)
{
    __s32   i, tmpBlk, tmpFreeBlk, tmpStartBlk, result;
    __s32   tmpValidTblBlk, tmpInvalidTblBlk, tmpValidTblFreeBlk;

    if(pDieInfo->nDie == 0)
    {
        tmpStartBlk = DIE0_FIRST_BLK_NUM;
    }
    else
    {
        tmpStartBlk = 0;
    }

    //try to find a free block in the block mapping table block area
    for(tmpBlk=tmpStartBlk; tmpBlk<TBL_AREA_BLK_NUM; tmpBlk++)
    {
        if(pDieInfo->pPhyBlk[tmpBlk] == FREE_BLOCK_INFO)
        {
            //find a free block in the table block area
            pDieInfo->pPhyBlk[tmpBlk] = NULL_BLOCK_INFO;
            pDieInfo->nFreeCnt--;

            return tmpBlk;
        }
    }

    //can't find a free block in the block mapping table block area

    tmpValidTblBlk = -1;
    tmpInvalidTblBlk = -1;
    tmpValidTblFreeBlk = -1;

    for(tmpBlk=tmpStartBlk; tmpBlk<TBL_AREA_BLK_NUM; tmpBlk++)
    {
        if((pDieInfo->pPhyBlk[tmpBlk] & ALLOC_BLK_MARK) == ALLOC_BLK_MARK)
        {
            //current block is a free block that has been allocated to a valid block mapping table
            if(tmpValidTblFreeBlk == -1)
            {
                tmpValidTblFreeBlk = tmpBlk;
            }

            continue;
        }

        if(!GET_LOGIC_INFO_TYPE(pDieInfo->pPhyBlk[tmpBlk]))
        {
            //skip the special used type block for boot code, other block mapping table ex...
            if((tmpValidTblBlk == -1) && (pDieInfo->ZoneInfo[GET_LOGIC_INFO_ZONE(pDieInfo-> \
                pPhyBlk[tmpBlk])].ZoneTbl[GET_LOGIC_INFO_BLK(pDieInfo->pPhyBlk[tmpBlk])].PhyBlkNum == tmpBlk))
            {
                //get the first valid block in the die, we need select a data block, skip the log block
                tmpValidTblBlk = tmpBlk;
            }

            if((tmpInvalidTblBlk == -1) && (pDieInfo->ZoneInfo[GET_LOGIC_INFO_ZONE(pDieInfo-> \
                pPhyBlk[tmpBlk])].ZoneTbl[GET_LOGIC_INFO_BLK(pDieInfo->pPhyBlk[tmpBlk])].PhyBlkNum == tmpBlk))
            {
                if(!(pDieInfo->TblBitmap & (1 << GET_LOGIC_INFO_ZONE(pDieInfo->pPhyBlk[tmpBlk]))))
                {
                    tmpInvalidTblBlk = tmpBlk;

                    break;
                }
            }
        }
    }

    //look for a free block to replace the data block
    tmpFreeBlk = _GetFreeBlkFromDieInfo(pDieInfo);
    if(tmpFreeBlk < 0)
    {
        //look for a free block failed, there is too less free block
        FORMAT_ERR("[FORMAT_ERR] Look for a free block  failed, not enough valid blocks\n");

        return -1;
    }

    pDieInfo->nFreeCnt--;

    if(tmpInvalidTblBlk != -1)
    {
        result = _MergeDataBlkToFreeBlk(pDieInfo, tmpInvalidTblBlk, tmpFreeBlk);
        if(result < 0)
        {
            //replace the data block with the free block failed
            return -1;
        }

        return tmpInvalidTblBlk;
    }
    else if (tmpValidTblFreeBlk != -1)
    {
        struct __SuperPhyBlkType_t *tmpMapTbl = pDieInfo->ZoneInfo[GET_LOGIC_INFO_ZONE(pDieInfo-> \
                                pPhyBlk[tmpValidTblFreeBlk])].ZoneTbl;

        //set the valid block mapping table to invlalid
        pDieInfo->TblBitmap &= ~(1 << GET_LOGIC_INFO_ZONE(pDieInfo->pPhyBlk[tmpValidTblFreeBlk]));

        //replace the block in the table by the free block
        for(i=0; i<DATA_BLK_CNT_OF_ZONE - 1; i++)
        {
            if(tmpMapTbl[i].PhyBlkNum == tmpValidTblFreeBlk)
            {
                pDieInfo->pPhyBlk[tmpFreeBlk] = pDieInfo->pPhyBlk[tmpValidTblFreeBlk];

                tmpMapTbl[i].PhyBlkNum = tmpFreeBlk;
            }
        }

        for(i=DATA_BLK_CNT_OF_ZONE; i<BLOCK_CNT_OF_ZONE - 1; i++)
        {
            if(tmpMapTbl[i].PhyBlkNum == tmpValidTblFreeBlk)
            {
                pDieInfo->pPhyBlk[tmpFreeBlk] = pDieInfo->pPhyBlk[tmpValidTblFreeBlk];

                tmpMapTbl[i].PhyBlkNum = tmpFreeBlk;
            }

            if(tmpMapTbl[i].PhyBlkNum != 0xffff)
            {
                //release the blocks in the free block table
                pDieInfo->pPhyBlk[tmpMapTbl[i].PhyBlkNum] = FREE_BLOCK_INFO;
                pDieInfo->nFreeCnt++;

                tmpMapTbl[i].PhyBlkNum = 0xffff;
            }
        }

        //set the data block count and the free block count
        pDieInfo->ZoneInfo[GET_LOGIC_INFO_ZONE(pDieInfo-> \
                                pPhyBlk[tmpValidTblFreeBlk])].nDataBlkCnt = DATA_BLK_CNT_OF_ZONE;

        pDieInfo->ZoneInfo[GET_LOGIC_INFO_ZONE(pDieInfo-> \
                                pPhyBlk[tmpValidTblFreeBlk])].nFreeBlkCnt = 0;

        return tmpValidTblFreeBlk;
    }
    else if(tmpValidTblBlk != -1)
    {
        struct __SuperPhyBlkType_t *tmpMapTbl = pDieInfo->ZoneInfo[GET_LOGIC_INFO_ZONE(pDieInfo-> \
                                pPhyBlk[tmpValidTblBlk])].ZoneTbl;

        //set the valid block mapping table to invlalid
        pDieInfo->TblBitmap &= ~(1 << GET_LOGIC_INFO_ZONE(pDieInfo->pPhyBlk[tmpBlk]));
        pDieInfo->nFreeCnt--;

        result = _MergeDataBlkToFreeBlk(pDieInfo, tmpValidTblBlk, tmpFreeBlk);

        //release the blocks in the free block table
        for(i=DATA_BLK_CNT_OF_ZONE; i<BLOCK_CNT_OF_ZONE - 1; i++)
        {
            if(tmpMapTbl[i].PhyBlkNum != 0xffff)
            {
                //release the blocks in the free block table
                pDieInfo->pPhyBlk[tmpMapTbl[i].PhyBlkNum] = FREE_BLOCK_INFO;
                pDieInfo->nFreeCnt++;

                tmpMapTbl[i].PhyBlkNum = 0xffff;
            }
        }

        //set the data block count and the free block count
        pDieInfo->ZoneInfo[GET_LOGIC_INFO_ZONE(pDieInfo-> \
                                pPhyBlk[tmpValidTblFreeBlk])].nDataBlkCnt = DATA_BLK_CNT_OF_ZONE;

        pDieInfo->ZoneInfo[GET_LOGIC_INFO_ZONE(pDieInfo-> \
                                pPhyBlk[tmpValidTblFreeBlk])].nFreeBlkCnt = 0;

        if(result < 0)
        {
            return -1;
        }

        return tmpValidTblBlk;
    }
    else
    {
        //look for a valid block in the table block area for table failed
        FORMAT_DBG("[FORMAT_DBG] Look for a block in table block area for table failed\n");

        return tmpFreeBlk;
    }
}


/*
************************************************************************************************************************
*                       GET PHYSICAL BLOCK FOR SAVING BLOCK MAPPING TABLE
*
*Description: Get physical block for saving block mapping table, the physical block should be in
*             the block mapping table block area, which is in front of the die.
*
*Arguments  : pDieInfo   the pointer to the die information.
*
*Return     : get mapping table block result;
*               = 0     get mapping table block successful;
*               < 0     get mapping table block failed.
************************************************************************************************************************
*/
static __s32 _GetMapTblBlock(struct __ScanDieInfo_t *pDieInfo)
{
    __s32   i, result, tmpTryGet;

    for(i=0; i<ZONE_CNT_OF_DIE; i++)
    {
        //check if the physical block for saving block mapping table is already exist
        if(pDieInfo->TblBitmap & (1 << i))
        {
            //the block mapping table is valid, the mapping table block is already ok
            continue;
        }

        if(ZoneTblPstInfo[i + pDieInfo->nDie * ZONE_CNT_OF_DIE].PhyBlkNum < TBL_AREA_BLK_NUM)
        {
            //the block for saving block mapping table is ok
            continue;
        }

        tmpTryGet = 0;
        while(tmpTryGet < 5)
        {
            //try to get several times
            tmpTryGet++;

            //get a new physical block for saving block mapping table
            result = _GetNewTableBlk(pDieInfo, i);
            if(!(result < 0))
            {
                break;
            }
        }

        if(result < 0)
        {
            //get new physical block in table area for saving block mapping table failed
            FORMAT_ERR("[FORMAT_ERR] Get new physical block for mapping table failed in die 0x%x!\n", pDieInfo->nDie);
            return -1;
        }


        //set the block mapping table position parameter
        ZoneTblPstInfo[i + pDieInfo->nDie * ZONE_CNT_OF_DIE].PhyBlkNum = result;
        ZoneTblPstInfo[i + pDieInfo->nDie * ZONE_CNT_OF_DIE].TablePst = 0xffff;

        //mark the physical block as a block mapping table block in the die informaion
        pDieInfo->pPhyBlk[result] = TABLE_BLK_MARK | (i<<10) | (1<<14);
    }

    return 0;
}


/*
************************************************************************************************************************
*                       KICK FREE BLOCKS FROM VALID BLOCK TABLE
*
*Description: Kick free blocks from valid block table, the other free blocks is used for allocating to
*             the block tables which need be rebuiled.
*
*Arguments  : pDieInfo   the pointer to the die information.
*
*Return     : kick free block result;
*               = 0     kick free block successful;
*               < 0     kick free block failed.
************************************************************************************************************************
*/
static __s32 _KickValidTblBlk(struct __ScanDieInfo_t *pDieInfo)
{
    __s32   i, j;
    __u32   tmpTblBlk, tmpTblPage;
    __u16   tmpPhyBlk;

    for(i=0; i<ZONE_CNT_OF_DIE; i++)
    {
        //check if the block mapping table of the zone is valid
        if(!(pDieInfo->TblBitmap & (1 << i)))
        {
            //block mapping table is invalid, ignore the zone table
            continue;
        }

        tmpTblBlk = ZoneTblPstInfo[i + pDieInfo->nDie * ZONE_CNT_OF_DIE].PhyBlkNum;
        tmpTblPage = ZoneTblPstInfo[i + pDieInfo->nDie * ZONE_CNT_OF_DIE].TablePst;
        //read the block mapping table to the block mapping table buffer
        _VirtualPageRead(pDieInfo->nDie, tmpTblBlk, tmpTblPage+DATA_TBL_OFFSET, DATA_TABLE_BITMAP, FORMAT_PAGE_BUF, NULL);
        MEMCPY(pDieInfo->ZoneInfo[i].ZoneTbl, FORMAT_PAGE_BUF, SECTOR_SIZE * 4);
        _VirtualPageRead(pDieInfo->nDie, tmpTblBlk, tmpTblPage+DATA_TBL_OFFSET+1, DATA_TABLE_BITMAP, FORMAT_PAGE_BUF, NULL);
        MEMCPY(&pDieInfo->ZoneInfo[i].ZoneTbl[BLOCK_CNT_OF_ZONE / 2], FORMAT_PAGE_BUF, SECTOR_SIZE * 4);
        //read the log table the block mapping table buffer
        _VirtualPageRead(pDieInfo->nDie, tmpTblBlk, tmpTblPage+LOG_TBL_OFFSET, LOG_TABLE_BITMAP, FORMAT_PAGE_BUF, NULL);
        MEMCPY(&pDieInfo->ZoneInfo[i].LogBlkTbl, FORMAT_PAGE_BUF, MAX_LOG_BLK_CNT * sizeof(struct __LogBlkType_t));

        for(j=0; j<BLOCK_CNT_OF_ZONE-1; j++)
        {
            tmpPhyBlk = pDieInfo->ZoneInfo[i].ZoneTbl[j].PhyBlkNum;

            if(tmpPhyBlk == 0xffff)
            {
                //the table item is empty
                continue;
            }

            if(pDieInfo->pPhyBlk[tmpPhyBlk] == FREE_BLOCK_INFO)
            {
                //the free block has been used by current valid zone, kick the free block
                pDieInfo->pPhyBlk[tmpPhyBlk] = (ALLOC_BLK_MARK | (i<<10));
                pDieInfo->nFreeCnt--;
            }
        }
    }

    return 0;
}


/*
************************************************************************************************************************
*                       REPAIR THE LOG BLOCK TABLE FOR A DIE
*
*Description: Repair the log block table for a die.
*
*Arguments  : pDieInfo   the pointer to the die information.
*
*Return     : repair log block table result;
*               = 0     repair log block table successful;
*               < 0     repair log block table failed.
************************************************************************************************************************
*/
static __s32 _RepairLogBlkTbl(struct __ScanDieInfo_t *pDieInfo)
{

    __s32 i, tmpZone, tmpLastUsedPage;
    struct __LogBlkType_t *tmpLogTbl;

    for(tmpZone=0; tmpZone<ZONE_CNT_OF_DIE; tmpZone++)
    {
        //skip the valid block mapping table
        if(pDieInfo->TblBitmap & (1 << tmpZone))
        {
            continue;
        }

        tmpLogTbl = pDieInfo->ZoneInfo[tmpZone].LogBlkTbl;

        for(i=0; i<MAX_LOG_BLK_CNT; i++)
        {
            if(tmpLogTbl->LogicBlkNum != 0xffff)
            {
                tmpLastUsedPage = _GetLastUsedPage(pDieInfo->nDie, tmpLogTbl->PhyBlk.PhyBlkNum);

                tmpLogTbl->LastUsedPage = tmpLastUsedPage;
            }

            tmpLogTbl++;
        }
    }

    return 0;
}


/*
************************************************************************************************************************
*                       DISTRIBUTE FREE BLOCKS TO BLOCK MAPPING TABLES
*
*Description: Destribute the free blocks to the block mapping tables, distribute free block to fill
*             the empty items in the data block table at first, assure every data block table item
*             has a block; then distribute the free blocks to the free block table items.
*
*Arguments  : pDieInfo   the pointer to the die information.
*
*Return     : distribute free block result;
*               = 0     distribute free block successful;
*               < 0     distribute free block failed.
************************************************************************************************************************
*/
static __s32 _DistributeFreeBlk(struct __ScanDieInfo_t *pDieInfo)
{
    __s32   i, tmpZone, tmpFreeBlk;

    struct __SuperPhyBlkType_t *tmpZoneTbl;

    //initiate the first super block of the die
    if(pDieInfo->nDie == 0)
    {
        pDieInfo->nFreeIndex = DIE0_FIRST_BLK_NUM;
    }
    else
    {
        pDieInfo->nFreeIndex = 0;
    }

    //look for free block to fill the empty item in the data block table
    for(tmpZone=0; tmpZone<ZONE_CNT_OF_DIE; tmpZone++)
    {
        //skip the valid block mapping table
        if(pDieInfo->TblBitmap & (1 << tmpZone))
        {
            continue;
        }

        //check if the free block is enough to fill the empty item in the data block table
        if(pDieInfo->nFreeCnt < (DATA_BLK_CNT_OF_ZONE - pDieInfo->ZoneInfo[tmpZone].nDataBlkCnt))
        {
            FORMAT_ERR("[FORMAT_ERR] There is not enough valid block for using!\n");

            return -1;
        }

        tmpZoneTbl = pDieInfo->ZoneInfo[tmpZone].ZoneTbl;

        //init the free block table item index
        pDieInfo->ZoneInfo[tmpZone].nFreeBlkIndex = DATA_BLK_CNT_OF_ZONE;

        for(i=0; i<DATA_BLK_CNT_OF_ZONE; i++)
        {
            if(tmpZoneTbl[i].PhyBlkNum == 0xffff)
            {
                //current data block is empty
                tmpFreeBlk = _GetFreeBlkFromDieInfo(pDieInfo);
                if(tmpFreeBlk < 0)
                {
                    FORMAT_ERR("[FORMAT_ERR] There is not enough valid block for using!\n");

                    return -1;
                }

                tmpZoneTbl[i].PhyBlkNum = tmpFreeBlk;
                pDieInfo->ZoneInfo[tmpZone].nDataBlkCnt++;
                pDieInfo->pPhyBlk[tmpFreeBlk] = (tmpZone<<10) | ALLOC_BLK_MARK;
            }
        }
    }

    //look for the free block to distribute to the free block table

	while(pDieInfo->nFreeCnt > 0)
    {
        tmpZone = _LeastBlkCntZone(pDieInfo);

        tmpZoneTbl =  pDieInfo->ZoneInfo[tmpZone].ZoneTbl;

        if(pDieInfo->ZoneInfo[tmpZone].nFreeBlkIndex < BLOCK_CNT_OF_ZONE)
        {
            tmpFreeBlk = _GetFreeBlkFromDieInfo(pDieInfo);
            if(tmpFreeBlk < 0)
            {
                FORMAT_DBG("[FORMAT_WARNNING] Get free block failed when it should be successful!\n");
                continue;
            }

            tmpZoneTbl[pDieInfo->ZoneInfo[tmpZone].nFreeBlkIndex].PhyBlkNum = tmpFreeBlk;
            pDieInfo->ZoneInfo[tmpZone].nFreeBlkIndex++;
            pDieInfo->ZoneInfo[tmpZone].nFreeBlkCnt++;

            pDieInfo->pPhyBlk[tmpFreeBlk] = (tmpZone<<10) | ALLOC_BLK_MARK;
        }
        else
        {
            FORMAT_DBG("[FORMAT_WARNNING] There is some blocks more than we used!\n");
        }
    }

    return 0;

}


/*
************************************************************************************************************************
*                       FILL BLOCK MAPPING TABLE INFORMATION
*
*Description: Fill the block mapping table information with the logical informaton buffer of the die.
*
*Arguments  : pDieInfo   the pointer to the die information whose mapping table information need be filled.
*
*Return     : fill block mapping table result;
*               = 0     fill block mapping table successful;
*               < 0     fill block mapping table failed.
************************************************************************************************************************
*/
static __s32 _FillZoneTblInfo(struct __ScanDieInfo_t *pDieInfo)
{
    __u16   tmpLogicInfo;
    __u32   tmpPhyBlk, tmpBlkErase;
    __s32   result;

    //calculte the first block is used in the die
    if(pDieInfo->nDie == 0)
    {
        tmpPhyBlk = DIE0_FIRST_BLK_NUM;
    }
    else
    {
        tmpPhyBlk = 0;
    }

    //check the logical information of every physical block in the logical information buffer to fill the zone table
    for( ; tmpPhyBlk<SuperBlkCntOfDie; tmpPhyBlk++)
    {
        tmpLogicInfo = pDieInfo->pPhyBlk[tmpPhyBlk];

        //added by penggang 20101206
        //the last block is degenrous, if it is free block, kick it as a bad block
        if(tmpPhyBlk == SuperBlkCntOfDie-1)
        {
            if(tmpLogicInfo == FREE_BLOCK_INFO)
            {
                FORMAT_DBG("[FORMAT_DBG] mark the last block as bad block \n");
                pDieInfo->pPhyBlk[tmpPhyBlk] = BAD_BLOCK_INFO;
                _WriteBadBlkFlag(pDieInfo->nDie, tmpPhyBlk);

            }
        }

        tmpLogicInfo = pDieInfo->pPhyBlk[tmpPhyBlk];


        //check if the block is a bad block
        if(tmpLogicInfo == BAD_BLOCK_INFO)
        {
            pDieInfo->nBadCnt++;

            continue;
        }

        //check if the block is a free block
        if(tmpLogicInfo == FREE_BLOCK_INFO)
        {
            pDieInfo->nFreeCnt++;
            continue;
        }

        //check if the block is a special type block
        if(GET_LOGIC_INFO_TYPE(tmpLogicInfo) == 1)
        {
            //check if the block is a boot block
            if(GET_LOGIC_INFO_BLK(tmpLogicInfo) == BOOT_BLK_MARK)
            {
                if(pDieInfo->nDie == 0)
                {
                    continue;
                }
                else
                {
                    //the boot type block should be in the die0, in other die is invalid
                    FORMAT_DBG("[FORMAT_DBG] Find a boot type block(0x%x) not in die0!\n", tmpPhyBlk);
                    //erase the super block for other use
                    result = _VirtualBlockErase(pDieInfo->nDie, tmpPhyBlk);
                    if(result < 0)
                    {
                        //erase the virtual block failed, the block is a bad block, need write bad block flag
                        pDieInfo->pPhyBlk[tmpPhyBlk] = BAD_BLOCK_INFO;
                        _WriteBadBlkFlag(pDieInfo->nDie, tmpPhyBlk);

                        continue;
                    }

                    //the block will be a new free block, modify the logical information
                    pDieInfo->pPhyBlk[tmpPhyBlk] = FREE_BLOCK_INFO;
                    pDieInfo->nFreeCnt++;

                    continue;
                }
            }
            //check if the block is a block mapping table block
            else if(GET_LOGIC_INFO_BLK(tmpLogicInfo) == TABLE_BLK_MARK)
            {
                //check if the block mapping table is valid, check the logical information and the position
                if((GET_LOGIC_INFO_ZONE(tmpLogicInfo) < ZONE_CNT_OF_DIE)
                    && (tmpPhyBlk < TBL_AREA_BLK_NUM))
                {
                    //the block mapping table block is valid
                    continue;
                }
                else
                {
                    //the block mapping table block is invalid, need erase it for other use
                    FORMAT_DBG("[FORMAT_DBG] Find an invalid block mapping table in Die 0x%x !\n", pDieInfo->nDie);

                    result = _VirtualBlockErase(pDieInfo->nDie, tmpPhyBlk);
                    if(result < 0)
                    {
                        //erase the virtual block failed, the block is a bad block, need write bad block flag
                        pDieInfo->pPhyBlk[tmpPhyBlk] = BAD_BLOCK_INFO;
                        _WriteBadBlkFlag(pDieInfo->nDie, tmpPhyBlk);

                        continue;
                    }

                    //the block will be a new free block, modify the logical information
                    pDieInfo->pPhyBlk[tmpPhyBlk] = FREE_BLOCK_INFO;
                    pDieInfo->nFreeCnt++;

                    continue;
                }
            }
            else
            {
                //the block is an unrecgnized special block, need erased it for other used
                FORMAT_DBG("[FORMAT_DBG] Find an recgnized special type block in die 0x%x!\n", pDieInfo->nDie);

                result = _VirtualBlockErase(pDieInfo->nDie, tmpPhyBlk);
                if(result < 0)
                {
                    //erase the virtual block failed, the block is a bad block, need write bad block flag
                    pDieInfo->pPhyBlk[tmpPhyBlk] = BAD_BLOCK_INFO;
                    _WriteBadBlkFlag(pDieInfo->nDie, tmpPhyBlk);

                    continue;
                }

                //the block will be a new free block, modify the logical information
                pDieInfo->pPhyBlk[tmpPhyBlk] = FREE_BLOCK_INFO;
                pDieInfo->nFreeCnt++;

                continue;
            }
        }

        //check if the logical information of the physical block is valid
        if((GET_LOGIC_INFO_ZONE(tmpLogicInfo) >= ZONE_CNT_OF_DIE) || \
                    (GET_LOGIC_INFO_BLK(tmpLogicInfo) >= DATA_BLK_CNT_OF_ZONE))
        {
            //the logic information of the physical block is invalid, erase it for other use
            result = _VirtualBlockErase(pDieInfo->nDie, tmpPhyBlk);
            if(result < 0)
            {
                //erase the virtual block failed, the block is a bad block, need write bad block flag
                pDieInfo->pPhyBlk[tmpPhyBlk] = BAD_BLOCK_INFO;
                _WriteBadBlkFlag(pDieInfo->nDie, tmpPhyBlk);

                continue;
            }

            //the block will be a new free block, modify the logical information
            pDieInfo->pPhyBlk[tmpPhyBlk] = FREE_BLOCK_INFO;
            pDieInfo->nFreeCnt++;

            continue;
        }

        //check if the super block is used by the zone which need not rebuiled
        if((1 << GET_LOGIC_INFO_ZONE(tmpLogicInfo)) & (pDieInfo->TblBitmap))
        {
            continue;
        }

        //fill the physical block to the zone table which the block is belonged to
        result = _FillBlkToZoneTbl(pDieInfo, tmpLogicInfo, tmpPhyBlk, &tmpBlkErase);
        if(tmpBlkErase != 0xffff)
        {
            //fill the physical block to the zone table failed, erase it for other use
            result = _VirtualBlockErase(pDieInfo->nDie, tmpBlkErase);
            if(result < 0)
            {
                //erase the virtual block failed, the block is a bad block, need write bad block flag
                pDieInfo->pPhyBlk[tmpBlkErase] = BAD_BLOCK_INFO;
                _WriteBadBlkFlag(pDieInfo->nDie, tmpBlkErase);

                continue;
            }

            //the block will be a new free block, modify the logical information
            pDieInfo->pPhyBlk[tmpBlkErase] = FREE_BLOCK_INFO;
            pDieInfo->nFreeCnt++;

            continue;
        }
    }

#if DBG_DUMP_DIE_INFO

    _DumpDieInfo(pDieInfo);

#endif

    //fill the data block item finish
    return 0;
}


/*
************************************************************************************************************************
*                       WRITE BLOCK MAPPING TABLE TO NAND FLASH
*
*Description: Write the block mapping table to nand flash, the block mapping table is stored
*             in the buffer indexed by the die information.
*
*Arguments  : pDieInfo   the pointer to the die information
*
*Return     : write block mapping table result;
*               = 0     write block mapping table successful;
*               < 0     write block mapping table failed.
************************************************************************************************************************
*/
static __s32 _WriteBlkMapTbl(struct __ScanDieInfo_t *pDieInfo)
{
    __s32   i, tmpZone, tmpTblBlk, tmpTblPage, tmpGlobzone, result;
    struct __NandUserData_t tmpSpare[2];
    struct __SuperPhyBlkType_t *tmpDataBlk;
    struct __LogBlkType_t *tmpLogBlk;

    for(tmpZone=0; tmpZone<ZONE_CNT_OF_DIE; tmpZone++)
    {
        //skip the valid block mapping tables
        if(pDieInfo->TblBitmap & (1 << tmpZone))
        {
            continue;
        }

        tmpGlobzone = (pDieInfo->nDie) * ZONE_CNT_OF_DIE + tmpZone;

        tmpTblBlk = ZoneTblPstInfo[tmpGlobzone].PhyBlkNum;
        tmpTblPage = ZoneTblPstInfo[tmpGlobzone].TablePst;

        //calculate the number of the page will be used for writing table
        if(tmpTblPage == 0xffff)
        {
            tmpTblPage = 0;
        }
        else
        {
            tmpTblPage += PAGE_CNT_OF_TBL_GROUP;
        }

        //check if the table block need be erased
        if(!(tmpTblPage < PAGE_CNT_OF_SUPER_BLK))
        {
            result = _VirtualBlockErase(pDieInfo->nDie, tmpTblBlk);
            if(result < 0)
            {
                FORMAT_DBG("[FORMAT_DBG] Erase block failed when write block mapping table!\n");
                //erase the virtual block failed, the block is a bad block, need write bad block flag
                _WriteBadBlkFlag(pDieInfo->nDie, tmpTblBlk);

                return -1;
            }

            tmpTblPage = 0;
        }

        //clear the block erase count of every physical block in the block mapping table
        tmpDataBlk = pDieInfo->ZoneInfo[tmpZone].ZoneTbl;
        tmpLogBlk = pDieInfo->ZoneInfo[tmpZone].LogBlkTbl;
        for(i=0; i<BLOCK_CNT_OF_ZONE; i++)
        {
            if(tmpDataBlk[i].PhyBlkNum != 0xffff)
            {
                tmpDataBlk[i].BlkEraseCnt = 0;
            }
        }

        for(i=0; i<MAX_LOG_BLK_CNT; i++)
        {
            if(tmpLogBlk[i].LogicBlkNum != 0xffff)
            {
                tmpLogBlk[i].PhyBlk.BlkEraseCnt = 0;
            }
        }

        //set spare area data for write zone table
        tmpSpare[0].BadBlkFlag = 0xff;
        tmpSpare[1].BadBlkFlag = 0xff;
        tmpSpare[0].LogicInfo = (1<<14) | (tmpZone<<10) | TABLE_BLK_MARK;
        tmpSpare[1].LogicInfo = (1<<14) | (tmpZone<<10) | TABLE_BLK_MARK;
        tmpSpare[0].LogicPageNum = 0xffff;
        tmpSpare[1].LogicPageNum = 0xffff;
        tmpSpare[0].PageStatus = 0x55;
        tmpSpare[1].PageStatus = 0x55;

        //set table data checksum for data block table
        result = _CalCheckSum((__u32 *)tmpDataBlk, BLOCK_CNT_OF_ZONE - 1);
        *(__u32 *)&tmpDataBlk[BLOCK_CNT_OF_ZONE - 1] = (__u32)result;

        //write data block table to nand flash
        MEMSET(FORMAT_PAGE_BUF, 0xff, SECTOR_CNT_OF_SUPER_PAGE * SECTOR_SIZE);
        MEMCPY(FORMAT_PAGE_BUF, (__u32 *)tmpDataBlk, SECTOR_SIZE * 4);


        result = _VirtualPageWrite(pDieInfo->nDie, tmpTblBlk, tmpTblPage + DATA_TBL_OFFSET, \
                            FULL_BITMAP_OF_SUPER_PAGE, FORMAT_PAGE_BUF, (void *)tmpSpare);
        if(result < 0)
        {
            FORMAT_DBG("[FORMAT_DBG] Write page failed when write block mapping table!\n");
            //write page failed, the block is a bad block, need write bad block flag
            _WriteBadBlkFlag(pDieInfo->nDie, tmpTblBlk);

            return -1;
        }


        MEMCPY(FORMAT_PAGE_BUF, (__u32 *)&tmpDataBlk[BLOCK_CNT_OF_ZONE / 2], SECTOR_SIZE * 4);
        result = _VirtualPageWrite(pDieInfo->nDie, tmpTblBlk, tmpTblPage + DATA_TBL_OFFSET + 1, \
                            FULL_BITMAP_OF_SUPER_PAGE, FORMAT_PAGE_BUF, (void *)tmpSpare);
        if(result < 0)
        {
            FORMAT_DBG("[FORMAT_DBG] Write page failed when write block mapping table!\n");
            //write page failed, the block is a bad block, need write bad block flag
            _WriteBadBlkFlag(pDieInfo->nDie, tmpTblBlk);

            return -1;
        }

        //process log block table data
        MEMSET(FORMAT_PAGE_BUF, 0xff, SECTOR_CNT_OF_SUPER_PAGE * SECTOR_SIZE);
        MEMCPY(FORMAT_PAGE_BUF, (__u32 *)tmpLogBlk, LOG_BLK_CNT_OF_ZONE*sizeof(struct __LogBlkType_t));
        //set table data checksum for log block table
        result = _CalCheckSum((__u32 *)FORMAT_PAGE_BUF, LOG_BLK_CNT_OF_ZONE*sizeof(struct __LogBlkType_t)/sizeof(__u32));
        ((__u32*)FORMAT_PAGE_BUF)[511] = (__u32)result;
        result = _VirtualPageWrite(pDieInfo->nDie, tmpTblBlk, tmpTblPage + LOG_TBL_OFFSET, \
                            FULL_BITMAP_OF_SUPER_PAGE, FORMAT_PAGE_BUF, (void *)tmpSpare);
        if(result < 0)
        {
            FORMAT_DBG("[FORMAT_DBG] Write page failed when write block mapping table!\n");
            //write page failed, the block is a bad block, need write bad block flag
            _WriteBadBlkFlag(pDieInfo->nDie, tmpTblBlk);

            return -1;
        }

        //set the data block group position in the table block
        ZoneTblPstInfo[tmpGlobzone].TablePst = tmpTblPage;
    }

    //write every block mapping table successful
    return 0;
}


/*
************************************************************************************************************************
*                       SEARCH ZONE TABLES FROM ONE NAND DIE
*
*Description: Search zone tables from one nand flash die.
*
*Arguments  : pDieInfo   the pointer to the die information whose block mapping table need be searched
*
*Return     : search result;
*               = 0     search zone table successful;
*               < 0     search zone table failed.
************************************************************************************************************************
*/
static __s32 _SearchZoneTbls(struct __ScanDieInfo_t *pDieInfo)
{
    __u32   tmpSuperBlk, tmpZoneInDie, tmpPage, tmpVar;
    __s32   result;
    struct  __NandUserData_t tmpSpareData[2];
    struct  __SuperPhyBlkType_t *tmpDataBlkTbl;

    FORMAT_DBG("[FORMAT_DBG] Search the block mapping table on DIE 0x%x\n", pDieInfo->nDie);

    if(pDieInfo->nDie == 0)
    {
        //some physical blocks of die 0 is used for boot, so need ignore this blocks for efficiency
        tmpSuperBlk = DIE0_FIRST_BLK_NUM;
    }
    else
    {
        //the physical blocks of other die all used for block mapping
        tmpSuperBlk = 0;
    }

    for( ; tmpSuperBlk<TBL_AREA_BLK_NUM; tmpSuperBlk++)
    {
        //init the bad block flag for the tmpSpareData
        tmpSpareData[0].BadBlkFlag = tmpSpareData[1].BadBlkFlag = 0xff;

        //read page0 to get the block logical information
        _VirtualPageRead(pDieInfo->nDie, tmpSuperBlk, 0, SPARE_DATA_BITMAP, FORMAT_PAGE_BUF, (void *)&tmpSpareData);

        //check if the bock is a valid block-mapping-table block
        if((tmpSpareData[0].BadBlkFlag != 0xff) || (tmpSpareData[1].BadBlkFlag != 0xff) || \
                (GET_LOGIC_INFO_TYPE(tmpSpareData[0].LogicInfo) != 1) || \
                        (GET_LOGIC_INFO_BLK(tmpSpareData[0].LogicInfo) != 0xaa))
        {
            //the block is not a valid block-mapping-table block, ignore it
            continue;
        }

        tmpZoneInDie = GET_LOGIC_INFO_ZONE(tmpSpareData[0].LogicInfo);
        //check if the zone number in the logicial information of the block is valid
        if(!(tmpZoneInDie < ZONE_CNT_OF_DIE))
        {
            //the zone number in the logical infomation of the block is invalid, not a valid block-mapping-table block
            result = _VirtualBlockErase(pDieInfo->nDie, tmpSuperBlk);
            if(result < 0)
            {
                //erase the virtual block failed, the block is a bad block, need write bad block flag
                _WriteBadBlkFlag(pDieInfo->nDie, tmpSuperBlk);
            }

            continue;
        }

        //check if there is a block-mapping-table block for the zone already
        if(ZoneTblPstInfo[tmpZoneInDie + pDieInfo->nDie * ZONE_CNT_OF_DIE].PhyBlkNum != 0xffff)
        {
            //there is a block-mapping-table block for the zone already, so, need erase current block
            result = _VirtualBlockErase(pDieInfo->nDie, tmpSuperBlk);
            if(result < 0)
            {
                //erase the virtual block failed, the block is a bad block, need write bad block flag
                _WriteBadBlkFlag(pDieInfo->nDie, tmpSuperBlk);
            }

            continue;
        }

        //look for the position of the last block-mpping-table in the block
        tmpPage = _SearchBlkTblPst(pDieInfo->nDie, tmpSuperBlk);

        //set the block-mapping-talbe position information
        ZoneTblPstInfo[tmpZoneInDie + pDieInfo->nDie * ZONE_CNT_OF_DIE].PhyBlkNum = tmpSuperBlk;
        ZoneTblPstInfo[tmpZoneInDie + pDieInfo->nDie * ZONE_CNT_OF_DIE].TablePst = tmpPage;

        //check the dirty flag of the block mapping table to find if the block mapping table is valid
        _VirtualPageRead(pDieInfo->nDie, tmpSuperBlk, tmpPage+DIRTY_FLAG_OFFSET, DIRTY_FLAG_BITMAP, FORMAT_PAGE_BUF, NULL);
        if(FORMAT_PAGE_BUF[0] != 0xff)
        {
            //the block mapping table is invalid
            FORMAT_DBG("[FORMAT_DBG] Find the table block %d for zone 0x%x of die 0x%x, but the table is invalid!\n",
                        tmpSuperBlk,tmpZoneInDie, pDieInfo->nDie);
            continue;
        }

        //check the data block table data, the size of the table is 4k byte, stored in two pages
        tmpDataBlkTbl = (struct  __SuperPhyBlkType_t *)MALLOC(BLOCK_CNT_OF_ZONE * sizeof(struct __SuperPhyBlkType_t));
        _VirtualPageRead(pDieInfo->nDie, tmpSuperBlk, tmpPage+DATA_TBL_OFFSET, DATA_TABLE_BITMAP, FORMAT_PAGE_BUF, NULL);
        MEMCPY(&tmpDataBlkTbl[0], FORMAT_PAGE_BUF, SECTOR_SIZE * 4);
        _VirtualPageRead(pDieInfo->nDie, tmpSuperBlk, tmpPage+DATA_TBL_OFFSET+1, DATA_TABLE_BITMAP, FORMAT_PAGE_BUF, NULL);
        MEMCPY(&tmpDataBlkTbl[BLOCK_CNT_OF_ZONE / 2], FORMAT_PAGE_BUF, SECTOR_SIZE * 4);
        //calculate the data block table data checksum
        tmpVar = _CalCheckSum((__u32 *)tmpDataBlkTbl, BLOCK_CNT_OF_ZONE - 1);
        if(tmpVar != ((__u32 *)tmpDataBlkTbl)[BLOCK_CNT_OF_ZONE - 1])
        {
            //the checksum of the data block table is invalid
            FORMAT_DBG("[FORMAT_DBG] Find the table block %d for zone 0x%x of die 0x%x,"
                       " but the data block table is invalid!\n",tmpSuperBlk,tmpZoneInDie, pDieInfo->nDie);

            //release the data block table buffer
            FREE(tmpDataBlkTbl,BLOCK_CNT_OF_ZONE * sizeof(struct __SuperPhyBlkType_t));
            continue;
        }
        FREE(tmpDataBlkTbl,BLOCK_CNT_OF_ZONE * sizeof(struct __SuperPhyBlkType_t));

        //check the log block table data, the size of the log block table is only 0.5k byte
        _VirtualPageRead(pDieInfo->nDie, tmpSuperBlk, tmpPage+LOG_TBL_OFFSET, LOG_TABLE_BITMAP, FORMAT_PAGE_BUF, NULL);
        //calcluate the log block table data checksum
        tmpVar = _CalCheckSum((__u32 *)FORMAT_PAGE_BUF, LOG_BLK_CNT_OF_ZONE*sizeof(struct __LogBlkType_t)/sizeof(__u32));
        if(tmpVar != ((__u32 *)FORMAT_PAGE_BUF)[511])
        {
            //the checksum of the log block table is invalid
            FORMAT_DBG("[FORMAT_DBG] Find the table block for zone 0x%x of die 0x%x,"
                       "but the log block tabl is invalid!\n",tmpZoneInDie, pDieInfo->nDie);
            continue;
        }

        //set the flag that which mark the zone table status, the current block mapping table is valid
        pDieInfo->TblBitmap |= (1 << tmpZoneInDie);

        FORMAT_DBG("[FORMAT_DBG] Search block mapping table for zone 0x%x of die 0x%x successfully!\n",
                    tmpZoneInDie, pDieInfo->nDie);
    }

    //check if all of the block mapping table of the die has been seared successfully
    for(tmpVar=0; tmpVar<ZONE_CNT_OF_DIE; tmpVar++)
    {
        if(!(pDieInfo->TblBitmap & (1 << tmpVar)))
        {
            //search block mapping table of the die failed, report result
            FORMAT_DBG("[FORMAT_DBG] Search block mapping table for die 0x%x failed!\n", pDieInfo->nDie);
            return -1;
        }
    }

    //search block mapping table of the die successful
    return 0;
}


/*
************************************************************************************************************************
*                       REBUILD ZONE TABLES FOR ONE NAND FLASH DIE
*
*Description: Rebuild zone tables for one nand flash die.
*
*Arguments  : pDieInfo   the pointer to the die information whose block mapping table need be rebuiled.
*
*Return     :  rebuild result;
*               = 0     rebuild zone tables successful;
*               < 0     rebuild zone tables failed.
************************************************************************************************************************
*/
static __s32 _RebuildZoneTbls(struct __ScanDieInfo_t *pDieInfo)
{
    __s32 i, result;

    //request buffer for get the logical information of every physical block in the die
    pDieInfo->pPhyBlk = (__u16 *)MALLOC(SuperBlkCntOfDie * sizeof(__u16));
    if(!pDieInfo->pPhyBlk)
    {
        //request buffer failed, reprot error
        FORMAT_ERR("[FORMAT_ERR] Malloc buffer for logical information of physical block failed!\n");

        return -1;
    }
    //request buffer for process the block mapping table data of a die
    pDieInfo->ZoneInfo = (struct __ScanZoneInfo_t *) MALLOC(ZONE_CNT_OF_DIE * sizeof(struct __ScanZoneInfo_t));
    if(!pDieInfo->ZoneInfo)
    {
        //request buffer failed, release the buffer which has been got, and report error
        FORMAT_ERR("[FORMAT_ERR] Malloc buffer for proccess the block mapping table data failed!\n");
        FREE(pDieInfo->pPhyBlk,SuperBlkCntOfDie * sizeof(__u16));

        return -1;
    }

    //initiate the buffer data to default value
    MEMSET(pDieInfo->pPhyBlk, 0xff, SuperBlkCntOfDie * sizeof(__u16));
    for(i=0; i<ZONE_CNT_OF_DIE; i++)
    {
        //initiate every zone information structure of the die
        pDieInfo->ZoneInfo[i].nDataBlkCnt = 0;
        pDieInfo->ZoneInfo[i].nFreeBlkCnt = 0;
        pDieInfo->ZoneInfo[i].nFreeBlkIndex = 0;
        MEMSET(pDieInfo->ZoneInfo[i].ZoneTbl, 0xff, BLOCK_CNT_OF_ZONE * sizeof(__u32));
        MEMSET(pDieInfo->ZoneInfo[i].LogBlkTbl, 0xff, MAX_LOG_BLK_CNT * sizeof(struct __LogBlkType_t));
    }

    //initiate the first super block of the die
    if(pDieInfo->nDie == 0)
    {
        pDieInfo->nFreeIndex = DIE0_FIRST_BLK_NUM;
    }
    else
    {
        pDieInfo->nFreeIndex = 0;
    }

    //read the logical information of every physical block of the die
    result = _GetBlkLogicInfo(pDieInfo);

    //fill the zone table information structure with the logical block information in the Die informaton buffer
    result = _FillZoneTblInfo(pDieInfo);

    //kick the free blocks that has been used by the valid zone table
    result = _KickValidTblBlk(pDieInfo);

    //get a physical block in the table block area for every mapping table to save block mapping table
    result = _GetMapTblBlock(pDieInfo);
    if(result < 0)
    {
        FORMAT_ERR("[FORMAT_ERR] Get block for saving block mapping table failed in die 0x%x!\n", pDieInfo->nDie);
        FREE(pDieInfo->pPhyBlk,SuperBlkCntOfDie * sizeof(__u16));
        FREE(pDieInfo->ZoneInfo,ZONE_CNT_OF_DIE * sizeof(struct __ScanZoneInfo_t));

        return -1;
    }

    //repair the log block table
    result = _RepairLogBlkTbl(pDieInfo);

    //allocate the free block to every block mapping table
    result = _DistributeFreeBlk(pDieInfo);
    if(result < 0)
    {
        FORMAT_ERR("[FORMAT_ERR] There is not enough free blocks for distribute!\n");

        FREE(pDieInfo->pPhyBlk,SuperBlkCntOfDie * sizeof(__u16));
        FREE(pDieInfo->ZoneInfo,ZONE_CNT_OF_DIE * sizeof(struct __ScanZoneInfo_t));

        return -1;
    }

    //write block mapping table to nand flash
    result = _WriteBlkMapTbl(pDieInfo);
    if(result < 0)
    {
        FORMAT_ERR("[FORMAT_DBG] Write block mapping table failed!\n");

        FREE(pDieInfo->pPhyBlk,SuperBlkCntOfDie * sizeof(__u16));
        FREE(pDieInfo->ZoneInfo,ZONE_CNT_OF_DIE * sizeof(struct __ScanZoneInfo_t));

        return RET_FORMAT_TRY_AGAIN;
    }

#if DBG_DUMP_DIE_INFO

    _DumpDieInfo(pDieInfo);

#endif

    //release the memory resouce
    FREE(pDieInfo->pPhyBlk,SuperBlkCntOfDie * sizeof(__u16));
    FREE(pDieInfo->ZoneInfo,ZONE_CNT_OF_DIE * sizeof(struct __ScanZoneInfo_t));

    return 0;
}


/*
************************************************************************************************************************
*                                   FORMAT NAND FLASH DISK MODULE INIT
*
*Description: Init the nand disk format module, initiate some variables and request resource.
*
*Arguments  : none
*
*Return     : init result;
*               = 0     format module init successful;
*               < 0     format module init failed.
************************************************************************************************************************
*/
#if(0)
__s32 FMT_Init(void)
{
    __s32 i;
	__u32 TmpBlkSize;

	DIE0_FIRST_BLK_NUM = 0;
	/*get block number for boot*/
	for (i = 0; ; i++)
	{
		TmpBlkSize = blks_array[i].blk_size;
		if ( (TmpBlkSize == 0xffffffff) || (TmpBlkSize == SECTOR_CNT_OF_SINGLE_PAGE*PAGE_CNT_OF_PHY_BLK/2))
		{
			DIE0_FIRST_BLK_NUM = blks_array[i].blks_boot0 + blks_array[i].blks_boot1;
			break;
		}

	}

	//init the global nand flash dirver paramter structure
    NandDriverInfo.NandStorageInfo = &NandStorageInfo;
    NandDriverInfo.ZoneTblPstInfo = ZoneTblPstInfo;
    NandDriverInfo.LogicalArchitecture = &LogicArchiPar;
    NandDriverInfo.PageCachePool = &PageCachePool;

    //init the logical architecture paramters
    LogicArchiPar.LogicBlkCntPerZone = NandStorageInfo.ValidBlkRatio;
    LogicArchiPar.SectCntPerLogicPage = NandStorageInfo.SectorCntPerPage * NandStorageInfo.PlaneCntPerDie;
    LogicArchiPar.PageCntPerLogicBlk = NandStorageInfo.PageCntPerPhyBlk * NandStorageInfo.BankCntPerChip;
    if(SUPPORT_EXT_INTERLEAVE)
    {
        LogicArchiPar.PageCntPerLogicBlk *= NandStorageInfo.ChipCnt;
    }
    LogicArchiPar.ZoneCntPerDie = (NandStorageInfo.BlkCntPerDie / NandStorageInfo.PlaneCntPerDie) / BLOCK_CNT_OF_ZONE;

    //init block mapping table position information
    for(i=0; i<MAX_ZONE_CNT; i++)
    {
        ZoneTblPstInfo[i].PhyBlkNum = 0xffff;
        ZoneTblPstInfo[i].TablePst = 0xffff;
    }

    //init some local variable
    DieCntOfNand = NandStorageInfo.DieCntPerChip / NandStorageInfo.BankCntPerChip;
    if(!SUPPORT_EXT_INTERLEAVE)
    {
        DieCntOfNand *= NandStorageInfo.ChipCnt;
    }
    SuperBlkCntOfDie = NandStorageInfo.BlkCntPerDie / NandStorageInfo.PlaneCntPerDie;

    MEMSET(FORMAT_SPARE_BUF, 0xff, SECTOR_CNT_OF_SUPER_PAGE * 4);

    FORMAT_DBG("\n");
    FORMAT_DBG("[FORMAT_DBG] ===========Logical Architecture Paramter===========\n");
    FORMAT_DBG("[FORMAT_DBG]    Logic Block Count of Zone:  0x%x\n", LogicArchiPar.LogicBlkCntPerZone);
    FORMAT_DBG("[FORMAT_DBG]    Page Count of Logic Block:  0x%x\n", LogicArchiPar.PageCntPerLogicBlk);
    FORMAT_DBG("[FORMAT_DBG]    Sector Count of Logic Page: 0x%x\n", LogicArchiPar.SectCntPerLogicPage);
    FORMAT_DBG("[FORMAT_DBG]    Zone Count of Die:          0x%x\n", LogicArchiPar.ZoneCntPerDie);
    FORMAT_DBG("[FORMAT_DBG] ===================================================\n");

    return 0;
}
#elif(1)
__s32 FMT_Init(void)
{
    __s32 i;
	__u32 TmpBlkSize;

	DIE0_FIRST_BLK_NUM = 0;
	/*get block number for boot*/
	for (i = 0; ; i++)
	{
		TmpBlkSize = blks_array[i].blk_size;
		if ( (TmpBlkSize == 0xffffffff) || (TmpBlkSize == SECTOR_CNT_OF_SINGLE_PAGE*PAGE_CNT_OF_PHY_BLK/2))
		{
			DIE0_FIRST_BLK_NUM = blks_array[i].blks_boot0 + blks_array[i].blks_boot1;
			break;
		}

	}

	//init the global nand flash dirver paramter structure
    NandDriverInfo.NandStorageInfo = &NandStorageInfo;
    NandDriverInfo.ZoneTblPstInfo = ZoneTblPstInfo;
    NandDriverInfo.LogicalArchitecture = &LogicArchiPar;
    NandDriverInfo.PageCachePool = &PageCachePool;

    //init the logical architecture paramters
    LogicArchiPar.LogicBlkCntPerZone = NandStorageInfo.ValidBlkRatio;
    LogicArchiPar.SectCntPerLogicPage = NandStorageInfo.SectorCntPerPage * NandStorageInfo.PlaneCntPerDie;
    LogicArchiPar.PageCntPerLogicBlk = NandStorageInfo.PageCntPerPhyBlk * NandStorageInfo.BankCntPerChip;
    if(SUPPORT_EXT_INTERLEAVE)
    {
       if(NandStorageInfo.ChipCnt >=2)
          LogicArchiPar.PageCntPerLogicBlk *= 2;
    }
    LogicArchiPar.ZoneCntPerDie = (NandStorageInfo.BlkCntPerDie / NandStorageInfo.PlaneCntPerDie) / BLOCK_CNT_OF_ZONE;

    //init block mapping table position information
    for(i=0; i<MAX_ZONE_CNT; i++)
    {
        ZoneTblPstInfo[i].PhyBlkNum = 0xffff;
        ZoneTblPstInfo[i].TablePst = 0xffff;
    }

    //init some local variable
    DieCntOfNand = NandStorageInfo.DieCntPerChip / NandStorageInfo.BankCntPerChip;
    if(!SUPPORT_EXT_INTERLEAVE)
    {
        DieCntOfNand *= NandStorageInfo.ChipCnt;
    }
    if(SUPPORT_EXT_INTERLEAVE)
    {
       if(NandStorageInfo.ChipCnt >=2)
	   DieCntOfNand *= (NandStorageInfo.ChipCnt/2);
    }
    SuperBlkCntOfDie = NandStorageInfo.BlkCntPerDie / NandStorageInfo.PlaneCntPerDie;

    MEMSET(FORMAT_SPARE_BUF, 0xff, SECTOR_CNT_OF_SUPER_PAGE * 4);

    FORMAT_DBG("\n");
    FORMAT_DBG("[FORMAT_DBG] ===========Logical Architecture Paramter===========\n");
    FORMAT_DBG("[FORMAT_DBG]    Logic Block Count of Zone:  0x%x\n", LogicArchiPar.LogicBlkCntPerZone);
    FORMAT_DBG("[FORMAT_DBG]    Page Count of Logic Block:  0x%x\n", LogicArchiPar.PageCntPerLogicBlk);
    FORMAT_DBG("[FORMAT_DBG]    Sector Count of Logic Page: 0x%x\n", LogicArchiPar.SectCntPerLogicPage);
    FORMAT_DBG("[FORMAT_DBG]    Zone Count of Die:          0x%x\n", LogicArchiPar.ZoneCntPerDie);
    FORMAT_DBG("[FORMAT_DBG] ===================================================\n");

    return 0;
}
#endif

/*
************************************************************************************************************************
*                                   FORMAT NAND FLASH DISK MODULE EXIT
*
*Description: Exit the nand disk format module, release some resource.
*
*Arguments  : none
*
*Return     : exit result;
*               = 0     format module exit successful;
*               < 0     format module exit failed.
************************************************************************************************************************
*/
__s32 FMT_Exit(void)
{
    //release memory resource

    return 0;
}


/*
************************************************************************************************************************
*                                   FORMAT NAND FLASH DISK
*
*Description: Format the nand flash disk, create a logical disk area.
*
*Arguments  : none
*
*Return     : format result;
*               = 0     format nand successful;
*               < 0     format nand failed.
*
*Note       : This function look for the mapping information on the nand flash first, if the find all
*             mapping information and check successful, format nand disk successful; if the mapping
*             information has some error, need repair it. If find none mapping information, create it!
************************************************************************************************************************
*/
__s32 FMT_FormatNand(void)
{
    __s32 tmpDie, result, tmpTryAgain;
    struct __ScanDieInfo_t tmpDieInfo;

	result = 0;

    //process tables in every die in the nand storage system
    for(tmpDie=0; tmpDie<DieCntOfNand; tmpDie++)
    {
        //init the die information data structure
        MEMSET(&tmpDieInfo, 0, sizeof(struct __ScanDieInfo_t));
        tmpDieInfo.nDie = tmpDie;

        //search zone tables on the nand flash from several blocks in front of the die
        result = _SearchZoneTbls(&tmpDieInfo);
		//tmpDieInfo.TblBitmap = 0;
		//result = -1;
        if(result < 0)
        {
            tmpTryAgain = 5;
            while(tmpTryAgain > 0)
            {
                //search zone tables from the nand flash failed, need repair or build it
                result = _RebuildZoneTbls(&tmpDieInfo);

                if(result != RET_FORMAT_TRY_AGAIN)
                {
                    break;
                }

                tmpTryAgain--;
            }
        }
    }

    if(result < 0)
    {
        //format nand disk failed, report error
        FORMAT_ERR("[FORMAT_ERR] Format nand disk failed!\n");
        return -1;
    }

    //format nand disk successful
    return 0;
}

void clear_NAND_ZI( void )
{
    MEMSET(&PageCachePool, 0x00, sizeof(struct __NandPageCachePool_t));
}



