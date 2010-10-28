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

#include <linux/fs.h>
#include <linux/slab.h>

#include "flash.h"
#include "ffsdefs.h"
#include "lld.h"
#include "lld_nand.h"
#if CMD_DMA
#include "lld_cdma.h"
#endif

#define BLK_FROM_ADDR(addr)  ((u32)(addr >> DeviceInfo.nBitsInBlockDataSize))
#define PAGE_FROM_ADDR(addr, Block)  ((u16)((addr - (u64)Block * \
	DeviceInfo.wBlockDataSize) >> DeviceInfo.nBitsInPageDataSize))

#define IS_SPARE_BLOCK(blk)     (BAD_BLOCK != (pbt[blk] &\
	BAD_BLOCK) && SPARE_BLOCK == (pbt[blk] & SPARE_BLOCK))

#define IS_DATA_BLOCK(blk)      (0 == (pbt[blk] & BAD_BLOCK))

#define IS_DISCARDED_BLOCK(blk) (BAD_BLOCK != (pbt[blk] &\
	BAD_BLOCK) && DISCARD_BLOCK == (pbt[blk] & DISCARD_BLOCK))

#define IS_BAD_BLOCK(blk)       (BAD_BLOCK == (pbt[blk] & BAD_BLOCK))

#if DEBUG_BNDRY
void debug_boundary_lineno_error(int chnl, int limit, int no,
				int lineno, char *filename)
{
	if (chnl >= limit)
		printk(KERN_ERR "Boundary Check Fail value %d >= limit %d, "
		"at  %s:%d. Other info:%d. Aborting...\n",
		chnl, limit, filename, lineno, no);
}
/* static int globalmemsize; */
#endif

static u16 FTL_Cache_If_Hit(u64 dwPageAddr);
static int FTL_Cache_Read(u64 dwPageAddr);
static void FTL_Cache_Read_Page(u8 *pData, u64 dwPageAddr,
				u16 cache_blk);
static void FTL_Cache_Write_Page(u8 *pData, u64 dwPageAddr,
				 u8 cache_blk, u16 flag);
static int FTL_Cache_Write(void);
static void FTL_Calculate_LRU(void);
static u32 FTL_Get_Block_Index(u32 wBlockNum);

static int FTL_Search_Block_Table_IN_Block(u32 BT_Block,
					   u8 BT_Tag, u16 *Page);
static int FTL_Read_Block_Table(void);
static int FTL_Write_Block_Table(int wForce);
static int FTL_Write_Block_Table_Data(void);
static int FTL_Check_Block_Table(int wOldTable);
static int FTL_Static_Wear_Leveling(void);
static u32 FTL_Replace_Block_Table(void);
static int FTL_Write_IN_Progress_Block_Table_Page(void);

static u32 FTL_Get_Page_Num(u64 length);
static u64 FTL_Get_Physical_Block_Addr(u64 blk_addr);

static u32 FTL_Replace_OneBlock(u32 wBlockNum,
				      u32 wReplaceNum);
static u32 FTL_Replace_LWBlock(u32 wBlockNum,
				     int *pGarbageCollect);
static u32 FTL_Replace_MWBlock(void);
static int FTL_Replace_Block(u64 blk_addr);
static int FTL_Adjust_Relative_Erase_Count(u32 Index_of_MAX);

struct device_info_tag DeviceInfo;
struct flash_cache_tag Cache;
static struct spectra_l2_cache_info cache_l2;

static u8 *cache_l2_page_buf;
static u8 *cache_l2_blk_buf;

u8 *g_pBlockTable;
u8 *g_pWearCounter;
u16 *g_pReadCounter;
u32 *g_pBTBlocks;
static u16 g_wBlockTableOffset;
static u32 g_wBlockTableIndex;
static u8 g_cBlockTableStatus;

static u8 *g_pTempBuf;
static u8 *flag_check_blk_table;
static u8 *tmp_buf_search_bt_in_block;
static u8 *spare_buf_search_bt_in_block;
static u8 *spare_buf_bt_search_bt_in_block;
static u8 *tmp_buf1_read_blk_table;
static u8 *tmp_buf2_read_blk_table;
static u8 *flags_static_wear_leveling;
static u8 *tmp_buf_write_blk_table_data;
static u8 *tmp_buf_read_disturbance;

u8 *buf_read_page_main_spare;
u8 *buf_write_page_main_spare;
u8 *buf_read_page_spare;
u8 *buf_get_bad_block;

#if (RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE && CMD_DMA)
struct flash_cache_delta_list_tag int_cache[MAX_CHANS + MAX_DESCS];
struct flash_cache_tag cache_start_copy;
#endif

int g_wNumFreeBlocks;
u8 g_SBDCmdIndex;

static u8 *g_pIPF;
static u8 bt_flag = FIRST_BT_ID;
static u8 bt_block_changed;

static u16 cache_block_to_write;
static u8 last_erased = FIRST_BT_ID;

static u8 GC_Called;
static u8 BT_GC_Called;

#if CMD_DMA
#define COPY_BACK_BUF_NUM 10

static u8 ftl_cmd_cnt;  /* Init value is 0 */
u8 *g_pBTDelta;
u8 *g_pBTDelta_Free;
u8 *g_pBTStartingCopy;
u8 *g_pWearCounterCopy;
u16 *g_pReadCounterCopy;
u8 *g_pBlockTableCopies;
u8 *g_pNextBlockTable;
static u8 *cp_back_buf_copies[COPY_BACK_BUF_NUM];
static int cp_back_buf_idx;

static u8 *g_temp_buf;

#pragma pack(push, 1)
#pragma pack(1)
struct BTableChangesDelta {
	u8 ftl_cmd_cnt;
	u8 ValidFields;
	u16 g_wBlockTableOffset;
	u32 g_wBlockTableIndex;
	u32 BT_Index;
	u32 BT_Entry_Value;
	u32 WC_Index;
	u8 WC_Entry_Value;
	u32 RC_Index;
	u16 RC_Entry_Value;
};

#pragma pack(pop)

struct BTableChangesDelta *p_BTableChangesDelta;
#endif


#define MARK_BLOCK_AS_BAD(blocknode)      (blocknode |= BAD_BLOCK)
#define MARK_BLK_AS_DISCARD(blk)  (blk = (blk & ~SPARE_BLOCK) | DISCARD_BLOCK)

#define FTL_Get_LBAPBA_Table_Mem_Size_Bytes() (DeviceInfo.wDataBlockNum *\
						sizeof(u32))
#define FTL_Get_WearCounter_Table_Mem_Size_Bytes() (DeviceInfo.wDataBlockNum *\
						sizeof(u8))
#define FTL_Get_ReadCounter_Table_Mem_Size_Bytes() (DeviceInfo.wDataBlockNum *\
						sizeof(u16))
#if SUPPORT_LARGE_BLOCKNUM
#define FTL_Get_LBAPBA_Table_Flash_Size_Bytes() (DeviceInfo.wDataBlockNum *\
						sizeof(u8) * 3)
#else
#define FTL_Get_LBAPBA_Table_Flash_Size_Bytes() (DeviceInfo.wDataBlockNum *\
						sizeof(u16))
#endif
#define FTL_Get_WearCounter_Table_Flash_Size_Bytes \
	FTL_Get_WearCounter_Table_Mem_Size_Bytes
#define FTL_Get_ReadCounter_Table_Flash_Size_Bytes \
	FTL_Get_ReadCounter_Table_Mem_Size_Bytes

static u32 FTL_Get_Block_Table_Flash_Size_Bytes(void)
{
	u32 byte_num;

	if (DeviceInfo.MLCDevice) {
		byte_num = FTL_Get_LBAPBA_Table_Flash_Size_Bytes() +
			DeviceInfo.wDataBlockNum * sizeof(u8) +
			DeviceInfo.wDataBlockNum * sizeof(u16);
	} else {
		byte_num = FTL_Get_LBAPBA_Table_Flash_Size_Bytes() +
			DeviceInfo.wDataBlockNum * sizeof(u8);
	}

	byte_num += 4 * sizeof(u8);

	return byte_num;
}

static u16  FTL_Get_Block_Table_Flash_Size_Pages(void)
{
	return (u16)FTL_Get_Page_Num(FTL_Get_Block_Table_Flash_Size_Bytes());
}

static int FTL_Copy_Block_Table_To_Flash(u8 *flashBuf, u32 sizeToTx,
					u32 sizeTxed)
{
	u32 wBytesCopied, blk_tbl_size, wBytes;
	u32 *pbt = (u32 *)g_pBlockTable;

	blk_tbl_size = FTL_Get_LBAPBA_Table_Flash_Size_Bytes();
	for (wBytes = 0;
	(wBytes < sizeToTx) && ((wBytes + sizeTxed) < blk_tbl_size);
	wBytes++) {
#if SUPPORT_LARGE_BLOCKNUM
		flashBuf[wBytes] = (u8)(pbt[(wBytes + sizeTxed) / 3]
		>> (((wBytes + sizeTxed) % 3) ?
		((((wBytes + sizeTxed) % 3) == 2) ? 0 : 8) : 16)) & 0xFF;
#else
		flashBuf[wBytes] = (u8)(pbt[(wBytes + sizeTxed) / 2]
		>> (((wBytes + sizeTxed) % 2) ? 0 : 8)) & 0xFF;
#endif
	}

	sizeTxed = (sizeTxed > blk_tbl_size) ? (sizeTxed - blk_tbl_size) : 0;
	blk_tbl_size = FTL_Get_WearCounter_Table_Flash_Size_Bytes();
	wBytesCopied = wBytes;
	wBytes = ((blk_tbl_size - sizeTxed) > (sizeToTx - wBytesCopied)) ?
		(sizeToTx - wBytesCopied) : (blk_tbl_size - sizeTxed);
	memcpy(flashBuf + wBytesCopied, g_pWearCounter + sizeTxed, wBytes);

	sizeTxed = (sizeTxed > blk_tbl_size) ? (sizeTxed - blk_tbl_size) : 0;

	if (DeviceInfo.MLCDevice) {
		blk_tbl_size = FTL_Get_ReadCounter_Table_Flash_Size_Bytes();
		wBytesCopied += wBytes;
		for (wBytes = 0; ((wBytes + wBytesCopied) < sizeToTx) &&
			((wBytes + sizeTxed) < blk_tbl_size); wBytes++)
			flashBuf[wBytes + wBytesCopied] =
			(g_pReadCounter[(wBytes + sizeTxed) / 2] >>
			(((wBytes + sizeTxed) % 2) ? 0 : 8)) & 0xFF;
	}

	return wBytesCopied + wBytes;
}

static int FTL_Copy_Block_Table_From_Flash(u8 *flashBuf,
				u32 sizeToTx, u32 sizeTxed)
{
	u32 wBytesCopied, blk_tbl_size, wBytes;
	u32 *pbt = (u32 *)g_pBlockTable;

	blk_tbl_size = FTL_Get_LBAPBA_Table_Flash_Size_Bytes();
	for (wBytes = 0; (wBytes < sizeToTx) &&
		((wBytes + sizeTxed) < blk_tbl_size); wBytes++) {
#if SUPPORT_LARGE_BLOCKNUM
		if (!((wBytes + sizeTxed) % 3))
			pbt[(wBytes + sizeTxed) / 3] = 0;
		pbt[(wBytes + sizeTxed) / 3] |=
			(flashBuf[wBytes] << (((wBytes + sizeTxed) % 3) ?
			((((wBytes + sizeTxed) % 3) == 2) ? 0 : 8) : 16));
#else
		if (!((wBytes + sizeTxed) % 2))
			pbt[(wBytes + sizeTxed) / 2] = 0;
		pbt[(wBytes + sizeTxed) / 2] |=
			(flashBuf[wBytes] << (((wBytes + sizeTxed) % 2) ?
			0 : 8));
#endif
	}

	sizeTxed = (sizeTxed > blk_tbl_size) ? (sizeTxed - blk_tbl_size) : 0;
	blk_tbl_size = FTL_Get_WearCounter_Table_Flash_Size_Bytes();
	wBytesCopied = wBytes;
	wBytes = ((blk_tbl_size - sizeTxed) > (sizeToTx - wBytesCopied)) ?
		(sizeToTx - wBytesCopied) : (blk_tbl_size - sizeTxed);
	memcpy(g_pWearCounter + sizeTxed, flashBuf + wBytesCopied, wBytes);
	sizeTxed = (sizeTxed > blk_tbl_size) ? (sizeTxed - blk_tbl_size) : 0;

	if (DeviceInfo.MLCDevice) {
		wBytesCopied += wBytes;
		blk_tbl_size = FTL_Get_ReadCounter_Table_Flash_Size_Bytes();
		for (wBytes = 0; ((wBytes + wBytesCopied) < sizeToTx) &&
			((wBytes + sizeTxed) < blk_tbl_size); wBytes++) {
			if (((wBytes + sizeTxed) % 2))
				g_pReadCounter[(wBytes + sizeTxed) / 2] = 0;
			g_pReadCounter[(wBytes + sizeTxed) / 2] |=
				(flashBuf[wBytes] <<
				(((wBytes + sizeTxed) % 2) ? 0 : 8));
		}
	}

	return wBytesCopied+wBytes;
}

static int FTL_Insert_Block_Table_Signature(u8 *buf, u8 tag)
{
	int i;

	for (i = 0; i < BTSIG_BYTES; i++)
		buf[BTSIG_OFFSET + i] =
		((tag + (i * BTSIG_DELTA) - FIRST_BT_ID) %
		(1 + LAST_BT_ID-FIRST_BT_ID)) + FIRST_BT_ID;

	return PASS;
}

static int FTL_Extract_Block_Table_Tag(u8 *buf, u8 **tagarray)
{
	static u8 tag[BTSIG_BYTES >> 1];
	int i, j, k, tagi, tagtemp, status;

	*tagarray = (u8 *)tag;
	tagi = 0;

	for (i = 0; i < (BTSIG_BYTES - 1); i++) {
		for (j = i + 1; (j < BTSIG_BYTES) &&
			(tagi < (BTSIG_BYTES >> 1)); j++) {
			tagtemp = buf[BTSIG_OFFSET + j] -
				buf[BTSIG_OFFSET + i];
			if (tagtemp && !(tagtemp % BTSIG_DELTA)) {
				tagtemp = (buf[BTSIG_OFFSET + i] +
					(1 + LAST_BT_ID - FIRST_BT_ID) -
					(i * BTSIG_DELTA)) %
					(1 + LAST_BT_ID - FIRST_BT_ID);
				status = FAIL;
				for (k = 0; k < tagi; k++) {
					if (tagtemp == tag[k])
						status = PASS;
				}

				if (status == FAIL) {
					tag[tagi++] = tagtemp;
					i = (j == (i + 1)) ? i + 1 : i;
					j = (j == (i + 1)) ? i + 1 : i;
				}
			}
		}
	}

	return tagi;
}


static int FTL_Execute_SPL_Recovery(void)
{
	u32 j, block, blks;
	u32 *pbt = (u32 *)g_pBlockTable;
	int ret;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
				__FILE__, __LINE__, __func__);

	blks = DeviceInfo.wSpectraEndBlock - DeviceInfo.wSpectraStartBlock;
	for (j = 0; j <= blks; j++) {
		block = (pbt[j]);
		if (((block & BAD_BLOCK) != BAD_BLOCK) &&
			((block & SPARE_BLOCK) == SPARE_BLOCK)) {
			ret =  GLOB_LLD_Erase_Block(block & ~BAD_BLOCK);
			if (FAIL == ret) {
				nand_dbg_print(NAND_DBG_WARN,
					"NAND Program fail in %s, Line %d, "
					"Function: %s, new Bad Block %d "
					"generated!\n",
					__FILE__, __LINE__, __func__,
					(int)(block & ~BAD_BLOCK));
				MARK_BLOCK_AS_BAD(pbt[j]);
			}
		}
	}

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_IdentifyDevice
* Inputs:       pointer to identify data structure
* Outputs:      PASS / FAIL
* Description:  the identify data structure is filled in with
*                   information for the block driver.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_IdentifyDevice(struct spectra_indentfy_dev_tag *dev_data)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
				__FILE__, __LINE__, __func__);

	dev_data->NumBlocks = DeviceInfo.wTotalBlocks;
	dev_data->PagesPerBlock = DeviceInfo.wPagesPerBlock;
	dev_data->PageDataSize = DeviceInfo.wPageDataSize;
	dev_data->wECCBytesPerSector = DeviceInfo.wECCBytesPerSector;
	dev_data->wDataBlockNum = DeviceInfo.wDataBlockNum;

	return PASS;
}

/* ..... */
static int allocate_memory(void)
{
	u32 block_table_size, page_size, block_size, mem_size;
	u32 total_bytes = 0;
	int i;
#if CMD_DMA
	int j;
#endif

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	page_size = DeviceInfo.wPageSize;
	block_size = DeviceInfo.wPagesPerBlock * DeviceInfo.wPageDataSize;

	block_table_size = DeviceInfo.wDataBlockNum *
		(sizeof(u32) + sizeof(u8) + sizeof(u16));
	block_table_size += (DeviceInfo.wPageDataSize -
		(block_table_size % DeviceInfo.wPageDataSize)) %
		DeviceInfo.wPageDataSize;

	/* Malloc memory for block tables */
	g_pBlockTable = kmalloc(block_table_size, GFP_ATOMIC);
	if (!g_pBlockTable)
		goto block_table_fail;
	memset(g_pBlockTable, 0, block_table_size);
	total_bytes += block_table_size;

	g_pWearCounter = (u8 *)(g_pBlockTable +
		DeviceInfo.wDataBlockNum * sizeof(u32));

	if (DeviceInfo.MLCDevice)
		g_pReadCounter = (u16 *)(g_pBlockTable +
			DeviceInfo.wDataBlockNum *
			(sizeof(u32) + sizeof(u8)));

	/* Malloc memory and init for cache items */
	for (i = 0; i < CACHE_ITEM_NUM; i++) {
		Cache.array[i].address = NAND_CACHE_INIT_ADDR;
		Cache.array[i].use_cnt = 0;
		Cache.array[i].changed = CLEAR;
		Cache.array[i].buf = kmalloc(Cache.cache_item_size,
			GFP_ATOMIC);
		if (!Cache.array[i].buf)
			goto cache_item_fail;
		memset(Cache.array[i].buf, 0, Cache.cache_item_size);
		total_bytes += Cache.cache_item_size;
	}

	/* Malloc memory for IPF */
	g_pIPF = kmalloc(page_size, GFP_ATOMIC);
	if (!g_pIPF)
		goto ipf_fail;
	memset(g_pIPF, 0, page_size);
	total_bytes += page_size;

	/* Malloc memory for data merging during Level2 Cache flush */
	cache_l2_page_buf = kmalloc(page_size, GFP_ATOMIC);
	if (!cache_l2_page_buf)
		goto cache_l2_page_buf_fail;
	memset(cache_l2_page_buf, 0xff, page_size);
	total_bytes += page_size;

	cache_l2_blk_buf = kmalloc(block_size, GFP_ATOMIC);
	if (!cache_l2_blk_buf)
		goto cache_l2_blk_buf_fail;
	memset(cache_l2_blk_buf, 0xff, block_size);
	total_bytes += block_size;

	/* Malloc memory for temp buffer */
	g_pTempBuf = kmalloc(Cache.cache_item_size, GFP_ATOMIC);
	if (!g_pTempBuf)
		goto Temp_buf_fail;
	memset(g_pTempBuf, 0, Cache.cache_item_size);
	total_bytes += Cache.cache_item_size;

	/* Malloc memory for block table blocks */
	mem_size = (1 + LAST_BT_ID - FIRST_BT_ID) * sizeof(u32);
	g_pBTBlocks = kmalloc(mem_size, GFP_ATOMIC);
	if (!g_pBTBlocks)
		goto bt_blocks_fail;
	memset(g_pBTBlocks, 0xff, mem_size);
	total_bytes += mem_size;

	/* Malloc memory for function FTL_Check_Block_Table */
	flag_check_blk_table = kmalloc(DeviceInfo.wDataBlockNum, GFP_ATOMIC);
	if (!flag_check_blk_table)
		goto flag_check_blk_table_fail;
	total_bytes += DeviceInfo.wDataBlockNum;

	/* Malloc memory for function FTL_Search_Block_Table_IN_Block */
	tmp_buf_search_bt_in_block = kmalloc(page_size, GFP_ATOMIC);
	if (!tmp_buf_search_bt_in_block)
		goto tmp_buf_search_bt_in_block_fail;
	memset(tmp_buf_search_bt_in_block, 0xff, page_size);
	total_bytes += page_size;

	mem_size = DeviceInfo.wPageSize - DeviceInfo.wPageDataSize;
	spare_buf_search_bt_in_block = kmalloc(mem_size, GFP_ATOMIC);
	if (!spare_buf_search_bt_in_block)
		goto spare_buf_search_bt_in_block_fail;
	memset(spare_buf_search_bt_in_block, 0xff, mem_size);
	total_bytes += mem_size;

	spare_buf_bt_search_bt_in_block = kmalloc(mem_size, GFP_ATOMIC);
	if (!spare_buf_bt_search_bt_in_block)
		goto spare_buf_bt_search_bt_in_block_fail;
	memset(spare_buf_bt_search_bt_in_block, 0xff, mem_size);
	total_bytes += mem_size;

	/* Malloc memory for function FTL_Read_Block_Table */
	tmp_buf1_read_blk_table = kmalloc(page_size, GFP_ATOMIC);
	if (!tmp_buf1_read_blk_table)
		goto tmp_buf1_read_blk_table_fail;
	memset(tmp_buf1_read_blk_table, 0xff, page_size);
	total_bytes += page_size;

	tmp_buf2_read_blk_table = kmalloc(page_size, GFP_ATOMIC);
	if (!tmp_buf2_read_blk_table)
		goto tmp_buf2_read_blk_table_fail;
	memset(tmp_buf2_read_blk_table, 0xff, page_size);
	total_bytes += page_size;

	/* Malloc memory for function FTL_Static_Wear_Leveling */
	flags_static_wear_leveling = kmalloc(DeviceInfo.wDataBlockNum,
					GFP_ATOMIC);
	if (!flags_static_wear_leveling)
		goto flags_static_wear_leveling_fail;
	total_bytes += DeviceInfo.wDataBlockNum;

	/* Malloc memory for function FTL_Write_Block_Table_Data */
	if (FTL_Get_Block_Table_Flash_Size_Pages() > 3)
		mem_size = FTL_Get_Block_Table_Flash_Size_Bytes() -
				2 * DeviceInfo.wPageSize;
	else
		mem_size = DeviceInfo.wPageSize;
	tmp_buf_write_blk_table_data = kmalloc(mem_size, GFP_ATOMIC);
	if (!tmp_buf_write_blk_table_data)
		goto tmp_buf_write_blk_table_data_fail;
	memset(tmp_buf_write_blk_table_data, 0xff, mem_size);
	total_bytes += mem_size;

	/* Malloc memory for function FTL_Read_Disturbance */
	tmp_buf_read_disturbance = kmalloc(block_size, GFP_ATOMIC);
	if (!tmp_buf_read_disturbance)
		goto tmp_buf_read_disturbance_fail;
	memset(tmp_buf_read_disturbance, 0xff, block_size);
	total_bytes += block_size;

	/* Alloc mem for function NAND_Read_Page_Main_Spare of lld_nand.c */
	buf_read_page_main_spare = kmalloc(DeviceInfo.wPageSize, GFP_ATOMIC);
	if (!buf_read_page_main_spare)
		goto buf_read_page_main_spare_fail;
	total_bytes += DeviceInfo.wPageSize;

	/* Alloc mem for function NAND_Write_Page_Main_Spare of lld_nand.c */
	buf_write_page_main_spare = kmalloc(DeviceInfo.wPageSize, GFP_ATOMIC);
	if (!buf_write_page_main_spare)
		goto buf_write_page_main_spare_fail;
	total_bytes += DeviceInfo.wPageSize;

	/* Alloc mem for function NAND_Read_Page_Spare of lld_nand.c */
	buf_read_page_spare = kmalloc(DeviceInfo.wPageSpareSize, GFP_ATOMIC);
	if (!buf_read_page_spare)
		goto buf_read_page_spare_fail;
	memset(buf_read_page_spare, 0xff, DeviceInfo.wPageSpareSize);
	total_bytes += DeviceInfo.wPageSpareSize;

	/* Alloc mem for function NAND_Get_Bad_Block of lld_nand.c */
	buf_get_bad_block = kmalloc(DeviceInfo.wPageSpareSize, GFP_ATOMIC);
	if (!buf_get_bad_block)
		goto buf_get_bad_block_fail;
	memset(buf_get_bad_block, 0xff, DeviceInfo.wPageSpareSize);
	total_bytes += DeviceInfo.wPageSpareSize;

#if CMD_DMA
	g_temp_buf = kmalloc(block_size, GFP_ATOMIC);
	if (!g_temp_buf)
		goto temp_buf_fail;
	memset(g_temp_buf, 0xff, block_size);
	total_bytes += block_size;

	/* Malloc memory for copy of block table used in CDMA mode */
	g_pBTStartingCopy = kmalloc(block_table_size, GFP_ATOMIC);
	if (!g_pBTStartingCopy)
		goto bt_starting_copy;
	memset(g_pBTStartingCopy, 0, block_table_size);
	total_bytes += block_table_size;

	g_pWearCounterCopy = (u8 *)(g_pBTStartingCopy +
		DeviceInfo.wDataBlockNum * sizeof(u32));

	if (DeviceInfo.MLCDevice)
		g_pReadCounterCopy = (u16 *)(g_pBTStartingCopy +
			DeviceInfo.wDataBlockNum *
			(sizeof(u32) + sizeof(u8)));

	/* Malloc memory for block table copies */
	mem_size = 5 * DeviceInfo.wDataBlockNum * sizeof(u32) +
			5 * DeviceInfo.wDataBlockNum * sizeof(u8);
	if (DeviceInfo.MLCDevice)
		mem_size += 5 * DeviceInfo.wDataBlockNum * sizeof(u16);
	g_pBlockTableCopies = kmalloc(mem_size, GFP_ATOMIC);
	if (!g_pBlockTableCopies)
		goto blk_table_copies_fail;
	memset(g_pBlockTableCopies, 0, mem_size);
	total_bytes += mem_size;
	g_pNextBlockTable = g_pBlockTableCopies;

	/* Malloc memory for Block Table Delta */
	mem_size = MAX_DESCS * sizeof(struct BTableChangesDelta);
	g_pBTDelta = kmalloc(mem_size, GFP_ATOMIC);
	if (!g_pBTDelta)
		goto bt_delta_fail;
	memset(g_pBTDelta, 0, mem_size);
	total_bytes += mem_size;
	g_pBTDelta_Free = g_pBTDelta;

	/* Malloc memory for Copy Back Buffers */
	for (j = 0; j < COPY_BACK_BUF_NUM; j++) {
		cp_back_buf_copies[j] = kmalloc(block_size, GFP_ATOMIC);
		if (!cp_back_buf_copies[j])
			goto cp_back_buf_copies_fail;
		memset(cp_back_buf_copies[j], 0, block_size);
		total_bytes += block_size;
	}
	cp_back_buf_idx = 0;

	/* Malloc memory for pending commands list */
	mem_size = sizeof(struct pending_cmd) * MAX_DESCS;
	info.pcmds = kzalloc(mem_size, GFP_KERNEL);
	if (!info.pcmds)
		goto pending_cmds_buf_fail;
	total_bytes += mem_size;

	/* Malloc memory for CDMA descripter table */
	mem_size = sizeof(struct cdma_descriptor) * MAX_DESCS;
	info.cdma_desc_buf = kzalloc(mem_size, GFP_KERNEL);
	if (!info.cdma_desc_buf)
		goto cdma_desc_buf_fail;
	total_bytes += mem_size;

	/* Malloc memory for Memcpy descripter table */
	mem_size = sizeof(struct memcpy_descriptor) * MAX_DESCS;
	info.memcp_desc_buf = kzalloc(mem_size, GFP_KERNEL);
	if (!info.memcp_desc_buf)
		goto memcp_desc_buf_fail;
	total_bytes += mem_size;
#endif

	nand_dbg_print(NAND_DBG_WARN,
		"Total memory allocated in FTL layer: %d\n", total_bytes);

	return PASS;

#if CMD_DMA
memcp_desc_buf_fail:
	kfree(info.cdma_desc_buf);
cdma_desc_buf_fail:
	kfree(info.pcmds);
pending_cmds_buf_fail:
cp_back_buf_copies_fail:
	j--;
	for (; j >= 0; j--)
		kfree(cp_back_buf_copies[j]);
	kfree(g_pBTDelta);
bt_delta_fail:
	kfree(g_pBlockTableCopies);
blk_table_copies_fail:
	kfree(g_pBTStartingCopy);
bt_starting_copy:
	kfree(g_temp_buf);
temp_buf_fail:
	kfree(buf_get_bad_block);
#endif

buf_get_bad_block_fail:
	kfree(buf_read_page_spare);
buf_read_page_spare_fail:
	kfree(buf_write_page_main_spare);
buf_write_page_main_spare_fail:
	kfree(buf_read_page_main_spare);
buf_read_page_main_spare_fail:
	kfree(tmp_buf_read_disturbance);
tmp_buf_read_disturbance_fail:
	kfree(tmp_buf_write_blk_table_data);
tmp_buf_write_blk_table_data_fail:
	kfree(flags_static_wear_leveling);
flags_static_wear_leveling_fail:
	kfree(tmp_buf2_read_blk_table);
tmp_buf2_read_blk_table_fail:
	kfree(tmp_buf1_read_blk_table);
tmp_buf1_read_blk_table_fail:
	kfree(spare_buf_bt_search_bt_in_block);
spare_buf_bt_search_bt_in_block_fail:
	kfree(spare_buf_search_bt_in_block);
spare_buf_search_bt_in_block_fail:
	kfree(tmp_buf_search_bt_in_block);
tmp_buf_search_bt_in_block_fail:
	kfree(flag_check_blk_table);
flag_check_blk_table_fail:
	kfree(g_pBTBlocks);
bt_blocks_fail:
	kfree(g_pTempBuf);
Temp_buf_fail:
	kfree(cache_l2_blk_buf);
cache_l2_blk_buf_fail:
	kfree(cache_l2_page_buf);
cache_l2_page_buf_fail:
	kfree(g_pIPF);
ipf_fail:
cache_item_fail:
	i--;
	for (; i >= 0; i--)
		kfree(Cache.array[i].buf);
	kfree(g_pBlockTable);
block_table_fail:
	printk(KERN_ERR "Failed to kmalloc memory in %s Line %d.\n",
		__FILE__, __LINE__);

	return -ENOMEM;
}

/* .... */
static int free_memory(void)
{
	int i;

#if CMD_DMA
	kfree(info.memcp_desc_buf);
	kfree(info.cdma_desc_buf);
	kfree(info.pcmds);
	for (i = COPY_BACK_BUF_NUM - 1; i >= 0; i--)
		kfree(cp_back_buf_copies[i]);
	kfree(g_pBTDelta);
	kfree(g_pBlockTableCopies);
	kfree(g_pBTStartingCopy);
	kfree(g_temp_buf);
	kfree(buf_get_bad_block);
#endif
	kfree(buf_read_page_spare);
	kfree(buf_write_page_main_spare);
	kfree(buf_read_page_main_spare);
	kfree(tmp_buf_read_disturbance);
	kfree(tmp_buf_write_blk_table_data);
	kfree(flags_static_wear_leveling);
	kfree(tmp_buf2_read_blk_table);
	kfree(tmp_buf1_read_blk_table);
	kfree(spare_buf_bt_search_bt_in_block);
	kfree(spare_buf_search_bt_in_block);
	kfree(tmp_buf_search_bt_in_block);
	kfree(flag_check_blk_table);
	kfree(g_pBTBlocks);
	kfree(g_pTempBuf);
	kfree(g_pIPF);
	for (i = CACHE_ITEM_NUM - 1; i >= 0; i--)
		kfree(Cache.array[i].buf);
	kfree(g_pBlockTable);

	return 0;
}

static void dump_cache_l2_table(void)
{
	struct list_head *p;
	struct spectra_l2_cache_list *pnd;
	int n;

	n = 0;
	list_for_each(p, &cache_l2.table.list) {
		pnd = list_entry(p, struct spectra_l2_cache_list, list);
		nand_dbg_print(NAND_DBG_WARN, "dump_cache_l2_table node: %d, logical_blk_num: %d\n", n, pnd->logical_blk_num);
/*
		for (i = 0; i < DeviceInfo.wPagesPerBlock; i++) {
			if (pnd->pages_array[i] != MAX_U32_VALUE)
				nand_dbg_print(NAND_DBG_WARN, "    pages_array[%d]: 0x%x\n", i, pnd->pages_array[i]);
		}
*/
		n++;
	}
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Init
* Inputs:       none
* Outputs:      PASS=0 / FAIL=1
* Description:  allocates the memory for cache array,
*               important data structures
*               clears the cache array
*               reads the block table from flash into array
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Init(void)
{
	int i;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	Cache.pages_per_item = 1;
	Cache.cache_item_size = 1 * DeviceInfo.wPageDataSize;

	if (allocate_memory() != PASS)
		return FAIL;

#if CMD_DMA
#if RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE
	memcpy((void *)&cache_start_copy, (void *)&Cache,
		sizeof(struct flash_cache_tag));
	memset((void *)&int_cache, -1,
		sizeof(struct flash_cache_delta_list_tag) *
		(MAX_CHANS + MAX_DESCS));
#endif
	ftl_cmd_cnt = 0;
#endif

	if (FTL_Read_Block_Table() != PASS)
		return FAIL;

	/* Init the Level2 Cache data structure */
	for (i = 0; i < BLK_NUM_FOR_L2_CACHE; i++)
		cache_l2.blk_array[i] = MAX_U32_VALUE;
	cache_l2.cur_blk_idx = 0;
	cache_l2.cur_page_num = 0;
	INIT_LIST_HEAD(&cache_l2.table.list);
	cache_l2.table.logical_blk_num = MAX_U32_VALUE;

	dump_cache_l2_table();

	return 0;
}


#if CMD_DMA
#if 0
static void save_blk_table_changes(u16 idx)
{
	u8 ftl_cmd;
	u32 *pbt = (u32 *)g_pBTStartingCopy;

#if RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE
	u16 id;
	u8 cache_blks;

	id = idx - MAX_CHANS;
	if (int_cache[id].item != -1) {
		cache_blks = int_cache[id].item;
		cache_start_copy.array[cache_blks].address =
			int_cache[id].cache.address;
		cache_start_copy.array[cache_blks].changed =
			int_cache[id].cache.changed;
	}
#endif

	ftl_cmd = p_BTableChangesDelta->ftl_cmd_cnt;

	while (ftl_cmd <= PendingCMD[idx].Tag) {
		if (p_BTableChangesDelta->ValidFields == 0x01) {
			g_wBlockTableOffset =
				p_BTableChangesDelta->g_wBlockTableOffset;
		} else if (p_BTableChangesDelta->ValidFields == 0x0C) {
			pbt[p_BTableChangesDelta->BT_Index] =
				p_BTableChangesDelta->BT_Entry_Value;
			debug_boundary_error(((
				p_BTableChangesDelta->BT_Index)),
				DeviceInfo.wDataBlockNum, 0);
		} else if (p_BTableChangesDelta->ValidFields == 0x03) {
			g_wBlockTableOffset =
				p_BTableChangesDelta->g_wBlockTableOffset;
			g_wBlockTableIndex =
				p_BTableChangesDelta->g_wBlockTableIndex;
		} else if (p_BTableChangesDelta->ValidFields == 0x30) {
			g_pWearCounterCopy[p_BTableChangesDelta->WC_Index] =
				p_BTableChangesDelta->WC_Entry_Value;
		} else if ((DeviceInfo.MLCDevice) &&
			(p_BTableChangesDelta->ValidFields == 0xC0)) {
			g_pReadCounterCopy[p_BTableChangesDelta->RC_Index] =
				p_BTableChangesDelta->RC_Entry_Value;
			nand_dbg_print(NAND_DBG_DEBUG,
				"In event status setting read counter "
				"GLOB_ftl_cmd_cnt %u Count %u Index %u\n",
				ftl_cmd,
				p_BTableChangesDelta->RC_Entry_Value,
				(unsigned int)p_BTableChangesDelta->RC_Index);
		} else {
			nand_dbg_print(NAND_DBG_DEBUG,
				"This should never occur \n");
		}
		p_BTableChangesDelta += 1;
		ftl_cmd = p_BTableChangesDelta->ftl_cmd_cnt;
	}
}

static void discard_cmds(u16 n)
{
	u32 *pbt = (u32 *)g_pBTStartingCopy;
	u8 ftl_cmd;
	unsigned long k;
#if RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE
	u8 cache_blks;
	u16 id;
#endif

	if ((PendingCMD[n].CMD == WRITE_MAIN_CMD) ||
		(PendingCMD[n].CMD == WRITE_MAIN_SPARE_CMD)) {
		for (k = 0; k < DeviceInfo.wDataBlockNum; k++) {
			if (PendingCMD[n].Block == (pbt[k] & (~BAD_BLOCK)))
				MARK_BLK_AS_DISCARD(pbt[k]);
		}
	}

	ftl_cmd = p_BTableChangesDelta->ftl_cmd_cnt;
	while (ftl_cmd <= PendingCMD[n].Tag) {
		p_BTableChangesDelta += 1;
		ftl_cmd = p_BTableChangesDelta->ftl_cmd_cnt;
	}

#if RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE
	id = n - MAX_CHANS;

	if (int_cache[id].item != -1) {
		cache_blks = int_cache[id].item;
		if (PendingCMD[n].CMD == MEMCOPY_CMD) {
			if ((cache_start_copy.array[cache_blks].buf <=
				PendingCMD[n].DataDestAddr) &&
				((cache_start_copy.array[cache_blks].buf +
				Cache.cache_item_size) >
				PendingCMD[n].DataDestAddr)) {
				cache_start_copy.array[cache_blks].address =
						NAND_CACHE_INIT_ADDR;
				cache_start_copy.array[cache_blks].use_cnt =
								0;
				cache_start_copy.array[cache_blks].changed =
								CLEAR;
			}
		} else {
			cache_start_copy.array[cache_blks].address =
					int_cache[id].cache.address;
			cache_start_copy.array[cache_blks].changed =
					int_cache[id].cache.changed;
		}
	}
#endif
}

static void process_cmd_pass(int *first_failed_cmd, u16 idx)
{
	if (0 == *first_failed_cmd)
		save_blk_table_changes(idx);
	else
		discard_cmds(idx);
}

static void process_cmd_fail_abort(int *first_failed_cmd,
				u16 idx, int event)
{
	u32 *pbt = (u32 *)g_pBTStartingCopy;
	u8 ftl_cmd;
	unsigned long i;
	int erase_fail, program_fail;
#if RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE
	u8 cache_blks;
	u16 id;
#endif

	if (0 == *first_failed_cmd)
		*first_failed_cmd = PendingCMD[idx].SBDCmdIndex;

	nand_dbg_print(NAND_DBG_DEBUG, "Uncorrectable error has occured "
		"while executing %u Command %u accesing Block %u\n",
		(unsigned int)p_BTableChangesDelta->ftl_cmd_cnt,
		PendingCMD[idx].CMD,
		(unsigned int)PendingCMD[idx].Block);

	ftl_cmd = p_BTableChangesDelta->ftl_cmd_cnt;
	while (ftl_cmd <= PendingCMD[idx].Tag) {
		p_BTableChangesDelta += 1;
		ftl_cmd = p_BTableChangesDelta->ftl_cmd_cnt;
	}

#if RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE
	id = idx - MAX_CHANS;

	if (int_cache[id].item != -1) {
		cache_blks = int_cache[id].item;
		if ((PendingCMD[idx].CMD == WRITE_MAIN_CMD)) {
			cache_start_copy.array[cache_blks].address =
					int_cache[id].cache.address;
			cache_start_copy.array[cache_blks].changed = SET;
		} else if ((PendingCMD[idx].CMD == READ_MAIN_CMD)) {
			cache_start_copy.array[cache_blks].address =
				NAND_CACHE_INIT_ADDR;
			cache_start_copy.array[cache_blks].use_cnt = 0;
			cache_start_copy.array[cache_blks].changed =
							CLEAR;
		} else if (PendingCMD[idx].CMD == ERASE_CMD) {
			/* ? */
		} else if (PendingCMD[idx].CMD == MEMCOPY_CMD) {
			/* ? */
		}
	}
#endif

	erase_fail = (event == EVENT_ERASE_FAILURE) &&
			(PendingCMD[idx].CMD == ERASE_CMD);

	program_fail = (event == EVENT_PROGRAM_FAILURE) &&
			((PendingCMD[idx].CMD == WRITE_MAIN_CMD) ||
			(PendingCMD[idx].CMD == WRITE_MAIN_SPARE_CMD));

	if (erase_fail || program_fail) {
		for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
			if (PendingCMD[idx].Block ==
				(pbt[i] & (~BAD_BLOCK)))
				MARK_BLOCK_AS_BAD(pbt[i]);
		}
	}
}

static void process_cmd(int *first_failed_cmd, u16 idx, int event)
{
	u8 ftl_cmd;
	int cmd_match = 0;

	if (p_BTableChangesDelta->ftl_cmd_cnt == PendingCMD[idx].Tag)
		cmd_match = 1;

	if (PendingCMD[idx].Status == CMD_PASS) {
		process_cmd_pass(first_failed_cmd, idx);
	} else if ((PendingCMD[idx].Status == CMD_FAIL) ||
			(PendingCMD[idx].Status == CMD_ABORT)) {
		process_cmd_fail_abort(first_failed_cmd, idx, event);
	} else if ((PendingCMD[idx].Status == CMD_NOT_DONE) &&
					PendingCMD[idx].Tag) {
		nand_dbg_print(NAND_DBG_DEBUG,
			" Command no. %hu is not executed\n",
			(unsigned int)PendingCMD[idx].Tag);
		ftl_cmd = p_BTableChangesDelta->ftl_cmd_cnt;
		while (ftl_cmd <= PendingCMD[idx].Tag) {
			p_BTableChangesDelta += 1;
			ftl_cmd = p_BTableChangesDelta->ftl_cmd_cnt;
		}
	}
}
#endif

static void process_cmd(int *first_failed_cmd, u16 idx, int event)
{
	printk(KERN_ERR "temporary workaround function. "
		"Should not be called! \n");
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:    	GLOB_FTL_Event_Status
* Inputs:       none
* Outputs:      Event Code
* Description:	It is called by SBD after hardware interrupt signalling
*               completion of commands chain
*               It does following things
*               get event status from LLD
*               analyze command chain status
*               determine last command executed
*               analyze results
*               rebuild the block table in case of uncorrectable error
*               return event code
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Event_Status(int *first_failed_cmd)
{
	int event_code = PASS;
	u16 i_P;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	*first_failed_cmd = 0;

	event_code = GLOB_LLD_Event_Status();

	switch (event_code) {
	case EVENT_PASS:
		nand_dbg_print(NAND_DBG_DEBUG, "Handling EVENT_PASS\n");
		break;
	case EVENT_UNCORRECTABLE_DATA_ERROR:
		nand_dbg_print(NAND_DBG_DEBUG, "Handling Uncorrectable ECC!\n");
		break;
	case EVENT_PROGRAM_FAILURE:
	case EVENT_ERASE_FAILURE:
		nand_dbg_print(NAND_DBG_WARN, "Handling Ugly case. "
			"Event code: 0x%x\n", event_code);
		p_BTableChangesDelta =
			(struct BTableChangesDelta *)g_pBTDelta;
		for (i_P = MAX_CHANS; i_P < (ftl_cmd_cnt + MAX_CHANS);
				i_P++)
			process_cmd(first_failed_cmd, i_P, event_code);
		memcpy(g_pBlockTable, g_pBTStartingCopy,
			DeviceInfo.wDataBlockNum * sizeof(u32));
		memcpy(g_pWearCounter, g_pWearCounterCopy,
			DeviceInfo.wDataBlockNum * sizeof(u8));
		if (DeviceInfo.MLCDevice)
			memcpy(g_pReadCounter, g_pReadCounterCopy,
				DeviceInfo.wDataBlockNum * sizeof(u16));

#if RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE
		memcpy((void *)&Cache, (void *)&cache_start_copy,
			sizeof(struct flash_cache_tag));
		memset((void *)&int_cache, -1,
			sizeof(struct flash_cache_delta_list_tag) *
			(MAX_DESCS + MAX_CHANS));
#endif
		break;
	default:
		nand_dbg_print(NAND_DBG_WARN,
			"Handling unexpected event code - 0x%x\n",
			event_code);
		event_code = ERR;
		break;
	}

	memcpy(g_pBTStartingCopy, g_pBlockTable,
		DeviceInfo.wDataBlockNum * sizeof(u32));
	memcpy(g_pWearCounterCopy, g_pWearCounter,
		DeviceInfo.wDataBlockNum * sizeof(u8));
	if (DeviceInfo.MLCDevice)
		memcpy(g_pReadCounterCopy, g_pReadCounter,
			DeviceInfo.wDataBlockNum * sizeof(u16));

	g_pBTDelta_Free = g_pBTDelta;
	ftl_cmd_cnt = 0;
	g_pNextBlockTable = g_pBlockTableCopies;
	cp_back_buf_idx = 0;

#if RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE
	memcpy((void *)&cache_start_copy, (void *)&Cache,
		sizeof(struct flash_cache_tag));
	memset((void *)&int_cache, -1,
		sizeof(struct flash_cache_delta_list_tag) *
		(MAX_DESCS + MAX_CHANS));
#endif

	return event_code;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     glob_ftl_execute_cmds
* Inputs:       none
* Outputs:      none
* Description:  pass thru to LLD
***************************************************************/
u16 glob_ftl_execute_cmds(void)
{
	nand_dbg_print(NAND_DBG_TRACE,
		"glob_ftl_execute_cmds: ftl_cmd_cnt %u\n",
		(unsigned int)ftl_cmd_cnt);
	g_SBDCmdIndex = 0;
	return glob_lld_execute_cmds();
}

#endif

#if !CMD_DMA
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Read Immediate
* Inputs:         pointer to data
*                     address of data
* Outputs:      PASS / FAIL
* Description:  Reads one page of data into RAM directly from flash without
*       using or disturbing cache.It is assumed this function is called
*       with CMD-DMA disabled.
*****************************************************************/
int GLOB_FTL_Read_Immediate(u8 *read_data, u64 addr)
{
	int wResult = FAIL;
	u32 Block;
	u16 Page;
	u32 phy_blk;
	u32 *pbt = (u32 *)g_pBlockTable;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	Block = BLK_FROM_ADDR(addr);
	Page = PAGE_FROM_ADDR(addr, Block);

	if (!IS_SPARE_BLOCK(Block))
		return FAIL;

	phy_blk = pbt[Block];
	wResult = GLOB_LLD_Read_Page_Main(read_data, phy_blk, Page, 1);

	if (DeviceInfo.MLCDevice) {
		g_pReadCounter[phy_blk - DeviceInfo.wSpectraStartBlock]++;
		if (g_pReadCounter[phy_blk - DeviceInfo.wSpectraStartBlock]
			>= MAX_READ_COUNTER)
			FTL_Read_Disturbance(phy_blk);
		if (g_cBlockTableStatus != IN_PROGRESS_BLOCK_TABLE) {
			g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
			FTL_Write_IN_Progress_Block_Table_Page();
		}
	}

	return wResult;
}
#endif

#ifdef SUPPORT_BIG_ENDIAN
/*********************************************************************
* Function:     FTL_Invert_Block_Table
* Inputs:       none
* Outputs:      none
* Description:  Re-format the block table in ram based on BIG_ENDIAN and
*                     LARGE_BLOCKNUM if necessary
**********************************************************************/
static void FTL_Invert_Block_Table(void)
{
	u32 i;
	u32 *pbt = (u32 *)g_pBlockTable;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

#ifdef SUPPORT_LARGE_BLOCKNUM
	for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
		pbt[i] = INVERTUINT32(pbt[i]);
		g_pWearCounter[i] = INVERTUINT32(g_pWearCounter[i]);
	}
#else
	for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
		pbt[i] = INVERTUINT16(pbt[i]);
		g_pWearCounter[i] = INVERTUINT16(g_pWearCounter[i]);
	}
#endif
}
#endif

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Flash_Init
* Inputs:       none
* Outputs:      PASS=0 / FAIL=0x01 (based on read ID)
* Description:  The flash controller is initialized
*               The flash device is reset
*               Perform a flash READ ID command to confirm that a
*                   valid device is attached and active.
*                   The DeviceInfo structure gets filled in
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Flash_Init(void)
{
	int status = FAIL;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	g_SBDCmdIndex = 0;

	GLOB_LLD_Flash_Init();

	status = GLOB_LLD_Read_Device_ID();

	return status;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Inputs:       none
* Outputs:      PASS=0 / FAIL=0x01 (based on read ID)
* Description:  The flash controller is released
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Flash_Release(void)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	return GLOB_LLD_Flash_Release();
}


/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Cache_Release
* Inputs:       none
* Outputs:      none
* Description:  release all allocated memory in GLOB_FTL_Init
*               (allocated in GLOB_FTL_Init)
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
void GLOB_FTL_Cache_Release(void)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	free_memory();
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_If_Hit
* Inputs:       Page Address
* Outputs:      Block number/UNHIT BLOCK
* Description:  Determines if the addressed page is in cache
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u16 FTL_Cache_If_Hit(u64 page_addr)
{
	u16 item;
	u64 addr;
	int i;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	item = UNHIT_CACHE_ITEM;
	for (i = 0; i < CACHE_ITEM_NUM; i++) {
		addr = Cache.array[i].address;
		if ((page_addr >= addr) &&
			(page_addr < (addr + Cache.cache_item_size))) {
			item = i;
			break;
		}
	}

	return item;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Calculate_LRU
* Inputs:       None
* Outputs:      None
* Description:  Calculate the least recently block in a cache and record its
*               index in LRU field.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static void FTL_Calculate_LRU(void)
{
	u16 i, bCurrentLRU, bTempCount;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	bCurrentLRU = 0;
	bTempCount = MAX_WORD_VALUE;

	for (i = 0; i < CACHE_ITEM_NUM; i++) {
		if (Cache.array[i].use_cnt < bTempCount) {
			bCurrentLRU = i;
			bTempCount = Cache.array[i].use_cnt;
		}
	}

	Cache.LRU = bCurrentLRU;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Read_Page
* Inputs:       pointer to read buffer, logical address and cache item number
* Outputs:      None
* Description:  Read the page from the cached block addressed by blocknumber
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static void FTL_Cache_Read_Page(u8 *data_buf, u64 logic_addr, u16 cache_item)
{
	u8 *start_addr;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	start_addr = Cache.array[cache_item].buf;
	start_addr += (u32)(((logic_addr - Cache.array[cache_item].address) >>
		DeviceInfo.nBitsInPageDataSize) * DeviceInfo.wPageDataSize);

#if CMD_DMA
	GLOB_LLD_MemCopy_CMD(data_buf, start_addr,
			DeviceInfo.wPageDataSize, 0);
	ftl_cmd_cnt++;
#else
	memcpy(data_buf, start_addr, DeviceInfo.wPageDataSize);
#endif

	if (Cache.array[cache_item].use_cnt < MAX_WORD_VALUE)
		Cache.array[cache_item].use_cnt++;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Read_All
* Inputs:       pointer to read buffer,block address
* Outputs:      PASS=0 / FAIL =1
* Description:  It reads pages in cache
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Cache_Read_All(u8 *pData, u64 phy_addr)
{
	int wResult = PASS;
	u32 Block;
	u32 lba;
	u16 Page;
	u16 PageCount;
	u32 *pbt = (u32 *)g_pBlockTable;
	u32 i;

	Block = BLK_FROM_ADDR(phy_addr);
	Page = PAGE_FROM_ADDR(phy_addr, Block);
	PageCount = Cache.pages_per_item;

	nand_dbg_print(NAND_DBG_DEBUG,
			"%s, Line %d, Function: %s, Block: 0x%x\n",
			__FILE__, __LINE__, __func__, Block);

	lba = 0xffffffff;
	for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
		if ((pbt[i] & (~BAD_BLOCK)) == Block) {
			lba = i;
			if (IS_SPARE_BLOCK(i) || IS_BAD_BLOCK(i) ||
				IS_DISCARDED_BLOCK(i)) {
				/* Add by yunpeng -2008.12.3 */
#if CMD_DMA
				GLOB_LLD_MemCopy_CMD(pData, g_temp_buf,
				PageCount * DeviceInfo.wPageDataSize, 0);
				ftl_cmd_cnt++;
#else
				memset(pData, 0xFF,
					PageCount * DeviceInfo.wPageDataSize);
#endif
				return wResult;
			} else {
				continue; /* break ?? */
			}
		}
	}

	if (0xffffffff == lba)
		printk(KERN_ERR "FTL_Cache_Read_All: Block is not found in BT\n");

#if CMD_DMA
	wResult = GLOB_LLD_Read_Page_Main_cdma(pData, Block, Page,
			PageCount, LLD_CMD_FLAG_MODE_CDMA);
	if (DeviceInfo.MLCDevice) {
		g_pReadCounter[Block - DeviceInfo.wSpectraStartBlock]++;
		nand_dbg_print(NAND_DBG_DEBUG,
			       "Read Counter modified in ftl_cmd_cnt %u"
				" Block %u Counter%u\n",
			       ftl_cmd_cnt, (unsigned int)Block,
			       g_pReadCounter[Block -
			       DeviceInfo.wSpectraStartBlock]);

		p_BTableChangesDelta =
			(struct BTableChangesDelta *)g_pBTDelta_Free;
		g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
		p_BTableChangesDelta->ftl_cmd_cnt = ftl_cmd_cnt;
		p_BTableChangesDelta->RC_Index =
			Block - DeviceInfo.wSpectraStartBlock;
		p_BTableChangesDelta->RC_Entry_Value =
			g_pReadCounter[Block - DeviceInfo.wSpectraStartBlock];
		p_BTableChangesDelta->ValidFields = 0xC0;

		ftl_cmd_cnt++;

		if (g_pReadCounter[Block - DeviceInfo.wSpectraStartBlock] >=
		    MAX_READ_COUNTER)
			FTL_Read_Disturbance(Block);
		if (g_cBlockTableStatus != IN_PROGRESS_BLOCK_TABLE) {
			g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
			FTL_Write_IN_Progress_Block_Table_Page();
		}
	} else {
		ftl_cmd_cnt++;
	}
#else
	wResult = GLOB_LLD_Read_Page_Main(pData, Block, Page, PageCount);
	if (wResult == FAIL)
		return wResult;

	if (DeviceInfo.MLCDevice) {
		g_pReadCounter[Block - DeviceInfo.wSpectraStartBlock]++;
		if (g_pReadCounter[Block - DeviceInfo.wSpectraStartBlock] >=
						MAX_READ_COUNTER)
			FTL_Read_Disturbance(Block);
		if (g_cBlockTableStatus != IN_PROGRESS_BLOCK_TABLE) {
			g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
			FTL_Write_IN_Progress_Block_Table_Page();
		}
	}
#endif
	return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Write_All
* Inputs:       pointer to cache in sys memory
*               address of free block in flash
* Outputs:      PASS=0 / FAIL=1
* Description:  writes all the pages of the block in cache to flash
*
*               NOTE:need to make sure this works ok when cache is limited
*               to a partial block. This is where copy-back would be
*               activated.  This would require knowing which pages in the
*               cached block are clean/dirty.Right now we only know if
*               the whole block is clean/dirty.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Cache_Write_All(u8 *pData, u64 blk_addr)
{
	u16 wResult = PASS;
	u32 Block;
	u16 Page;
	u16 PageCount;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	nand_dbg_print(NAND_DBG_DEBUG, "This block %d going to be written "
		"on %d\n", cache_block_to_write,
		(u32)(blk_addr >> DeviceInfo.nBitsInBlockDataSize));

	Block = BLK_FROM_ADDR(blk_addr);
	Page = PAGE_FROM_ADDR(blk_addr, Block);
	PageCount = Cache.pages_per_item;

#if CMD_DMA
	if (FAIL == GLOB_LLD_Write_Page_Main_cdma(pData,
					Block, Page, PageCount)) {
		nand_dbg_print(NAND_DBG_WARN,
			"NAND Program fail in %s, Line %d, "
			"Function: %s, new Bad Block %d generated! "
			"Need Bad Block replacing.\n",
			__FILE__, __LINE__, __func__, Block);
		wResult = FAIL;
	}
	ftl_cmd_cnt++;
#else
	if (FAIL == GLOB_LLD_Write_Page_Main(pData, Block, Page, PageCount)) {
		nand_dbg_print(NAND_DBG_WARN, "NAND Program fail in %s,"
			" Line %d, Function %s, new Bad Block %d generated!"
			"Need Bad Block replacing.\n",
			__FILE__, __LINE__, __func__, Block);
		wResult = FAIL;
	}
#endif
	return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Copy_Block
* Inputs:       source block address
*               Destination block address
* Outputs:      PASS=0 / FAIL=1
* Description:  used only for static wear leveling to move the block
*               containing static data to new blocks(more worn)
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int FTL_Copy_Block(u64 old_blk_addr, u64 blk_addr)
{
	int i, r1, r2, wResult = PASS;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	for (i = 0; i < DeviceInfo.wPagesPerBlock; i += Cache.pages_per_item) {
		r1 = FTL_Cache_Read_All(g_pTempBuf, old_blk_addr +
					i * DeviceInfo.wPageDataSize);
		r2 = FTL_Cache_Write_All(g_pTempBuf, blk_addr +
					i * DeviceInfo.wPageDataSize);
		if ((ERR == r1) || (FAIL == r2)) {
			wResult = FAIL;
			break;
		}
	}

	return wResult;
}

/* Search the block table to find out the least wear block and then return it */
static u32 find_least_worn_blk_for_l2_cache(void)
{
	int i;
	u32 *pbt = (u32 *)g_pBlockTable;
	u8 least_wear_cnt = MAX_BYTE_VALUE;
	u32 least_wear_blk_idx = MAX_U32_VALUE;
	u32 phy_idx;

	for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
		if (IS_SPARE_BLOCK(i)) {
			phy_idx = (u32)((~BAD_BLOCK) & pbt[i]);
			if (phy_idx > DeviceInfo.wSpectraEndBlock)
				printk(KERN_ERR "find_least_worn_blk_for_l2_cache: "
					"Too big phy block num (%d)\n", phy_idx);
			if (g_pWearCounter[phy_idx -DeviceInfo.wSpectraStartBlock] < least_wear_cnt) {
				least_wear_cnt = g_pWearCounter[phy_idx - DeviceInfo.wSpectraStartBlock];
				least_wear_blk_idx = i;
			}
		}
	}

	nand_dbg_print(NAND_DBG_WARN,
		"find_least_worn_blk_for_l2_cache: "
		"find block %d with least worn counter (%d)\n",
		least_wear_blk_idx, least_wear_cnt);

	return least_wear_blk_idx;
}



/* Get blocks for Level2 Cache */
static int get_l2_cache_blks(void)
{
	int n;
	u32 blk;
	u32 *pbt = (u32 *)g_pBlockTable;

	for (n = 0; n < BLK_NUM_FOR_L2_CACHE; n++) {
		blk = find_least_worn_blk_for_l2_cache();
		if (blk >= DeviceInfo.wDataBlockNum) {
			nand_dbg_print(NAND_DBG_WARN,
				"find_least_worn_blk_for_l2_cache: "
				"No enough free NAND blocks (n: %d) for L2 Cache!\n", n);
			return FAIL;
		}
		/* Tag the free block as discard in block table */
		pbt[blk] = (pbt[blk] & (~BAD_BLOCK)) | DISCARD_BLOCK;
		/* Add the free block to the L2 Cache block array */
		cache_l2.blk_array[n] = pbt[blk] & (~BAD_BLOCK);
	}

	return PASS;
}

static int erase_l2_cache_blocks(void)
{
	int i, ret = PASS;
	u32 pblk, lblk = BAD_BLOCK;
	u64 addr;
	u32 *pbt = (u32 *)g_pBlockTable;

	nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	for (i = 0; i < BLK_NUM_FOR_L2_CACHE; i++) {
		pblk = cache_l2.blk_array[i];

		/* If the L2 cache block is invalid, then just skip it */
		if (MAX_U32_VALUE == pblk)
			continue;

		BUG_ON(pblk > DeviceInfo.wSpectraEndBlock);

		addr = (u64)pblk << DeviceInfo.nBitsInBlockDataSize;
		if (PASS == GLOB_FTL_Block_Erase(addr)) {
			/* Get logical block number of the erased block */
			lblk = FTL_Get_Block_Index(pblk);
			BUG_ON(BAD_BLOCK == lblk);
			/* Tag it as free in the block table */
			pbt[lblk] &= (u32)(~DISCARD_BLOCK);
			pbt[lblk] |= (u32)(SPARE_BLOCK);
		} else {
			MARK_BLOCK_AS_BAD(pbt[lblk]);
			ret = ERR;
		}
	}

	return ret;
}

/*
 * Merge the valid data page in the L2 cache blocks into NAND.
*/
static int flush_l2_cache(void)
{
	struct list_head *p;
	struct spectra_l2_cache_list *pnd, *tmp_pnd;
	u32 *pbt = (u32 *)g_pBlockTable;
	u32 phy_blk, l2_blk;
	u64 addr;
	u16 l2_page;
	int i, ret = PASS;

	nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	if (list_empty(&cache_l2.table.list)) /* No data to flush */
		return ret;

	//dump_cache_l2_table();

	if (IN_PROGRESS_BLOCK_TABLE != g_cBlockTableStatus) {
		g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
		FTL_Write_IN_Progress_Block_Table_Page();
	}

	list_for_each(p, &cache_l2.table.list) {
		pnd = list_entry(p, struct spectra_l2_cache_list, list);
		if (IS_SPARE_BLOCK(pnd->logical_blk_num) ||
			IS_BAD_BLOCK(pnd->logical_blk_num) ||
			IS_DISCARDED_BLOCK(pnd->logical_blk_num)) {
			nand_dbg_print(NAND_DBG_WARN, "%s, Line %d\n", __FILE__, __LINE__);
			memset(cache_l2_blk_buf, 0xff, DeviceInfo.wPagesPerBlock * DeviceInfo.wPageDataSize);			
		} else {
			nand_dbg_print(NAND_DBG_WARN, "%s, Line %d\n", __FILE__, __LINE__);
			phy_blk = pbt[pnd->logical_blk_num] & (~BAD_BLOCK);
			ret = GLOB_LLD_Read_Page_Main(cache_l2_blk_buf,
				phy_blk, 0, DeviceInfo.wPagesPerBlock);
			if (ret == FAIL) {
				printk(KERN_ERR "Read NAND page fail in %s, Line %d\n", __FILE__, __LINE__);
			}
		}

		for (i = 0; i < DeviceInfo.wPagesPerBlock; i++) {
			if (pnd->pages_array[i] != MAX_U32_VALUE) {
				l2_blk = cache_l2.blk_array[(pnd->pages_array[i] >> 16) & 0xffff];
				l2_page = pnd->pages_array[i] & 0xffff;
				ret = GLOB_LLD_Read_Page_Main(cache_l2_page_buf, l2_blk, l2_page, 1);
				if (ret == FAIL) {
					printk(KERN_ERR "Read NAND page fail in %s, Line %d\n", __FILE__, __LINE__);
				}
				memcpy(cache_l2_blk_buf + i * DeviceInfo.wPageDataSize, cache_l2_page_buf, DeviceInfo.wPageDataSize);
			}
		}

		/* Find a free block and tag the original block as discarded */
		addr = (u64)pnd->logical_blk_num << DeviceInfo.nBitsInBlockDataSize;
		ret = FTL_Replace_Block(addr);
		if (ret == FAIL) {
			printk(KERN_ERR "FTL_Replace_Block fail in %s, Line %d\n", __FILE__, __LINE__);
		}

		/* Write back the updated data into NAND */
		phy_blk = pbt[pnd->logical_blk_num] & (~BAD_BLOCK);
		if (FAIL == GLOB_LLD_Write_Page_Main(cache_l2_blk_buf, phy_blk, 0, DeviceInfo.wPagesPerBlock)) {
			nand_dbg_print(NAND_DBG_WARN,
				"Program NAND block %d fail in %s, Line %d\n",
				phy_blk, __FILE__, __LINE__);
			/* This may not be really a bad block. So just tag it as discarded. */
			/* Then it has a chance to be erased when garbage collection. */
			/* If it is really bad, then the erase will fail and it will be marked */
			/* as bad then. Otherwise it will be marked as free and can be used again */
			MARK_BLK_AS_DISCARD(pbt[pnd->logical_blk_num]);
			/* Find another free block and write it again */
			FTL_Replace_Block(addr);
			phy_blk = pbt[pnd->logical_blk_num] & (~BAD_BLOCK);
			if (FAIL == GLOB_LLD_Write_Page_Main(cache_l2_blk_buf, phy_blk, 0, DeviceInfo.wPagesPerBlock)) {
				printk(KERN_ERR "Failed to write back block %d when flush L2 cache."
					"Some data will be lost!\n", phy_blk);
				MARK_BLOCK_AS_BAD(pbt[pnd->logical_blk_num]);
			}
		} else {
			/* tag the new free block as used block */
			pbt[pnd->logical_blk_num] &= (~SPARE_BLOCK);
		}
	}

	/* Destroy the L2 Cache table and free the memory of all nodes */
	list_for_each_entry_safe(pnd, tmp_pnd, &cache_l2.table.list, list) {
		list_del(&pnd->list);
		kfree(pnd);
	}

	/* Erase discard L2 cache blocks */
	if (erase_l2_cache_blocks() != PASS)
		nand_dbg_print(NAND_DBG_WARN,
			" Erase L2 cache blocks error in %s, Line %d\n",
			__FILE__, __LINE__);

	/* Init the Level2 Cache data structure */
	for (i = 0; i < BLK_NUM_FOR_L2_CACHE; i++)
		cache_l2.blk_array[i] = MAX_U32_VALUE;
	cache_l2.cur_blk_idx = 0;
	cache_l2.cur_page_num = 0;
	INIT_LIST_HEAD(&cache_l2.table.list);
	cache_l2.table.logical_blk_num = MAX_U32_VALUE;

	return ret;
}

/*
 * Write back a changed victim cache item to the Level2 Cache
 * and update the L2 Cache table to map the change.
 * If the L2 Cache is full, then start to do the L2 Cache flush.
*/
static int write_back_to_l2_cache(u8 *buf, u64 logical_addr)
{
	u32 logical_blk_num;
	u16 logical_page_num;
	struct list_head *p;
	struct spectra_l2_cache_list *pnd, *pnd_new;
	u32 node_size;
	int i, found;

	nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	/*
	 * If Level2 Cache table is empty, then it means either:
	 * 1. This is the first time that the function called after FTL_init
	 * or
	 * 2. The Level2 Cache has just been flushed
	 *
	 * So, 'steal' some free blocks from NAND for L2 Cache using
	 * by just mask them as discard in the block table
	*/
	if (list_empty(&cache_l2.table.list)) {
		BUG_ON(cache_l2.cur_blk_idx != 0);
		BUG_ON(cache_l2.cur_page_num!= 0);
		BUG_ON(cache_l2.table.logical_blk_num != MAX_U32_VALUE);
		if (FAIL == get_l2_cache_blks()) {
			GLOB_FTL_Garbage_Collection();
			if (FAIL == get_l2_cache_blks()) {
				printk(KERN_ALERT "Fail to get L2 cache blks!\n");
				return FAIL;
			}
		}
	}

	logical_blk_num = BLK_FROM_ADDR(logical_addr);
	logical_page_num = PAGE_FROM_ADDR(logical_addr, logical_blk_num);
	BUG_ON(logical_blk_num == MAX_U32_VALUE);

	/* Write the cache item data into the current position of L2 Cache */
#if CMD_DMA
	/*
	 * TODO
	 */
#else
	if (FAIL == GLOB_LLD_Write_Page_Main(buf,
		cache_l2.blk_array[cache_l2.cur_blk_idx],
		cache_l2.cur_page_num, 1)) {
		nand_dbg_print(NAND_DBG_WARN, "NAND Program fail in "
			"%s, Line %d, new Bad Block %d generated!\n",
			__FILE__, __LINE__,
			cache_l2.blk_array[cache_l2.cur_blk_idx]);

		/* TODO: tag the current block as bad and try again */

		return FAIL;
	}
#endif

	/* 
	 * Update the L2 Cache table.
	 *
	 * First seaching in the table to see whether the logical block
	 * has been mapped. If not, then kmalloc a new node for the
	 * logical block, fill data, and then insert it to the list.
	 * Otherwise, just update the mapped node directly.
	 */
	found = 0;
	list_for_each(p, &cache_l2.table.list) {
		pnd = list_entry(p, struct spectra_l2_cache_list, list);
		if (pnd->logical_blk_num == logical_blk_num) {
			pnd->pages_array[logical_page_num] =
				(cache_l2.cur_blk_idx << 16) |
				cache_l2.cur_page_num;
			found = 1;
			break;
		}
	}
	if (!found) { /* Create new node for the logical block here */

		/* The logical pages to physical pages map array is
		 * located at the end of struct spectra_l2_cache_list.
		 */ 
		node_size = sizeof(struct spectra_l2_cache_list) +
			sizeof(u32) * DeviceInfo.wPagesPerBlock;
		pnd_new = kmalloc(node_size, GFP_ATOMIC);
		if (!pnd_new) {
			printk(KERN_ERR "Failed to kmalloc in %s Line %d\n",
				__FILE__, __LINE__);
			/* 
			 * TODO: Need to flush all the L2 cache into NAND ASAP
			 * since no memory available here
			 */
		}
		pnd_new->logical_blk_num = logical_blk_num;
		for (i = 0; i < DeviceInfo.wPagesPerBlock; i++)
			pnd_new->pages_array[i] = MAX_U32_VALUE;
		pnd_new->pages_array[logical_page_num] =
			(cache_l2.cur_blk_idx << 16) | cache_l2.cur_page_num;
		list_add(&pnd_new->list, &cache_l2.table.list);
	}

	/* Increasing the current position pointer of the L2 Cache */
	cache_l2.cur_page_num++;
	if (cache_l2.cur_page_num >= DeviceInfo.wPagesPerBlock) {
		cache_l2.cur_blk_idx++;
		if (cache_l2.cur_blk_idx >= BLK_NUM_FOR_L2_CACHE) {
			/* The L2 Cache is full. Need to flush it now */
			nand_dbg_print(NAND_DBG_WARN,
				"L2 Cache is full, will start to flush it\n");
			flush_l2_cache();
		} else {
			cache_l2.cur_page_num = 0;
		}
	}

	return PASS;
}

/*
 * Seach in the Level2 Cache table to find the cache item.
 * If find, read the data from the NAND page of L2 Cache,
 * Otherwise, return FAIL.
 */
static int search_l2_cache(u8 *buf, u64 logical_addr)
{
	u32 logical_blk_num;
	u16 logical_page_num;
	struct list_head *p;
	struct spectra_l2_cache_list *pnd;
	u32 tmp = MAX_U32_VALUE;
	u32 phy_blk;
	u16 phy_page;
	int ret = FAIL;

	logical_blk_num = BLK_FROM_ADDR(logical_addr);
	logical_page_num = PAGE_FROM_ADDR(logical_addr, logical_blk_num);

	list_for_each(p, &cache_l2.table.list) {
		pnd = list_entry(p, struct spectra_l2_cache_list, list);
		if (pnd->logical_blk_num == logical_blk_num) {
			tmp = pnd->pages_array[logical_page_num];
			break;
		}
	}

	if (tmp != MAX_U32_VALUE) { /* Found valid map */
		phy_blk = cache_l2.blk_array[(tmp >> 16) & 0xFFFF];
		phy_page = tmp & 0xFFFF;
#if CMD_DMA
		/* TODO */
#else
		ret = GLOB_LLD_Read_Page_Main(buf, phy_blk, phy_page, 1);
#endif
	}

	return ret;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Write_Page
* Inputs:       Pointer to buffer, page address, cache block number
* Outputs:      PASS=0 / FAIL=1
* Description:  It writes the data in Cache Block
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static void FTL_Cache_Write_Page(u8 *pData, u64 page_addr,
				u8 cache_blk, u16 flag)
{
	u8 *pDest;
	u64 addr;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	addr = Cache.array[cache_blk].address;
	pDest = Cache.array[cache_blk].buf;

	pDest += (unsigned long)(page_addr - addr);
	Cache.array[cache_blk].changed = SET;
#if CMD_DMA
#if RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE
	int_cache[ftl_cmd_cnt].item = cache_blk;
	int_cache[ftl_cmd_cnt].cache.address =
			Cache.array[cache_blk].address;
	int_cache[ftl_cmd_cnt].cache.changed =
			Cache.array[cache_blk].changed;
#endif
	GLOB_LLD_MemCopy_CMD(pDest, pData, DeviceInfo.wPageDataSize, flag);
	ftl_cmd_cnt++;
#else
	memcpy(pDest, pData, DeviceInfo.wPageDataSize);
#endif
	if (Cache.array[cache_blk].use_cnt < MAX_WORD_VALUE)
		Cache.array[cache_blk].use_cnt++;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Write
* Inputs:       none
* Outputs:      PASS=0 / FAIL=1
* Description:  It writes least frequently used Cache block to flash if it
*               has been changed
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Cache_Write(void)
{
	int i, bResult = PASS;
	u16 bNO, least_count = 0xFFFF;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	FTL_Calculate_LRU();

	bNO = Cache.LRU;
	nand_dbg_print(NAND_DBG_DEBUG, "FTL_Cache_Write: "
		"Least used cache block is %d\n", bNO);

	if (Cache.array[bNO].changed != SET)
		return bResult;

	nand_dbg_print(NAND_DBG_DEBUG, "FTL_Cache_Write: Cache"
		" Block %d containing logical block %d is dirty\n",
		bNO,
		(u32)(Cache.array[bNO].address >>
		DeviceInfo.nBitsInBlockDataSize));
#if CMD_DMA
#if RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE
	int_cache[ftl_cmd_cnt].item = bNO;
	int_cache[ftl_cmd_cnt].cache.address =
				Cache.array[bNO].address;
	int_cache[ftl_cmd_cnt].cache.changed = CLEAR;
#endif
#endif
	bResult = write_back_to_l2_cache(Cache.array[bNO].buf,
			Cache.array[bNO].address);
	if (bResult != ERR)
		Cache.array[bNO].changed = CLEAR;

	least_count = Cache.array[bNO].use_cnt;

	for (i = 0; i < CACHE_ITEM_NUM; i++) {
		if (i == bNO)
			continue;
		if (Cache.array[i].use_cnt > 0)
			Cache.array[i].use_cnt -= least_count;
	}

	return bResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Cache_Read
* Inputs:       Page address
* Outputs:      PASS=0 / FAIL=1
* Description:  It reads the block from device in Cache Block
*               Set the LRU count to 1
*               Mark the Cache Block as clean
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Cache_Read(u64 logical_addr)
{
	u64 item_addr, phy_addr;
	u16 num;
	int ret;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	num = Cache.LRU; /* The LRU cache item will be overwritten */

	item_addr = (u64)GLOB_u64_Div(logical_addr, Cache.cache_item_size) *
		Cache.cache_item_size;
	Cache.array[num].address = item_addr;
	Cache.array[num].use_cnt = 1;
	Cache.array[num].changed = CLEAR;

#if CMD_DMA
#if RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE
	int_cache[ftl_cmd_cnt].item = num;
	int_cache[ftl_cmd_cnt].cache.address =
			Cache.array[num].address;
	int_cache[ftl_cmd_cnt].cache.changed =
			Cache.array[num].changed;
#endif
#endif
	/*
	 * Search in L2 Cache. If hit, fill data into L1 Cache item buffer,
	 * Otherwise, read it from NAND
	 */
	ret = search_l2_cache(Cache.array[num].buf, logical_addr);
	if (PASS == ret) /* Hit in L2 Cache */
		return ret;

	/* Compute the physical start address of NAND device according to */
	/* the logical start address of the cache item (LRU cache item) */
	phy_addr = FTL_Get_Physical_Block_Addr(item_addr) +
		GLOB_u64_Remainder(item_addr, 2);

	return FTL_Cache_Read_All(Cache.array[num].buf, phy_addr);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Check_Block_Table
* Inputs:       ?
* Outputs:      PASS=0 / FAIL=1
* Description:  It checks the correctness of each block table entry
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Check_Block_Table(int wOldTable)
{
	u32 i;
	int wResult = PASS;
	u32 blk_idx;
	u32 *pbt = (u32 *)g_pBlockTable;
	u8 *pFlag = flag_check_blk_table;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (NULL != pFlag) {
		memset(pFlag, FAIL, DeviceInfo.wDataBlockNum);
		for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
			blk_idx = (u32)(pbt[i] & (~BAD_BLOCK));

			/*
			 * 20081006/KBV - Changed to pFlag[i] reference
			 * to avoid buffer overflow
			 */

			/*
			 * 2008-10-20 Yunpeng Note: This change avoid
			 * buffer overflow, but changed function of
			 * the code, so it should be re-write later
			 */
			if ((blk_idx > DeviceInfo.wSpectraEndBlock) ||
				PASS == pFlag[i]) {
				wResult = FAIL;
				break;
			} else {
				pFlag[i] = PASS;
			}
		}
	}

	return wResult;
}


/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Write_Block_Table
* Inputs:       flasg
* Outputs:      0=Block Table was updated. No write done. 1=Block write needs to
* happen. -1 Error
* Description:  It writes the block table
*               Block table always mapped to LBA 0 which inturn mapped
*               to any physical block
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Write_Block_Table(int wForce)
{
	u32 *pbt = (u32 *)g_pBlockTable;
	int wSuccess = PASS;
	u32 wTempBlockTableIndex;
	u16 bt_pages, new_bt_offset;
	u8 blockchangeoccured = 0;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	bt_pages = FTL_Get_Block_Table_Flash_Size_Pages();

	if (IN_PROGRESS_BLOCK_TABLE != g_cBlockTableStatus)
		return 0;

	if (PASS == wForce) {
		g_wBlockTableOffset =
			(u16)(DeviceInfo.wPagesPerBlock - bt_pages);
#if CMD_DMA
		p_BTableChangesDelta =
			(struct BTableChangesDelta *)g_pBTDelta_Free;
		g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

		p_BTableChangesDelta->ftl_cmd_cnt = ftl_cmd_cnt;
		p_BTableChangesDelta->g_wBlockTableOffset =
			g_wBlockTableOffset;
		p_BTableChangesDelta->ValidFields = 0x01;
#endif
	}

	nand_dbg_print(NAND_DBG_DEBUG,
		"Inside FTL_Write_Block_Table: block %d Page:%d\n",
		g_wBlockTableIndex, g_wBlockTableOffset);

	do {
		new_bt_offset = g_wBlockTableOffset + bt_pages + 1;
		if ((0 == (new_bt_offset % DeviceInfo.wPagesPerBlock)) ||
			(new_bt_offset > DeviceInfo.wPagesPerBlock) ||
			(FAIL == wSuccess)) {
			wTempBlockTableIndex = FTL_Replace_Block_Table();
			if (BAD_BLOCK == wTempBlockTableIndex)
				return ERR;
			if (!blockchangeoccured) {
				bt_block_changed = 1;
				blockchangeoccured = 1;
			}

			g_wBlockTableIndex = wTempBlockTableIndex;
			g_wBlockTableOffset = 0;
			pbt[BLOCK_TABLE_INDEX] = g_wBlockTableIndex;
#if CMD_DMA
			p_BTableChangesDelta =
				(struct BTableChangesDelta *)g_pBTDelta_Free;
			g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

			p_BTableChangesDelta->ftl_cmd_cnt =
				    ftl_cmd_cnt;
			p_BTableChangesDelta->g_wBlockTableOffset =
				    g_wBlockTableOffset;
			p_BTableChangesDelta->g_wBlockTableIndex =
				    g_wBlockTableIndex;
			p_BTableChangesDelta->ValidFields = 0x03;

			p_BTableChangesDelta =
				(struct BTableChangesDelta *)g_pBTDelta_Free;
			g_pBTDelta_Free +=
				sizeof(struct BTableChangesDelta);

			p_BTableChangesDelta->ftl_cmd_cnt =
				    ftl_cmd_cnt;
			p_BTableChangesDelta->BT_Index =
				    BLOCK_TABLE_INDEX;
			p_BTableChangesDelta->BT_Entry_Value =
				    pbt[BLOCK_TABLE_INDEX];
			p_BTableChangesDelta->ValidFields = 0x0C;
#endif
		}

		wSuccess = FTL_Write_Block_Table_Data();
		if (FAIL == wSuccess)
			MARK_BLOCK_AS_BAD(pbt[BLOCK_TABLE_INDEX]);
	} while (FAIL == wSuccess);

	g_cBlockTableStatus = CURRENT_BLOCK_TABLE;

	return 1;
}

static int  force_format_nand(void)
{
	u32 i;

	/* Force erase the whole unprotected physical partiton of NAND */
	printk(KERN_ALERT "Start to force erase whole NAND device ...\n");
	printk(KERN_ALERT "From phyical block %d to %d\n",
		DeviceInfo.wSpectraStartBlock, DeviceInfo.wSpectraEndBlock);
	for (i = DeviceInfo.wSpectraStartBlock; i <= DeviceInfo.wSpectraEndBlock; i++) {
		if (GLOB_LLD_Erase_Block(i))
			printk(KERN_ERR "Failed to force erase NAND block %d\n", i);
	}
	printk(KERN_ALERT "Force Erase ends. Please reboot the system ...\n");
	while(1);

	return PASS;
}

int GLOB_FTL_Flash_Format(void)
{
	//return FTL_Format_Flash(1);
	return force_format_nand();

}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Search_Block_Table_IN_Block
* Inputs:       Block Number
*               Pointer to page
* Outputs:      PASS / FAIL
*               Page contatining the block table
* Description:  It searches the block table in the block
*               passed as an argument.
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Search_Block_Table_IN_Block(u32 BT_Block,
						u8 BT_Tag, u16 *Page)
{
	u16 i, j, k;
	u16 Result = PASS;
	u16 Last_IPF = 0;
	u8  BT_Found = 0;
	u8 *tagarray;
	u8 *tempbuf = tmp_buf_search_bt_in_block;
	u8 *pSpareBuf = spare_buf_search_bt_in_block;
	u8 *pSpareBufBTLastPage = spare_buf_bt_search_bt_in_block;
	u8 bt_flag_last_page = 0xFF;
	u8 search_in_previous_pages = 0;
	u16 bt_pages;

	nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	nand_dbg_print(NAND_DBG_DEBUG,
		       "Searching block table in %u block\n",
		       (unsigned int)BT_Block);

	bt_pages = FTL_Get_Block_Table_Flash_Size_Pages();

	for (i = bt_pages; i < DeviceInfo.wPagesPerBlock;
				i += (bt_pages + 1)) {
		nand_dbg_print(NAND_DBG_DEBUG,
			       "Searching last IPF: %d\n", i);
		Result = GLOB_LLD_Read_Page_Main_Polling(tempbuf,
							BT_Block, i, 1);

		if (0 == memcmp(tempbuf, g_pIPF, DeviceInfo.wPageDataSize)) {
			if ((i + bt_pages + 1) < DeviceInfo.wPagesPerBlock) {
				continue;
			} else {
				search_in_previous_pages = 1;
				Last_IPF = i;
			}
		}

		if (!search_in_previous_pages) {
			if (i != bt_pages) {
				i -= (bt_pages + 1);
				Last_IPF = i;
			}
		}

		if (0 == Last_IPF)
			break;

		if (!search_in_previous_pages) {
			i = i + 1;
			nand_dbg_print(NAND_DBG_DEBUG,
				"Reading the spare area of Block %u Page %u",
				(unsigned int)BT_Block, i);
			Result = GLOB_LLD_Read_Page_Spare(pSpareBuf,
							BT_Block, i, 1);
			nand_dbg_print(NAND_DBG_DEBUG,
				"Reading the spare area of Block %u Page %u",
				(unsigned int)BT_Block, i + bt_pages - 1);
			Result = GLOB_LLD_Read_Page_Spare(pSpareBufBTLastPage,
				BT_Block, i + bt_pages - 1, 1);

			k = 0;
			j = FTL_Extract_Block_Table_Tag(pSpareBuf, &tagarray);
			if (j) {
				for (; k < j; k++) {
					if (tagarray[k] == BT_Tag)
						break;
				}
			}

			if (k < j)
				bt_flag = tagarray[k];
			else
				Result = FAIL;

			if (Result == PASS) {
				k = 0;
				j = FTL_Extract_Block_Table_Tag(
					pSpareBufBTLastPage, &tagarray);
				if (j) {
					for (; k < j; k++) {
						if (tagarray[k] == BT_Tag)
							break;
					}
				}

				if (k < j)
					bt_flag_last_page = tagarray[k];
				else
					Result = FAIL;

				if (Result == PASS) {
					if (bt_flag == bt_flag_last_page) {
						nand_dbg_print(NAND_DBG_DEBUG,
							"Block table is found"
							" in page after IPF "
							"at block %d "
							"page %d\n",
							(int)BT_Block, i);
						BT_Found = 1;
						*Page  = i;
						g_cBlockTableStatus =
							CURRENT_BLOCK_TABLE;
						break;
					} else {
						Result = FAIL;
					}
				}
			}
		}

		if (search_in_previous_pages)
			i = i - bt_pages;
		else
			i = i - (bt_pages + 1);

		Result = PASS;

		nand_dbg_print(NAND_DBG_DEBUG,
			"Reading the spare area of Block %d Page %d",
			(int)BT_Block, i);

		Result = GLOB_LLD_Read_Page_Spare(pSpareBuf, BT_Block, i, 1);
		nand_dbg_print(NAND_DBG_DEBUG,
			"Reading the spare area of Block %u Page %u",
			(unsigned int)BT_Block, i + bt_pages - 1);

		Result = GLOB_LLD_Read_Page_Spare(pSpareBufBTLastPage,
					BT_Block, i + bt_pages - 1, 1);

		k = 0;
		j = FTL_Extract_Block_Table_Tag(pSpareBuf, &tagarray);
		if (j) {
			for (; k < j; k++) {
				if (tagarray[k] == BT_Tag)
					break;
			}
		}

		if (k < j)
			bt_flag = tagarray[k];
		else
			Result = FAIL;

		if (Result == PASS) {
			k = 0;
			j = FTL_Extract_Block_Table_Tag(pSpareBufBTLastPage,
						&tagarray);
			if (j) {
				for (; k < j; k++) {
					if (tagarray[k] == BT_Tag)
						break;
				}
			}

			if (k < j) {
				bt_flag_last_page = tagarray[k];
			} else {
				Result = FAIL;
				break;
			}

			if (Result == PASS) {
				if (bt_flag == bt_flag_last_page) {
					nand_dbg_print(NAND_DBG_DEBUG,
						"Block table is found "
						"in page prior to IPF "
						"at block %u page %d\n",
						(unsigned int)BT_Block, i);
					BT_Found = 1;
					*Page  = i;
					g_cBlockTableStatus =
						IN_PROGRESS_BLOCK_TABLE;
					break;
				} else {
					Result = FAIL;
					break;
				}
			}
		}
	}

	if (Result == FAIL) {
		if ((Last_IPF > bt_pages) && (i < Last_IPF) && (!BT_Found)) {
			BT_Found = 1;
			*Page = i - (bt_pages + 1);
		}
		if ((Last_IPF == bt_pages) && (i < Last_IPF) && (!BT_Found))
			goto func_return;
	}

	if (Last_IPF == 0) {
		i = 0;
		Result = PASS;
		nand_dbg_print(NAND_DBG_DEBUG, "Reading the spare area of "
			"Block %u Page %u", (unsigned int)BT_Block, i);

		Result = GLOB_LLD_Read_Page_Spare(pSpareBuf, BT_Block, i, 1);
		nand_dbg_print(NAND_DBG_DEBUG,
			"Reading the spare area of Block %u Page %u",
			(unsigned int)BT_Block, i + bt_pages - 1);
		Result = GLOB_LLD_Read_Page_Spare(pSpareBufBTLastPage,
					BT_Block, i + bt_pages - 1, 1);

		k = 0;
		j = FTL_Extract_Block_Table_Tag(pSpareBuf, &tagarray);
		if (j) {
			for (; k < j; k++) {
				if (tagarray[k] == BT_Tag)
					break;
			}
		}

		if (k < j)
			bt_flag = tagarray[k];
		else
			Result = FAIL;

		if (Result == PASS) {
			k = 0;
			j = FTL_Extract_Block_Table_Tag(pSpareBufBTLastPage,
							&tagarray);
			if (j) {
				for (; k < j; k++) {
					if (tagarray[k] == BT_Tag)
						break;
				}
			}

			if (k < j)
				bt_flag_last_page = tagarray[k];
			else
				Result = FAIL;

			if (Result == PASS) {
				if (bt_flag == bt_flag_last_page) {
					nand_dbg_print(NAND_DBG_DEBUG,
						"Block table is found "
						"in page after IPF at "
						"block %u page %u\n",
						(unsigned int)BT_Block,
						(unsigned int)i);
					BT_Found = 1;
					*Page  = i;
					g_cBlockTableStatus =
						CURRENT_BLOCK_TABLE;
					goto func_return;
				} else {
					Result = FAIL;
				}
			}
		}

		if (Result == FAIL)
			goto func_return;
	}
func_return:
	return Result;
}

u8 *get_blk_table_start_addr(void)
{
	return g_pBlockTable;
}

unsigned long get_blk_table_len(void)
{
	return DeviceInfo.wDataBlockNum * sizeof(u32);
}

u8 *get_wear_leveling_table_start_addr(void)
{
	return g_pWearCounter;
}

unsigned long get_wear_leveling_table_len(void)
{
	return DeviceInfo.wDataBlockNum * sizeof(u8);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Read_Block_Table
* Inputs:       none
* Outputs:      PASS / FAIL
* Description:  read the flash spare area and find a block containing the
*               most recent block table(having largest block_table_counter).
*               Find the last written Block table in this block.
*               Check the correctness of Block Table
*               If CDMA is enabled, this function is called in
*               polling mode.
*               We don't need to store changes in Block table in this
*               function as it is called only at initialization
*
*               Note: Currently this function is called at initialization
*               before any read/erase/write command issued to flash so,
*               there is no need to wait for CDMA list to complete as of now
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Read_Block_Table(void)
{
	u16 i = 0;
	int k, j;
	u8 *tempBuf, *tagarray;
	int wResult = FAIL;
	int status = FAIL;
	u8 block_table_found = 0;
	int search_result;
	u32 Block;
	u16 Page = 0;
	u16 PageCount;
	u16 bt_pages;
	int wBytesCopied = 0, tempvar;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	tempBuf = tmp_buf1_read_blk_table;
	bt_pages = FTL_Get_Block_Table_Flash_Size_Pages();

	for (j = DeviceInfo.wSpectraStartBlock;
		j <= (int)DeviceInfo.wSpectraEndBlock;
			j++) {
		status = GLOB_LLD_Read_Page_Spare(tempBuf, j, 0, 1);
		k = 0;
		i = FTL_Extract_Block_Table_Tag(tempBuf, &tagarray);
		if (i) {
			status  = GLOB_LLD_Read_Page_Main_Polling(tempBuf,
								j, 0, 1);
			for (; k < i; k++) {
				if (tagarray[k] == tempBuf[3])
					break;
			}
		}

		if (k < i)
			k = tagarray[k];
		else
			continue;

		nand_dbg_print(NAND_DBG_DEBUG,
				"Block table is contained in Block %d %d\n",
				       (unsigned int)j, (unsigned int)k);

		if (g_pBTBlocks[k-FIRST_BT_ID] == BTBLOCK_INVAL) {
			g_pBTBlocks[k-FIRST_BT_ID] = j;
			block_table_found = 1;
		} else {
			printk(KERN_ERR "FTL_Read_Block_Table -"
				"This should never happens. "
				"Two block table have same counter %u!\n", k);
		}
	}

	if (block_table_found) {
		if (g_pBTBlocks[FIRST_BT_ID - FIRST_BT_ID] != BTBLOCK_INVAL &&
		g_pBTBlocks[LAST_BT_ID - FIRST_BT_ID] != BTBLOCK_INVAL) {
			j = LAST_BT_ID;
			while ((j > FIRST_BT_ID) &&
			(g_pBTBlocks[j - FIRST_BT_ID] != BTBLOCK_INVAL))
				j--;
			if (j == FIRST_BT_ID) {
				j = LAST_BT_ID;
				last_erased = LAST_BT_ID;
			} else {
				last_erased = (u8)j + 1;
				while ((j > FIRST_BT_ID) && (BTBLOCK_INVAL ==
					g_pBTBlocks[j - FIRST_BT_ID]))
					j--;
			}
		} else {
			j = FIRST_BT_ID;
			while (g_pBTBlocks[j - FIRST_BT_ID] == BTBLOCK_INVAL)
				j++;
			last_erased = (u8)j;
			while ((j < LAST_BT_ID) && (BTBLOCK_INVAL !=
				g_pBTBlocks[j - FIRST_BT_ID]))
				j++;
			if (g_pBTBlocks[j-FIRST_BT_ID] == BTBLOCK_INVAL)
				j--;
		}

		if (last_erased > j)
			j += (1 + LAST_BT_ID - FIRST_BT_ID);

		for (; (j >= last_erased) && (FAIL == wResult); j--) {
			i = (j - FIRST_BT_ID) %
				(1 + LAST_BT_ID - FIRST_BT_ID);
			search_result =
			FTL_Search_Block_Table_IN_Block(g_pBTBlocks[i],
						i + FIRST_BT_ID, &Page);
			if (g_cBlockTableStatus == IN_PROGRESS_BLOCK_TABLE)
				block_table_found = 0;

			while ((search_result == PASS) && (FAIL == wResult)) {
				nand_dbg_print(NAND_DBG_DEBUG,
					"FTL_Read_Block_Table:"
					"Block: %u Page: %u "
					"contains block table\n",
					(unsigned int)g_pBTBlocks[i],
					(unsigned int)Page);

				tempBuf = tmp_buf2_read_blk_table;

				for (k = 0; k < bt_pages; k++) {
					Block = g_pBTBlocks[i];
					PageCount = 1;

					status  =
					GLOB_LLD_Read_Page_Main_Polling(
					tempBuf, Block, Page, PageCount);

					tempvar = k ? 0 : 4;

					wBytesCopied +=
					FTL_Copy_Block_Table_From_Flash(
					tempBuf + tempvar,
					DeviceInfo.wPageDataSize - tempvar,
					wBytesCopied);

					Page++;
				}

				wResult = FTL_Check_Block_Table(FAIL);
				if (FAIL == wResult) {
					block_table_found = 0;
					if (Page > bt_pages)
						Page -= ((bt_pages<<1) + 1);
					else
						search_result = FAIL;
				}
			}
		}
	}

	if (PASS == wResult) {
		if (!block_table_found)
			FTL_Execute_SPL_Recovery();

		if (g_cBlockTableStatus == IN_PROGRESS_BLOCK_TABLE)
			g_wBlockTableOffset = (u16)Page + 1;
		else
			g_wBlockTableOffset = (u16)Page - bt_pages;

		g_wBlockTableIndex = (u32)g_pBTBlocks[i];

#if CMD_DMA
		if (DeviceInfo.MLCDevice)
			memcpy(g_pBTStartingCopy, g_pBlockTable,
				DeviceInfo.wDataBlockNum * sizeof(u32)
				+ DeviceInfo.wDataBlockNum * sizeof(u8)
				+ DeviceInfo.wDataBlockNum * sizeof(u16));
		else
			memcpy(g_pBTStartingCopy, g_pBlockTable,
				DeviceInfo.wDataBlockNum * sizeof(u32)
				+ DeviceInfo.wDataBlockNum * sizeof(u8));
#endif
	}

	if (FAIL == wResult)
		printk(KERN_ERR "Yunpeng - "
		"Can not find valid spectra block table!\n");

#if AUTO_FORMAT_FLASH
	if (FAIL == wResult) {
		nand_dbg_print(NAND_DBG_DEBUG, "doing auto-format\n");
		wResult = FTL_Format_Flash(0);
	}
#endif

	return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Get_Page_Num
* Inputs:       Size in bytes
* Outputs:      Size in pages
* Description:  It calculates the pages required for the length passed
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u32 FTL_Get_Page_Num(u64 length)
{
	return (u32)((length >> DeviceInfo.nBitsInPageDataSize) +
		(GLOB_u64_Remainder(length , 1) > 0 ? 1 : 0));
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Get_Physical_Block_Addr
* Inputs:       Block Address (byte format)
* Outputs:      Physical address of the block.
* Description:  It translates LBA to PBA by returning address stored
*               at the LBA location in the block table
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u64 FTL_Get_Physical_Block_Addr(u64 logical_addr)
{
	u32 *pbt;
	u64 physical_addr;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	pbt = (u32 *)g_pBlockTable;
	physical_addr = (u64) DeviceInfo.wBlockDataSize *
		(pbt[BLK_FROM_ADDR(logical_addr)] & (~BAD_BLOCK));

	return physical_addr;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Get_Block_Index
* Inputs:       Physical Block no.
* Outputs:      Logical block no. /BAD_BLOCK
* Description:  It returns the logical block no. for the PBA passed
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u32 FTL_Get_Block_Index(u32 wBlockNum)
{
	u32 *pbt = (u32 *)g_pBlockTable;
	u32 i;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	for (i = 0; i < DeviceInfo.wDataBlockNum; i++)
		if (wBlockNum == (pbt[i] & (~BAD_BLOCK)))
			return i;

	return BAD_BLOCK;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Wear_Leveling
* Inputs:       none
* Outputs:      PASS=0
* Description:  This is static wear leveling (done by explicit call)
*               do complete static wear leveling
*               do complete garbage collection
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Wear_Leveling(void)
{
	nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	FTL_Static_Wear_Leveling();
	GLOB_FTL_Garbage_Collection();

	return PASS;
}

static void find_least_most_worn(u8 *chg,
	u32 *least_idx, u8 *least_cnt,
	u32 *most_idx, u8 *most_cnt)
{
	u32 *pbt = (u32 *)g_pBlockTable;
	u32 idx;
	u8 cnt;
	int i;

	for (i = BLOCK_TABLE_INDEX + 1; i < DeviceInfo.wDataBlockNum; i++) {
		if (IS_BAD_BLOCK(i) || PASS == chg[i])
			continue;

		idx = (u32) ((~BAD_BLOCK) & pbt[i]);
		cnt = g_pWearCounter[idx - DeviceInfo.wSpectraStartBlock];

		if (IS_SPARE_BLOCK(i)) {
			if (cnt > *most_cnt) {
				*most_cnt = cnt;
				*most_idx = idx;
			}
		}

		if (IS_DATA_BLOCK(i)) {
			if (cnt < *least_cnt) {
				*least_cnt = cnt;
				*least_idx = idx;
			}
		}

		if (PASS == chg[*most_idx] || PASS == chg[*least_idx]) {
			debug_boundary_error(*most_idx,
				DeviceInfo.wDataBlockNum, 0);
			debug_boundary_error(*least_idx,
				DeviceInfo.wDataBlockNum, 0);
			continue;
		}
	}
}

static int move_blks_for_wear_leveling(u8 *chg,
	u32 *least_idx, u32 *rep_blk_num, int *result)
{
	u32 *pbt = (u32 *)g_pBlockTable;
	u32 rep_blk;
	int j, ret_cp_blk, ret_erase;
	int ret = PASS;

	chg[*least_idx] = PASS;
	debug_boundary_error(*least_idx, DeviceInfo.wDataBlockNum, 0);

	rep_blk = FTL_Replace_MWBlock();
	if (rep_blk != BAD_BLOCK) {
		nand_dbg_print(NAND_DBG_DEBUG,
			"More than two spare blocks exist so do it\n");
		nand_dbg_print(NAND_DBG_DEBUG, "Block Replaced is %d\n",
				rep_blk);

		chg[rep_blk] = PASS;

		if (IN_PROGRESS_BLOCK_TABLE != g_cBlockTableStatus) {
			g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
			FTL_Write_IN_Progress_Block_Table_Page();
		}

		for (j = 0; j < RETRY_TIMES; j++) {
			ret_cp_blk = FTL_Copy_Block((u64)(*least_idx) *
				DeviceInfo.wBlockDataSize,
				(u64)rep_blk * DeviceInfo.wBlockDataSize);
			if (FAIL == ret_cp_blk) {
				ret_erase = GLOB_FTL_Block_Erase((u64)rep_blk
					* DeviceInfo.wBlockDataSize);
				if (FAIL == ret_erase)
					MARK_BLOCK_AS_BAD(pbt[rep_blk]);
			} else {
				nand_dbg_print(NAND_DBG_DEBUG,
					"FTL_Copy_Block == OK\n");
				break;
			}
		}

		if (j < RETRY_TIMES) {
			u32 tmp;
			u32 old_idx = FTL_Get_Block_Index(*least_idx);
			u32 rep_idx = FTL_Get_Block_Index(rep_blk);
			tmp = (u32)(DISCARD_BLOCK | pbt[old_idx]);
			pbt[old_idx] = (u32)((~SPARE_BLOCK) &
							pbt[rep_idx]);
			pbt[rep_idx] = tmp;
#if CMD_DMA
			p_BTableChangesDelta = (struct BTableChangesDelta *)
						g_pBTDelta_Free;
			g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
			p_BTableChangesDelta->ftl_cmd_cnt =
						ftl_cmd_cnt;
			p_BTableChangesDelta->BT_Index = old_idx;
			p_BTableChangesDelta->BT_Entry_Value = pbt[old_idx];
			p_BTableChangesDelta->ValidFields = 0x0C;

			p_BTableChangesDelta = (struct BTableChangesDelta *)
						g_pBTDelta_Free;
			g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

			p_BTableChangesDelta->ftl_cmd_cnt =
						ftl_cmd_cnt;
			p_BTableChangesDelta->BT_Index = rep_idx;
			p_BTableChangesDelta->BT_Entry_Value = pbt[rep_idx];
			p_BTableChangesDelta->ValidFields = 0x0C;
#endif
		} else {
			pbt[FTL_Get_Block_Index(rep_blk)] |= BAD_BLOCK;
#if CMD_DMA
			p_BTableChangesDelta = (struct BTableChangesDelta *)
						g_pBTDelta_Free;
			g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

			p_BTableChangesDelta->ftl_cmd_cnt =
						ftl_cmd_cnt;
			p_BTableChangesDelta->BT_Index =
					FTL_Get_Block_Index(rep_blk);
			p_BTableChangesDelta->BT_Entry_Value =
					pbt[FTL_Get_Block_Index(rep_blk)];
			p_BTableChangesDelta->ValidFields = 0x0C;
#endif
			*result = FAIL;
			ret = FAIL;
		}

		if (((*rep_blk_num)++) > WEAR_LEVELING_BLOCK_NUM)
			ret = FAIL;
	} else {
		printk(KERN_ERR "Less than 3 spare blocks exist so quit\n");
		ret = FAIL;
	}

	return ret;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Static_Wear_Leveling
* Inputs:       none
* Outputs:      PASS=0 / FAIL=1
* Description:  This is static wear leveling (done by explicit call)
*               search for most&least used
*               if difference < GATE:
*                   update the block table with exhange
*                   mark block table in flash as IN_PROGRESS
*                   copy flash block
*               the caller should handle GC clean up after calling this function
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int FTL_Static_Wear_Leveling(void)
{
	u8 most_worn_cnt;
	u8 least_worn_cnt;
	u32 most_worn_idx;
	u32 least_worn_idx;
	int result = PASS;
	int go_on = PASS;
	u32 replaced_blks = 0;
	u8 *chang_flag = flags_static_wear_leveling;

	nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (!chang_flag)
		return FAIL;

	memset(chang_flag, FAIL, DeviceInfo.wDataBlockNum);
	while (go_on == PASS) {
		nand_dbg_print(NAND_DBG_DEBUG,
			"starting static wear leveling\n");
		most_worn_cnt = 0;
		least_worn_cnt = 0xFF;
		least_worn_idx = BLOCK_TABLE_INDEX;
		most_worn_idx = BLOCK_TABLE_INDEX;

		find_least_most_worn(chang_flag, &least_worn_idx,
			&least_worn_cnt, &most_worn_idx, &most_worn_cnt);

		nand_dbg_print(NAND_DBG_DEBUG,
			"Used and least worn is block %u, whos count is %u\n",
			(unsigned int)least_worn_idx,
			(unsigned int)least_worn_cnt);

		nand_dbg_print(NAND_DBG_DEBUG,
			"Free and  most worn is block %u, whos count is %u\n",
			(unsigned int)most_worn_idx,
			(unsigned int)most_worn_cnt);

		if ((most_worn_cnt > least_worn_cnt) &&
			(most_worn_cnt - least_worn_cnt > WEAR_LEVELING_GATE))
			go_on = move_blks_for_wear_leveling(chang_flag,
				&least_worn_idx, &replaced_blks, &result);
		else
			go_on = FAIL;
	}

	return result;
}

#if CMD_DMA
static int do_garbage_collection(u32 discard_cnt)
{
	u32 *pbt = (u32 *)g_pBlockTable;
	u32 pba;
	u8 bt_block_erased = 0;
	int i, cnt, ret = FAIL;
	u64 addr;

	i = 0;
	while ((i < DeviceInfo.wDataBlockNum) && (discard_cnt > 0) &&
			((ftl_cmd_cnt + 28) < 256)) {
		if (((pbt[i] & BAD_BLOCK) != BAD_BLOCK) &&
				(pbt[i] & DISCARD_BLOCK)) {
			if (IN_PROGRESS_BLOCK_TABLE != g_cBlockTableStatus) {
				g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
				FTL_Write_IN_Progress_Block_Table_Page();
			}

			addr = FTL_Get_Physical_Block_Addr((u64)i *
						DeviceInfo.wBlockDataSize);
			pba = BLK_FROM_ADDR(addr);

			for (cnt = FIRST_BT_ID; cnt <= LAST_BT_ID; cnt++) {
				if (pba == g_pBTBlocks[cnt - FIRST_BT_ID]) {
					nand_dbg_print(NAND_DBG_DEBUG,
						"GC will erase BT block %u\n",
						(unsigned int)pba);
					discard_cnt--;
					i++;
					bt_block_erased = 1;
					break;
				}
			}

			if (bt_block_erased) {
				bt_block_erased = 0;
				continue;
			}

			addr = FTL_Get_Physical_Block_Addr((u64)i *
						DeviceInfo.wBlockDataSize);

			if (PASS == GLOB_FTL_Block_Erase(addr)) {
				pbt[i] &= (u32)(~DISCARD_BLOCK);
				pbt[i] |= (u32)(SPARE_BLOCK);
				p_BTableChangesDelta =
					(struct BTableChangesDelta *)
					g_pBTDelta_Free;
				g_pBTDelta_Free +=
					sizeof(struct BTableChangesDelta);
				p_BTableChangesDelta->ftl_cmd_cnt =
					ftl_cmd_cnt - 1;
				p_BTableChangesDelta->BT_Index = i;
				p_BTableChangesDelta->BT_Entry_Value = pbt[i];
				p_BTableChangesDelta->ValidFields = 0x0C;
				discard_cnt--;
				ret = PASS;
			} else {
				MARK_BLOCK_AS_BAD(pbt[i]);
			}
		}

		i++;
	}

	return ret;
}

#else
static int do_garbage_collection(u32 discard_cnt)
{
	u32 *pbt = (u32 *)g_pBlockTable;
	u32 pba;
	u8 bt_block_erased = 0;
	int i, cnt, ret = FAIL;
	u64 addr;

	i = 0;
	while ((i < DeviceInfo.wDataBlockNum) && (discard_cnt > 0)) {
		if (((pbt[i] & BAD_BLOCK) != BAD_BLOCK) &&
				(pbt[i] & DISCARD_BLOCK)) {
			if (IN_PROGRESS_BLOCK_TABLE != g_cBlockTableStatus) {
				g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
				FTL_Write_IN_Progress_Block_Table_Page();
			}

			addr = FTL_Get_Physical_Block_Addr((u64)i *
						DeviceInfo.wBlockDataSize);
			pba = BLK_FROM_ADDR(addr);

			for (cnt = FIRST_BT_ID; cnt <= LAST_BT_ID; cnt++) {
				if (pba == g_pBTBlocks[cnt - FIRST_BT_ID]) {
					nand_dbg_print(NAND_DBG_DEBUG,
						"GC will erase BT block %d\n",
						pba);
					discard_cnt--;
					i++;
					bt_block_erased = 1;
					break;
				}
			}

			if (bt_block_erased) {
				bt_block_erased = 0;
				continue;
			}

			/* If the discard block is L2 cache block, then just skip it */
			for (cnt = 0; cnt < BLK_NUM_FOR_L2_CACHE; cnt++) {
				if (cache_l2.blk_array[cnt] == pba) {
					nand_dbg_print(NAND_DBG_DEBUG,
						"GC will erase L2 cache blk %d\n",
						pba);
					break;
				}
			}
			if (cnt < BLK_NUM_FOR_L2_CACHE) { /* Skip it */
				discard_cnt--;
				i++;
				continue;
			}

			addr = FTL_Get_Physical_Block_Addr((u64)i *
						DeviceInfo.wBlockDataSize);

			if (PASS == GLOB_FTL_Block_Erase(addr)) {
				pbt[i] &= (u32)(~DISCARD_BLOCK);
				pbt[i] |= (u32)(SPARE_BLOCK);
				discard_cnt--;
				ret = PASS;
			} else {
				MARK_BLOCK_AS_BAD(pbt[i]);
			}
		}

		i++;
	}

	return ret;
}
#endif

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Garbage_Collection
* Inputs:       none
* Outputs:      PASS / FAIL (returns the number of un-erased blocks
* Description:  search the block table for all discarded blocks to erase
*               for each discarded block:
*                   set the flash block to IN_PROGRESS
*                   erase the block
*                   update the block table
*                   write the block table to flash
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Garbage_Collection(void)
{
	u32 i;
	u32 wDiscard = 0;
	int wResult = FAIL;
	u32 *pbt = (u32 *)g_pBlockTable;

	nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	if (GC_Called) {
		printk(KERN_ALERT "GLOB_FTL_Garbage_Collection() "
			"has been re-entered! Exit.\n");
		return PASS;
	}

	GC_Called = 1;

	GLOB_FTL_BT_Garbage_Collection();

	for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
		if (IS_DISCARDED_BLOCK(i))
			wDiscard++;
	}

	if (wDiscard <= 0) {
		GC_Called = 0;
		return wResult;
	}

	nand_dbg_print(NAND_DBG_DEBUG,
		"Found %d discarded blocks\n", wDiscard);

	FTL_Write_Block_Table(FAIL);

	wResult = do_garbage_collection(wDiscard);

	FTL_Write_Block_Table(FAIL);

	GC_Called = 0;

	return wResult;
}


#if CMD_DMA
static int do_bt_garbage_collection(void)
{
	u32 pba, lba;
	u32 *pbt = (u32 *)g_pBlockTable;
	u32 *pBTBlocksNode = (u32 *)g_pBTBlocks;
	u64 addr;
	int i, ret = FAIL;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	if (BT_GC_Called)
		return PASS;

	BT_GC_Called = 1;

	for (i = last_erased; (i <= LAST_BT_ID) &&
		(g_pBTBlocks[((i + 2) % (1 + LAST_BT_ID - FIRST_BT_ID)) +
		FIRST_BT_ID - FIRST_BT_ID] != BTBLOCK_INVAL) &&
		((ftl_cmd_cnt + 28)) < 256; i++) {
		pba = pBTBlocksNode[i - FIRST_BT_ID];
		lba = FTL_Get_Block_Index(pba);
		nand_dbg_print(NAND_DBG_DEBUG,
			"do_bt_garbage_collection: pba %d, lba %d\n",
			pba, lba);
		nand_dbg_print(NAND_DBG_DEBUG,
			"Block Table Entry: %d", pbt[lba]);

		if (((pbt[lba] & BAD_BLOCK) != BAD_BLOCK) &&
			(pbt[lba] & DISCARD_BLOCK)) {
			nand_dbg_print(NAND_DBG_DEBUG,
				"do_bt_garbage_collection_cdma: "
				"Erasing Block tables present in block %d\n",
				pba);
			addr = FTL_Get_Physical_Block_Addr((u64)lba *
						DeviceInfo.wBlockDataSize);
			if (PASS == GLOB_FTL_Block_Erase(addr)) {
				pbt[lba] &= (u32)(~DISCARD_BLOCK);
				pbt[lba] |= (u32)(SPARE_BLOCK);

				p_BTableChangesDelta =
					(struct BTableChangesDelta *)
					g_pBTDelta_Free;
				g_pBTDelta_Free +=
					sizeof(struct BTableChangesDelta);

				p_BTableChangesDelta->ftl_cmd_cnt =
					ftl_cmd_cnt - 1;
				p_BTableChangesDelta->BT_Index = lba;
				p_BTableChangesDelta->BT_Entry_Value =
								pbt[lba];

				p_BTableChangesDelta->ValidFields = 0x0C;

				ret = PASS;
				pBTBlocksNode[last_erased - FIRST_BT_ID] =
							BTBLOCK_INVAL;
				nand_dbg_print(NAND_DBG_DEBUG,
					"resetting bt entry at index %d "
					"value %d\n", i,
					pBTBlocksNode[i - FIRST_BT_ID]);
				if (last_erased == LAST_BT_ID)
					last_erased = FIRST_BT_ID;
				else
					last_erased++;
			} else {
				MARK_BLOCK_AS_BAD(pbt[lba]);
			}
		}
	}

	BT_GC_Called = 0;

	return ret;
}

#else
static int do_bt_garbage_collection(void)
{
	u32 pba, lba;
	u32 *pbt = (u32 *)g_pBlockTable;
	u32 *pBTBlocksNode = (u32 *)g_pBTBlocks;
	u64 addr;
	int i, ret = FAIL;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	if (BT_GC_Called)
		return PASS;

	BT_GC_Called = 1;

	for (i = last_erased; (i <= LAST_BT_ID) &&
		(g_pBTBlocks[((i + 2) % (1 + LAST_BT_ID - FIRST_BT_ID)) +
		FIRST_BT_ID - FIRST_BT_ID] != BTBLOCK_INVAL); i++) {
		pba = pBTBlocksNode[i - FIRST_BT_ID];
		lba = FTL_Get_Block_Index(pba);
		nand_dbg_print(NAND_DBG_DEBUG,
			"do_bt_garbage_collection_cdma: pba %d, lba %d\n",
			pba, lba);
		nand_dbg_print(NAND_DBG_DEBUG,
			"Block Table Entry: %d", pbt[lba]);

		if (((pbt[lba] & BAD_BLOCK) != BAD_BLOCK) &&
			(pbt[lba] & DISCARD_BLOCK)) {
			nand_dbg_print(NAND_DBG_DEBUG,
				"do_bt_garbage_collection: "
				"Erasing Block tables present in block %d\n",
				pba);
			addr = FTL_Get_Physical_Block_Addr((u64)lba *
						DeviceInfo.wBlockDataSize);
			if (PASS == GLOB_FTL_Block_Erase(addr)) {
				pbt[lba] &= (u32)(~DISCARD_BLOCK);
				pbt[lba] |= (u32)(SPARE_BLOCK);
				ret = PASS;
				pBTBlocksNode[last_erased - FIRST_BT_ID] =
							BTBLOCK_INVAL;
				nand_dbg_print(NAND_DBG_DEBUG,
					"resetting bt entry at index %d "
					"value %d\n", i,
					pBTBlocksNode[i - FIRST_BT_ID]);
				if (last_erased == LAST_BT_ID)
					last_erased = FIRST_BT_ID;
				else
					last_erased++;
			} else {
				MARK_BLOCK_AS_BAD(pbt[lba]);
			}
		}
	}

	BT_GC_Called = 0;

	return ret;
}

#endif

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_BT_Garbage_Collection
* Inputs:       none
* Outputs:      PASS / FAIL (returns the number of un-erased blocks
* Description:  Erases discarded blocks containing Block table
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_BT_Garbage_Collection(void)
{
	return do_bt_garbage_collection();
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Replace_OneBlock
* Inputs:       Block number 1
*               Block number 2
* Outputs:      Replaced Block Number
* Description:  Interchange block table entries at wBlockNum and wReplaceNum
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u32 FTL_Replace_OneBlock(u32 blk, u32 rep_blk)
{
	u32 tmp_blk;
	u32 replace_node = BAD_BLOCK;
	u32 *pbt = (u32 *)g_pBlockTable;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	if (rep_blk != BAD_BLOCK) {
		if (IS_BAD_BLOCK(blk))
			tmp_blk = pbt[blk];
		else
			tmp_blk = DISCARD_BLOCK | (~SPARE_BLOCK & pbt[blk]);

		replace_node = (u32) ((~SPARE_BLOCK) & pbt[rep_blk]);
		pbt[blk] = replace_node;
		pbt[rep_blk] = tmp_blk;

#if CMD_DMA
		p_BTableChangesDelta =
			(struct BTableChangesDelta *)g_pBTDelta_Free;
		g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

		p_BTableChangesDelta->ftl_cmd_cnt = ftl_cmd_cnt;
		p_BTableChangesDelta->BT_Index = blk;
		p_BTableChangesDelta->BT_Entry_Value = pbt[blk];

		p_BTableChangesDelta->ValidFields = 0x0C;

		p_BTableChangesDelta =
			(struct BTableChangesDelta *)g_pBTDelta_Free;
		g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

		p_BTableChangesDelta->ftl_cmd_cnt = ftl_cmd_cnt;
		p_BTableChangesDelta->BT_Index = rep_blk;
		p_BTableChangesDelta->BT_Entry_Value = pbt[rep_blk];
		p_BTableChangesDelta->ValidFields = 0x0C;
#endif
	}

	return replace_node;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Write_Block_Table_Data
* Inputs:       Block table size in pages
* Outputs:      PASS=0 / FAIL=1
* Description:  Write block table data in flash
*               If first page and last page
*                  Write data+BT flag
*               else
*                  Write data
*               BT flag is a counter. Its value is incremented for block table
*               write in a new Block
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Write_Block_Table_Data(void)
{
	u64 dwBlockTableAddr, pTempAddr;
	u32 Block;
	u16 Page, PageCount;
	u8 *tempBuf = tmp_buf_write_blk_table_data;
	int wBytesCopied;
	u16 bt_pages;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	dwBlockTableAddr =
		(u64)((u64)g_wBlockTableIndex * DeviceInfo.wBlockDataSize +
		(u64)g_wBlockTableOffset * DeviceInfo.wPageDataSize);
	pTempAddr = dwBlockTableAddr;

	bt_pages = FTL_Get_Block_Table_Flash_Size_Pages();

	nand_dbg_print(NAND_DBG_DEBUG, "FTL_Write_Block_Table_Data: "
			       "page= %d BlockTableIndex= %d "
			       "BlockTableOffset=%d\n", bt_pages,
			       g_wBlockTableIndex, g_wBlockTableOffset);

	Block = BLK_FROM_ADDR(pTempAddr);
	Page = PAGE_FROM_ADDR(pTempAddr, Block);
	PageCount = 1;

	if (bt_block_changed) {
		if (bt_flag == LAST_BT_ID) {
			bt_flag = FIRST_BT_ID;
			g_pBTBlocks[bt_flag - FIRST_BT_ID] = Block;
		} else if (bt_flag < LAST_BT_ID) {
			bt_flag++;
			g_pBTBlocks[bt_flag - FIRST_BT_ID] = Block;
		}

		if ((bt_flag > (LAST_BT_ID-4)) &&
			g_pBTBlocks[FIRST_BT_ID - FIRST_BT_ID] !=
						BTBLOCK_INVAL) {
			bt_block_changed = 0;
			GLOB_FTL_BT_Garbage_Collection();
		}

		bt_block_changed = 0;
		nand_dbg_print(NAND_DBG_DEBUG,
			"Block Table Counter is %u Block %u\n",
			bt_flag, (unsigned int)Block);
	}

	memset(tempBuf, 0, 3);
	tempBuf[3] = bt_flag;
	wBytesCopied = FTL_Copy_Block_Table_To_Flash(tempBuf + 4,
			DeviceInfo.wPageDataSize - 4, 0);
	memset(&tempBuf[wBytesCopied + 4], 0xff,
		DeviceInfo.wPageSize - (wBytesCopied + 4));
	FTL_Insert_Block_Table_Signature(&tempBuf[DeviceInfo.wPageDataSize],
					bt_flag);

#if CMD_DMA
	memcpy(g_pNextBlockTable, tempBuf,
		DeviceInfo.wPageSize * sizeof(u8));
	nand_dbg_print(NAND_DBG_DEBUG, "Writing First Page of Block Table "
		"Block %u Page %u\n", (unsigned int)Block, Page);
	if (FAIL == GLOB_LLD_Write_Page_Main_Spare_cdma(g_pNextBlockTable,
		Block, Page, 1,
		LLD_CMD_FLAG_MODE_CDMA | LLD_CMD_FLAG_ORDER_BEFORE_REST)) {
		nand_dbg_print(NAND_DBG_WARN, "NAND Program fail in "
			"%s, Line %d, Function: %s, "
			"new Bad Block %d generated!\n",
			__FILE__, __LINE__, __func__, Block);
		goto func_return;
	}

	ftl_cmd_cnt++;
	g_pNextBlockTable += ((DeviceInfo.wPageSize * sizeof(u8)));
#else
	if (FAIL == GLOB_LLD_Write_Page_Main_Spare(tempBuf, Block, Page, 1)) {
		nand_dbg_print(NAND_DBG_WARN,
			"NAND Program fail in %s, Line %d, Function: %s, "
			"new Bad Block %d generated!\n",
			__FILE__, __LINE__, __func__, Block);
		goto func_return;
	}
#endif

	if (bt_pages > 1) {
		PageCount = bt_pages - 1;
		if (PageCount > 1) {
			wBytesCopied += FTL_Copy_Block_Table_To_Flash(tempBuf,
				DeviceInfo.wPageDataSize * (PageCount - 1),
				wBytesCopied);

#if CMD_DMA
			memcpy(g_pNextBlockTable, tempBuf,
				(PageCount - 1) * DeviceInfo.wPageDataSize);
			if (FAIL == GLOB_LLD_Write_Page_Main_cdma(
				g_pNextBlockTable, Block, Page + 1,
				PageCount - 1)) {
				nand_dbg_print(NAND_DBG_WARN,
					"NAND Program fail in %s, Line %d, "
					"Function: %s, "
					"new Bad Block %d generated!\n",
					__FILE__, __LINE__, __func__,
					(int)Block);
				goto func_return;
			}

			ftl_cmd_cnt++;
			g_pNextBlockTable += (PageCount - 1) *
				DeviceInfo.wPageDataSize * sizeof(u8);
#else
			if (FAIL == GLOB_LLD_Write_Page_Main(tempBuf,
					Block, Page + 1, PageCount - 1)) {
				nand_dbg_print(NAND_DBG_WARN,
					"NAND Program fail in %s, Line %d, "
					"Function: %s, "
					"new Bad Block %d generated!\n",
					__FILE__, __LINE__, __func__,
					(int)Block);
				goto func_return;
			}
#endif
		}

		wBytesCopied = FTL_Copy_Block_Table_To_Flash(tempBuf,
				DeviceInfo.wPageDataSize, wBytesCopied);
		memset(&tempBuf[wBytesCopied], 0xff,
			DeviceInfo.wPageSize-wBytesCopied);
		FTL_Insert_Block_Table_Signature(
			&tempBuf[DeviceInfo.wPageDataSize], bt_flag);
#if CMD_DMA
		memcpy(g_pNextBlockTable, tempBuf,
				DeviceInfo.wPageSize * sizeof(u8));
		nand_dbg_print(NAND_DBG_DEBUG,
			"Writing the last Page of Block Table "
			"Block %u Page %u\n",
			(unsigned int)Block, Page + bt_pages - 1);
		if (FAIL == GLOB_LLD_Write_Page_Main_Spare_cdma(
			g_pNextBlockTable, Block, Page + bt_pages - 1, 1,
			LLD_CMD_FLAG_MODE_CDMA |
			LLD_CMD_FLAG_ORDER_BEFORE_REST)) {
			nand_dbg_print(NAND_DBG_WARN,
				"NAND Program fail in %s, Line %d, "
				"Function: %s, new Bad Block %d generated!\n",
				__FILE__, __LINE__, __func__, Block);
			goto func_return;
		}
		ftl_cmd_cnt++;
#else
		if (FAIL == GLOB_LLD_Write_Page_Main_Spare(tempBuf,
					Block, Page+bt_pages - 1, 1)) {
			nand_dbg_print(NAND_DBG_WARN,
				"NAND Program fail in %s, Line %d, "
				"Function: %s, "
				"new Bad Block %d generated!\n",
				__FILE__, __LINE__, __func__, Block);
			goto func_return;
		}
#endif
	}

	nand_dbg_print(NAND_DBG_DEBUG, "FTL_Write_Block_Table_Data: done\n");

func_return:
	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Replace_Block_Table
* Inputs:       None
* Outputs:      PASS=0 / FAIL=1
* Description:  Get a new block to write block table
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u32 FTL_Replace_Block_Table(void)
{
	u32 blk;
	int gc;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	blk = FTL_Replace_LWBlock(BLOCK_TABLE_INDEX, &gc);

	if ((BAD_BLOCK == blk) && (PASS == gc)) {
		GLOB_FTL_Garbage_Collection();
		blk = FTL_Replace_LWBlock(BLOCK_TABLE_INDEX, &gc);
	}
	if (BAD_BLOCK == blk)
		printk(KERN_ERR "%s, %s: There is no spare block. "
			"It should never happen\n",
			__FILE__, __func__);

	nand_dbg_print(NAND_DBG_DEBUG, "New Block table Block is %d\n", blk);

	return blk;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Replace_LWBlock
* Inputs:       Block number
*               Pointer to Garbage Collect flag
* Outputs:
* Description:  Determine the least weared block by traversing
*               block table
*               Set Garbage collection to be called if number of spare
*               block is less than Free Block Gate count
*               Change Block table entry to map least worn block for current
*               operation
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u32 FTL_Replace_LWBlock(u32 wBlockNum, int *pGarbageCollect)
{
	u32 i;
	u32 *pbt = (u32 *)g_pBlockTable;
	u8 wLeastWornCounter = 0xFF;
	u32 wLeastWornIndex = BAD_BLOCK;
	u32 wSpareBlockNum = 0;
	u32 wDiscardBlockNum = 0;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	if (IS_SPARE_BLOCK(wBlockNum)) {
		*pGarbageCollect = FAIL;
		pbt[wBlockNum] = (u32)(pbt[wBlockNum] & (~SPARE_BLOCK));
#if CMD_DMA
		p_BTableChangesDelta =
			(struct BTableChangesDelta *)g_pBTDelta_Free;
		g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
		p_BTableChangesDelta->ftl_cmd_cnt =
						ftl_cmd_cnt;
		p_BTableChangesDelta->BT_Index = (u32)(wBlockNum);
		p_BTableChangesDelta->BT_Entry_Value = pbt[wBlockNum];
		p_BTableChangesDelta->ValidFields = 0x0C;
#endif
		return pbt[wBlockNum];
	}

	for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
		if (IS_DISCARDED_BLOCK(i))
			wDiscardBlockNum++;

		if (IS_SPARE_BLOCK(i)) {
			u32 wPhysicalIndex = (u32)((~BAD_BLOCK) & pbt[i]);
			if (wPhysicalIndex > DeviceInfo.wSpectraEndBlock)
				printk(KERN_ERR "FTL_Replace_LWBlock: "
					"This should never occur!\n");
			if (g_pWearCounter[wPhysicalIndex -
				DeviceInfo.wSpectraStartBlock] <
				wLeastWornCounter) {
				wLeastWornCounter =
					g_pWearCounter[wPhysicalIndex -
					DeviceInfo.wSpectraStartBlock];
				wLeastWornIndex = i;
			}
			wSpareBlockNum++;
		}
	}

	nand_dbg_print(NAND_DBG_WARN,
		"FTL_Replace_LWBlock: Least Worn Counter %d\n",
		(int)wLeastWornCounter);

	if ((wDiscardBlockNum >= NUM_FREE_BLOCKS_GATE) ||
		(wSpareBlockNum <= NUM_FREE_BLOCKS_GATE))
		*pGarbageCollect = PASS;
	else
		*pGarbageCollect = FAIL;

	nand_dbg_print(NAND_DBG_DEBUG,
		"FTL_Replace_LWBlock: Discarded Blocks %u Spare"
		" Blocks %u\n",
		(unsigned int)wDiscardBlockNum,
		(unsigned int)wSpareBlockNum);

	return FTL_Replace_OneBlock(wBlockNum, wLeastWornIndex);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Replace_MWBlock
* Inputs:       None
* Outputs:      most worn spare block no./BAD_BLOCK
* Description:  It finds most worn spare block.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static u32 FTL_Replace_MWBlock(void)
{
	u32 i;
	u32 *pbt = (u32 *)g_pBlockTable;
	u8 wMostWornCounter = 0;
	u32 wMostWornIndex = BAD_BLOCK;
	u32 wSpareBlockNum = 0;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
		if (IS_SPARE_BLOCK(i)) {
			u32 wPhysicalIndex = (u32)((~SPARE_BLOCK) & pbt[i]);
			if (g_pWearCounter[wPhysicalIndex -
			    DeviceInfo.wSpectraStartBlock] >
			    wMostWornCounter) {
				wMostWornCounter =
				    g_pWearCounter[wPhysicalIndex -
				    DeviceInfo.wSpectraStartBlock];
				wMostWornIndex = wPhysicalIndex;
			}
			wSpareBlockNum++;
		}
	}

	if (wSpareBlockNum <= 2)
		return BAD_BLOCK;

	return wMostWornIndex;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Replace_Block
* Inputs:       Block Address
* Outputs:      PASS=0 / FAIL=1
* Description:  If block specified by blk_addr parameter is not free,
*               replace it with the least worn block.
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Replace_Block(u64 blk_addr)
{
	u32 current_blk = BLK_FROM_ADDR(blk_addr);
	u32 *pbt = (u32 *)g_pBlockTable;
	int wResult = PASS;
	int GarbageCollect = FAIL;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	if (IS_SPARE_BLOCK(current_blk)) {
		pbt[current_blk] = (~SPARE_BLOCK) & pbt[current_blk];
#if CMD_DMA
		p_BTableChangesDelta =
			(struct BTableChangesDelta *)g_pBTDelta_Free;
		g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
		p_BTableChangesDelta->ftl_cmd_cnt =
			ftl_cmd_cnt;
		p_BTableChangesDelta->BT_Index = current_blk;
		p_BTableChangesDelta->BT_Entry_Value = pbt[current_blk];
		p_BTableChangesDelta->ValidFields = 0x0C ;
#endif
		return wResult;
	}

	FTL_Replace_LWBlock(current_blk, &GarbageCollect);

	if (PASS == GarbageCollect)
		wResult = GLOB_FTL_Garbage_Collection();

	return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Is_BadBlock
* Inputs:       block number to test
* Outputs:      PASS (block is BAD) / FAIL (block is not bad)
* Description:  test if this block number is flagged as bad
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Is_BadBlock(u32 wBlockNum)
{
	u32 *pbt = (u32 *)g_pBlockTable;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	if (wBlockNum >= DeviceInfo.wSpectraStartBlock
		&& BAD_BLOCK == (pbt[wBlockNum] & BAD_BLOCK))
		return PASS;
	else
		return FAIL;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Flush_Cache
* Inputs:       none
* Outputs:      PASS=0 / FAIL=1
* Description:  flush all the cache blocks to flash
*               if a cache block is not dirty, don't do anything with it
*               else, write the block and update the block table
* Note:         This function should be called at shutdown/power down.
*               to write important data into device
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Flush_Cache(void)
{
	int i, ret;

	nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	for (i = 0; i < CACHE_ITEM_NUM; i++) {
		if (SET == Cache.array[i].changed) {
#if CMD_DMA
#if RESTORE_CACHE_ON_CDMA_CHAIN_FAILURE
			int_cache[ftl_cmd_cnt].item = i;
			int_cache[ftl_cmd_cnt].cache.address =
					Cache.array[i].address;
			int_cache[ftl_cmd_cnt].cache.changed = CLEAR;
#endif
#endif
			ret = write_back_to_l2_cache(Cache.array[i].buf, Cache.array[i].address);
			if (PASS == ret) {
				Cache.array[i].changed = CLEAR;
			} else {
				printk(KERN_ALERT "Failed when write back to L2 cache!\n");
				/* TODO - How to handle this? */
			}
		}
	}

	flush_l2_cache();

	return FTL_Write_Block_Table(FAIL);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Page_Read
* Inputs:       pointer to data
*                   logical address of data (u64 is LBA * Bytes/Page)
* Outputs:      PASS=0 / FAIL=1
* Description:  reads a page of data into RAM from the cache
*               if the data is not already in cache, read from flash to cache
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Page_Read(u8 *data, u64 logical_addr)
{
	u16 cache_item;
	int res = PASS;

	nand_dbg_print(NAND_DBG_DEBUG, "GLOB_FTL_Page_Read - "
		"page_addr: %llu\n", logical_addr);

	cache_item = FTL_Cache_If_Hit(logical_addr);

	if (UNHIT_CACHE_ITEM == cache_item) {
		nand_dbg_print(NAND_DBG_DEBUG,
			       "GLOB_FTL_Page_Read: Cache not hit\n");
		res = FTL_Cache_Write();
		if (ERR == FTL_Cache_Read(logical_addr))
			res = ERR;
		cache_item = Cache.LRU;
	}

	FTL_Cache_Read_Page(data, logical_addr, cache_item);

	return res;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Page_Write
* Inputs:       pointer to data
*               address of data (ADDRESSTYPE is LBA * Bytes/Page)
* Outputs:      PASS=0 / FAIL=1
* Description:  writes a page of data from RAM to the cache
*               if the data is not already in cache, write back the
*               least recently used block and read the addressed block
*               from flash to cache
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Page_Write(u8 *pData, u64 dwPageAddr)
{
	u16 cache_blk;
	u32 *pbt = (u32 *)g_pBlockTable;
	int wResult = PASS;

	nand_dbg_print(NAND_DBG_TRACE, "GLOB_FTL_Page_Write - "
		"dwPageAddr: %llu\n", dwPageAddr);

	cache_blk = FTL_Cache_If_Hit(dwPageAddr);

	if (UNHIT_CACHE_ITEM == cache_blk) {
		wResult = FTL_Cache_Write();
		if (IS_BAD_BLOCK(BLK_FROM_ADDR(dwPageAddr))) {
			wResult = FTL_Replace_Block(dwPageAddr);
			pbt[BLK_FROM_ADDR(dwPageAddr)] |= SPARE_BLOCK;
			if (wResult == FAIL)
				return FAIL;
		}
		if (ERR == FTL_Cache_Read(dwPageAddr))
			wResult = ERR;
		cache_blk = Cache.LRU;
		FTL_Cache_Write_Page(pData, dwPageAddr, cache_blk, 0);
	} else {
#if CMD_DMA
		FTL_Cache_Write_Page(pData, dwPageAddr, cache_blk,
				LLD_CMD_FLAG_ORDER_BEFORE_REST);
#else
		FTL_Cache_Write_Page(pData, dwPageAddr, cache_blk, 0);
#endif
	}

	return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_FTL_Block_Erase
* Inputs:       address of block to erase (now in byte format, should change to
* block format)
* Outputs:      PASS=0 / FAIL=1
* Description:  erases the specified block
*               increments the erase count
*               If erase count reaches its upper limit,call function to
*               do the ajustment as per the relative erase count values
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_FTL_Block_Erase(u64 blk_addr)
{
	int status;
	u32 BlkIdx;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	BlkIdx = (u32)(blk_addr >> DeviceInfo.nBitsInBlockDataSize);

	if (BlkIdx < DeviceInfo.wSpectraStartBlock) {
		printk(KERN_ERR "GLOB_FTL_Block_Erase: "
			"This should never occur\n");
		return FAIL;
	}

#if CMD_DMA
	status = GLOB_LLD_Erase_Block_cdma(BlkIdx, LLD_CMD_FLAG_MODE_CDMA);
	if (status == FAIL)
		nand_dbg_print(NAND_DBG_WARN,
			       "NAND Program fail in %s, Line %d, "
			       "Function: %s, new Bad Block %d generated!\n",
			       __FILE__, __LINE__, __func__, BlkIdx);
#else
	status = GLOB_LLD_Erase_Block(BlkIdx);
	if (status == FAIL) {
		nand_dbg_print(NAND_DBG_WARN,
			       "NAND Program fail in %s, Line %d, "
			       "Function: %s, new Bad Block %d generated!\n",
			       __FILE__, __LINE__, __func__, BlkIdx);
		return status;
	}
#endif

	if (DeviceInfo.MLCDevice) {
		g_pReadCounter[BlkIdx - DeviceInfo.wSpectraStartBlock] = 0;
		if (g_cBlockTableStatus != IN_PROGRESS_BLOCK_TABLE) {
			g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
			FTL_Write_IN_Progress_Block_Table_Page();
		}
	}

	g_pWearCounter[BlkIdx - DeviceInfo.wSpectraStartBlock]++;

#if CMD_DMA
	p_BTableChangesDelta =
		(struct BTableChangesDelta *)g_pBTDelta_Free;
	g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
	p_BTableChangesDelta->ftl_cmd_cnt = ftl_cmd_cnt;
	p_BTableChangesDelta->WC_Index =
		BlkIdx - DeviceInfo.wSpectraStartBlock;
	p_BTableChangesDelta->WC_Entry_Value =
		g_pWearCounter[BlkIdx - DeviceInfo.wSpectraStartBlock];
	p_BTableChangesDelta->ValidFields = 0x30;

	if (DeviceInfo.MLCDevice) {
		p_BTableChangesDelta =
			(struct BTableChangesDelta *)g_pBTDelta_Free;
		g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
		p_BTableChangesDelta->ftl_cmd_cnt =
			ftl_cmd_cnt;
		p_BTableChangesDelta->RC_Index =
			BlkIdx - DeviceInfo.wSpectraStartBlock;
		p_BTableChangesDelta->RC_Entry_Value =
			g_pReadCounter[BlkIdx -
				DeviceInfo.wSpectraStartBlock];
		p_BTableChangesDelta->ValidFields = 0xC0;
	}

	ftl_cmd_cnt++;
#endif

	if (g_pWearCounter[BlkIdx - DeviceInfo.wSpectraStartBlock] == 0xFE)
		FTL_Adjust_Relative_Erase_Count(BlkIdx);

	return status;
}


/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Adjust_Relative_Erase_Count
* Inputs:       index to block that was just incremented and is at the max
* Outputs:      PASS=0 / FAIL=1
* Description:  If any erase counts at MAX, adjusts erase count of every
*               block by substracting least worn
*               counter from counter value of every entry in wear table
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
static int FTL_Adjust_Relative_Erase_Count(u32 Index_of_MAX)
{
	u8 wLeastWornCounter = MAX_BYTE_VALUE;
	u8 wWearCounter;
	u32 i, wWearIndex;
	u32 *pbt = (u32 *)g_pBlockTable;
	int wResult = PASS;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	for (i = 0; i < DeviceInfo.wDataBlockNum; i++) {
		if (IS_BAD_BLOCK(i))
			continue;
		wWearIndex = (u32)(pbt[i] & (~BAD_BLOCK));

		if ((wWearIndex - DeviceInfo.wSpectraStartBlock) < 0)
			printk(KERN_ERR "FTL_Adjust_Relative_Erase_Count:"
					"This should never occur\n");
		wWearCounter = g_pWearCounter[wWearIndex -
			DeviceInfo.wSpectraStartBlock];
		if (wWearCounter < wLeastWornCounter)
			wLeastWornCounter = wWearCounter;
	}

	if (wLeastWornCounter == 0) {
		nand_dbg_print(NAND_DBG_WARN,
			"Adjusting Wear Levelling Counters: Special Case\n");
		g_pWearCounter[Index_of_MAX -
			DeviceInfo.wSpectraStartBlock]--;
#if CMD_DMA
		p_BTableChangesDelta =
			(struct BTableChangesDelta *)g_pBTDelta_Free;
		g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
		p_BTableChangesDelta->ftl_cmd_cnt = ftl_cmd_cnt;
		p_BTableChangesDelta->WC_Index =
			Index_of_MAX - DeviceInfo.wSpectraStartBlock;
		p_BTableChangesDelta->WC_Entry_Value =
			g_pWearCounter[Index_of_MAX -
				DeviceInfo.wSpectraStartBlock];
		p_BTableChangesDelta->ValidFields = 0x30;
#endif
		FTL_Static_Wear_Leveling();
	} else {
		for (i = 0; i < DeviceInfo.wDataBlockNum; i++)
			if (!IS_BAD_BLOCK(i)) {
				wWearIndex = (u32)(pbt[i] & (~BAD_BLOCK));
				g_pWearCounter[wWearIndex -
					DeviceInfo.wSpectraStartBlock] =
					(u8)(g_pWearCounter
					[wWearIndex -
					DeviceInfo.wSpectraStartBlock] -
					wLeastWornCounter);
#if CMD_DMA
				p_BTableChangesDelta =
				(struct BTableChangesDelta *)g_pBTDelta_Free;
				g_pBTDelta_Free +=
					sizeof(struct BTableChangesDelta);

				p_BTableChangesDelta->ftl_cmd_cnt =
					ftl_cmd_cnt;
				p_BTableChangesDelta->WC_Index = wWearIndex -
					DeviceInfo.wSpectraStartBlock;
				p_BTableChangesDelta->WC_Entry_Value =
					g_pWearCounter[wWearIndex -
					DeviceInfo.wSpectraStartBlock];
				p_BTableChangesDelta->ValidFields = 0x30;
#endif
			}
	}

	return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Write_IN_Progress_Block_Table_Page
* Inputs:       None
* Outputs:      None
* Description:  It writes in-progress flag page to the page next to
*               block table
***********************************************************************/
static int FTL_Write_IN_Progress_Block_Table_Page(void)
{
	int wResult = PASS;
	u16 bt_pages;
	u16 dwIPFPageAddr;
#if CMD_DMA
#else
	u32 *pbt = (u32 *)g_pBlockTable;
	u32 wTempBlockTableIndex;
#endif

	nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

	bt_pages = FTL_Get_Block_Table_Flash_Size_Pages();

	dwIPFPageAddr = g_wBlockTableOffset + bt_pages;

	nand_dbg_print(NAND_DBG_DEBUG, "Writing IPF at "
			       "Block %d Page %d\n",
			       g_wBlockTableIndex, dwIPFPageAddr);

#if CMD_DMA
	wResult = GLOB_LLD_Write_Page_Main_Spare_cdma(g_pIPF,
		g_wBlockTableIndex, dwIPFPageAddr, 1,
		LLD_CMD_FLAG_MODE_CDMA | LLD_CMD_FLAG_ORDER_BEFORE_REST);
	if (wResult == FAIL) {
		nand_dbg_print(NAND_DBG_WARN,
			       "NAND Program fail in %s, Line %d, "
			       "Function: %s, new Bad Block %d generated!\n",
			       __FILE__, __LINE__, __func__,
			       g_wBlockTableIndex);
	}
	g_wBlockTableOffset = dwIPFPageAddr + 1;
	p_BTableChangesDelta = (struct BTableChangesDelta *)g_pBTDelta_Free;
	g_pBTDelta_Free += sizeof(struct BTableChangesDelta);
	p_BTableChangesDelta->ftl_cmd_cnt = ftl_cmd_cnt;
	p_BTableChangesDelta->g_wBlockTableOffset = g_wBlockTableOffset;
	p_BTableChangesDelta->ValidFields = 0x01;
	ftl_cmd_cnt++;
#else
	wResult = GLOB_LLD_Write_Page_Main_Spare(g_pIPF,
		g_wBlockTableIndex, dwIPFPageAddr, 1);
	if (wResult == FAIL) {
		nand_dbg_print(NAND_DBG_WARN,
			       "NAND Program fail in %s, Line %d, "
			       "Function: %s, new Bad Block %d generated!\n",
			       __FILE__, __LINE__, __func__,
			       (int)g_wBlockTableIndex);
		MARK_BLOCK_AS_BAD(pbt[BLOCK_TABLE_INDEX]);
		wTempBlockTableIndex = FTL_Replace_Block_Table();
		bt_block_changed = 1;
		if (BAD_BLOCK == wTempBlockTableIndex)
			return ERR;
		g_wBlockTableIndex = wTempBlockTableIndex;
		g_wBlockTableOffset = 0;
		/* Block table tag is '00'. Means it's used one */
		pbt[BLOCK_TABLE_INDEX] = g_wBlockTableIndex;
		return FAIL;
	}
	g_wBlockTableOffset = dwIPFPageAddr + 1;
#endif
	return wResult;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     FTL_Read_Disturbance
* Inputs:       block address
* Outputs:      PASS=0 / FAIL=1
* Description:  used to handle read disturbance. Data in block that
*               reaches its read limit is moved to new block
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int FTL_Read_Disturbance(u32 blk_addr)
{
	int wResult = FAIL;
	u32 *pbt = (u32 *) g_pBlockTable;
	u32 dwOldBlockAddr = blk_addr;
	u32 wBlockNum;
	u32 i;
	u32 wLeastReadCounter = 0xFFFF;
	u32 wLeastReadIndex = BAD_BLOCK;
	u32 wSpareBlockNum = 0;
	u32 wTempNode;
	u32 wReplacedNode;
	u8 *g_pTempBuf;

	nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
			       __FILE__, __LINE__, __func__);

#if CMD_DMA
	g_pTempBuf = cp_back_buf_copies[cp_back_buf_idx];
	cp_back_buf_idx++;
	if (cp_back_buf_idx > COPY_BACK_BUF_NUM) {
		printk(KERN_ERR "cp_back_buf_copies overflow! Exit."
		"Maybe too many pending commands in your CDMA chain.\n");
		return FAIL;
	}
#else
	g_pTempBuf = tmp_buf_read_disturbance;
#endif

	wBlockNum = FTL_Get_Block_Index(blk_addr);

	do {
		/* This is a bug.Here 'i' should be logical block number
		 * and start from 1 (0 is reserved for block table).
		 * Have fixed it.        - Yunpeng 2008. 12. 19
		 */
		for (i = 1; i < DeviceInfo.wDataBlockNum; i++) {
			if (IS_SPARE_BLOCK(i)) {
				u32 wPhysicalIndex =
					(u32)((~SPARE_BLOCK) & pbt[i]);
				if (g_pReadCounter[wPhysicalIndex -
					DeviceInfo.wSpectraStartBlock] <
					wLeastReadCounter) {
					wLeastReadCounter =
						g_pReadCounter[wPhysicalIndex -
						DeviceInfo.wSpectraStartBlock];
					wLeastReadIndex = i;
				}
				wSpareBlockNum++;
			}
		}

		if (wSpareBlockNum <= NUM_FREE_BLOCKS_GATE) {
			wResult = GLOB_FTL_Garbage_Collection();
			if (PASS == wResult)
				continue;
			else
				break;
		} else {
			wTempNode = (u32)(DISCARD_BLOCK | pbt[wBlockNum]);
			wReplacedNode = (u32)((~SPARE_BLOCK) &
					pbt[wLeastReadIndex]);
#if CMD_DMA
			pbt[wBlockNum] = wReplacedNode;
			pbt[wLeastReadIndex] = wTempNode;
			p_BTableChangesDelta =
				(struct BTableChangesDelta *)g_pBTDelta_Free;
			g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

			p_BTableChangesDelta->ftl_cmd_cnt =
					ftl_cmd_cnt;
			p_BTableChangesDelta->BT_Index = wBlockNum;
			p_BTableChangesDelta->BT_Entry_Value = pbt[wBlockNum];
			p_BTableChangesDelta->ValidFields = 0x0C;

			p_BTableChangesDelta =
				(struct BTableChangesDelta *)g_pBTDelta_Free;
			g_pBTDelta_Free += sizeof(struct BTableChangesDelta);

			p_BTableChangesDelta->ftl_cmd_cnt =
					ftl_cmd_cnt;
			p_BTableChangesDelta->BT_Index = wLeastReadIndex;
			p_BTableChangesDelta->BT_Entry_Value =
					pbt[wLeastReadIndex];
			p_BTableChangesDelta->ValidFields = 0x0C;

			wResult = GLOB_LLD_Read_Page_Main_cdma(g_pTempBuf,
				dwOldBlockAddr, 0, DeviceInfo.wPagesPerBlock,
				LLD_CMD_FLAG_MODE_CDMA);
			if (wResult == FAIL)
				return wResult;

			ftl_cmd_cnt++;

			if (wResult != FAIL) {
				if (FAIL == GLOB_LLD_Write_Page_Main_cdma(
					g_pTempBuf, pbt[wBlockNum], 0,
					DeviceInfo.wPagesPerBlock)) {
					nand_dbg_print(NAND_DBG_WARN,
						"NAND Program fail in "
						"%s, Line %d, Function: %s, "
						"new Bad Block %d "
						"generated!\n",
						__FILE__, __LINE__, __func__,
						(int)pbt[wBlockNum]);
					wResult = FAIL;
					MARK_BLOCK_AS_BAD(pbt[wBlockNum]);
				}
				ftl_cmd_cnt++;
			}
#else
			wResult = GLOB_LLD_Read_Page_Main(g_pTempBuf,
				dwOldBlockAddr, 0, DeviceInfo.wPagesPerBlock);
			if (wResult == FAIL)
				return wResult;

			if (wResult != FAIL) {
				/* This is a bug. At this time, pbt[wBlockNum]
				is still the physical address of
				discard block, and should not be write.
				Have fixed it as below.
					-- Yunpeng 2008.12.19
				*/
				wResult = GLOB_LLD_Write_Page_Main(g_pTempBuf,
					wReplacedNode, 0,
					DeviceInfo.wPagesPerBlock);
				if (wResult == FAIL) {
					nand_dbg_print(NAND_DBG_WARN,
						"NAND Program fail in "
						"%s, Line %d, Function: %s, "
						"new Bad Block %d "
						"generated!\n",
						__FILE__, __LINE__, __func__,
						(int)wReplacedNode);
					MARK_BLOCK_AS_BAD(wReplacedNode);
				} else {
					pbt[wBlockNum] = wReplacedNode;
					pbt[wLeastReadIndex] = wTempNode;
				}
			}

			if ((wResult == PASS) && (g_cBlockTableStatus !=
				IN_PROGRESS_BLOCK_TABLE)) {
				g_cBlockTableStatus = IN_PROGRESS_BLOCK_TABLE;
				FTL_Write_IN_Progress_Block_Table_Page();
			}
#endif
		}
	} while (wResult != PASS)
	;

#if CMD_DMA
	/* ... */
#endif

	return wResult;
}

