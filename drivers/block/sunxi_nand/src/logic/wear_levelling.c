/*
 * drivers/block/sunxi_nand/src/logic/wear_levelling.c
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
*                   NAND FLASH LOGIC MANAGE LAYER WEAR-LEVELLING
*
*Description: Equate the erase cycles among all physical blocks.
*
*Arguments  : none
*
*Return     : do wear-levelling result;
*               = 0     do wear-levelling successful;
*               = -1    do wear-levelling failed.
*
*Notes      : The erase cycle of a physical block is limited, if the erase cycle overun this
*             limit, the physical block may be invalid. so a policy is needed to equate the
*             millions of erase cycles to ervery physical block.
************************************************************************************************************************
*/
__s32 LML_WearLevelling(void)
{
    #if CFG_SUPPORT_WEAR_LEVELLING

    __s32   i, result;

    __u32   tmpLogicBlk;
    __u16   tmpLowEc = 0xffff;
    struct __SuperPhyBlkType_t tmpFreeBlk, tmpDataBlk;
    struct __NandUserData_t tmpSpare[2];
    struct __PhysicOpPara_t tmpSrcPage, tmpDstPage;
    BLK_ERASE_CNTER = 0;

    //scan the data block table, to look for a physical block with lowest erase count
    for(i=DATA_BLK_CNT_OF_ZONE-1; i>=0; i--)
    {
        if(DATA_BLK_TBL[i].BlkEraseCnt < tmpLowEc)
        {
            tmpLowEc = DATA_BLK_TBL[i].BlkEraseCnt;
            tmpLogicBlk = i;
        }
    }

    //get a free block which has the highest erase count
    result = BMM_GetFreeBlk(HIGHEST_EC_TYPE, &tmpFreeBlk);
    if(result < 0)
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] Get free block failed when do wear-levelling!\n");
        return -1;
    }

    //clear the block erase counter
    BLK_ERASE_CNTER = 0;

    if(tmpLowEc >= tmpFreeBlk.BlkEraseCnt)
    {
        if(tmpLowEc == 0xffff)
        {
            //the lowest erase count reach the highest value, clear erase count of all physical block
            for(i=0; i<DATA_BLK_CNT_OF_ZONE; i++)
            {
                //clear the erase count for the data block
                DATA_BLK_TBL[i].BlkEraseCnt = 0x00;
            }

            for(i=0; i<FREE_BLK_CNT_OF_ZONE; i++)
            {
                //clear the erase count for the free block
                if(FREE_BLK_TBL[i].PhyBlkNum != 0xffff)
                {
                    FREE_BLK_TBL[i].BlkEraseCnt = 0x00;
                }
            }

            for(i=0; i<MAX_LOG_BLK_CNT; i++)
            {
                //clear the erase count for the log block
                if(LOG_BLK_TBL[i].LogicBlkNum != 0xffff)
                {
                    LOG_BLK_TBL[i].PhyBlk.BlkEraseCnt = 0x00;
                }
            }
        }

        BMM_SetFreeBlk(&tmpFreeBlk);

        return 0;
    }

    BMM_GetDataBlk(tmpLogicBlk, &tmpDataBlk);
    result = BMM_GetLogBlk(tmpLogicBlk, NULL);
    if(result < 0)
    {
        //check if the data block is empty
        LML_CalculatePhyOpPar(&tmpSrcPage, CUR_MAP_ZONE, tmpDataBlk.PhyBlkNum, 0);
        tmpSrcPage.SectBitmap = 0x03;
        tmpSrcPage.MDataPtr = LML_TEMP_BUF;
        tmpSrcPage.SDataPtr = (void *)tmpSpare;
        LML_VirtualPageRead(&tmpSrcPage);

        if(tmpSpare[0].LogicInfo != 0xffff)
        {
            //need copy data from the data block to the free block
            tmpSrcPage.SectBitmap = FULL_BITMAP_OF_SUPER_PAGE;
            tmpSrcPage.MDataPtr = NULL;
            tmpSrcPage.SDataPtr = NULL;

            tmpDstPage.SectBitmap = FULL_BITMAP_OF_SUPER_PAGE;
            tmpDstPage.MDataPtr = NULL;
            tmpDstPage.SDataPtr = NULL;

            for(i=0; i<PAGE_CNT_OF_SUPER_BLK; i++)
            {
                LML_CalculatePhyOpPar(&tmpSrcPage, CUR_MAP_ZONE, tmpDataBlk.PhyBlkNum, i);
                LML_CalculatePhyOpPar(&tmpDstPage, CUR_MAP_ZONE, tmpFreeBlk.PhyBlkNum, i);

                PHY_PageCopyback(&tmpSrcPage, &tmpDstPage);
                //check page copy result
                result = PHY_SynchBank(tmpDstPage.BankNum, SYNC_CHIP_MODE);
                if(result < 0)
                {
                    LOGICCTL_DBG("[LOGICCTL_DBG] Copy page failed when doing wear-levelling!\n");
                    result = LML_BadBlkManage(&tmpFreeBlk, CUR_MAP_ZONE, 0, NULL);
                    if(result < 0)
                    {
                        LOGICCTL_ERR("[LOGICCTL_ERR] Bad block manage failed when doing wear-levelling!\n");
                        return -1;
                    }

                    return 0;
                }
            }
        }

        //set the data block item by the free block
        BMM_SetDataBlk(tmpLogicBlk, &tmpFreeBlk);

        if(tmpSpare[0].LogicInfo != 0xffff)
        {
            //erase the data block to a new free block
            result = LML_VirtualBlkErase(CUR_MAP_ZONE, tmpDataBlk.PhyBlkNum);
            if(result < 0)
            {
                LOGICCTL_DBG("[LOGICCTL_DBG] Erase super block failed when doing wear-levelling!\n");
                result = LML_BadBlkManage(&tmpDataBlk, CUR_MAP_ZONE, 0, NULL);
                if(result < 0)
                {
                    LOGICCTL_ERR("[LOGICCTL_ERR] Bad block manage failed when doing wear-levelling!\n");
                   return -1;
                }

                return 0;
            }
        }

        //set the the data block to free block table
        tmpDataBlk.BlkEraseCnt++;
        BMM_SetFreeBlk(&tmpDataBlk);
    }
    else
    {
        //set the free block back to free table
        BMM_SetFreeBlk(&tmpFreeBlk);
    }

    #endif
    return 0;
}

