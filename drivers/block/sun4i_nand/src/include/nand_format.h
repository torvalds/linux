/*
************************************************************************************************************************
*                                                      eNand
*                                     Nand flash driver format module define
*
*                             Copyright(C), 2008-2009, SoftWinners Microelectronic Co., Ltd.
*											       All Rights Reserved
*
* File Name : nand_format.h
*
* Author : Kevin.z
*
* Version : v0.1
*
* Date : 2008.03.25
*
* Description : This file define the function interface and some data structure export
*               for the format module.
*
* Others : None at present.
*
*
* History :
*
*  <Author>        <time>       <version>      <description>
*
* Kevin.z         2008.03.25      0.1          build the file
*
************************************************************************************************************************
*/
#ifndef __NAND_FORMAT_H__
#define __NAND_FORMAT_H__

#include "nand_type.h"
#include "nand_physic.h"


//define the structure for a zone detail information
struct __ScanZoneInfo_t
{
    __u16   nDataBlkCnt;                //the count of data blocks in a zone
    __u16   nFreeBlkCnt;                //the count of free blocks in a zone
    __u16   nFreeBlkIndex;              //the index of the free blocks in a zone
    __u16   Reserved;                   //reserved for 32bit aligned
    struct __SuperPhyBlkType_t ZoneTbl[BLOCK_CNT_OF_ZONE];  //the zone table buffer
	struct __LogBlkType_t LogBlkTbl[MAX_LOG_BLK_CNT];       //the log block mapping table buffer
};


//define the structure for a nand flash die detail information
struct __ScanDieInfo_t
{
    __u8    nDie;                       //the number of the die in the nand flash storage system
    __u8    TblBitmap;                  //the bitmap that mark the block mapping table status in a die
    __u16   nBadCnt;                    //the count of the bad block in a nand die
    __u16   nFreeCnt;                   //the count of the free block in a nand die
    __u16   nFreeIndex;                 //the free block allocate index
    __u16   *pPhyBlk;                   //the pointer to the physical block information buffer of a whole die
    struct __ScanZoneInfo_t *ZoneInfo;  //the pointer to the zone table detail information of the whole die
};

//define the first super block used for block mapping, the front used for boot
typedef struct _blk_for_boot1_t
{
	__u32 blk_size; //unit by k
	__u32 blks_boot0;
	__u32 blks_boot1;
}blk_for_boot1_t;

//define the max value of the default position of the block mapping table
#define TBL_AREA_BLK_NUM    32

//define the sector bitmap in a super page to get the user data
#if (0)
#define SPARE_DATA_BITMAP   (SUPPORT_MULTI_PROGRAM ? (0x3 | (0x3 << SECTOR_CNT_OF_SINGLE_PAGE)) : 0x1)
#elif (1)
#define SPARE_DATA_BITMAP   FULL_BITMAP_OF_SUPER_PAGE
#endif
//define the sector bitmap in a super page to get the logical information in the spare area
#if (0)
#define LOGIC_INFO_BITMAP   (SUPPORT_MULTI_PROGRAM ? (0x1 | (0x1 << SECTOR_CNT_OF_SINGLE_PAGE)) : 0x1)
#elif (1)
#define LOGIC_INFO_BITMAP   FULL_BITMAP_OF_SUPER_PAGE
#endif
//define the sector bitmap in a super page for table data operation
#define DATA_TABLE_BITMAP   0xf         //the bitmap for check data block table of block mapping table
#define LOG_TABLE_BITMAP    0xf         //the bitmap for check log block table of block mapping table
#define DIRTY_FLAG_BITMAP   0x1         //the bitmap for check dirty flag of block mapping table

//define the offset of tables in one table group
#define DATA_TBL_OFFSET     0           //page0 and page1 of a table group are used for data & free block table
#define LOG_TBL_OFFSET      2           //page2 of a table group is used for log block table
#define DIRTY_FLAG_OFFSET   3           //page3 of a table group is used for dirty flag
#define PAGE_CNT_OF_TBL_GROUP       4   //one table group contain 4 pages

//define the page buffer for cache the super page data for read or write
#define FORMAT_PAGE_BUF     (PageCachePool.PageCache0)

#define FORMAT_SPARE_BUF    (PageCachePool.SpareCache)

//define the empty item in the logical information buffer
#define NULL_BLOCK_INFO     0xfffd
//define the bad block information in the logcial information buffer
#define BAD_BLOCK_INFO      0xfffe
//define the free block infomation in the logical information buffer
#define FREE_BLOCK_INFO     0xffff

//define the mark for allocate the free block
#define ALLOC_BLK_MARK      ((0x1<<14) | 0xff)


//define the macro for compare the log age of the blocks
#define COMPARE_AGE(a, b)           ((signed char)((signed char)(a) - (signed char)(b)))

//define the macro for get the zone number in the logical information of physical block
#define GET_LOGIC_INFO_ZONE(a)      ((((unsigned int)(a))>>10) & 0x0f)
//define the macro for get the block used type in the logical information of physical block
#define GET_LOGIC_INFO_TYPE(a)      ((((unsigned int)(a))>>14) & 0x01)
//define the macro for get the logical block number in the logical information of physical block
#define GET_LOGIC_INFO_BLK(a)       (((unsigned int)(a)) & 0x03ff)

//define the return value of format error when we should try format again
#define RET_FORMAT_TRY_AGAIN        (-2)
	
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
__s32 FMT_Init(void);


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
__s32 FMT_Exit(void);


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
__s32 FMT_FormatNand(void);


void clear_NAND_ZI( void );

#endif  //ifndef __NAND_FORMAT_H__


