/*********************************************************************************
*                                       NAND FLASH DRIVER
*                                (c) Copyright 2008, SoftWinners Co,Ld.
*                                       All Right Reserved
*file : mapping.c
*description : this file create a interface to mange map table:
*history :
*    v0.1  2008-04-09 Richard
*            support all kinds of access way of block map and page map.
**********************************************************************************/

#include "../include/nand_logic.h"

extern struct __NandDriverGlobal_t     NandDriverInfo;

struct __BlkMapTblCachePool_t BlkMapTblCachePool;
struct __PageMapTblCachePool_t PageMapTblCachePool;

void dump(void *buf, __u32 len , __u8 nbyte,__u8 linelen)
{
	__u32 i;
	__u32 tmplen = len/nbyte;
		
	PRINT("/********************************************/\n");
	
	for (i = 0; i < tmplen; i++)
	{
		if (nbyte == 1)
			PRINT("%x  ",((__u8 *)buf)[i]);
		else if (nbyte == 2)
			PRINT("%x  ",((__u16 *)buf)[i]);
		else if (nbyte == 4)
			PRINT("%x  ",((__u32 *)buf)[i]);
		else
			break;
			
		if(i%linelen == (linelen - 1))
			PRINT("\n");
	}

	return;
	
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
static __u32 _GetTblCheckSum(__u32 *pTblBuf, __u32 nLength)
{
    __u32   i;
    __u32   tmpCheckSum = 0;

    for(i= 0; i<nLength; i++)
    {
        tmpCheckSum += pTblBuf[i];
    }

    return tmpCheckSum;
}


/*
************************************************************************************************************************
*                       INIT PAGE MAPPING TABLE CACHE
*
*Description: Init page mapping table cache.
*
*Arguments  : none.
*
*Return     : init result;
*               = 0         init page mapping table cache successful;
*               = -1        init page mapping table cache failed.
************************************************************************************************************************
*/
__s32 PMM_InitMapTblCache(void)
{
    __u32   i;

    PAGE_MAP_CACHE_POOL = &PageMapTblCachePool;

    for(i = 0; i<PAGE_MAP_TBL_CACHE_CNT; i++)
    {
        PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].AccessCnt = 0;
        PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].DirtyFlag = 0;
        PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].LogBlkPst = 0xff;
        PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].ZoneNum = 0xff;
        PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].PageMapTbl = \
                (void *)MALLOC(PAGE_CNT_OF_SUPER_BLK * sizeof(struct __PageMapTblItem_t));
        if (!PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].PageMapTbl)
        {
            return  -ERR_MAPPING;
        }
    }

    return NAND_OP_TRUE;
}


/*
************************************************************************************************************************
*                      CALCUALTE PAGE MAPPING TABLE ACCESS COUNT
*
*Description: Calculate page mapping table access count for table cache switch.
*
*Arguments  : none.
*
*Return     : none.
************************************************************************************************************************
*/
static void _CalPageTblAccessCount(void)
{
    __u32   i;

    for(i=0; i<PAGE_MAP_TBL_CACHE_CNT; i++)
    {
        PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].AccessCnt++;
    }

    PAGE_MAP_CACHE->AccessCnt = 0;
}


/*
************************************************************************************************************************
*                       EXIT PAGE MAPPING TABLE CACHE
*
*Description: Exit page mapping table cache.
*
*Arguments  : none.
*
*Return     : exit result;
*               = 0         exit page mapping table cache successful;
*               = -1        exit page mapping table cache failed.
************************************************************************************************************************
*/
__s32 PMM_ExitMapTblCache(void)
{
    __u32   i;

    for (i = 0; i<PAGE_MAP_TBL_CACHE_CNT; i++)
    {
        FREE(PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].PageMapTbl,PAGE_CNT_OF_SUPER_BLK * sizeof(struct __PageMapTblItem_t));
    }

    return NAND_OP_TRUE;
}


/*the page map table in the cahce pool? cahce hit?*/
static __s32 _page_map_tbl_cache_hit(__u32 nLogBlkPst)
{
    __u32 i;

    for(i=0; i<PAGE_MAP_TBL_CACHE_CNT; i++)
    {
        if((PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].ZoneNum == CUR_MAP_ZONE)\
            && (PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].LogBlkPst == nLogBlkPst))
        {

            PAGE_MAP_CACHE = &(PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i]);
            return NAND_OP_TRUE;
        }
    }

    return NAND_OP_FALSE;

}

/*find post cache, clear cache or LRU cache */
static __u32 _find_page_tbl_post_location(void)
{
    __u32   i, location = 0;
    __u16   access_cnt;

    /*try to find clear cache*/
    for(i=0; i<PAGE_MAP_TBL_CACHE_CNT; i++)
    {
        if(PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].ZoneNum == 0xff)
        {
            return i;
        }
    }

    /*try to find least used cache recently*/
    access_cnt = PAGE_MAP_CACHE_POOL->PageMapTblCachePool[0].AccessCnt;

    for (i = 1; i < PAGE_MAP_TBL_CACHE_CNT; i++){
        if (access_cnt < PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].AccessCnt){
            location = i;
            access_cnt = PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].AccessCnt;
        }
    }

    /*clear access counter*/
    for (i = 0; i < PAGE_MAP_TBL_CACHE_CNT; i++)
        PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].AccessCnt = 0;

    return location;

}

static __s32 _write_back_page_map_tbl(__u32 nLogBlkPst)
{
    __u16 TablePage;
    __u32 TableBlk;
    struct  __NandUserData_t  UserData[2];
    struct  __PhysicOpPara_t  param;
    struct  __SuperPhyBlkType_t BadBlk,NewBlk;


    /*check page poisition, merge if no free page*/
    TablePage = LOG_BLK_TBL[nLogBlkPst].LastUsedPage + 1;
    TableBlk = LOG_BLK_TBL[nLogBlkPst].PhyBlk.PhyBlkNum;
    if (TablePage == PAGE_CNT_OF_SUPER_BLK){
        /*block id full,need merge*/
        if (LML_MergeLogBlk(SPECIAL_MERGE_MODE,LOG_BLK_TBL[nLogBlkPst].LogicBlkNum)){
            MAPPING_ERR("write back page tbl : merge err\n");
            return NAND_OP_FALSE;
        }

        if (PAGE_MAP_CACHE->ZoneNum != 0xff){
            /*move merge*/
            TablePage = LOG_BLK_TBL[nLogBlkPst].LastUsedPage + 1;
            TableBlk = LOG_BLK_TBL[nLogBlkPst].PhyBlk.PhyBlkNum;
        }
        else
            return NAND_OP_TRUE;
    }

rewrite:
//PRINT("-------------------write back page tbl for blk %x\n",TableBlk);
    /*write page map table*/
    MEMSET((void *)&UserData,0xff,sizeof(struct __NandUserData_t) * 2);
    UserData[0].PageStatus = 0xaa;
    MEMSET(LML_PROCESS_TBL_BUF,0xff,SECTOR_CNT_OF_SUPER_PAGE * SECTOR_SIZE);

	if(PAGE_CNT_OF_SUPER_BLK >= 512)
	{
		__u32 page;

		for(page = 0; page < PAGE_CNT_OF_SUPER_BLK; page++)
			*((__u16 *)LML_PROCESS_TBL_BUF + page) = PAGE_MAP_TBL[page].PhyPageNum;
		
		((__u32 *)LML_PROCESS_TBL_BUF)[511] = \
        	_GetTblCheckSum((__u32 *)LML_PROCESS_TBL_BUF, PAGE_CNT_OF_SUPER_BLK*2/(sizeof (__u32)));
	}
	
	else
	{   
		MEMCPY(LML_PROCESS_TBL_BUF, PAGE_MAP_TBL,PAGE_CNT_OF_SUPER_BLK*sizeof(struct __PageMapTblItem_t));
    	((__u32 *)LML_PROCESS_TBL_BUF)[511] = \
        	_GetTblCheckSum((__u32 *)LML_PROCESS_TBL_BUF, PAGE_CNT_OF_SUPER_BLK*sizeof(struct __PageMapTblItem_t)/(sizeof (__u32)));
	}
	
    param.MDataPtr = LML_PROCESS_TBL_BUF;
    param.SDataPtr = (void *)&UserData;
    param.SectBitmap = FULL_BITMAP_OF_SUPER_PAGE;
		 
//rewrite:
    LML_CalculatePhyOpPar(&param, CUR_MAP_ZONE, TableBlk, TablePage);
    LML_VirtualPageWrite(&param);
    if (NAND_OP_TRUE != PHY_SynchBank(param.BankNum, SYNC_CHIP_MODE)){
        BadBlk.PhyBlkNum = TableBlk;
        if (NAND_OP_TRUE != LML_BadBlkManage(&BadBlk,CUR_MAP_ZONE,TablePage,&NewBlk)){
            MAPPING_ERR("write page map table : bad block mange err after write\n");
            return NAND_OP_FALSE;
        }
        TableBlk = NewBlk.PhyBlkNum;
        LOG_BLK_TBL[nLogBlkPst].PhyBlk = NewBlk;
        goto rewrite;
    }

    LOG_BLK_TBL[nLogBlkPst].LastUsedPage = TablePage;
    PAGE_MAP_CACHE->ZoneNum = 0xff;
    PAGE_MAP_CACHE->LogBlkPst = 0xff;

    return NAND_OP_TRUE;

}

static __s32 _rebuild_page_map_tbl(__u32 nLogBlkPst)
{
    __s32 ret;
    __u16 TablePage;
    __u32 TableBlk;
    __u16 logicpagenum;
    //__u8  status;
    struct  __NandUserData_t  UserData[2];
    struct  __PhysicOpPara_t  param;

    MEMSET(PAGE_MAP_TBL,0xff, PAGE_CNT_OF_SUPER_BLK*sizeof(struct __PageMapTblItem_t));
    TableBlk = LOG_BLK_TBL[nLogBlkPst].PhyBlk.PhyBlkNum;

    param.MDataPtr = LML_PROCESS_TBL_BUF;
    param.SDataPtr = (void *)&UserData;
    param.SectBitmap = 0x3;

	//PRINT("-----------------------rebuild page table for blk %x\n",TableBlk);
	
    for(TablePage = 0; TablePage < PAGE_CNT_OF_SUPER_BLK; TablePage++){
        LML_CalculatePhyOpPar(&param, CUR_MAP_ZONE, TableBlk, TablePage);
        ret = LML_VirtualPageRead(&param);
        if (ret < 0){
            MAPPING_ERR("rebuild logic block %x page map table : read err\n",LOG_BLK_TBL[nLogBlkPst].LogicBlkNum);
            return NAND_OP_FALSE;
        }

        //status = UserData[0].PageStatus;
        logicpagenum = UserData[0].LogicPageNum;

        //if(((!TablePage || (status == 0x55))) && (logicpagenum != 0xffff) && (logicpagenum < PAGE_CNT_OF_SUPER_BLK)) /*legal page*/
		if((logicpagenum != 0xffff) && (logicpagenum < PAGE_CNT_OF_SUPER_BLK)) /*legal page*/
		{
            PAGE_MAP_TBL[logicpagenum].PhyPageNum = TablePage; /*l2p:logical to physical*/
        }
    }

    PAGE_MAP_CACHE->DirtyFlag = 1;
	BMM_SetDirtyFlag();

	return NAND_OP_TRUE;
}

static __s32 _read_page_map_tbl(__u32 nLogBlkPst)
{
    __s32 ret;
    __u16 TablePage;
    __u32 TableBlk, checksum;
    __u16 logicpagenum;
    __u8  status;
    struct  __NandUserData_t  UserData[2];
    struct  __PhysicOpPara_t  param;


    /*check page poisition, merge if no free page*/
    TablePage = LOG_BLK_TBL[nLogBlkPst].LastUsedPage;
    TableBlk = LOG_BLK_TBL[nLogBlkPst].PhyBlk.PhyBlkNum;

    if (TablePage == 0xffff){
        /*log block is empty*/
        MEMSET(PAGE_MAP_TBL, 0xff,PAGE_CNT_OF_SUPER_BLK*sizeof(struct __PageMapTblItem_t) );
        return NAND_OP_TRUE;
    }

    /*read page map table*/
    param.MDataPtr = LML_PROCESS_TBL_BUF;
    param.SDataPtr = (void *)&UserData;
    param.SectBitmap = 0xf;

    LML_CalculatePhyOpPar(&param, CUR_MAP_ZONE, TableBlk, TablePage);
    ret = LML_VirtualPageRead(&param);

	if(PAGE_CNT_OF_SUPER_BLK >= 512)
	{	
		checksum = _GetTblCheckSum((__u32 *)LML_PROCESS_TBL_BUF,  \
                	PAGE_CNT_OF_SUPER_BLK*2/sizeof(__u32));
	}
	else
	{
		checksum = _GetTblCheckSum((__u32 *)LML_PROCESS_TBL_BUF,  \
                	PAGE_CNT_OF_SUPER_BLK*sizeof(struct __PageMapTblItem_t)/sizeof(__u32));
	}
	
    status = UserData[0].PageStatus;
    logicpagenum = UserData[0].LogicPageNum;

    if((ret < 0) || (status != 0xaa) || (logicpagenum != 0xffff) || (checksum != ((__u32 *)LML_PROCESS_TBL_BUF)[511]))
    {
        if(NAND_OP_TRUE != _rebuild_page_map_tbl(nLogBlkPst))
        {
            MAPPING_ERR("rebuild page map table err\n");
            return NAND_OP_FALSE;
        }
    }
    else
    {
    	if(PAGE_CNT_OF_SUPER_BLK >= 512)
    	{
			__u32 page;

			for(page = 0; page < PAGE_CNT_OF_SUPER_BLK; page++)
				PAGE_MAP_TBL[page].PhyPageNum = *((__u16 *)LML_PROCESS_TBL_BUF + page);
		}
		else	
        	MEMCPY(PAGE_MAP_TBL,LML_PROCESS_TBL_BUF, PAGE_CNT_OF_SUPER_BLK*sizeof(struct __PageMapTblItem_t));
    }

    return NAND_OP_TRUE;
}


/*post current zone map table in cache*/
static __s32 _page_map_tbl_cache_post(__u32 nLogBlkPst)
{
    __u8 poisition;
    __u8 i;

    struct __BlkMapTblCache_t *TmpBmt = BLK_MAP_CACHE;

    /*find the cache to be post*/
    poisition = _find_page_tbl_post_location();
    PAGE_MAP_CACHE = &(PAGE_MAP_CACHE_POOL->PageMapTblCachePool[poisition]);

    if (PAGE_MAP_CACHE->DirtyFlag && (PAGE_MAP_CACHE->ZoneNum != 0xff)){
    /*write back page  map table*/
        if (PAGE_MAP_CACHE->ZoneNum != TmpBmt->ZoneNum){
            for (i = 0; i < BLOCK_MAP_TBL_CACHE_CNT; i++)
            {
                if (BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].ZoneNum == PAGE_MAP_CACHE->ZoneNum){
                    BLK_MAP_CACHE = &(BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i]);
                    break;
                }
            }

            if (i == BLOCK_MAP_TBL_CACHE_CNT){
                MAPPING_ERR("_page_map_tbl_cache_post : position %d ,page map zone %d,blk map zone %d\n",
							poisition,PAGE_MAP_CACHE->ZoneNum,BLK_MAP_CACHE->ZoneNum);
                return NAND_OP_FALSE;
            }

        }
        /* write back new table in flash if dirty*/
		BMM_SetDirtyFlag();
        if (NAND_OP_TRUE != _write_back_page_map_tbl(PAGE_MAP_CACHE->LogBlkPst)){
            MAPPING_ERR("write back page tbl err\n");
            return NAND_OP_FALSE;
        }

        BLK_MAP_CACHE = TmpBmt;

    }

    PAGE_MAP_CACHE->DirtyFlag = 0;

    /*fetch current page map table*/
    if (NAND_OP_TRUE != _read_page_map_tbl(nLogBlkPst)){
        MAPPING_ERR("read page map tbl err\n");
        return NAND_OP_FALSE;
    }

    PAGE_MAP_CACHE->ZoneNum = CUR_MAP_ZONE;
    PAGE_MAP_CACHE->LogBlkPst = nLogBlkPst;

    return NAND_OP_TRUE;
}

/*
************************************************************************************************************************
*                      SWITCH PAGE MAPPING TABLE
*
*Description: Switch page mapping table cache.
*
*Arguments  : nLogBlkPst    the position of the log block in the log block table.
*
*Return     : switch result;
*               = 0     switch table successful;
*               = -1    switch table failed.
************************************************************************************************************************
*/
__s32 PMM_SwitchMapTbl(__u32 nLogBlkPst)
{
    __s32   result = NAND_OP_TRUE;
    if (NAND_OP_TRUE !=_page_map_tbl_cache_hit(nLogBlkPst))
    {
        result = (_page_map_tbl_cache_post(nLogBlkPst));
    }

    _CalPageTblAccessCount();

    return result;
}


/*
************************************************************************************************************************
*                       INIT BLOCK MAPPING TABLE CACHE
*
*Description: Initiate block mapping talbe cache.
*
*Arguments  : none.
*
*Return     : init result;
*               = 0     init successful;
*               = -1    init failed.
************************************************************************************************************************
*/
__s32 BMM_InitMapTblCache(void)
{
    __u32 i;

    BLK_MAP_CACHE_POOL = &BlkMapTblCachePool;

    BLK_MAP_CACHE_POOL->LogBlkAccessTimer = 0x0;
    BLK_MAP_CACHE_POOL->SuperBlkEraseCnt = 0x0;

    /*init block map table cache*/
    for(i=0; i<BLOCK_MAP_TBL_CACHE_CNT; i++)
    {
        //init the parmater for block mapping table cache management
        BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].ZoneNum = 0xff;
        BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].DirtyFlag = 0x0;
        BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].AccessCnt = 0x0;
        BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].LastFreeBlkPst = 0xff;

        //request buffer for data block table and free block table
        BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].DataBlkTbl = \
                    (struct __SuperPhyBlkType_t *)MALLOC(sizeof(struct __SuperPhyBlkType_t)*BLOCK_CNT_OF_ZONE);
        if(NULL == BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].DataBlkTbl)
        {
            MAPPING_ERR("BMM_InitMapTblCache : allocate memory err\n");
            return -ERR_MALLOC;
        }
        //set free block table pointer
        BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].FreeBlkTbl = \
                    BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].DataBlkTbl + DATA_BLK_CNT_OF_ZONE;

        //request buffer for log block table
        BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].LogBlkTbl =  \
                    (struct __LogBlkType_t *)MALLOC(sizeof(struct __LogBlkType_t)*LOG_BLK_CNT_OF_ZONE);
        if(NULL == BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].LogBlkTbl)
        {
            MAPPING_ERR("BMM_InitMapTblCache : allocate memory err\n");
            FREE(BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].DataBlkTbl,sizeof(struct __SuperPhyBlkType_t)*BLOCK_CNT_OF_ZONE);
            return -ERR_MALLOC;
        }
    }

    /*init log block access time*/
    MEMSET(BLK_MAP_CACHE_POOL->LogBlkAccessAge, 0x0, MAX_LOG_BLK_CNT);

    return NAND_OP_TRUE;
}


/*
************************************************************************************************************************
*                       CALCULATE BLOCK MAPPING TABLE ACCESS COUNT
*
*Description: Calculate block mapping table access count for cache switch.
*
*Arguments  : none.
*
*Return     : none;
************************************************************************************************************************
*/
static void _CalBlkTblAccessCount(void)
{
    __u32   i;

    for (i=0; i<BLOCK_MAP_TBL_CACHE_CNT; i++)
    {
        BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].AccessCnt++;
    }

    BLK_MAP_CACHE->AccessCnt = 0;
}


/*
************************************************************************************************************************
*                       BLOCK MAPPING TABLE CACHE EXIT
*
*Description: exit block mapping table cache.
*
*Arguments  : none.
*
*Return     : exit result;
*               = 0     exit successful;
*               = -1    exit failed.
************************************************************************************************************************
*/
__s32 BMM_ExitMapTblCache(void)
{
    __u32 i;

    for (i=0; i<BLOCK_MAP_TBL_CACHE_CNT; i++)
    {

        FREE(BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].DataBlkTbl,sizeof(struct __SuperPhyBlkType_t)*BLOCK_CNT_OF_ZONE);
        FREE(BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].LogBlkTbl,sizeof(struct __LogBlkType_t)*LOG_BLK_CNT_OF_ZONE);
    }

    return NAND_OP_TRUE;
}

/*the zone table in the cahce pool? cahce hit?*/
static __s32 _blk_map_tbl_cache_hit(__u32 nZone)
{
    __u32 i;

    for (i = 0; i < BLOCK_MAP_TBL_CACHE_CNT; i++){
        if (BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].ZoneNum == nZone){
            BLK_MAP_CACHE = &(BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i]);
            return NAND_OP_TRUE;
        }
    }

    return NAND_OP_FALSE;

}

/*find post cache, clear cache or LRU cache */
static __u32 _find_blk_tbl_post_location(void)
{
    __u32 i;
    __u8 location;
    __u16 access_cnt ;
	
    /*try to find clear cache*/
    for (i = 0; i < BLOCK_MAP_TBL_CACHE_CNT; i++)
    {
        if (BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].ZoneNum == 0xff)
            return i;
    }
    /*try to find least used cache recently*/
    location = 0;
    access_cnt = BLK_MAP_CACHE_POOL->BlkMapTblCachePool[0].AccessCnt;

    for (i = 1; i < BLOCK_MAP_TBL_CACHE_CNT; i++){
        if (access_cnt < BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].AccessCnt){
            location = i;
            access_cnt = BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].AccessCnt;
        }
    }

    /*clear access counter*/
    for (i = 0; i < BLOCK_MAP_TBL_CACHE_CNT; i++)
        BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].AccessCnt = 0;

    return location;

}

static __s32 _write_back_all_page_map_tbl(__u8 nZone)
{
    __u32 i;

    for(i=0; i<PAGE_MAP_TBL_CACHE_CNT; i++)
    {
        if((PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].ZoneNum == nZone)\
            && (PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i].DirtyFlag == 1))
        {
            PAGE_MAP_CACHE = &(PAGE_MAP_CACHE_POOL->PageMapTblCachePool[i]);
            if (NAND_OP_TRUE != _write_back_page_map_tbl(PAGE_MAP_CACHE->LogBlkPst))
            {
                MAPPING_ERR("write back all page tbl : write page map table err \n");
                return NAND_OP_FALSE;
            }
            PAGE_MAP_CACHE->DirtyFlag = 0;
        }
    }

    return NAND_OP_TRUE;
}



/*write block map table to flash*/
static __s32 _write_back_block_map_tbl(__u8 nZone)
{
    __s32 TablePage;
    __u32 TableBlk;
    struct  __NandUserData_t  UserData[2];
    struct  __PhysicOpPara_t  param;
    struct __SuperPhyBlkType_t BadBlk,NewBlk;
	
    /*write back all page map table within this zone*/
    if (NAND_OP_TRUE != _write_back_all_page_map_tbl(nZone)){
        MAPPING_ERR("write back all page map tbl err\n");
        return NAND_OP_FALSE;
    }

    /*set table block number and table page number*/
    TableBlk = NandDriverInfo.ZoneTblPstInfo[nZone].PhyBlkNum;
    TablePage = NandDriverInfo.ZoneTblPstInfo[nZone].TablePst;
    if(TablePage >= PAGE_CNT_OF_SUPER_BLK - 4)
    {
        if(NAND_OP_TRUE != LML_VirtualBlkErase(nZone, TableBlk))
        {
            BadBlk.PhyBlkNum = TableBlk;

            if(NAND_OP_TRUE != LML_BadBlkManage(&BadBlk,CUR_MAP_ZONE,0,&NewBlk))
            {
                MAPPING_ERR("write back block tbl : bad block manage err erase data block\n");
                return NAND_OP_FALSE;
            }

            TableBlk = NewBlk.PhyBlkNum;
        }
        TablePage = -4;
    }

    TablePage += 4;

    //calculate checksum for data block table and free block table
    ((__u32 *)DATA_BLK_TBL)[1023] = \
        _GetTblCheckSum((__u32 *)DATA_BLK_TBL, (DATA_BLK_CNT_OF_ZONE + FREE_BLK_CNT_OF_ZONE));
    //clear full page data
    MEMSET(LML_PROCESS_TBL_BUF, 0xff, SECTOR_CNT_OF_SUPER_PAGE * SECTOR_SIZE);

rewrite:
    /*write back data block and free block map table*/
    MEMSET((void *)&UserData,0xff,sizeof(struct __NandUserData_t) * 2);
    MEMCPY(LML_PROCESS_TBL_BUF,DATA_BLK_TBL,2048);
    /*write page 0, need set spare info*/
    if (TablePage == 0)
    {
        UserData[0].LogicInfo = (1<<14) | ((nZone % ZONE_CNT_OF_DIE) << 10) | 0xaa ;
    }
    UserData[0].PageStatus = 0x55;
    param.MDataPtr = LML_PROCESS_TBL_BUF;
    param.SDataPtr = (void *)&UserData;
    param.SectBitmap = FULL_BITMAP_OF_SUPER_PAGE;
    LML_CalculatePhyOpPar(&param, nZone, TableBlk, TablePage);    	
    LML_VirtualPageWrite(&param);
    if (NAND_OP_TRUE !=  PHY_SynchBank(param.BankNum, SYNC_CHIP_MODE)){
        BadBlk.PhyBlkNum = TableBlk;
        if (NAND_OP_TRUE != LML_BadBlkManage(&BadBlk,nZone,0,&NewBlk)){
            MAPPING_ERR("write blk map table : bad block mange err after write\n");
            return NAND_OP_FALSE;
        }
        TableBlk = NewBlk.PhyBlkNum;
        TablePage = 0;
        goto rewrite;
    }     
	
    MEMCPY(LML_PROCESS_TBL_BUF, &DATA_BLK_TBL[512], 2048);
    TablePage ++;
    param.MDataPtr = LML_PROCESS_TBL_BUF;
    MEMSET((void *)&UserData,0xff,sizeof(struct __NandUserData_t) * 2);
    UserData[0].PageStatus = 0x55;
    LML_CalculatePhyOpPar(&param, nZone, TableBlk, TablePage);
    LML_VirtualPageWrite(&param);
    if(NAND_OP_TRUE != PHY_SynchBank(param.BankNum, SYNC_CHIP_MODE))
    {
        BadBlk.PhyBlkNum = TableBlk;
        if(NAND_OP_TRUE != LML_BadBlkManage(&BadBlk,nZone,0,&NewBlk))
        {
            MAPPING_ERR("write blk map table : bad block mange err after write\n");
            return NAND_OP_FALSE;
        }
        TableBlk = NewBlk.PhyBlkNum;
        TablePage = 0;
        goto rewrite;
    }
	
	
    /*write back log block map table*/
    TablePage++;
    MEMSET(LML_PROCESS_TBL_BUF, 0xff, SECTOR_CNT_OF_SUPER_PAGE * SECTOR_SIZE);
    MEMCPY(LML_PROCESS_TBL_BUF,LOG_BLK_TBL,LOG_BLK_CNT_OF_ZONE*sizeof(struct __LogBlkType_t));
    /*cal checksum*/
    ((__u32 *)LML_PROCESS_TBL_BUF)[511] = \
        _GetTblCheckSum((__u32 *)LML_PROCESS_TBL_BUF, LOG_BLK_CNT_OF_ZONE*sizeof(struct __LogBlkType_t)/sizeof(__u32));
    LML_CalculatePhyOpPar(&param, nZone, TableBlk, TablePage);
    LML_VirtualPageWrite(&param);
    if(NAND_OP_TRUE !=  PHY_SynchBank(param.BankNum, SYNC_CHIP_MODE))
    {
        BadBlk.PhyBlkNum = TableBlk;
        if(NAND_OP_TRUE != LML_BadBlkManage(&BadBlk,nZone,0,&NewBlk))
        {
            MAPPING_ERR("write blk map table : bad block mange err after write\n");
            return NAND_OP_FALSE;
        }
        TableBlk = NewBlk.PhyBlkNum;
        TablePage = 0;
        goto rewrite;
    }

    /*reset zone info*/
    NandDriverInfo.ZoneTblPstInfo[nZone].PhyBlkNum = TableBlk;
    NandDriverInfo.ZoneTblPstInfo[nZone].TablePst = TablePage - 2;

    return NAND_OP_TRUE;
}

/* fetch block map table from flash */
static __s32 _read_block_map_tbl(__u8 nZone)
{
    __s32 TablePage;
    __u32 TableBlk;
    struct  __PhysicOpPara_t  param;

    /*set table block number and table page number*/
    TableBlk = NandDriverInfo.ZoneTblPstInfo[nZone].PhyBlkNum;
    TablePage = NandDriverInfo.ZoneTblPstInfo[nZone].TablePst;

    /*read data block and free block map tbl*/

	param.MDataPtr = LML_PROCESS_TBL_BUF;
    param.SDataPtr = NULL;
    param.SectBitmap = 0xf;
    LML_CalculatePhyOpPar(&param, nZone, TableBlk, TablePage);
    if(LML_VirtualPageRead(&param) < 0)
    {
        MAPPING_ERR("_read_block_map_tbl :read block map table0 err\n");
        return NAND_OP_FALSE;
    }	
	
    MEMCPY(DATA_BLK_TBL,LML_PROCESS_TBL_BUF,2048);

    TablePage++;
    param.MDataPtr = LML_PROCESS_TBL_BUF;
    LML_CalculatePhyOpPar(&param, nZone, TableBlk, TablePage);
    if( LML_VirtualPageRead(&param) < 0)
    {
        MAPPING_ERR("_read_block_map_tbl : read block map table1 err\n");
        return NAND_OP_FALSE;
    }
	
    MEMCPY(&DATA_BLK_TBL[512],LML_PROCESS_TBL_BUF,2048);
    if(((__u32 *)DATA_BLK_TBL)[1023] != \
        _GetTblCheckSum((__u32 *)DATA_BLK_TBL,(DATA_BLK_CNT_OF_ZONE+FREE_BLK_CNT_OF_ZONE)))
    {
    	MAPPING_ERR("_read_block_map_tbl : read data block map table checksum err\n");
		dump((void*)DATA_BLK_TBL,1024*4,4,8);
		return NAND_OP_FALSE;
    }

    /*read log block table*/
    TablePage++;
    param.MDataPtr = LML_PROCESS_TBL_BUF;
    LML_CalculatePhyOpPar(&param, nZone, TableBlk, TablePage);
    if ( LML_VirtualPageRead(&param) < 0){
        MAPPING_ERR("_read_block_map_tbl : read block map table2 err\n");
        return NAND_OP_FALSE;
    }
    if (((__u32 *)LML_PROCESS_TBL_BUF)[511] != \
        _GetTblCheckSum((__u32 *)LML_PROCESS_TBL_BUF, LOG_BLK_CNT_OF_ZONE*sizeof(struct __LogBlkType_t)/sizeof(__u32)))
    {
    	MAPPING_ERR("_read_block_map_tbl : read log block table checksum err\n");
		dump((void*)LML_PROCESS_TBL_BUF,512*8,2,8);
        return NAND_OP_FALSE;
    }
    MEMCPY(LOG_BLK_TBL,LML_PROCESS_TBL_BUF,LOG_BLK_CNT_OF_ZONE*sizeof(struct __LogBlkType_t));

    return NAND_OP_TRUE;
}

/*post current zone map table in cache*/
static __s32 _blk_map_tbl_cache_post(__u32 nZone)
{
    __u8 poisition;

    /*find the cache to be post*/
    poisition = _find_blk_tbl_post_location();
    BLK_MAP_CACHE = &(BLK_MAP_CACHE_POOL->BlkMapTblCachePool[poisition]);

    /* write back new table in flash if dirty*/
    if (BLK_MAP_CACHE->DirtyFlag){
        if (NAND_OP_TRUE != _write_back_block_map_tbl(CUR_MAP_ZONE)){
            MAPPING_ERR("_blk_map_tbl_cache_post : write back zone tbl err\n");
            return NAND_OP_FALSE;
        }
    }

    /*fetch current zone map table*/
    if (NAND_OP_TRUE != _read_block_map_tbl(nZone)){
        MAPPING_ERR("_blk_map_tbl_cache_post : read zone tbl err\n");
            return NAND_OP_FALSE;
    }
    CUR_MAP_ZONE = nZone;
    BLK_MAP_CACHE->DirtyFlag = 0;

    return NAND_OP_TRUE;
}

/*
************************************************************************************************************************
*                       SWITCH BLOCK MAPPING TABLE
*
*Description: Switch block mapping table.
*
*Arguments  : nZone     zone number which block mapping table need be accessed.
*
*Return     : switch result;
*               = 0     switch successful;
*               = -1    switch failed.
************************************************************************************************************************
*/
__s32 BMM_SwitchMapTbl(__u32 nZone)
{
    __s32   result = NAND_OP_TRUE;

    if(NAND_OP_TRUE != _blk_map_tbl_cache_hit(nZone))
    {	
        MAPPING_DBG("BMM_SwitchMapTbl : post zone %d cache\n",nZone);
		result = (_blk_map_tbl_cache_post(nZone));
    }

    _CalBlkTblAccessCount();

    return result;
}


/*
************************************************************************************************************************
*                           WRITE BACK ALL MAPPING TABLE
*
*Description: Write back all mapping table.
*
*Arguments  : none.
*
*Return     : write table result;
*               = 0     write successful;
*               = -1    write failed.
************************************************************************************************************************
*/
__s32 BMM_WriteBackAllMapTbl(void)
{
     __u8 i;

        /*save current scene*/
        struct __BlkMapTblCache_t *TmpBmt = BLK_MAP_CACHE;
        struct __PageMapTblCache_t *TmpPmt = PAGE_MAP_CACHE;

        for (i = 0; i < BLOCK_MAP_TBL_CACHE_CNT; i++)
        {
            if (BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i].DirtyFlag){
                   BLK_MAP_CACHE = &(BLK_MAP_CACHE_POOL->BlkMapTblCachePool[i]);
                   if (NAND_OP_TRUE != _write_back_block_map_tbl(CUR_MAP_ZONE))
                        return NAND_OP_FALSE;
                    BLK_MAP_CACHE->DirtyFlag = 0;
            }
       }

        /*resore current scene*/
        BLK_MAP_CACHE  = TmpBmt;
        PAGE_MAP_CACHE = TmpPmt;

        return NAND_OP_TRUE;
}

static __s32 _write_dirty_flag(__u8 nZone)
{
    __s32 TablePage;
    __u32 TableBlk;
    struct  __PhysicOpPara_t  param;
    struct  __NandUserData_t  UserData[2];

    /*set table block number and table page number*/
    TableBlk = NandDriverInfo.ZoneTblPstInfo[nZone].PhyBlkNum;
    TablePage = NandDriverInfo.ZoneTblPstInfo[nZone].TablePst;

    TablePage += 3;
    MEMSET((void *)&UserData,0xff,sizeof(struct __NandUserData_t) * 2);
    UserData[0].PageStatus = 0x55;
    MEMSET(LML_PROCESS_TBL_BUF,0x55,512);
    param.MDataPtr = LML_PROCESS_TBL_BUF;
    param.SDataPtr = (void *)&UserData;

    LML_CalculatePhyOpPar(&param, nZone, TableBlk, TablePage);
    LML_VirtualPageWrite(&param);
    PHY_SynchBank(param.BankNum, SYNC_CHIP_MODE);

    return NAND_OP_TRUE;

}


/*
************************************************************************************************************************
*                       SET DIRTY FLAG FOR BLOCK MAPPING TABLE
*
*Description: Set dirty flag for block mapping table.
*
*Arguments  : none.
*
*Return     : set dirty flag result;
*               = 0     set dirty flag successful;
*               = -1    set dirty flag failed.
************************************************************************************************************************
*/
__s32 BMM_SetDirtyFlag(void)
{
    if (0 == BLK_MAP_CACHE->DirtyFlag){
       _write_dirty_flag(CUR_MAP_ZONE);
       BLK_MAP_CACHE->DirtyFlag = 1;
    }

    return NAND_OP_TRUE;
}




