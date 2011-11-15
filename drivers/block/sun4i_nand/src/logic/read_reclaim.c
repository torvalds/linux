/*
************************************************************************************************************************
*                                                      eNand
*                                         Nand flash driver logic manage module
*
*                             Copyright(C), 2008-2009, SoftWinners Microelectronic Co., Ltd.
*											       All Rights Reserved
*
* File Name : read_reclaim.c
*
* Author : Kevin.z
*
* Version : v0.1
*
* Date : 2008.04.07
*
* Description : This file is the read-reclaim module.
*
* Others : None at present.
*
*
* History :
*
*  <Author>        <time>       <version>      <description>
*
* Kevin.z         2008.04.07      0.1          build the file
*
************************************************************************************************************************
*/
#include "../include/nand_logic.h"

extern struct __NandDriverGlobal_t     NandDriverInfo;

/*
************************************************************************************************************************
*                       NAND FLASH LOGIC MANAGE LAYER READ-RECLAIM
*
*Description: Repair the logic block whose data has reach the limit of the ability of
*             the HW ECC module correct.
*
*Arguments  : nPage     the page address where need be repaired.
*
*Return     : read-reclaim result;
*               = 0     do read-reclaim successful;
*               = -1    do read-reclaim failed.
*
*Notes      : if read a physical page millions of times, there may be some bit error in
*             the page, and the error bit number will increase along with the read times,
*             so, if the number of the error bit reachs the ECC limit, the data should be
*             read out and write to another physical blcok.
************************************************************************************************************************
*/
__s32 LML_ReadReclaim(__u32 nPage)
{
    __s32   result;

    #if CFG_SUPPORT_READ_RECLAIM

	LOGICCTL_ERR("[LOGICCTL_ERR] read reclaim go\n");
	
    //flush the page cache to nand flash first, because need use the buffer
    result = LML_FlushPageCache();
    if(result < 0)
    {
        LOGICCTL_ERR("[LOGICCTL_ERR] Flush page cache failed when do read reclaim! Error:0x%x\n", result);
        return -1;
    }

    //read the full page data to buffer
    result = LML_PageRead(nPage, FULL_BITMAP_OF_LOGIC_PAGE, LML_WRITE_PAGE_CACHE);
    if(result < 0)
    {
        return -1;
    }

    //the data in the page cache is full, write it to nand flash
    result = LML_PageWrite(nPage, FULL_BITMAP_OF_LOGIC_PAGE, LML_WRITE_PAGE_CACHE);
    if(result < 0)
    {
        return -1;
    }

    #endif
    return 0;
}


