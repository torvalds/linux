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
#include <linux/mtd/mtd.h>
#include "flash.h"
#include "ffsdefs.h"
#include "lld_emu.h"
#include "lld.h"
#if CMD_DMA
#include "lld_cdma.h"
#endif

#define GLOB_LLD_PAGES           64
#define GLOB_LLD_PAGE_SIZE       (512+16)
#define GLOB_LLD_PAGE_DATA_SIZE  512
#define GLOB_LLD_BLOCKS          2048

#if CMD_DMA
#include "lld_cdma.h"
u32 totalUsedBanks;
u32 valid_banks[MAX_CHANS];
#endif

static struct mtd_info *spectra_mtd;
static int mtddev = -1;
module_param(mtddev, int, 0);

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     mtd_Flash_Init
* Inputs:       none
* Outputs:      PASS=0 (notice 0=ok here)
* Description:  Creates & initializes the flash RAM array.
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 mtd_Flash_Init(void)
{
	if (mtddev == -1) {
		printk(KERN_ERR "No MTD device specified. Give mtddev parameter\n");
		return FAIL;
	}

	spectra_mtd = get_mtd_device(NULL, mtddev);
	if (!spectra_mtd) {
		printk(KERN_ERR "Failed to obtain MTD device #%d\n", mtddev);
		return FAIL;
	}

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     mtd_Flash_Release
* Inputs:       none
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Releases the flash.
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int mtd_Flash_Release(void)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);
	if (!spectra_mtd)
		return PASS;

	put_mtd_device(spectra_mtd);
	spectra_mtd = NULL;

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     mtd_Read_Device_ID
* Inputs:       none
* Outputs:      PASS=1 FAIL=0
* Description:  Reads the info from the controller registers.
*               Sets up DeviceInfo structure with device parameters
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/

u16 mtd_Read_Device_ID(void)
{
	uint64_t tmp;
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (!spectra_mtd)
		return FAIL;

	DeviceInfo.wDeviceMaker = 0;
	DeviceInfo.wDeviceType = 8;
	DeviceInfo.wSpectraStartBlock = SPECTRA_START_BLOCK;
	tmp = spectra_mtd->size;
	do_div(tmp, spectra_mtd->erasesize);
	DeviceInfo.wTotalBlocks = tmp;
	DeviceInfo.wSpectraEndBlock = DeviceInfo.wTotalBlocks - 1;
	DeviceInfo.wPagesPerBlock = spectra_mtd->erasesize / spectra_mtd->writesize;
	DeviceInfo.wPageSize = spectra_mtd->writesize + spectra_mtd->oobsize;
	DeviceInfo.wPageDataSize = spectra_mtd->writesize;
	DeviceInfo.wPageSpareSize = spectra_mtd->oobsize;
	DeviceInfo.wBlockSize = DeviceInfo.wPageSize * DeviceInfo.wPagesPerBlock;
	DeviceInfo.wBlockDataSize = DeviceInfo.wPageDataSize * DeviceInfo.wPagesPerBlock;
	DeviceInfo.wDataBlockNum = (u32) (DeviceInfo.wSpectraEndBlock -
						DeviceInfo.wSpectraStartBlock
						+ 1);
	DeviceInfo.MLCDevice = 0;//spectra_mtd->celltype & NAND_CI_CELLTYPE_MSK;
	DeviceInfo.nBitsInPageNumber =
		(u8)GLOB_Calc_Used_Bits(DeviceInfo.wPagesPerBlock);
	DeviceInfo.nBitsInPageDataSize =
		(u8)GLOB_Calc_Used_Bits(DeviceInfo.wPageDataSize);
	DeviceInfo.nBitsInBlockDataSize =
		(u8)GLOB_Calc_Used_Bits(DeviceInfo.wBlockDataSize);

#if CMD_DMA
	totalUsedBanks = 4;
	valid_banks[0] = 1;
	valid_banks[1] = 1;
	valid_banks[2] = 1;
	valid_banks[3] = 1;
#endif

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     mtd_Flash_Reset
* Inputs:       none
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Reset the flash
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 mtd_Flash_Reset(void)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	return PASS;
}

void erase_callback(struct erase_info *e)
{
	complete((void *)e->priv);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     mtd_Erase_Block
* Inputs:       Address
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Erase a block
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 mtd_Erase_Block(u32 block_add)
{
	struct erase_info erase;
	DECLARE_COMPLETION_ONSTACK(comp);
	int ret;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (block_add >= DeviceInfo.wTotalBlocks) {
		printk(KERN_ERR "mtd_Erase_Block error! "
		       "Too big block address: %d\n", block_add);
		return FAIL;
	}

	nand_dbg_print(NAND_DBG_DEBUG, "Erasing block %d\n",
		(int)block_add);

	erase.mtd = spectra_mtd;
	erase.callback = erase_callback;
	erase.addr = block_add * spectra_mtd->erasesize;
	erase.len = spectra_mtd->erasesize;
	erase.priv = (unsigned long)&comp;

	ret = spectra_mtd->erase(spectra_mtd, &erase);
	if (!ret) {
		wait_for_completion(&comp);
		if (erase.state != MTD_ERASE_DONE)
			ret = -EIO;
	}
	if (ret) {
		printk(KERN_WARNING "mtd_Erase_Block error! "
		       "erase of region [0x%llx, 0x%llx] failed\n",
		       erase.addr, erase.len);
		return FAIL;
	}

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     mtd_Write_Page_Main
* Inputs:       Write buffer address pointer
*               Block number
*               Page  number
*               Number of pages to process
* Outputs:      PASS=0 (notice 0=ok here)
* Description:  Write the data in the buffer to main area of flash
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 mtd_Write_Page_Main(u8 *write_data, u32 Block,
			   u16 Page, u16 PageCount)
{
	size_t retlen;
	int ret = 0;

	if (Block >= DeviceInfo.wTotalBlocks)
		return FAIL;

	if (Page + PageCount > DeviceInfo.wPagesPerBlock)
		return FAIL;

	nand_dbg_print(NAND_DBG_DEBUG, "mtd_Write_Page_Main: "
		       "lba %u Page %u PageCount %u\n",
		       (unsigned int)Block,
		       (unsigned int)Page, (unsigned int)PageCount);


	while (PageCount) {
		ret = spectra_mtd->write(spectra_mtd,
					 (Block * spectra_mtd->erasesize) + (Page * spectra_mtd->writesize),
					 DeviceInfo.wPageDataSize, &retlen, write_data);
		if (ret) {
			printk(KERN_ERR "%s failed %d\n", __func__, ret);
			return FAIL;
		}
		write_data += DeviceInfo.wPageDataSize;
		Page++;
		PageCount--;
	}

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     mtd_Read_Page_Main
* Inputs:       Read buffer address pointer
*               Block number
*               Page  number
*               Number of pages to process
* Outputs:      PASS=0 (notice 0=ok here)
* Description:  Read the data from the flash main area to the buffer
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 mtd_Read_Page_Main(u8 *read_data, u32 Block,
			  u16 Page, u16 PageCount)
{
	size_t retlen;
	int ret = 0;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (Block >= DeviceInfo.wTotalBlocks)
		return FAIL;

	if (Page + PageCount > DeviceInfo.wPagesPerBlock)
		return FAIL;

	nand_dbg_print(NAND_DBG_DEBUG, "mtd_Read_Page_Main: "
		       "lba %u Page %u PageCount %u\n",
		       (unsigned int)Block,
		       (unsigned int)Page, (unsigned int)PageCount);


	while (PageCount) {
		ret = spectra_mtd->read(spectra_mtd,
					(Block * spectra_mtd->erasesize) + (Page * spectra_mtd->writesize),
					DeviceInfo.wPageDataSize, &retlen, read_data);
		if (ret) {
			printk(KERN_ERR "%s failed %d\n", __func__, ret);
			return FAIL;
		}
		read_data += DeviceInfo.wPageDataSize;
		Page++;
		PageCount--;
	}

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	return PASS;
}

#ifndef ELDORA
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     mtd_Read_Page_Main_Spare
* Inputs:       Write Buffer
*                       Address
*                       Buffer size
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Read from flash main+spare area
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 mtd_Read_Page_Main_Spare(u8 *read_data, u32 Block,
				u16 Page, u16 PageCount)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (Block >= DeviceInfo.wTotalBlocks) {
		printk(KERN_ERR "Read Page Main+Spare "
		       "Error: Block Address too big\n");
		return FAIL;
	}

	if (Page + PageCount > DeviceInfo.wPagesPerBlock) {
		printk(KERN_ERR "Read Page Main+Spare "
		       "Error: Page number %d+%d too big in block %d\n",
		       Page, PageCount, Block);
		return FAIL;
	}

	nand_dbg_print(NAND_DBG_DEBUG, "Read Page Main + Spare - "
		       "No. of pages %u block %u start page %u\n",
		       (unsigned int)PageCount,
		       (unsigned int)Block, (unsigned int)Page);


	while (PageCount) {
		struct mtd_oob_ops ops;
		int ret;

		ops.mode = MTD_OOB_AUTO;
		ops.datbuf = read_data;
		ops.len = DeviceInfo.wPageDataSize;
		ops.oobbuf = read_data + DeviceInfo.wPageDataSize + BTSIG_OFFSET;
		ops.ooblen = BTSIG_BYTES;
		ops.ooboffs = 0;

		ret = spectra_mtd->read_oob(spectra_mtd,
					    (Block * spectra_mtd->erasesize) + (Page * spectra_mtd->writesize),
					    &ops);
		if (ret) {
			printk(KERN_ERR "%s failed %d\n", __func__, ret);
			return FAIL;
		}
		read_data += DeviceInfo.wPageSize;
		Page++;
		PageCount--;
	}

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     mtd_Write_Page_Main_Spare
* Inputs:       Write buffer
*                       address
*                       buffer length
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Write the buffer to main+spare area of flash
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 mtd_Write_Page_Main_Spare(u8 *write_data, u32 Block,
				 u16 Page, u16 page_count)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (Block >= DeviceInfo.wTotalBlocks) {
		printk(KERN_ERR "Write Page Main + Spare "
		       "Error: Block Address too big\n");
		return FAIL;
	}

	if (Page + page_count > DeviceInfo.wPagesPerBlock) {
		printk(KERN_ERR "Write Page Main + Spare "
		       "Error: Page number %d+%d too big in block %d\n",
		       Page, page_count, Block);
		WARN_ON(1);
		return FAIL;
	}

	nand_dbg_print(NAND_DBG_DEBUG, "Write Page Main+Spare - "
		       "No. of pages %u block %u start page %u\n",
		       (unsigned int)page_count,
		       (unsigned int)Block, (unsigned int)Page);

	while (page_count) {
		struct mtd_oob_ops ops;
		int ret;

		ops.mode = MTD_OOB_AUTO;
		ops.datbuf = write_data;
		ops.len = DeviceInfo.wPageDataSize;
		ops.oobbuf = write_data + DeviceInfo.wPageDataSize + BTSIG_OFFSET;
		ops.ooblen = BTSIG_BYTES;
		ops.ooboffs = 0;

		ret = spectra_mtd->write_oob(spectra_mtd,
					     (Block * spectra_mtd->erasesize) + (Page * spectra_mtd->writesize),
					     &ops);
		if (ret) {
			printk(KERN_ERR "%s failed %d\n", __func__, ret);
			return FAIL;
		}
		write_data += DeviceInfo.wPageSize;
		Page++;
		page_count--;
	}

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     mtd_Write_Page_Spare
* Inputs:       Write buffer
*                       Address
*                       buffer size
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Write the buffer in the spare area
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 mtd_Write_Page_Spare(u8 *write_data, u32 Block,
			    u16 Page, u16 PageCount)
{
	WARN_ON(1);
	return FAIL;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     mtd_Read_Page_Spare
* Inputs:       Write Buffer
*                       Address
*                       Buffer size
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Read data from the spare area
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 mtd_Read_Page_Spare(u8 *read_data, u32 Block,
			   u16 Page, u16 PageCount)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (Block >= DeviceInfo.wTotalBlocks) {
		printk(KERN_ERR "Read Page Spare "
		       "Error: Block Address too big\n");
		return FAIL;
	}

	if (Page + PageCount > DeviceInfo.wPagesPerBlock) {
		printk(KERN_ERR "Read Page Spare "
		       "Error: Page number too big\n");
		return FAIL;
	}

	nand_dbg_print(NAND_DBG_DEBUG, "Read Page Spare- "
		       "block %u page %u (%u pages)\n",
		       (unsigned int)Block, (unsigned int)Page, PageCount);

	while (PageCount) {
		struct mtd_oob_ops ops;
		int ret;

		ops.mode = MTD_OOB_AUTO;
		ops.datbuf = NULL;
		ops.len = 0;
		ops.oobbuf = read_data;
		ops.ooblen = BTSIG_BYTES;
		ops.ooboffs = 0;

		ret = spectra_mtd->read_oob(spectra_mtd,
					    (Block * spectra_mtd->erasesize) + (Page * spectra_mtd->writesize),
					    &ops);
		if (ret) {
			printk(KERN_ERR "%s failed %d\n", __func__, ret);
			return FAIL;
		}

		read_data += DeviceInfo.wPageSize;
		Page++;
		PageCount--;
	}

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     mtd_Enable_Disable_Interrupts
* Inputs:       enable or disable
* Outputs:      none
* Description:  NOP
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
void mtd_Enable_Disable_Interrupts(u16 INT_ENABLE)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);
}

u16 mtd_Get_Bad_Block(u32 block)
{
	return 0;
}

#if CMD_DMA
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Support for CDMA functions
************************************
*       mtd_CDMA_Flash_Init
*           CDMA_process_data command   (use LLD_CDMA)
*           CDMA_MemCopy_CMD            (use LLD_CDMA)
*       mtd_CDMA_execute all commands
*       mtd_CDMA_Event_Status
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 mtd_CDMA_Flash_Init(void)
{
	u16 i;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	for (i = 0; i < MAX_DESCS + MAX_CHANS; i++) {
		PendingCMD[i].CMD = 0;
		PendingCMD[i].Tag = 0;
		PendingCMD[i].DataAddr = 0;
		PendingCMD[i].Block = 0;
		PendingCMD[i].Page = 0;
		PendingCMD[i].PageCount = 0;
		PendingCMD[i].DataDestAddr = 0;
		PendingCMD[i].DataSrcAddr = 0;
		PendingCMD[i].MemCopyByteCnt = 0;
		PendingCMD[i].ChanSync[0] = 0;
		PendingCMD[i].ChanSync[1] = 0;
		PendingCMD[i].ChanSync[2] = 0;
		PendingCMD[i].ChanSync[3] = 0;
		PendingCMD[i].ChanSync[4] = 0;
		PendingCMD[i].Status = 3;
	}

	return PASS;
}

static void mtd_isr(int irq, void *dev_id)
{
	/* TODO:  ... */
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_Execute_CMDs
* Inputs:       tag_count:  the number of pending cmds to do
* Outputs:      PASS/FAIL
* Description:  execute each command in the pending CMD array
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 mtd_CDMA_Execute_CMDs(u16 tag_count)
{
	u16 i, j;
	u8 CMD;		/* cmd parameter */
	u8 *data;
	u32 block;
	u16 page;
	u16 count;
	u16 status = PASS;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	nand_dbg_print(NAND_DBG_TRACE, "At start of Execute CMDs: "
		       "Tag Count %u\n", tag_count);

	for (i = 0; i < totalUsedBanks; i++) {
		PendingCMD[i].CMD = DUMMY_CMD;
		PendingCMD[i].Tag = 0xFF;
		PendingCMD[i].Block =
		    (DeviceInfo.wTotalBlocks / totalUsedBanks) * i;

		for (j = 0; j <= MAX_CHANS; j++)
			PendingCMD[i].ChanSync[j] = 0;
	}

	CDMA_Execute_CMDs(tag_count);

#ifdef VERBOSE
		print_pending_cmds(tag_count);
#endif
#if DEBUG_SYNC
	}
	debug_sync_cnt++;
#endif

	for (i = MAX_CHANS;
	     i < tag_count + MAX_CHANS; i++) {
		CMD = PendingCMD[i].CMD;
		data = PendingCMD[i].DataAddr;
		block = PendingCMD[i].Block;
		page = PendingCMD[i].Page;
		count = PendingCMD[i].PageCount;

		switch (CMD) {
		case ERASE_CMD:
			mtd_Erase_Block(block);
			PendingCMD[i].Status = PASS;
			break;
		case WRITE_MAIN_CMD:
			mtd_Write_Page_Main(data, block, page, count);
			PendingCMD[i].Status = PASS;
			break;
		case WRITE_MAIN_SPARE_CMD:
			mtd_Write_Page_Main_Spare(data, block, page, count);
			PendingCMD[i].Status = PASS;
			break;
		case READ_MAIN_CMD:
			mtd_Read_Page_Main(data, block, page, count);
			PendingCMD[i].Status = PASS;
			break;
		case MEMCOPY_CMD:
			memcpy(PendingCMD[i].DataDestAddr,
			       PendingCMD[i].DataSrcAddr,
			       PendingCMD[i].MemCopyByteCnt);
		case DUMMY_CMD:
			PendingCMD[i].Status = PASS;
			break;
		default:
			PendingCMD[i].Status = FAIL;
			break;
		}
	}

	/*
	 * Temperory adding code to reset PendingCMD array for basic testing.
	 * It should be done at the end of  event status function.
	 */
	for (i = tag_count + MAX_CHANS; i < MAX_DESCS; i++) {
		PendingCMD[i].CMD = 0;
		PendingCMD[i].Tag = 0;
		PendingCMD[i].DataAddr = 0;
		PendingCMD[i].Block = 0;
		PendingCMD[i].Page = 0;
		PendingCMD[i].PageCount = 0;
		PendingCMD[i].DataDestAddr = 0;
		PendingCMD[i].DataSrcAddr = 0;
		PendingCMD[i].MemCopyByteCnt = 0;
		PendingCMD[i].ChanSync[0] = 0;
		PendingCMD[i].ChanSync[1] = 0;
		PendingCMD[i].ChanSync[2] = 0;
		PendingCMD[i].ChanSync[3] = 0;
		PendingCMD[i].ChanSync[4] = 0;
		PendingCMD[i].Status = CMD_NOT_DONE;
	}

	nand_dbg_print(NAND_DBG_TRACE, "At end of Execute CMDs.\n");

	mtd_isr(0, 0); /* This is a null isr now. Need fill it in future */

	return status;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     mtd_Event_Status
* Inputs:       none
* Outputs:      Event_Status code
* Description:  This function can also be used to force errors
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 mtd_CDMA_Event_Status(void)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	return EVENT_PASS;
}

#endif /* CMD_DMA */
#endif /* !ELDORA */
