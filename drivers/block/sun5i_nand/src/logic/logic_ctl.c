/*
************************************************************************************************************************
*                                                      eNand
*                                         Nand flash driver logic manage module
*
*                             Copyright(C), 2008-2009, SoftWinners Microelectronic Co., Ltd.
*											       All Rights Reserved
*
* File Name : logic_ctl.c
*
* Author : Kevin.z
*
* Version : v0.1
*
* Date : 2008.03.29
*
* Description : This file is the nand flash driver logic manage control module, is the main
*               module of logic manage module.
*
* Others : None at present.
*
*
* History :
*
*  <Author>        <time>       <version>      <description>
*
* Kevin.z         2008.03.29      0.1          build the file
*
************************************************************************************************************************
*/
#include <linux/module.h>
#include "../include/nand_logic.h"

struct __NandDriverGlobal_t     NandDriverInfo;
struct __LogicArchitecture_t    LogicArchiPar;
struct __ZoneTblPstInfo_t       ZoneTblPstInfo[MAX_ZONE_CNT];
//define the parameter for manage the cached page
static struct __GlobalLogicPageType_t  CachePage;

//define the parameter for manage the logical page read and write
static struct __LogicCtlPar_t LogicalCtl;



/*
************************************************************************************************************************
*                       CALCLUATE PROCESSING DATA PARAMETER
*
*Description: Calculate the loigcal pages number and the sector bitmap in the page for the sectors
*             that pocessing currently.
*
*Arguments  : nSectNum      the number of the first sector of the sectors need be processed;
*             nSectCnt      the count of the sectors that how many sectors need be processed;
*             pHeadPage     the pointer to the paramter of the head page;
*             pMidPageCnt   the pointer ot the count of full pages;
*             pTailPage     the pointer to the paramter of the tail page.
*
*Return     : calculate sector parameter result;
*               = 0     calculate sector parameter successful;
*               < 0     calcualte sector parameter failed.
************************************************************************************************************************
*/
static __s32 _CalculateSectPar(__u32 nSectNum, __u32 nSectCnt, struct __GlobalLogicPageType_t *pHeadPage,
                                    __u32 *pMidPageCnt, struct __GlobalLogicPageType_t *pTailPage)
{
    __u32   tmpSectCnt, tmpBitmap;

    LOGICCTL_DBG("[LOGICCTL_DBG]: Calculate logical sectors parameter, Lba:0x%x, Cnt:0x%x\n", nSectNum, nSectCnt);

    //initiate the middle page and tail page parameters, because they may be empty
    *pMidPageCnt = 0x00;
    pTailPage->LogicPageNum = 0xffffffff;
    pTailPage->SectorBitmap = 0x00;

    //calculate the head page parameter
    pHeadPage->LogicPageNum = nSectNum / SECTOR_CNT_OF_LOGIC_PAGE;
    tmpSectCnt = nSectCnt + (nSectNum % SECTOR_CNT_OF_LOGIC_PAGE);
    tmpBitmap = (FULL_BITMAP_OF_LOGIC_PAGE << (nSectNum % SECTOR_CNT_OF_LOGIC_PAGE));

    if(tmpSectCnt > SECTOR_CNT_OF_LOGIC_PAGE)
    {
        //set the head page bitmap
        pHeadPage->SectorBitmap = tmpBitmap & FULL_BITMAP_OF_LOGIC_PAGE;

        //calcualte the count of full pages
        tmpSectCnt -= SECTOR_CNT_OF_LOGIC_PAGE;
        while(tmpSectCnt >= SECTOR_CNT_OF_LOGIC_PAGE)
        {
            ++*pMidPageCnt;
            tmpSectCnt -= SECTOR_CNT_OF_LOGIC_PAGE;
        }

        //calculate parameter for the tail page
        if(tmpSectCnt)
        {
            //calcualte the tail page sector bitmap
            pTailPage->SectorBitmap = FULL_BITMAP_OF_LOGIC_PAGE >> (SECTOR_CNT_OF_LOGIC_PAGE - tmpSectCnt);

            //calculate the number of the tail page
            pTailPage->LogicPageNum = pHeadPage->LogicPageNum + *pMidPageCnt + 1;
        }
    }
    else
    {
        //the head page is not a full page, reset the sector bitmap
        pHeadPage->SectorBitmap = tmpBitmap & (FULL_BITMAP_OF_LOGIC_PAGE >> (SECTOR_CNT_OF_LOGIC_PAGE - tmpSectCnt));
    }

    LOGICCTL_DBG("   head page number:0x%x, head page bitmap:0x%x\n", pHeadPage->LogicPageNum, pHeadPage->SectorBitmap);
    LOGICCTL_DBG("   the count of middle page:0x%x\n", *pMidPageCnt);
    LOGICCTL_DBG("   tail page number:0x%x, tail page bitmap:0x%x\n\n", pTailPage->LogicPageNum, pTailPage->SectorBitmap);

    return 0;
}


/*
************************************************************************************************************************
*                       CALCLUATE LOGICAL PAGE PARAMETER
*
*Description: Calculate the parameter for a logical page, the zone number, logic block number and
*             and page number in logical block.
*
*Arguments  : pLogicPage    the pointer to the parameter of the logical page;
*             nPage         the number of the global logical page;
*             nBitmap       the bitmap of the valid sector in the logical page.
*
*Return     : calculate page parameter result;
*               = 0     calculate page parameter successful;
*               < 0     calcualte page parameter failed.
************************************************************************************************************************
*/
static __s32 _CalculateLogicPagePar(struct __LogicPageType_t *pLogicPage, __u32 nPage, __u32 nBitmap)
{
    __u32   tmpPage, tmpBlk, tmpZone;

    tmpPage = nPage % PAGE_CNT_OF_LOGIC_BLK;
    tmpBlk = nPage / PAGE_CNT_OF_LOGIC_BLK;
    tmpZone = tmpBlk / DATA_BLK_CNT_OF_ZONE;
    tmpBlk %= DATA_BLK_CNT_OF_ZONE;

    pLogicPage->ZoneNum = tmpZone;
    pLogicPage->BlockNum = tmpBlk;
    pLogicPage->PageNum = tmpPage;
    pLogicPage->SectBitmap = nBitmap;

    LOGICCTL_DBG("[LOGICCTL_DBG] The parameter of logical page 0x%x is : zone 0x%x, block 0x%x, page 0x%x\n",
                    nPage, tmpZone, tmpBlk, tmpPage);

    return 0;
}


/*
************************************************************************************************************************
*                       UPDATE USER DATA WITH THE CACHE PAGE
*
*Description: Update the user data with the cache page, because the number of the page read is
*             same as the cached page, so need update the page data.
*
*Arguments  : nSectBitmap   the bitmap of the valid sectors in the user buffer;
*             pBuf          the pointer to he user buffer;
*
*Return     : update page data result;
*               = 0     update page data successful;
*               < 0     update page data failed.
************************************************************************************************************************
*/
static __s32 _UpdateReadPageData(__u32 nSectBitmap, void * pBuf)
{
    __s32   i;
    __u8    *tmpSrc = LML_WRITE_PAGE_CACHE, *tmpDst = pBuf;

    for(i=0; i<SECTOR_CNT_OF_LOGIC_PAGE; i++)
    {
        if(nSectBitmap & CachePage.SectorBitmap & (1<<i))
        {
            MEMCPY(tmpDst, tmpSrc, SECTOR_SIZE);
        }

        tmpSrc += SECTOR_SIZE;
        tmpDst += SECTOR_SIZE;
    }

    return 0;
}


/*
************************************************************************************************************************
*                       MERGE WRITE PAGE DATA WITH CACHE PAGE DATA
*
*Description: Merge the data of write page with the cached page.
*
*Arguments  : nPage     the number of the logical page which need write to nand flash;
*             nBitmap   the bitmap of the valid sectors in the write page;
*             pBuf      the pointer to the page data buffer;
*
*Return     : merge page data result;
*               = 0     merge page data successful;
*               < 0     merge page data failed.
************************************************************************************************************************
*/
static __s32 _MergeCachePageData(__u32 nPage, __u32 nBitmap, __u8 *pBuf)
{
    __s32   i;
    __u8    *tmpSrc = pBuf, *tmpDst = LML_WRITE_PAGE_CACHE;

    //check if the cache page is valid
    if(CachePage.LogicPageNum == 0xffffffff)
    {
        //cache page is valid, set the logical page number, and reset the sector bitmap to null
        CachePage.LogicPageNum = nPage;
        CachePage.SectorBitmap = 0;
    }

    for(i=0; i<SECTOR_CNT_OF_LOGIC_PAGE; i++)
    {
        if(nBitmap & (1<<i))
        {
            //the sector of the write page is valid, copy data to the page cache
            MEMCPY(tmpDst, tmpSrc, SECTOR_SIZE);
        }

        tmpSrc += SECTOR_SIZE;
        tmpDst += SECTOR_SIZE;
    }

    //reset the sector bitmap for the cached page
    CachePage.SectorBitmap |= nBitmap;

    return 0;
}


/*
************************************************************************************************************************
*                       WRITE DATA OF PAGE CACHE TO NAND FLASH
*
*Description: Write the data of page cache to nand flash.
*
*Arguments  : none.
*
*Return     : write page cache result;
*               = 0     write page cache successful;
*               < 0     write page cache failed.
************************************************************************************************************************
*/
static __s32 _WritePageCacheToNand(void)
{
    __s32   result = 0;
    __u32   tmpPage, tmpBitmap;
    __u8    *tmpBuf;

    tmpPage = CachePage.LogicPageNum;
    tmpBitmap = CachePage.SectorBitmap;
    tmpBuf = LML_WRITE_PAGE_CACHE;

    if(tmpPage != 0xffffffff)
    {

        if(tmpBitmap != FULL_BITMAP_OF_LOGIC_PAGE)
        {
            //get the data from logical page to fill the page cache
            result = LML_PageRead(tmpPage, tmpBitmap ^ FULL_BITMAP_OF_LOGIC_PAGE, tmpBuf);
            if(result < 0)
            {
                return -1;
            }
        }

        //the data in the page cache is full, write it to nand flash
        result = LML_PageWrite(tmpPage, FULL_BITMAP_OF_LOGIC_PAGE, tmpBuf);
        if(result < 0)
        {
            return -1;
        }

        //clear the buffer page parameter
        CachePage.LogicPageNum = 0xffffffff;
        CachePage.SectorBitmap = 0x00;
    }

    return 0;
}


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
__s32 LML_CalculatePhyOpPar(struct __PhysicOpPara_t *pPhyPar, __u32 nZone, __u32 nBlock, __u32 nPage)
{


    __u32   tmpDieNum, tmpBnkNum, tmpBlkNum, tmpPageNum;

    LOGICCTL_DBG("[LOGICCTL_DBG] Calculate the physical operation parameters.\n"
                 "         ZoneNum:0x%x, BlockNum:0x%x, PageNum: 0x%x\n", nZone, nBlock, nPage);

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

    else//(!SUPPORT_INT_INTERLEAVE && !SUPPORT_EXT_INTERLEAVE)
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

    LOGICCTL_DBG("         Calculate Result: BankNum 0x%x, BlkNum 0x%x, PageNum 0x%x\n", tmpBnkNum, tmpBlkNum, tmpPageNum);

    //calculate physical operation parameter successful
    return 0;
}  
#elif(1)
__s32 LML_CalculatePhyOpPar(struct __PhysicOpPara_t *pPhyPar, __u32 nZone, __u32 nBlock, __u32 nPage)
{


    __u32   tmpDieNum, tmpBnkNum, tmpBlkNum, tmpPageNum;

    LOGICCTL_DBG("[LOGICCTL_DBG] Calculate the physical operation parameters.\n"
                 "         ZoneNum:0x%x, BlockNum:0x%x, PageNum: 0x%x\n", nZone, nBlock, nPage);

    //calcualte the Die number by the zone number
    tmpDieNum = nZone / ZONE_CNT_OF_DIE;

    if(SUPPORT_INT_INTERLEAVE && SUPPORT_EXT_INTERLEAVE)
    {
        //nand flash support internal inter-leave and external iner-leave, the block number is
        //same as the virtual block number in the die, the bank number is the virtual page number
        //model the inter-leave bank count, and the page number is the virtual page number
        //divide the inter-leave bank count
        tmpBnkNum = nPage % INTERLEAVE_BANK_CNT + (tmpDieNum * INTERLEAVE_BANK_CNT);
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
        //tmpBnkNum = nPage % INTERLEAVE_BANK_CNT + (tmpDieNum * INTERLEAVE_BANK_CNT);
        tmpBnkNum = nPage % INTERLEAVE_BANK_CNT + (tmpDieNum/DIE_CNT_OF_CHIP) *INTERLEAVE_BANK_CNT;
        tmpBlkNum = nBlock+ ((tmpDieNum % DIE_CNT_OF_CHIP) * (BLOCK_CNT_OF_DIE / PLANE_CNT_OF_DIE));
        tmpPageNum = nPage / INTERLEAVE_BANK_CNT;
    }

    else//(!SUPPORT_INT_INTERLEAVE && !SUPPORT_EXT_INTERLEAVE)
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

    LOGICCTL_DBG("         Calculate Result: BankNum 0x%x, BlkNum 0x%x, PageNum 0x%x\n", tmpBnkNum, tmpBlkNum, tmpPageNum);

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
*Arguments  : pVirtualPage  the pointer to the virtual page paramter;
*
*Return     : read result;
*               = 0     read page data successful;
*               < 0     read page data failed.
************************************************************************************************************************
*/
__s32 LML_VirtualPageRead(struct __PhysicOpPara_t *pVirtualPage)
{
    __s32 i, result;
    __u32 tmpBitmap;
    __u8  *tmpSpare, *tmpSrcData, *tmpDstData, *tmpSrcPtr[4], *tmpDstPtr[4];
    struct __PhysicOpPara_t tmpPhyPage;

    tmpPhyPage = *pVirtualPage;
    tmpSpare = tmpPhyPage.SDataPtr;
    tmpBitmap = tmpPhyPage.SectBitmap;

    if(tmpSpare)
    {
        tmpPhyPage.SDataPtr = LML_SPARE_BUF;

        //process the pointer to spare area data
        for(i=0; i<2; i++)
        {
            if(tmpBitmap & (1<<i))
            {
                tmpSrcPtr[i] = LML_SPARE_BUF + 4 * i;
                tmpDstPtr[i] = tmpSpare + 4 * i;
            }
            else
            {
                tmpDstPtr[i] = NULL;
            }
        }

        for(i=0; i<2; i++)
        {
            if(tmpBitmap & (1<<(i + SECTOR_CNT_OF_SINGLE_PAGE)))
            {
                tmpSrcPtr[i+2] = LML_SPARE_BUF + 4 * (i + SECTOR_CNT_OF_SINGLE_PAGE);
                tmpDstPtr[i+2] = tmpSpare + 8 + 4 * i;
            }
            else
            {
                tmpDstPtr[i+2] = NULL;
            }
        }
    }
    else
    {
        tmpPhyPage.SDataPtr = NULL;
    }

    result = PHY_PageRead(&tmpPhyPage);

    //process spare area data
    if(tmpSpare)
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

	//add in 2010-05-21 by penggang, the logic layer needn't care the ecc error
	if(result == -ERR_ECC)
		result = 0;

    return result;
}


/*
************************************************************************************************************************
*                       WRITE PAGE DATA TO VIRTUAL BLOCK
*
*Description: Write page data to virtual block, the block is composed by several physical block.
*             It is named super block too.
*
*Arguments  : pVirtualPage  the pointer to the virtual page parameter.
*
*Return     : write result;
*               = 0     write page data successful;
*               < 0     write page data failed.
************************************************************************************************************************
*/
__s32 LML_VirtualPageWrite( struct __PhysicOpPara_t *pVirtualPage)
{
    __s32 i, result;
    __u32 tmpBitmap;
    __u8  *tmpSpare, *tmpSrcData, *tmpDstData, *tmpSrcPtr[4], *tmpDstPtr[4];
    struct __PhysicOpPara_t tmpPhyPage;

    tmpPhyPage = *pVirtualPage;
    tmpSpare = tmpPhyPage.SDataPtr;
    //tmpBitmap = tmpPhyPage.SectBitmap;
    tmpBitmap = FULL_BITMAP_OF_LOGIC_PAGE;
    tmpPhyPage.SDataPtr = LML_SPARE_BUF;

    //process spare area data
    if(tmpSpare)
    {
        //process the pointer to spare area data
        for(i=0; i<2; i++)
        {
            if(tmpBitmap & (1<<i))
            {
                tmpSrcPtr[i] = tmpSpare + 4 * i;
                tmpDstPtr[i] = LML_SPARE_BUF + 4 * i;
            }
            else
            {
                tmpDstPtr[i] = NULL;
            }
        }

        for(i=0; i<2; i++)
        {
            if(tmpBitmap & (1<<(i + SECTOR_CNT_OF_SINGLE_PAGE)))
            {
                tmpSrcPtr[i+2] = tmpSpare + 8 + 4 * i;
                tmpDstPtr[i+2] = LML_SPARE_BUF + 4 * (i + SECTOR_CNT_OF_SINGLE_PAGE);
            }
            else
            {
                tmpDstPtr[i+2] = NULL;
            }
        }

        MEMSET(LML_SPARE_BUF, 0xff, SECTOR_CNT_OF_SUPER_PAGE * 4);

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
        MEMSET(LML_SPARE_BUF, 0xff, SECTOR_CNT_OF_SUPER_PAGE * 4);
    }

    result = PHY_PageWrite(&tmpPhyPage);

    return result;
}


/*
************************************************************************************************************************
*                       NAND FLASH LOGIC MANAGE LAYER ERASE SUPER BLOCK
*
*Description: Erase the given super block.
*
*Arguments  : nZone         the number of the zone which the super block belonged to;
*             nSuperBlk     the number of the super block which need be erased.
*
*Return     : erase result
*               = 0     super block erase successful;
*               =-1     super block erase failed.
************************************************************************************************************************
*/
__s32 LML_VirtualBlkErase(__u32 nZone, __u32 nSuperBlk)
{
    __s32 i, result = 0;
    struct __PhysicOpPara_t tmpPhyBlk;

    #if CFG_SUPPORT_WEAR_LEVELLING

    //increase the erase counter of super block
    BLK_ERASE_CNTER++;

    #endif
    //erase every block belonged to different banks
    for(i=0; i<INTERLEAVE_BANK_CNT; i++)
    {
        //calculate the physical operation parameter by te die number, block number and page number
        LML_CalculatePhyOpPar(&tmpPhyBlk, nZone, nSuperBlk, i);

        PHY_BlockErase(&tmpPhyBlk);
    }
	
    //check the result of the block erase
    for(i=0; i<INTERLEAVE_BANK_CNT; i++)
    {
        result = PHY_SynchBank(i, SYNC_BANK_MODE);
        if(result < 0)
        {
        	//the operation of last page write is failed
            LOGICCTL_ERR("[LOGICCTL_DBG] erase zone %x block %x bank %x fail\n",
            				nZone,nSuperBlk,i);

            return -1;
        }
    }

    return 0;
}


/*
************************************************************************************************************************
*                       LOGICAL MANAGE LAYER CLOSE WRITE PAGE
*
*Description: Close last write page, for switch nand flash operation mode.
*
*Arguments  : none.
*
*Return     : clase page result;
*               = 0     close page successful;
*               = -1    close page failed.
************************************************************************************************************************
*/
static __s32 _CloseWritePage(void)
{
    __s32 result;
    struct __PhysicOpPara_t tmpPage;
    struct __LogBlkType_t tmpLogBlk;

    //check if current mode is write mode
    if(LogicalCtl.OpMode == 'w')
    {
        //calculate the paramter of last page
        LML_CalculatePhyOpPar(&tmpPage, LogicalCtl.ZoneNum, LogicalCtl.LogBlkNum.PhyBlkNum, LogicalCtl.LogPageNum);

        result = PHY_SynchBank(tmpPage.BankNum, SYNC_BANK_MODE);
        if(result < 0)
        {
            //the operation of last page write is failed
            LOGICCTL_DBG("[LOGICCTL_DBG] Write page failed when close write page! Bnk:0x%x, Blk:0x%x, Page:0x%x\n",
                            tmpPage.BankNum, tmpPage.BlkNum, tmpPage.PageNum);

            //process the bad block
            result = LML_BadBlkManage(&LogicalCtl.LogBlkNum, LogicalCtl.ZoneNum, LogicalCtl.LogPageNum + 1, &LogicalCtl.LogBlkNum);
            if(result < 0)
            {
                LOGICCTL_ERR("[LOGICCTL_ERR] Bad block proecess failed when close write page, Err:0x%x\n", result);
                return -1;
            }

            //reset the physical block number of the log block with the new log block
            BMM_GetLogBlk(LogicalCtl.LogicBlkNum, &tmpLogBlk);
            tmpLogBlk.PhyBlk = LogicalCtl.LogBlkNum;
            BMM_SetLogBlk(LogicalCtl.LogicBlkNum, &tmpLogBlk);
        }

        //wait the nand flash chip ready
        PHY_SynchBank(tmpPage.BankNum, SYNC_CHIP_MODE);
    }

    LogicalCtl.OpMode = 'n';

    return 0;
}


/*
************************************************************************************************************************
*                       NAND FLASH LOGIC MANAGE LAYER PAGE READ
*
*Description: Read data from logic disk to buffer based page.
*
*Arguments  : nPage     the page address which need be read;
*             nBitmap   the bitmap of the sectors in the page which need be read data;
*             pBuf      the pointer to the buffer where will store the data read out.
*
*Return     : page read result;
*               = 0     read successful;
*               > 0     read successful, but need do some process;
*               < 0     read failed.
************************************************************************************************************************
*/
__s32 LML_PageRead(__u32 nPage, __u32 nBitmap, void* pBuf)
{
    __s32 result;
    __u32 tmpSuperBlk, tmpSuperPage;
    struct __LogicPageType_t tmpLogicPage;
    struct __PhysicOpPara_t tmpPhyPage;
    struct __LogBlkType_t tmpLogBlk;

    _CalculateLogicPagePar(&tmpLogicPage, nPage, nBitmap);

    //check if last operation is read, if not, need set to read mode
    if(LogicalCtl.OpMode != 'r')
    {
        if(LogicalCtl.OpMode == 'w')
        {
            //last operation is write, need close last write logical page, switch to read mode
            result = _CloseWritePage();
            if(result < 0)
            {
                LOGICCTL_ERR("[LOGICCTL_ERR] Close write page failed, when read logical page!\n");
                return -ERR_LOGICCTL;
            }
        }

        //set operation mode to read
        LogicalCtl.OpMode = 'r';
    }

    //check if access the same zone area as last access
    if(tmpLogicPage.ZoneNum != LogicalCtl.ZoneNum)
    {
        //swap the block mapping table to ram which is need be accessing currently
        result = BMM_SwitchMapTbl(tmpLogicPage.ZoneNum);
        if(result < 0)
        {
            LOGICCTL_ERR("[LOGICCTL_ERR] Switch block mapping table failed when read logical page! Err:0x%x\n", result);
            return -ERR_MAPPING;
        }

        //set the zone number that accessing currently
        LogicalCtl.ZoneNum = tmpLogicPage.ZoneNum;
        //reset the number of logical block
        LogicalCtl.LogicBlkNum = 0xffff;
    }

    //check if accessing the same logical block as last access
    if(tmpLogicPage.BlockNum != LogicalCtl.LogicBlkNum)
    {
        //get the data block number and log block number for read
        BMM_GetDataBlk(tmpLogicPage.BlockNum, &LogicalCtl.DataBlkNum);

        BMM_GetLogBlk(tmpLogicPage.BlockNum, &tmpLogBlk);
        LogicalCtl.LogBlkNum = tmpLogBlk.PhyBlk;

        //set the logical block number for logical control parameter
        LogicalCtl.LogicBlkNum = tmpLogicPage.BlockNum;
    }

    //set the number for logical page that accessing currently
    LogicalCtl.LogicPageNum = tmpLogicPage.PageNum;

    //get the number of log page for read
    if(LogicalCtl.LogBlkNum.PhyBlkNum != 0xffff)
    {
        //get log page with read mode
        LogicalCtl.LogPageNum = PMM_GetLogPage(tmpLogicPage.BlockNum, LogicalCtl.LogicPageNum, 'r');
    }
    else
    {
        LogicalCtl.LogPageNum = 0xffff;
    }

    //set the number of super block and super page in the super block for get page data
    if(LogicalCtl.LogPageNum != 0xffff)
    {
        tmpSuperBlk = LogicalCtl.LogBlkNum.PhyBlkNum;
        tmpSuperPage = LogicalCtl.LogPageNum;
    }
    else
    {
        tmpSuperBlk = LogicalCtl.DataBlkNum.PhyBlkNum;
        tmpSuperPage = LogicalCtl.LogicPageNum;
    }

    //calculate the parameter of physical operation for get page data
    LML_CalculatePhyOpPar(&tmpPhyPage, LogicalCtl.ZoneNum, tmpSuperBlk, tmpSuperPage);
    tmpPhyPage.SectBitmap = tmpLogicPage.SectBitmap;
    tmpPhyPage.MDataPtr = pBuf;
    tmpPhyPage.SDataPtr = NULL;

    //get data from physical page with the physical operation
    result = LML_VirtualPageRead(&tmpPhyPage);
    if(result < 0)
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] Get page data failed when read logical page! Err:0x%x\n ", result);
        return -1;
    }

    return result;
}


/*
************************************************************************************************************************
*                       NAND FLASH LOGIC MANAGE LAYER PAGE WRITE
*
*Description: Write data from buffer to logic area based on page.
*
*Arguments  : nPage     the page address which need be write;
*             nBitmap   the bitmap of sectors in the page which need be write, it is always full;
*             pBuf      the pointer to the buffer where is storing the data.
*
*Return     : write result;
*               = 0     write successful;
*               > 0     write successful, but need do some process;
*               < 0     write failed.
************************************************************************************************************************
*/
__s32 LML_PageWrite(__u32 nPage, __u32 nBitmap, void* pBuf)
{
    __s32 result;
    struct __LogicPageType_t tmpLogicPage;
    struct __PhysicOpPara_t tmpPhyPage;
    struct __NandUserData_t tmpSpare[2];
    struct __LogBlkType_t tmpLogBlk;

		
    //check if the bitmap of valid sectors is full, if not, report error
    if(nBitmap != FULL_BITMAP_OF_LOGIC_PAGE)
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] Sector bitmap is not full when write logical page!\n");
        return -ERR_LOGICCTL;
    }

    //calculate the pamater for the logical page
    _CalculateLogicPagePar(&tmpLogicPage, nPage, nBitmap);

    //check if access the same zone area as last access
    if(tmpLogicPage.ZoneNum != LogicalCtl.ZoneNum)
    {
        //last operation is write, need close last write logical page, switch to read mode
        result = _CloseWritePage();
        if(result < 0)
        {
            LOGICCTL_ERR("[LOGICCTL_ERR] Close write page failed, when write logical page!\n");
            return -ERR_LOGICCTL;
        }

        //swap the block mapping table to ram which is need be accessing currently
        result = BMM_SwitchMapTbl(tmpLogicPage.ZoneNum);
        if(result < 0)
        {
            LOGICCTL_ERR("[LOGICCTL_ERR] Switch block mapping table failed when write logical page! Err:0x%x\n", result);
            return -ERR_MAPPING;
        }

        //set the zone number that accessing currently
        LogicalCtl.ZoneNum = tmpLogicPage.ZoneNum;
        //reset the number of logical block
        LogicalCtl.LogicBlkNum = 0xffff;
    }

    //set dirty flag for block mapping table
    BMM_SetDirtyFlag();

    //check if accessing the same logical block as last access
    if(tmpLogicPage.BlockNum != LogicalCtl.LogicBlkNum)
    {
        //last operation is write, need close last write logical page, switch to read mode
        result = _CloseWritePage();
        if(result < 0)
        {
            LOGICCTL_ERR("[LOGICCTL_ERR] Close write page failed, when write logical page!\n");
            return -ERR_LOGICCTL;
        }

        //set the logical block number for logical control parameter
        LogicalCtl.LogicBlkNum = tmpLogicPage.BlockNum;
    }

    //set the number of the logical page for logical control
    LogicalCtl.LogicPageNum = tmpLogicPage.PageNum;

    //get log page for write page data, log page is necessary when write logical page
    LogicalCtl.LogPageNum = PMM_GetLogPage(LogicalCtl.LogicBlkNum, LogicalCtl.LogicPageNum, 'w');
    if(LogicalCtl.LogPageNum == 0xffff)
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] Get log page failed when write logical page!\n");
        return -ERR_MAPPING;
    }

    //get the data block number and log block number for write
    BMM_GetDataBlk(tmpLogicPage.BlockNum, &LogicalCtl.DataBlkNum);

    BMM_GetLogBlk(tmpLogicPage.BlockNum, &tmpLogBlk);
    LogicalCtl.LogBlkNum = tmpLogBlk.PhyBlk;


    //check if the log block is valid, log block is nessesary necessary when write logical page
    if(LogicalCtl.LogBlkNum.PhyBlkNum == 0xffff)
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] Get log block failed when write logical page!\n");
        return -ERR_MAPPING;
    }

    //set spare area data for writing to the spare area of nand flash
    if(LogicalCtl.LogPageNum == 0)
    {
        //log page is the page0 of the super block, the meaning of the page status is log age
        LML_CalculatePhyOpPar(&tmpPhyPage, LogicalCtl.ZoneNum, LogicalCtl.DataBlkNum.PhyBlkNum, 0);
        tmpPhyPage.SectBitmap = 0x3;
        tmpPhyPage.MDataPtr = LML_TEMP_BUF;
        tmpPhyPage.SDataPtr = (void *)tmpSpare;
        result = LML_VirtualPageRead(&tmpPhyPage);
        if(result < 0)
        {
            LOGICCTL_ERR("[LOGICCTL_ERR] Get log age of data block failed when write logical page, Err:0x%x!\n", result);
            return -ERR_PHYSIC;
        }

        //increase the log age
        tmpSpare[0].PageStatus = tmpSpare[0].PageStatus + 1;
        tmpSpare[1].PageStatus = tmpSpare[0].PageStatus;
    }
    else
    {
        //set page used mark to the spare area data
        tmpSpare[0].PageStatus = 0x55;
        tmpSpare[1].PageStatus = 0x55;
    }
    tmpSpare[0].BadBlkFlag = 0xff;
    tmpSpare[1].BadBlkFlag = 0xff;
    tmpSpare[0].LogicInfo = ((LogicalCtl.ZoneNum % ZONE_CNT_OF_DIE)<<10) | LogicalCtl.LogicBlkNum;
    tmpSpare[1].LogicInfo = ((LogicalCtl.ZoneNum % ZONE_CNT_OF_DIE)<<10) | LogicalCtl.LogicBlkNum;
    tmpSpare[0].LogicPageNum = LogicalCtl.LogicPageNum;
    tmpSpare[1].LogicPageNum = LogicalCtl.LogicPageNum;

__TRY_WRITE_PHYSIC_PAGE:
    //calculate the parameter for writing page to nand flash
    LML_CalculatePhyOpPar(&tmpPhyPage, LogicalCtl.ZoneNum, LogicalCtl.LogBlkNum.PhyBlkNum, LogicalCtl.LogPageNum);
    tmpPhyPage.SectBitmap = tmpLogicPage.SectBitmap;
    tmpPhyPage.MDataPtr = pBuf;
    tmpPhyPage.SDataPtr = (void *)tmpSpare;

    #if !CFG_SUPPORT_CHECK_WRITE_SYNCH
	
	if (LogicalCtl.OpMode == 'w')
    {
    	//synch currently write bank
    	result = PHY_SynchBank(tmpPhyPage.BankNum, SYNC_BANK_MODE);
    	if(result < 0)
    	{
        	//the last write operation on current bank is failed, the block is bad, need proccess it
        	LOGICCTL_DBG("[LOGICCTL_DBG] Find a bad block when write logical page! bank:0x%x, block:0x%x, page:0x%x\n",
                tmpPhyPage.BankNum, tmpPhyPage.BlkNum, tmpPhyPage.PageNum);			
			
        	//process the bad block
        	result = LML_BadBlkManage(&LogicalCtl.LogBlkNum, LogicalCtl.ZoneNum, LogicalCtl.LogPageNum, &LogicalCtl.LogBlkNum);
        	if(result < 0)
        	{
            	LOGICCTL_ERR("[LOGICCTL_ERR] Bad block proecess failed when write page, Err:0x%x\n", result);
            	return -1;
        	}

        	//reset the physical block number of the log block with the new log block
        	BMM_GetLogBlk(LogicalCtl.LogicBlkNum, &tmpLogBlk);
        	tmpLogBlk.PhyBlk = LogicalCtl.LogBlkNum;
        	BMM_SetLogBlk(LogicalCtl.LogicBlkNum, &tmpLogBlk);

        	goto __TRY_WRITE_PHYSIC_PAGE;
    	}
    }
#endif

	 result = LML_VirtualPageWrite(&tmpPhyPage);
    if(result < 0)
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] Physical write module failed when write logical page, Err:0x%x!\n", result);
        return -ERR_LOGICCTL;
    }

#if CFG_SUPPORT_CHECK_WRITE_SYNCH

    //synch currently write bank
    result = PHY_SynchBank(tmpPhyPage.BankNum, SYNC_BANK_MODE);
    if(result < 0)
    {
        //the last write operation on current bank is failed, the block is bad, need proccess it
        LOGICCTL_DBG("[LOGICCTL_DBG] Find a bad block when write logical page! bank:0x%x, block:0x%x, page:0x%x\n",
                tmpPhyPage.BankNum, tmpPhyPage.BlkNum, tmpPhyPage.PageNum);

        //process the bad block
        result = LML_BadBlkManage(&LogicalCtl.LogBlkNum, LogicalCtl.ZoneNum, LogicalCtl.LogPageNum, &LogicalCtl.LogBlkNum);
        if(result < 0)
        {
            LOGICCTL_ERR("[LOGICCTL_ERR] Bad block proecess failed when write page, Err:0x$x\n", result);
            return -1;
        }

        //reset the physical block number of the log block with the new log block
        BMM_GetLogBlk(LogicalCtl.LogicBlkNum, &tmpLogBlk);
        tmpLogBlk.PhyBlk = LogicalCtl.LogBlkNum;
        BMM_SetLogBlk(LogicalCtl.LogicBlkNum, &tmpLogBlk);

        goto __TRY_WRITE_PHYSIC_PAGE;
    }

#endif

    //set the mode of operation
    LogicalCtl.OpMode = 'w';
    return 0;
}


/*
************************************************************************************************************************
*                       NAND FLASH LOGIC MANAGE LAYER FLUSH PAGE CACHE
*
*Description: Flush the data in the cache buffer to nand flash.
*
*Arguments  : none

*Return     : flush result;
*               = 0     flush successful;
*               = -1    flush failed.
************************************************************************************************************************
*/
__s32 LML_FlushPageCache(void)
{
    __s32   result;

    result = _WritePageCacheToNand();
    if(result < 0)
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] Write page cache to nand failed when flush page cache! Error:0x%x\n", result);
        return -1;
    }

    result = _CloseWritePage();
    if(result < 0)
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] Close write page failed when flush page cache! Error:0x%x\n", result);
        return -1;
    }

    return 0;
}


/*
************************************************************************************************************************
*                           NAND FLASH LOGIC MANAGE LAYER READ
*
*Description: Read data from logic disk area to buffer.
*
*Arguments  : nSectNum      the logic block address on the logic area from where to read;
*             nSectorCnt    the size of the data need be read, based on sector;
*             pBuf          the pointer to the buffer where will store the data readout of nand.
*
*Return     : read result;
*               = 0     read successful;
*               = -1    read failed.
************************************************************************************************************************
*/
__s32 LML_Read(__u32 nSectNum, __u32 nSectorCnt, void* pBuf)
{
    __s32   i, result;
    __u32   tmpMidPageCnt, tmpPageNum, tmpBitmap, tmpPageCnt;
    __u8    *tmpBuf;
    struct __GlobalLogicPageType_t tmpHeadPage, tmpTailPage;

    LOGICCTL_DBG("[LOGICCTL_DBG] LML_Read, sector number:0x%x, sector cnt:0x%x, Buffer:0x%x\n", nSectNum, nSectorCnt, pBuf);

		if(((__u32)pBuf)&0x3)
	  {
	      LOGICCTL_ERR("[LOGICCTL_ERR] LML_Write, invalid bufaddr: 0x%x \n", (__u32)(pBuf));
				return -ERR_ADDRBEYOND;
	  }
	  
    //check if the parameter is valid
    if(((nSectNum + nSectorCnt) > LogicalCtl.DiskCap)||(nSectNum>LogicalCtl.DiskCap-1)||(nSectorCnt>LogicalCtl.DiskCap-1))
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] LML_Read, the addr of read(start:%x, cnt:%x) is beyond the disk volume %x!!!\n", 
						nSectNum, nSectorCnt,LogicalCtl.DiskCap);
        return -ERR_ADDRBEYOND;
    }
    if(!nSectorCnt)
    {
        LOGICCTL_DBG("[WARNING] LML_Read, sector count of read is 0!!!\n");
        return 0;
    }
    //calculate logical parameter of the sectors
    _CalculateSectPar(nSectNum, nSectorCnt, &tmpHeadPage, &tmpMidPageCnt, &tmpTailPage);

    tmpBuf = pBuf;

    //calculate the buffer address for page align
    for(i=0; i<SECTOR_CNT_OF_LOGIC_PAGE; i++)
    {
        if(tmpHeadPage.SectorBitmap & (1<<i))
        {
            break;
        }
        tmpBuf -= SECTOR_SIZE;
    }

    tmpPageNum = tmpHeadPage.LogicPageNum;
    tmpBitmap = tmpHeadPage.SectorBitmap;

    //calculate the whole count the logical pages
    tmpPageCnt = 1 + tmpMidPageCnt;
    if(tmpTailPage.SectorBitmap)
    {
        tmpPageCnt++;
    }

    for(i=0; i<tmpPageCnt; i++)
    {
        //read data from the logical page
        result = LML_PageRead(tmpPageNum, tmpBitmap, tmpBuf);
        if(result < 0)
        {
            //get page data failed!
            LOGICCTL_ERR("[LOGICCTL_ERR] LML_Read, read page data faild! the page number:0x%x, sector bitmap:0x%x, buffer:0x%x, "
                    "error number:0x%x\n",tmpPageNum, tmpBitmap, (__u32)tmpBuf, result);
            return -1;
        }

        #if CFG_SUPPORT_READ_RECLAIM
        if(result == ECC_LIMIT)
        {
            LML_ReadReclaim(tmpPageNum);
        }
        #endif

        //check if need update the page data with the cache page
        if(tmpPageNum == CachePage.LogicPageNum)
        {
            _UpdateReadPageData(tmpBitmap, tmpBuf);
        }

        //increase the logical page number and the buffer address
        tmpPageNum++;
        tmpBuf += SECTOR_SIZE * SECTOR_CNT_OF_LOGIC_PAGE;

        //check if read data from the tail page
        if(tmpTailPage.SectorBitmap && (i == tmpPageCnt - 2))
        {
            tmpBitmap = tmpTailPage.SectorBitmap;
        }
        else
        {
            tmpBitmap = FULL_BITMAP_OF_LOGIC_PAGE;
        }
    }

    return 0;
}
EXPORT_SYMBOL(LML_Read);

/*
extern dump(void * buf, __u32 len, __u8 nbyte, __u8 linelen);

void echo_write_data (__u32 nSectNum, __u32 nSectorCnt, void* pBuf)
{
	__u32 i,j;
	__u8 *buf;

	for (i = 0,j = nSectNum; i < nSectorCnt; i++,j++)
	{
		buf = (__u8 *)pBuf + i*512;
		PRINT("***********echo data from sector %d***************\n",j);
		dump(buf, 16,1, 16);
	}

	return;
	
}*/

/*
************************************************************************************************************************
*                           NAND FLASH LOGIC MANAGE LAYER WRITE
*
*Description: Write data from buffer to logic disk area.
*
*Arguments  : nSectNum      the logic block address on the logic area from where to write;
*             nSectorCnt    the size of the data need to be write, based on sector;
*             pBuf          the pointer to the buffer where stored the data write to nand flash.
*
*Return     : write result;
*               = 0     write successful;
*               = -1    write failed.
************************************************************************************************************************
*/
__s32 LML_Write(__u32 nSectNum, __u32 nSectorCnt, void* pBuf)
{
    __s32   i, result;
    __u32   tmpMidPageCnt, tmpPageNum, tmpBitmap, tmpPageCnt;
    __u8    *tmpBuf;
    struct __GlobalLogicPageType_t tmpHeadPage, tmpTailPage;
    
	 	if(((__u32)pBuf)&0x3)
	  {
	      LOGICCTL_ERR("[LOGICCTL_ERR] LML_Write, invalid bufaddr: 0x%x \n", (__u32)(pBuf));
				return -ERR_ADDRBEYOND;
	  }
	
    if(((nSectNum + nSectorCnt) > LogicalCtl.DiskCap)||(nSectNum>LogicalCtl.DiskCap-1)||(nSectorCnt>LogicalCtl.DiskCap-1))
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] LML_Write, the addr of write(start:%x, cnt:%x) is beyond the disk volume %x!!!\n", 
						nSectNum, nSectorCnt,LogicalCtl.DiskCap);
				return -ERR_ADDRBEYOND;
    }
    if(!nSectorCnt)
    {
        LOGICCTL_DBG("[WARNING] LML_Write, sector count of write is 0!!!\n");
        return 0;
    }
    //calculate the logical parameter for the sectors
    _CalculateSectPar(nSectNum, nSectorCnt, &tmpHeadPage, &tmpMidPageCnt, &tmpTailPage);

    tmpBuf = pBuf;

    //calculate the buffer address for page align
    for(i=0; i<SECTOR_CNT_OF_LOGIC_PAGE; i++)
    {
        if(tmpHeadPage.SectorBitmap & (1<<i))
        {
            break;
        }
        tmpBuf -= SECTOR_SIZE;
    }

    tmpPageNum = tmpHeadPage.LogicPageNum;
    tmpBitmap = tmpHeadPage.SectorBitmap;

    //calculate the whole count the logical pages
    tmpPageCnt = 1 + tmpMidPageCnt;
    if(tmpTailPage.SectorBitmap)
    {
        tmpPageCnt++;
    }

    for(i=0; i<tmpPageCnt; i++)
    {
        //check if current write page is same as the cached page
        if(tmpPageNum == CachePage.LogicPageNum)
        {
            //merge current page data with the cached page
            _MergeCachePageData(tmpPageNum, tmpBitmap, tmpBuf);
        }
        else
        {
            //flush cached page to nand flash
            result = _WritePageCacheToNand();
            if(result < 0)
            {
                LOGICCTL_ERR("[LOGICCTL_ERR] Write page cache to nand flash failed!\n");
                return -1;
            }

            if(tmpBitmap == FULL_BITMAP_OF_LOGIC_PAGE)
            {
                //write the full page to nand flash
                result = LML_PageWrite(tmpPageNum, tmpBitmap, tmpBuf);
                if(result < 0)
                {
                    LOGICCTL_ERR("[LOGICCTL_ERR] Write logical page failed! Error:0x%x\n", result);
                    return -1;
                }
            }
            else
            {
                //write page is not full, merge the data to page cache
                _MergeCachePageData(tmpPageNum, tmpBitmap, tmpBuf);
            }
        }

        //increase the logical page number and the buffer address
        tmpPageNum++;
        tmpBuf += SECTOR_SIZE * SECTOR_CNT_OF_LOGIC_PAGE;

        //reset the sector bitmap in the logical page
        if(tmpTailPage.SectorBitmap && (i == tmpPageCnt - 2))
        {
            tmpBitmap = tmpTailPage.SectorBitmap;
        }
        else
        {
            tmpBitmap = FULL_BITMAP_OF_LOGIC_PAGE;
        }
    }
	 return 0;
}
EXPORT_SYMBOL(LML_Write);


/*
************************************************************************************************************************
*                           INITIATE NAND FLASH LOGIC MANAGE LAYER
*
*Description: initiate the logic manage layer for nand flash driver.
*
*Arguments  : none;
*
*Return     : intiate result;
*               = 0     init successful;
*               = -1    init failed.
************************************************************************************************************************
*/
__s32 LML_Init(void)
{
    __s32   result;

    CachePage.LogicPageNum = 0xffffffff;
    CachePage.SectorBitmap = 0x00000000;

    LogicalCtl.OpMode = 'n';
    LogicalCtl.ZoneNum = 0xff;
    LogicalCtl.LogicBlkNum = 0xffff;
    LogicalCtl.LogicPageNum = 0xffff;
    LogicalCtl.LogPageNum = 0xffff;
    LogicalCtl.DataBlkNum.BlkEraseCnt = 0xffff;
    LogicalCtl.DataBlkNum.PhyBlkNum = 0xffff;
    LogicalCtl.LogBlkNum.BlkEraseCnt = 0xffff;
    LogicalCtl.LogBlkNum.PhyBlkNum = 0xffff;
    LogicalCtl.DiskCap = SECTOR_CNT_OF_SINGLE_PAGE * PAGE_CNT_OF_PHY_BLK * BLOCK_CNT_OF_DIE * \
            DIE_CNT_OF_CHIP * NandDriverInfo.NandStorageInfo->ChipCnt  / BLOCK_CNT_OF_ZONE * DATA_BLK_CNT_OF_ZONE;

		
    //request page buffer for process data    
    NandDriverInfo.PageCachePool->PageCache1 = (__u8 *)MALLOC(SECTOR_CNT_OF_LOGIC_PAGE * SECTOR_SIZE);
    if(!NandDriverInfo.PageCachePool->PageCache1)
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] Request memory for nand flash page cache failed!!");
        return -ERR_MALLOC;
    }
    //NandDriverInfo.PageCachePool->PageCache2 = NULL;
     NandDriverInfo.PageCachePool->PageCache2 = (__u8 *)MALLOC(SECTOR_CNT_OF_LOGIC_PAGE * SECTOR_SIZE);
    if(!NandDriverInfo.PageCachePool->PageCache2)
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] Request memory for nand flash page cache failed!!");
        return -ERR_MALLOC;
    }

    //request buffer for process spare area data
    MEMSET(LML_SPARE_BUF, 0xff, SECTOR_CNT_OF_SUPER_PAGE * 4);

    //init the mapping tabel manage module
    result = BMM_InitMapTblCache();
    if(result < 0)
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] Init the mapping table manage module failed! Err:0x%x\n", result);
        return -ERR_MAPPING;
    }

    result = PMM_InitMapTblCache();
    if(result < 0)
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] Init the mapping table manage module failed! Err:0x%x\n", result);
        return -ERR_MAPPING;
    }

    return 0;
}


/*
************************************************************************************************************************
*                           NAND FLASH LOGIC MANAGE LAYER EXIT
*
*Description: exit nand flash logic manage layer.
*
*Arguments  : none;
*
*Return     : exit result;
*               = 0     exit successfu;
*               = -1    exit failed.
************************************************************************************************************************
*/
__s32 LML_Exit(void)
{
     //flush page cache to nand flash
    LML_FlushPageCache();

    //write all mapping table to nand flash
    BMM_WriteBackAllMapTbl();

    //free the mapping module
    PMM_ExitMapTblCache();
    BMM_ExitMapTblCache();

    FREE(NandDriverInfo.PageCachePool->PageCache1,SECTOR_CNT_OF_LOGIC_PAGE * SECTOR_SIZE);
	FREE(NandDriverInfo.PageCachePool->PageCache2,SECTOR_CNT_OF_LOGIC_PAGE * SECTOR_SIZE);

    return 0;
}

// 2010-12-04 modified
__u32 NAND_GetDiskSize(void)
{
    __u32 disksize;

    disksize = (SECTOR_CNT_OF_SINGLE_PAGE * PAGE_CNT_OF_PHY_BLK * BLOCK_CNT_OF_DIE * \
            DIE_CNT_OF_CHIP * NandStorageInfo.ChipCnt  / 1024 * DATA_BLK_CNT_OF_ZONE);

    return disksize;
}

