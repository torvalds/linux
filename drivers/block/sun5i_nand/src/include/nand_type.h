/*
************************************************************************************************************************
*                                                      eNand
*                                   Nand flash driver data struct type define
*
*                             Copyright(C), 2008-2009, SoftWinners Microelectronic Co., Ltd.
*											       All Rights Reserved
*
* File Name : nand_type.h
*
* Author : Kevin.z
*
* Version : v0.1
*
* Date : 2008.03.19
*
* Description : This file defines the data struct type and return value type for nand flash driver.
*
* Others : None at present.
*
*
* History :
*
*  <Author>        <time>       <version>      <description>
*
* Kevin.z         2008.03.19      0.1          build the file
*
************************************************************************************************************************
*/
#ifndef __NAND_TYPE_H
#define __NAND_TYPE_H

#include "nand_drv_cfg.h"

//==============================================================================
//  define the data structure for physic layer module
//==============================================================================

//define the optional physical operation parameter
struct __OptionalPhyOpPar_t
{
    __u8        MultiPlaneReadCmd[2];               //the command for multi-plane read, the sequence is [0] -ADDR- [0] -ADDR- [1] - DATA
    __u8        MultiPlaneWriteCmd[2];              //the command for multi-plane program, the sequence is 80 -ADDR- DATA - [0] - [1] -ADDR- DATA - 10/15
    __u8        MultiPlaneCopyReadCmd[3];           //the command for multi-plane page copy-back read, the sequence is [0] -ADDR- [1] -ADDR- [2]
    __u8        MultiPlaneCopyWriteCmd[3];          //the command for multi-plane page copy-back program, the sequence is [0] -ADDR- [1] - [2] -ADDR- 10
    __u8        MultiPlaneStatusCmd;                //the command for multi-plane operation status read, the command may be 0x70/0x71/0x78/...
    __u8        InterBnk0StatusCmd;                 //the command for inter-leave bank0 operation status read, the command may be 0xf1/0x78/...
    __u8        InterBnk1StatusCmd;                 //the command for inter-leave bank1 operation status read, the command may be 0xf2/0x78/...
    __u8        BadBlockFlagPosition;               //the flag that marks the position of the bad block flag,0x00-1stpage/ 0x01-1st&2nd page/ 0x02-last page/ 0x03-last 2 page
    __u16       MultiPlaneBlockOffset;              //the value of the block number offset between the left-plane block and the right pane block
};

typedef struct 
{
    __u8        ChipCnt;                            //the count of the total nand flash chips are currently connecting on the CE pin
    __u16       ChipConnectInfo;                    //chip connect information, bit == 1 means there is a chip connecting on the CE pin
	__u8		RbCnt;
	__u8		RbConnectInfo;						//the connect  information of the all rb  chips are connected
    __u8        RbConnectMode;						//the rb connect  mode
	__u8        BankCntPerChip;                     //the count of the banks in one nand chip, multiple banks can support Inter-Leave
    __u8        DieCntPerChip;                      //the count of the dies in one nand chip, block management is based on Die
    __u8        PlaneCntPerDie;                     //the count of planes in one die, multiple planes can support multi-plane operation
    __u8        SectorCntPerPage;                   //the count of sectors in one single physic page, one sector is 0.5k
    __u16       PageCntPerPhyBlk;                   //the count of physic pages in one physic block
    __u16       BlkCntPerDie;                       //the count of the physic blocks in one die, include valid block and invalid block
    __u16       OperationOpt;                       //the mask of the operation types which current nand flash can support support
    __u8        FrequencePar;                       //the parameter of the hardware access clock, based on 'MHz'
    __u8        EccMode;                            //the Ecc Mode for the nand flash chip, 0: bch-16, 1:bch-28, 2:bch_32   
    __u8        NandChipId[8];                      //the nand chip id of current connecting nand chip
    __u16       ValidBlkRatio;                      //the ratio of the valid physical blocks, based on 1024
	__u32 		good_block_ratio;					//good block ratio get from hwscan
	__u32		ReadRetryType;						//the read retry type
	__u32       DDRType;
	__u32		Reserved[32];
}boot_nand_para_t;

typedef struct boot_flash_info{
	__u32 chip_cnt;
	__u32 blk_cnt_per_chip;
	__u32 blocksize;
	__u32 pagesize;
	__u32 pagewithbadflag; /*bad block flag was written at the first byte of spare area of this page*/
}boot_flash_info_t;

//define the nand flash storage system information
struct __NandStorageInfo_t
{
    __u8        ChipCnt;                            //the count of the total nand flash chips are currently connecting on the CE pin
    __u16       ChipConnectInfo;                    //chip connect information, bit == 1 means there is a chip connecting on the CE pin
	__u8		RbCnt;
	__u8		RbConnectInfo;						//the connect  information of the all rb  chips are connected
    __u8        RbConnectMode;						//the rb connect  mode
	__u8        BankCntPerChip;                     //the count of the banks in one nand chip, multiple banks can support Inter-Leave
    __u8        DieCntPerChip;                      //the count of the dies in one nand chip, block management is based on Die
    __u8        PlaneCntPerDie;                     //the count of planes in one die, multiple planes can support multi-plane operation
    __u8        SectorCntPerPage;                   //the count of sectors in one single physic page, one sector is 0.5k
    __u16       PageCntPerPhyBlk;                   //the count of physic pages in one physic block
    __u16       BlkCntPerDie;                       //the count of the physic blocks in one die, include valid block and invalid block
    __u16       OperationOpt;                       //the mask of the operation types which current nand flash can support support
    __u8        FrequencePar;                       //the parameter of the hardware access clock, based on 'MHz'
    __u8        EccMode;                            //the Ecc Mode for the nand flash chip, 0: bch-16, 1:bch-28, 2:bch_32   
    __u8        NandChipId[8];                      //the nand chip id of current connecting nand chip
    __u16       ValidBlkRatio;                         //the ratio of the valid physical blocks, based on 1024
    __u32		ReadRetryType;						//the read retry type
    __u32       DDRType;
    struct __OptionalPhyOpPar_t OptPhyOpPar;        //the parameters for some optional operation
};


//define the page buffer pool for nand flash driver
struct __NandPageCachePool_t
{
    __u8        *PageCache0;                        //the pointer to the first page size ram buffer
    __u8        *PageCache1;                        //the pointer to the second page size ram buffer
    __u8        *PageCache2;                        //the pointer to the third page size ram buffer
    __u8		*SpareCache;

	__u8		*TmpPageCache;
};


//define the User Data structure for nand flash driver
struct __NandUserData_t
{
    __u8        BadBlkFlag;                         //the flag that marks if a physic block is a valid block or a invalid block
    __u16       LogicInfo;                          //the logical information of the physical block
    __u8        Reserved0;                          //reserved for 32bit align
    __u16       LogicPageNum;                       //the value of the logic page number, which the physic page is mapping to
    __u8        PageStatus;                         //the logical information of the physical page
    __u8        Reserved1;                          //reserved for 32bit align
} __attribute__ ((packed));


//define the paramter structure for physic operation function
struct __PhysicOpPara_t
{
    __u8        BankNum;                            //the number of the bank current accessed, bank NO. is different of chip NO.
    __u8        PageNum;                            //the number of the page current accessed, the page is based on single-plane or multi-plane
    __u16       BlkNum;                             //the number of the physic block, the block is based on single-plane or multi-plane
    __u32       SectBitmap;                         //the bitmap of the sector in the page which need access data
    void        *MDataPtr;                          //the pointer to main data buffer, it is the start address of a page size based buffer
    void        *SDataPtr;                          //the pointer to spare data buffer, it will be set to NULL if needn't access spare data
};


//==============================================================================
//  define the data structure for logic management module
//==============================================================================

//define the logical architecture parameter structure
struct __LogicArchitecture_t
{
    __u16       LogicBlkCntPerZone;                 //the counter that marks how many logic blocks in one zone
    __u16       PageCntPerLogicBlk;                 //the counter that marks how many pages in one logical block
    __u8        SectCntPerLogicPage;                //the counter that marks how many  sectors in one logical page
    __u8        ZoneCntPerDie;                      //the counter that marks how many zones in one die
    __u16       Reserved;                           //reserved for 32bit align
};

//define the super block type
struct __SuperPhyBlkType_t
{
    __u16       PhyBlkNum;                          //the super physic block offset number in a die,the first block of the die is 0
    __u16       BlkEraseCnt;                        //the erase count of the super physic block,record how many times it has been erased
};


//define the log block table item type
struct __LogBlkType_t
{
    __u16       LogicBlkNum;                        //the logic block number which the log block is belonged to
    __u16       LastUsedPage;                       //the number of the page which is the last used in the super physic block
    struct __SuperPhyBlkType_t PhyBlk;              //the super physic block number which the log block is mapping to
};


//define the zone table position information type
struct __ZoneTblPstInfo_t
{
    __u16       PhyBlkNum;                          //the physic block number in the chip which stored the block mapping table
    __u16       TablePst;                           //the page number in the physic block which stored the valid block mapping table
};


//define the block mapping table cache access type
struct __BlkMapTblCache_t
{
    __u8        ZoneNum;                            //the number of the zone which is the block mapping table belonged to
    __u8        DirtyFlag;                          //the flag that marks the status of the table in the nand, notes if the table need write back
    __u16       AccessCnt;                          //the counter that record how many times the block mapping table has been accessed
    struct __SuperPhyBlkType_t *DataBlkTbl;         //the pointer to the data block table of the block mapping table
    struct __LogBlkType_t *LogBlkTbl;               //the pointer to the log block table of the block mapping table
    struct __SuperPhyBlkType_t *FreeBlkTbl;         //the pointer to the free block table of the block mapping table
    __u16       LastFreeBlkPst;                     //the pointer to the free block position which is got last time
    __u16       Reserved;                           //reserved for 32bit align
};

//define the block mapping table cache management parameter type
struct __BlkMapTblCachePool_t
{
    struct __BlkMapTblCache_t *ActBlkMapTbl;                                //the pointer to the active block mapping table
    struct __BlkMapTblCache_t BlkMapTblCachePool[BLOCK_MAP_TBL_CACHE_CNT];  //the pool of the block mapping table cache
    __u16       LogBlkAccessAge[MAX_LOG_BLK_CNT];   //the time of accessing log block for the log block
    __u16       LogBlkAccessTimer;                  //the timer of the access time for recording the log block accessing time
    __u16       SuperBlkEraseCnt;                   //the counter of the super block erase, for do wear-levelling
};


//define the page mapping table item type
struct __PageMapTblItem_t
{
    __u16       PhyPageNum;                         //the physic page number which the logic page mapping to
};

//define the page mapping table access type
struct __PageMapTblCache_t
{
    __u8        ZoneNum;                            //the zone number which the page mapping table is belonged to
    __u8        LogBlkPst;                          //the position of the log block in the log block table
    __u16       AccessCnt;                          //the counter that the page mapping table has been accessed
	struct __PageMapTblItem_t *PageMapTbl;          //the pointer to the page mapping table
    __u8        DirtyFlag;                          //the flag that marks if the page mapping table need be writen back to nand flash
    __u8        Reserved[3];                        //reserved for 32bit align
};

//define the page mapping table cache management parameter type
struct __PageMapTblCachePool_t
{
    struct __PageMapTblCache_t *ActPageMapTbl;                              //the poninter to the active page mapping table
    struct __PageMapTblCache_t PageMapTblCachePool[PAGE_MAP_TBL_CACHE_CNT]; //the pool of the page mapping table cache
};


//define the global logical page parameter type
struct __GlobalLogicPageType_t
{
    __u32       LogicPageNum;                       //the global page number of the logic page, it is based on super page size
    __u32       SectorBitmap;                       //the bitmap of the sector in the logic page which data need access
};


//define the global logcial page based on zone and block parameter type
struct __LogicPageType_t
{
    __u32       SectBitmap;                         //the bitmap marks which sectors' data in the logical page need access
    __u16       BlockNum;                           //the value of the number of the logical block which the page is belonged to
    __u16       PageNum;                            //the value of the number of the page in the logical block
    __u8        ZoneNum;                            //the value of the number of the zone, which the page is belonged to
    __u8        Reserved[3];                        //reserved for 32bit align
};


//define the logical control layer management parameter type
struct __LogicCtlPar_t
{
    __u8        OpMode;                             //record nand flash driver last operation, may be read, write, or none.
    __u8        ZoneNum;                            //the number of the zone which is accessed last time
    __u16       LogicBlkNum;                        //the number of the logic block which is accessed last time
    __u16       LogicPageNum;                       //the number of the logic page which is accessed last time
    __u16       LogPageNum;                         //the number of the log page, which is accessed last time
    struct __SuperPhyBlkType_t  DataBlkNum;         //the number of the data block, which is accessed last time
    struct __SuperPhyBlkType_t  LogBlkNum;          //the number of the log block, which is accessed last time
    __u32       DiskCap;                            //the capacity of the logical disk
};


//define the nand flash physical information parameter type
struct __NandPhyInfoPar_t
{
    __u8        NandID[8];                          //the ID number of the nand flash chip
    __u8        DieCntPerChip;                      //the count of the Die in one nand flash chip
    __u8        SectCntPerPage;                     //the count of the sectors in one single physical page
    __u16       PageCntPerBlk;                      //the count of the pages in one single physical block
    __u16       BlkCntPerDie;                       //the count fo the physical blocks in one nand flash Die
    __u16       OperationOpt;                       //the bitmap that marks which optional operation that the nand flash can support
    __u16       ValidBlkRatio;                      //the valid block ratio, based on 1024 blocks
    __u16       AccessFreq;                         //the highest access frequence of the nand flash chip, based on MHz
    __u16       EccMode;                            //the Ecc Mode for the nand flash chip, 0: bch-16, 1:bch-28, 2:bch_32   
    __u32 		ReadRetryType;
    __u32       DDRType;
    struct __OptionalPhyOpPar_t *OptionOp;          //the pointer point to the optional operation parameter
};


//define the global paramter for nand flash driver to access all parameter
struct __NandDriverGlobal_t
{
    struct __NandStorageInfo_t  *NandStorageInfo;               //the pointer to the nand flash hardware information parameter
    struct __ZoneTblPstInfo_t   *ZoneTblPstInfo;                //the pointer to the block mapping table information parameter
    struct __BlkMapTblCachePool_t   *BlkMapTblCachePool;        //the pointer to the block mapping thable cache pool management parameter
    struct __PageMapTblCachePool_t  *PageMapTblCachePool;       //the pointer to the page mapping table cache pool management parameter
    struct __LogicArchitecture_t    *LogicalArchitecture;       //the pointer to the logical archtecture parameter
    struct __NandPageCachePool_t    *PageCachePool;             //the pointer to the page cache pool parameter
};


//==============================================================================
//  define some constant variable for the nand flash driver used
//==============================================================================

//define the mask for the nand flash optional operation
#define NAND_CACHE_READ         (1<<0)              //nand flash support cache read operation
#define NAND_CACHE_PROGRAM      (1<<1)              //nand flash support page cache program operation
#define NAND_MULTI_READ         (1<<2)              //nand flash support multi-plane page read operation
#define NAND_MULTI_PROGRAM      (1<<3)              //nand flash support multi-plane page program operation
#define NAND_PAGE_COPYBACK      (1<<4)              //nand flash support page copy-back command mode operation
#define NAND_INT_INTERLEAVE     (1<<5)              //nand flash support internal inter-leave operation, it based multi-bank
#define NAND_EXT_INTERLEAVE     (1<<6)              //nand flash support external inter-leave operation, it based multi-chip
#define NAND_RANDOM		        (1<<7)			    //nand flash support RANDOMIZER
#define NAND_READ_RETRY	        (1<<8)			    //nand falsh support READ RETRY
#define NAND_READ_UNIQUE_ID	    (1<<9)			    //nand falsh support READ UNIQUE_ID
#define NAND_PAGE_ADR_NO_SKIP	(1<<10)			    //nand falsh page adr no skip is requiered


//define the mask for the nand flash operation status
#define NAND_OPERATE_FAIL       (1<<0)              //nand flash program/erase failed mask
#define NAND_CACHE_READY        (1<<5)              //nand flash cache program true ready mask
#define NAND_STATUS_READY       (1<<6)              //nand flash ready/busy status mask
#define NAND_WRITE_PROTECT      (1<<7)              //nand flash write protected mask


//define the mark for physical page status
#define FREE_PAGE_MARK          0xff                //the page is storing no data, is not used
#define DATA_PAGE_MARK          0x55                //the physical page is used for storing the update data
#define TABLE_PAGE_MARK         0xaa                //the physical page is used for storing page mapping table

#define TABLE_BLK_MARK          0xaa                //the mark for the block mapping table block which is a special type block
#define BOOT_BLK_MARK           0xbb                //the mark for the boot block which is a special type block

//define the count of the physical blocks managed by one zone
#define BLOCK_CNT_OF_ZONE       1024                //one zone is organized based on 1024 blocks

//define the size of the sector
#define SECTOR_SIZE             512                 //the size of a sector, based on byte


//==============================================================================
//  define the function return value for different modules
//==============================================================================

#define NAND_OP_TRUE            (0)                     //define the successful return value
#define NAND_OP_FALSE           (-1)                    //define the failed return value


//define the return value
#define ECC_LIMIT               10                  //reach the limit of the ability of ECC
#define ERR_MALLOC              11                  //request buffer failed
#define ERR_ECC                 12                  //too much ecc error
#define ERR_NANDFAIL            13                  //nand flash program or erase fail
#define ERR_TIMEOUT             14                  //hardware timeout
#define ERR_PHYSIC              15                  //physical operation module error
#define ERR_SCAN                16                  //scan module error
#define ERR_FORMAT              17                  //format module error
#define ERR_MAPPING             18                  //mapping module error
#define ERR_LOGICCTL            19                  //logic control module error
#define ERR_ADDRBEYOND          20                  //the logical sectors need be accessed is beyond the logical disk
#define ERR_INVALIDPHYADDR		  21

#endif //ifndef __NAND_TYPE_H

