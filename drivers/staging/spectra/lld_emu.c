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
#include "lld_emu.h"
#include "lld.h"
#if CMD_DMA
#include "lld_cdma.h"
#endif

#define GLOB_LLD_PAGES           64
#define GLOB_LLD_PAGE_SIZE       (512+16)
#define GLOB_LLD_PAGE_DATA_SIZE  512
#define GLOB_LLD_BLOCKS          2048

#if (CMD_DMA  && FLASH_EMU)
#include "lld_cdma.h"
u32 totalUsedBanks;
u32 valid_banks[MAX_CHANS];
#endif

#if FLASH_EMU			/* This is for entire module */

static u8 *flash_memory[GLOB_LLD_BLOCKS * GLOB_LLD_PAGES];

/* Read nand emu file and then fill it's content to flash_memory */
int emu_load_file_to_mem(void)
{
	mm_segment_t fs;
	struct file *nef_filp = NULL;
	struct inode *inode = NULL;
	loff_t nef_size = 0;
	loff_t tmp_file_offset, file_offset;
	ssize_t nread;
	int i, rc = -EINVAL;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	fs = get_fs();
	set_fs(get_ds());

	nef_filp = filp_open("/root/nand_emu_file", O_RDWR | O_LARGEFILE, 0);
	if (IS_ERR(nef_filp)) {
		printk(KERN_ERR "filp_open error: "
		       "Unable to open nand emu file!\n");
		return PTR_ERR(nef_filp);
	}

	if (nef_filp->f_path.dentry) {
		inode = nef_filp->f_path.dentry->d_inode;
	} else {
		printk(KERN_ERR "Can not get valid inode!\n");
		goto out;
	}

	nef_size = i_size_read(inode->i_mapping->host);
	if (nef_size <= 0) {
		printk(KERN_ERR "Invalid nand emu file size: "
		       "0x%llx\n", nef_size);
		goto out;
	} else {
		nand_dbg_print(NAND_DBG_DEBUG, "nand emu file size: %lld\n",
			       nef_size);
	}

	file_offset = 0;
	for (i = 0; i < GLOB_LLD_BLOCKS * GLOB_LLD_PAGES; i++) {
		tmp_file_offset = file_offset;
		nread = vfs_read(nef_filp,
				 (char __user *)flash_memory[i],
				 GLOB_LLD_PAGE_SIZE, &tmp_file_offset);
		if (nread < GLOB_LLD_PAGE_SIZE) {
			printk(KERN_ERR "%s, Line %d - "
			       "nand emu file partial read: "
			       "%d bytes\n", __FILE__, __LINE__, (int)nread);
			goto out;
		}
		file_offset += GLOB_LLD_PAGE_SIZE;
	}
	rc = 0;

out:
	filp_close(nef_filp, current->files);
	set_fs(fs);
	return rc;
}

/* Write contents of flash_memory to nand emu file */
int emu_write_mem_to_file(void)
{
	mm_segment_t fs;
	struct file *nef_filp = NULL;
	struct inode *inode = NULL;
	loff_t nef_size = 0;
	loff_t tmp_file_offset, file_offset;
	ssize_t nwritten;
	int i, rc = -EINVAL;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	fs = get_fs();
	set_fs(get_ds());

	nef_filp = filp_open("/root/nand_emu_file", O_RDWR | O_LARGEFILE, 0);
	if (IS_ERR(nef_filp)) {
		printk(KERN_ERR "filp_open error: "
		       "Unable to open nand emu file!\n");
		return PTR_ERR(nef_filp);
	}

	if (nef_filp->f_path.dentry) {
		inode = nef_filp->f_path.dentry->d_inode;
	} else {
		printk(KERN_ERR "Invalid " "nef_filp->f_path.dentry value!\n");
		goto out;
	}

	nef_size = i_size_read(inode->i_mapping->host);
	if (nef_size <= 0) {
		printk(KERN_ERR "Invalid "
		       "nand emu file size: 0x%llx\n", nef_size);
		goto out;
	} else {
		nand_dbg_print(NAND_DBG_DEBUG, "nand emu file size: "
			       "%lld\n", nef_size);
	}

	file_offset = 0;
	for (i = 0; i < GLOB_LLD_BLOCKS * GLOB_LLD_PAGES; i++) {
		tmp_file_offset = file_offset;
		nwritten = vfs_write(nef_filp,
				     (char __user *)flash_memory[i],
				     GLOB_LLD_PAGE_SIZE, &tmp_file_offset);
		if (nwritten < GLOB_LLD_PAGE_SIZE) {
			printk(KERN_ERR "%s, Line %d - "
			       "nand emu file partial write: "
			       "%d bytes\n", __FILE__, __LINE__, (int)nwritten);
			goto out;
		}
		file_offset += GLOB_LLD_PAGE_SIZE;
	}
	rc = 0;

out:
	filp_close(nef_filp, current->files);
	set_fs(fs);
	return rc;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     emu_Flash_Init
* Inputs:       none
* Outputs:      PASS=0 (notice 0=ok here)
* Description:  Creates & initializes the flash RAM array.
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 emu_Flash_Init(void)
{
	int i;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	flash_memory[0] = (u8 *)vmalloc(GLOB_LLD_PAGE_SIZE *
						   GLOB_LLD_BLOCKS *
						   GLOB_LLD_PAGES *
						   sizeof(u8));
	if (!flash_memory[0]) {
		printk(KERN_ERR "Fail to allocate memory "
		       "for nand emulator!\n");
		return ERR;
	}

	memset((char *)(flash_memory[0]), 0xFF,
	       GLOB_LLD_PAGE_SIZE * GLOB_LLD_BLOCKS * GLOB_LLD_PAGES *
	       sizeof(u8));

	for (i = 1; i < GLOB_LLD_BLOCKS * GLOB_LLD_PAGES; i++)
		flash_memory[i] = flash_memory[i - 1] + GLOB_LLD_PAGE_SIZE;

	emu_load_file_to_mem(); /* Load nand emu file to mem */

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     emu_Flash_Release
* Inputs:       none
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Releases the flash.
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int emu_Flash_Release(void)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	emu_write_mem_to_file();  /* Write back mem to nand emu file */

	vfree(flash_memory[0]);
	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     emu_Read_Device_ID
* Inputs:       none
* Outputs:      PASS=1 FAIL=0
* Description:  Reads the info from the controller registers.
*               Sets up DeviceInfo structure with device parameters
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/

u16 emu_Read_Device_ID(void)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	DeviceInfo.wDeviceMaker = 0;
	DeviceInfo.wDeviceType = 8;
	DeviceInfo.wSpectraStartBlock = 36;
	DeviceInfo.wSpectraEndBlock = GLOB_LLD_BLOCKS - 1;
	DeviceInfo.wTotalBlocks = GLOB_LLD_BLOCKS;
	DeviceInfo.wPagesPerBlock = GLOB_LLD_PAGES;
	DeviceInfo.wPageSize = GLOB_LLD_PAGE_SIZE;
	DeviceInfo.wPageDataSize = GLOB_LLD_PAGE_DATA_SIZE;
	DeviceInfo.wPageSpareSize = GLOB_LLD_PAGE_SIZE -
	    GLOB_LLD_PAGE_DATA_SIZE;
	DeviceInfo.wBlockSize = DeviceInfo.wPageSize * GLOB_LLD_PAGES;
	DeviceInfo.wBlockDataSize = DeviceInfo.wPageDataSize * GLOB_LLD_PAGES;
	DeviceInfo.wDataBlockNum = (u32) (DeviceInfo.wSpectraEndBlock -
						DeviceInfo.wSpectraStartBlock
						+ 1);
	DeviceInfo.MLCDevice = 1; /* Emulate MLC device */
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
* Function:     emu_Flash_Reset
* Inputs:       none
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Reset the flash
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 emu_Flash_Reset(void)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     emu_Erase_Block
* Inputs:       Address
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Erase a block
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 emu_Erase_Block(u32 block_add)
{
	int i;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (block_add >= DeviceInfo.wTotalBlocks) {
		printk(KERN_ERR "emu_Erase_Block error! "
		       "Too big block address: %d\n", block_add);
		return FAIL;
	}

	nand_dbg_print(NAND_DBG_DEBUG, "Erasing block %d\n",
		(int)block_add);

	for (i = block_add * GLOB_LLD_PAGES;
	     i < ((block_add + 1) * GLOB_LLD_PAGES); i++) {
		if (flash_memory[i]) {
			memset((u8 *)(flash_memory[i]), 0xFF,
			       DeviceInfo.wPageSize * sizeof(u8));
		}
	}

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     emu_Write_Page_Main
* Inputs:       Write buffer address pointer
*               Block number
*               Page  number
*               Number of pages to process
* Outputs:      PASS=0 (notice 0=ok here)
* Description:  Write the data in the buffer to main area of flash
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 emu_Write_Page_Main(u8 *write_data, u32 Block,
			   u16 Page, u16 PageCount)
{
	int i;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (Block >= DeviceInfo.wTotalBlocks)
		return FAIL;

	if (Page + PageCount > DeviceInfo.wPagesPerBlock)
		return FAIL;

	nand_dbg_print(NAND_DBG_DEBUG, "emu_Write_Page_Main: "
		       "lba %u Page %u PageCount %u\n",
		       (unsigned int)Block,
		       (unsigned int)Page, (unsigned int)PageCount);

	for (i = 0; i < PageCount; i++) {
		if (NULL == flash_memory[Block * GLOB_LLD_PAGES + Page]) {
			printk(KERN_ERR "Run out of memory\n");
			return FAIL;
		}
		memcpy((u8 *) (flash_memory[Block * GLOB_LLD_PAGES + Page]),
		       write_data, DeviceInfo.wPageDataSize);
		write_data += DeviceInfo.wPageDataSize;
		Page++;
	}

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     emu_Read_Page_Main
* Inputs:       Read buffer address pointer
*               Block number
*               Page  number
*               Number of pages to process
* Outputs:      PASS=0 (notice 0=ok here)
* Description:  Read the data from the flash main area to the buffer
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 emu_Read_Page_Main(u8 *read_data, u32 Block,
			  u16 Page, u16 PageCount)
{
	int i;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (Block >= DeviceInfo.wTotalBlocks)
		return FAIL;

	if (Page + PageCount > DeviceInfo.wPagesPerBlock)
		return FAIL;

	nand_dbg_print(NAND_DBG_DEBUG, "emu_Read_Page_Main: "
		       "lba %u Page %u PageCount %u\n",
		       (unsigned int)Block,
		       (unsigned int)Page, (unsigned int)PageCount);

	for (i = 0; i < PageCount; i++) {
		if (NULL == flash_memory[Block * GLOB_LLD_PAGES + Page]) {
			memset(read_data, 0xFF, DeviceInfo.wPageDataSize);
		} else {
			memcpy(read_data,
			       (u8 *) (flash_memory[Block * GLOB_LLD_PAGES
						      + Page]),
			       DeviceInfo.wPageDataSize);
		}
		read_data += DeviceInfo.wPageDataSize;
		Page++;
	}

	return PASS;
}

#ifndef ELDORA
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     emu_Read_Page_Main_Spare
* Inputs:       Write Buffer
*                       Address
*                       Buffer size
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Read from flash main+spare area
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 emu_Read_Page_Main_Spare(u8 *read_data, u32 Block,
				u16 Page, u16 PageCount)
{
	int i;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (Block >= DeviceInfo.wTotalBlocks) {
		printk(KERN_ERR "Read Page Main+Spare "
		       "Error: Block Address too big\n");
		return FAIL;
	}

	if (Page + PageCount > DeviceInfo.wPagesPerBlock) {
		printk(KERN_ERR "Read Page Main+Spare "
		       "Error: Page number too big\n");
		return FAIL;
	}

	nand_dbg_print(NAND_DBG_DEBUG, "Read Page Main + Spare - "
		       "No. of pages %u block %u start page %u\n",
		       (unsigned int)PageCount,
		       (unsigned int)Block, (unsigned int)Page);

	for (i = 0; i < PageCount; i++) {
		if (NULL == flash_memory[Block * GLOB_LLD_PAGES + Page]) {
			memset(read_data, 0xFF, DeviceInfo.wPageSize);
		} else {
			memcpy(read_data, (u8 *) (flash_memory[Block *
								 GLOB_LLD_PAGES
								 + Page]),
			       DeviceInfo.wPageSize);
		}

		read_data += DeviceInfo.wPageSize;
		Page++;
	}

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     emu_Write_Page_Main_Spare
* Inputs:       Write buffer
*                       address
*                       buffer length
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Write the buffer to main+spare area of flash
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 emu_Write_Page_Main_Spare(u8 *write_data, u32 Block,
				 u16 Page, u16 page_count)
{
	u16 i;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (Block >= DeviceInfo.wTotalBlocks) {
		printk(KERN_ERR "Write Page Main + Spare "
		       "Error: Block Address too big\n");
		return FAIL;
	}

	if (Page + page_count > DeviceInfo.wPagesPerBlock) {
		printk(KERN_ERR "Write Page Main + Spare "
		       "Error: Page number too big\n");
		return FAIL;
	}

	nand_dbg_print(NAND_DBG_DEBUG, "Write Page Main+Spare - "
		       "No. of pages %u block %u start page %u\n",
		       (unsigned int)page_count,
		       (unsigned int)Block, (unsigned int)Page);

	for (i = 0; i < page_count; i++) {
		if (NULL == flash_memory[Block * GLOB_LLD_PAGES + Page]) {
			printk(KERN_ERR "Run out of memory!\n");
			return FAIL;
		}
		memcpy((u8 *) (flash_memory[Block * GLOB_LLD_PAGES + Page]),
		       write_data, DeviceInfo.wPageSize);
		write_data += DeviceInfo.wPageSize;
		Page++;
	}

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     emu_Write_Page_Spare
* Inputs:       Write buffer
*                       Address
*                       buffer size
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Write the buffer in the spare area
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 emu_Write_Page_Spare(u8 *write_data, u32 Block,
			    u16 Page, u16 PageCount)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (Block >= DeviceInfo.wTotalBlocks) {
		printk(KERN_ERR "Read Page Spare Error: "
		       "Block Address too big\n");
		return FAIL;
	}

	if (Page + PageCount > DeviceInfo.wPagesPerBlock) {
		printk(KERN_ERR "Read Page Spare Error: "
		       "Page number too big\n");
		return FAIL;
	}

	nand_dbg_print(NAND_DBG_DEBUG, "Write Page Spare- "
		       "block %u page %u\n",
		       (unsigned int)Block, (unsigned int)Page);

	if (NULL == flash_memory[Block * GLOB_LLD_PAGES + Page]) {
		printk(KERN_ERR "Run out of memory!\n");
		return FAIL;
	}

	memcpy((u8 *) (flash_memory[Block * GLOB_LLD_PAGES + Page] +
			 DeviceInfo.wPageDataSize), write_data,
	       (DeviceInfo.wPageSize - DeviceInfo.wPageDataSize));

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     emu_Read_Page_Spare
* Inputs:       Write Buffer
*                       Address
*                       Buffer size
* Outputs:      PASS=0 (notice 0=ok here)
* Description:          Read data from the spare area
*
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 emu_Read_Page_Spare(u8 *write_data, u32 Block,
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
		       "block %u page %u\n",
		       (unsigned int)Block, (unsigned int)Page);

	if (NULL == flash_memory[Block * GLOB_LLD_PAGES + Page]) {
		memset(write_data, 0xFF,
		       (DeviceInfo.wPageSize - DeviceInfo.wPageDataSize));
	} else {
		memcpy(write_data,
		       (u8 *) (flash_memory[Block * GLOB_LLD_PAGES + Page]
				 + DeviceInfo.wPageDataSize),
		       (DeviceInfo.wPageSize - DeviceInfo.wPageDataSize));
	}

	return PASS;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     emu_Enable_Disable_Interrupts
* Inputs:       enable or disable
* Outputs:      none
* Description:  NOP
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
void emu_Enable_Disable_Interrupts(u16 INT_ENABLE)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);
}

u16 emu_Get_Bad_Block(u32 block)
{
	return 0;
}

#if CMD_DMA
/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Support for CDMA functions
************************************
*       emu_CDMA_Flash_Init
*           CDMA_process_data command   (use LLD_CDMA)
*           CDMA_MemCopy_CMD            (use LLD_CDMA)
*       emu_CDMA_execute all commands
*       emu_CDMA_Event_Status
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 emu_CDMA_Flash_Init(void)
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

static void emu_isr(int irq, void *dev_id)
{
	/* TODO:  ... */
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     CDMA_Execute_CMDs
* Inputs:       tag_count:  the number of pending cmds to do
* Outputs:      PASS/FAIL
* Description:  execute each command in the pending CMD array
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 emu_CDMA_Execute_CMDs(u16 tag_count)
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

	print_pending_cmds(tag_count);

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
			emu_Erase_Block(block);
			PendingCMD[i].Status = PASS;
			break;
		case WRITE_MAIN_CMD:
			emu_Write_Page_Main(data, block, page, count);
			PendingCMD[i].Status = PASS;
			break;
		case WRITE_MAIN_SPARE_CMD:
			emu_Write_Page_Main_Spare(data, block, page, count);
			PendingCMD[i].Status = PASS;
			break;
		case READ_MAIN_CMD:
			emu_Read_Page_Main(data, block, page, count);
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

	emu_isr(0, 0); /* This is a null isr now. Need fill it in future */

	return status;
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     emu_Event_Status
* Inputs:       none
* Outputs:      Event_Status code
* Description:  This function can also be used to force errors
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u16 emu_CDMA_Event_Status(void)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	return EVENT_PASS;
}

#endif /* CMD_DMA */
#endif /* !ELDORA */
#endif /* FLASH_EMU */
