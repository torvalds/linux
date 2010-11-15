/*
 * NAND Flash Controller Device Driver
 * Copyright (c) 2009, Intel Corporation and its suppliers.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef _FLASH_INTERFACE_
#define _FLASH_INTERFACE_

#include "ffsport.h"
#include "spectraswconfig.h"

#define MAX_BYTE_VALUE        0xFF
#define MAX_WORD_VALUE        0xFFFF
#define MAX_U32_VALUE        0xFFFFFFFF

#define MAX_BLOCKNODE_VALUE     0xFFFFFF
#define DISCARD_BLOCK           0x800000
#define SPARE_BLOCK             0x400000
#define BAD_BLOCK               0xC00000

#define UNHIT_CACHE_ITEM         0xFFFF

#define NAND_CACHE_INIT_ADDR    0xffffffffffffffffULL

#define IN_PROGRESS_BLOCK_TABLE   0x00
#define CURRENT_BLOCK_TABLE       0x01

#define BTSIG_OFFSET   (0)
#define BTSIG_BYTES    (5)
#define BTSIG_DELTA    (3)

#define MAX_READ_COUNTER  0x2710

#define FIRST_BT_ID		(1)
#define LAST_BT_ID    (254)
#define BTBLOCK_INVAL  (u32)(0xFFFFFFFF)

struct device_info_tag {
	u16 wDeviceMaker;
	u16 wDeviceID;
	u32 wDeviceType;
	u32 wSpectraStartBlock;
	u32 wSpectraEndBlock;
	u32 wTotalBlocks;
	u16 wPagesPerBlock;
	u16 wPageSize;
	u16 wPageDataSize;
	u16 wPageSpareSize;
	u16 wNumPageSpareFlag;
	u16 wECCBytesPerSector;
	u32 wBlockSize;
	u32 wBlockDataSize;
	u32 wDataBlockNum;
	u8 bPlaneNum;
	u16 wDeviceMainAreaSize;
	u16 wDeviceSpareAreaSize;
	u16 wDevicesConnected;
	u16 wDeviceWidth;
	u16 wHWRevision;
	u16 wHWFeatures;

	u16 wONFIDevFeatures;
	u16 wONFIOptCommands;
	u16 wONFITimingMode;
	u16 wONFIPgmCacheTimingMode;

	u16 MLCDevice;
	u16 wSpareSkipBytes;

	u8 nBitsInPageNumber;
	u8 nBitsInPageDataSize;
	u8 nBitsInBlockDataSize;
};

extern struct device_info_tag DeviceInfo;

/* Cache item format */
struct flash_cache_item_tag {
	u64 address;
	u16 use_cnt;
	u16 changed;
	u8 *buf;
};

struct flash_cache_tag {
	u32 cache_item_size; /* Size in bytes of each cache item */
	u16 pages_per_item; /* How many NAND pages in each cache item */
	u16 LRU; /* No. of the least recently used cache item */
	struct flash_cache_item_tag array[CACHE_ITEM_NUM];
};

/*
 *Data structure for each list node of the management table
 * used for the Level 2 Cache. Each node maps one logical NAND block.
 */
struct spectra_l2_cache_list {
	struct list_head list;
	u32 logical_blk_num; /* Logical block number */
	u32 pages_array[]; /* Page map array of this logical block.
			   * Array index is the logical block number,
			   * and for every item of this arry:
			   * high 16 bit is index of the L2 cache block num,
			   * low 16 bit is the phy page num
			   * of the above L2 cache block.
			   * This array will be kmalloc during run time.
			   */
};

struct spectra_l2_cache_info {
	u32 blk_array[BLK_NUM_FOR_L2_CACHE];
	u16 cur_blk_idx; /* idx to the phy block number of current using */
	u16 cur_page_num; /* pages number of current using */
	struct spectra_l2_cache_list table; /* First node of the table */
};

#define RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE    1

#if RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE
struct flash_cache_mod_item_tag {
	u64 address;
	u8 changed;
};

struct flash_cache_delta_list_tag {
	u8 item;  /* used cache item */
	struct flash_cache_mod_item_tag cache;
};
#endif

extern struct flash_cache_tag Cache;

extern u8 *buf_read_page_main_spare;
extern u8 *buf_write_page_main_spare;
extern u8 *buf_read_page_spare;
extern u8 *buf_get_bad_block;
extern u8 *cdma_desc_buf;
extern u8 *memcp_desc_buf;

/* struture used for IndentfyDevice function */
struct spectra_indentfy_dev_tag {
	u32 NumBlocks;
	u16 PagesPerBlock;
	u16 PageDataSize;
	u16 wECCBytesPerSector;
	u32 wDataBlockNum;
};

int GLOB_FTL_Flash_Init(void);
int GLOB_FTL_Flash_Release(void);
/*void GLOB_FTL_Erase_Flash(void);*/
int GLOB_FTL_Block_Erase(u64 block_addr);
int GLOB_FTL_Is_BadBlock(u32 block_num);
int GLOB_FTL_IdentifyDevice(struct spectra_indentfy_dev_tag *dev_data);
int GLOB_FTL_Event_Status(int *);
u16 glob_ftl_execute_cmds(void);

/*int FTL_Read_Disturbance(ADDRESSTYPE dwBlockAddr);*/
int FTL_Read_Disturbance(u32 dwBlockAddr);

/*Flash r/w based on cache*/
int GLOB_FTL_Page_Read(u8 *read_data, u64 page_addr);
int GLOB_FTL_Page_Write(u8 *write_data, u64 page_addr);
int GLOB_FTL_Wear_Leveling(void);
int GLOB_FTL_Flash_Format(void);
int GLOB_FTL_Init(void);
int GLOB_FTL_Flush_Cache(void);
int GLOB_FTL_Garbage_Collection(void);
int GLOB_FTL_BT_Garbage_Collection(void);
void GLOB_FTL_Cache_Release(void);
u8 *get_blk_table_start_addr(void);
u8 *get_wear_leveling_table_start_addr(void);
unsigned long get_blk_table_len(void);
unsigned long get_wear_leveling_table_len(void);

#if DEBUG_BNDRY
void debug_boundary_lineno_error(int chnl, int limit, int no, int lineno,
				char *filename);
#define debug_boundary_error(chnl, limit, no) debug_boundary_lineno_error(chnl,\
						limit, no, __LINE__, __FILE__)
#else
#define debug_boundary_error(chnl, limit, no) ;
#endif

#endif /*_FLASH_INTERFACE_*/
