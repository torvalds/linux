/*
 * drivers/block/sunxi_nand/src/logic/bad_manage.c
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

#include "../include/nand_logic.h"

extern struct __NandDriverGlobal_t     NandDriverInfo;

/*
************************************************************************************************************************
*                       RESTORE VALID PAGE DATA FROM BAD BLOCK
*
*Description: Restore the valid page data from the bad block.
*
*Arguments  : pBadBlk   the pointer to the bad physical block parameter;
*             nErrPage  the number of the error page;
*             pNewBlk   the pointer to the new valid block parameter.
*
*Return     : restore page data result;
*               = 0     restore data successful;
*               = -1    restore data failed.
************************************************************************************************************************
*/
static __s32 _RestorePageData(struct __SuperPhyBlkType_t *pBadBlk, __u32 nZoneNum, __u32 nErrPage, struct __SuperPhyBlkType_t *pNewBlk)
{
    __s32 i, result;
    struct __PhysicOpPara_t tmpSrcPage, tmpDstPage;

    //set sector bitmap and buffer pointer for copy nand flash page
    tmpSrcPage.SectBitmap = FULL_BITMAP_OF_SUPER_PAGE;
    tmpDstPage.SectBitmap = FULL_BITMAP_OF_SUPER_PAGE;
    tmpSrcPage.MDataPtr = NULL;
    tmpSrcPage.SDataPtr = NULL;

    for(i=0; i<nErrPage; i++)
    {
        //calculate source page and destination page parameter for copy nand page
        LML_CalculatePhyOpPar(&tmpSrcPage, nZoneNum, pBadBlk->PhyBlkNum, i);
        LML_CalculatePhyOpPar(&tmpDstPage, nZoneNum, pNewBlk->PhyBlkNum, i);

        PHY_PageCopyback(&tmpSrcPage, &tmpDstPage);
        //check page copy result
        result = PHY_SynchBank(tmpDstPage.BankNum, SYNC_CHIP_MODE);
        if(result < 0)
        {
            LOGICCTL_DBG("[LOGICCTL_DBG] Copy page failed when restore bad block data!\n");
            return -1;
        }
    }

    return 0;
}


/*
************************************************************************************************************************
*                       WRITE BAD FLAG TO BAD BLOCK
*
*Description: Write bad block flag to bad block.
*
*Arguments  : pBadBlk   the pointer to the bad physical block parameter.
*
*Return     : mark bad block result;
*               = 0     mark bad block successful;
*               = -1    mark bad block failed.
************************************************************************************************************************
*/
static __s32 _MarkBadBlk(struct __SuperPhyBlkType_t *pBadBlk, __u32 nZoneNum)
{
    __s32   i;
	__s32   ret;
    struct __PhysicOpPara_t tmpPage;
    struct __NandUserData_t tmpSpare[2];

	//add by neil 20101201
	/* erase bad blcok */
	ret = LML_VirtualBlkErase(nZoneNum, pBadBlk->PhyBlkNum);
	if(ret)
	{
		LOGICCTL_DBG("[LOGICCTL_DBG] erase bad block fail!\n");
	}


    //set the spare area data for write
    MEMSET((void *)tmpSpare, 0x00, 2*sizeof(struct __NandUserData_t));

    tmpPage.SectBitmap = FULL_BITMAP_OF_SUPER_PAGE;
    tmpPage.MDataPtr = LML_TEMP_BUF;
    tmpPage.SDataPtr = (void *)tmpSpare;

    //write the bad flag in ervery single physical block of the super block
    for(i=0; i<INTERLEAVE_BANK_CNT; i++)
    {
        //write the bad flag in the first page of the physical block
        LML_CalculatePhyOpPar(&tmpPage, nZoneNum, pBadBlk->PhyBlkNum, i);
        LML_VirtualPageWrite(&tmpPage);
        PHY_SynchBank(tmpPage.BankNum, SYNC_CHIP_MODE);

        //write the bad flag in the last page of the physical block
        LML_CalculatePhyOpPar(&tmpPage, nZoneNum, pBadBlk->PhyBlkNum, PAGE_CNT_OF_SUPER_BLK - INTERLEAVE_BANK_CNT + i);
        LML_VirtualPageWrite(&tmpPage);
        PHY_SynchBank(tmpPage.BankNum, SYNC_CHIP_MODE);
    }

    return 0;
}


/*
************************************************************************************************************************
*                       NAND FLASH LOGIC MANAGE LAYER BAD BLOCK MANAGE
*
*Description: Nand flash bad block manage.
*
*Arguments  : pBadBlk   the pointer to the bad physical block parameter;
*             nZoneNum  the number of the zone which the bad block belonged to;
*             nErrPage  the number of the error page;
*             pNewBlk   the pointer to the new valid block parameter.
*
*Return     : bad block manage result;
*               = 0     do bad block manage successful;
*               = -1    do bad block manage failed.
************************************************************************************************************************
*/
__s32 LML_BadBlkManage(struct __SuperPhyBlkType_t *pBadBlk, __u32 nZoneNum, __u32 nErrPage, struct __SuperPhyBlkType_t *pNewBlk)
{
    __s32   result;
    struct __SuperPhyBlkType_t tmpFreeBlk;
    struct __SuperPhyBlkType_t tmpBadBlk;

    tmpBadBlk = *pBadBlk;

	LOGICCTL_ERR("%s : %d : bad block manage go\n",__FUNCTION__,__LINE__);

__PROCESS_BAD_BLOCK:

    if(pNewBlk)
    {
        //get a new free block to replace the bad block
        BMM_GetFreeBlk(LOWEST_EC_TYPE, &tmpFreeBlk);
        if(tmpFreeBlk.PhyBlkNum == 0xffff)
        {
            LOGICCTL_ERR("[LOGICCTL_ERR] Look for free block failed when replace bad block\n");
            return -1;
        }

        //restore the valid page data from the bad block
        if(nErrPage)
        {
            result = _RestorePageData(&tmpBadBlk, nZoneNum, nErrPage, &tmpFreeBlk);
            if(result < 0)
            {
                //restore data failed, mark bad flag to the new free block
                _MarkBadBlk(&tmpFreeBlk, nZoneNum);

                goto __PROCESS_BAD_BLOCK;
            }
        }

        *pNewBlk = tmpFreeBlk;
    }

    //write bad flag to the bad block
    _MarkBadBlk(&tmpBadBlk, nZoneNum);

    return 0;
}

