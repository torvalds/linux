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

#include "spectraswconfig.h"
#include "ffsport.h"
#include "ffsdefs.h"
#include "lld.h"
#include "lld_nand.h"

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
#if FLASH_EMU		/* vector all the LLD calls to the LLD_EMU code */
#include "lld_emu.h"
#include "lld_cdma.h"

/* common functions: */
u16 GLOB_LLD_Flash_Reset(void)
{
	return emu_Flash_Reset();
}

u16 GLOB_LLD_Read_Device_ID(void)
{
	return emu_Read_Device_ID();
}

int GLOB_LLD_Flash_Release(void)
{
	return emu_Flash_Release();
}

u16 GLOB_LLD_Flash_Init(void)
{
	return emu_Flash_Init();
}

u16 GLOB_LLD_Erase_Block(u32 block_add)
{
	return emu_Erase_Block(block_add);
}

u16 GLOB_LLD_Write_Page_Main(u8 *write_data, u32 block, u16 Page,
				u16 PageCount)
{
	return emu_Write_Page_Main(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Main(u8 *read_data, u32 block, u16 Page,
			       u16 PageCount)
{
	return emu_Read_Page_Main(read_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Main_Polling(u8 *read_data,
			u32 block, u16 page, u16 page_count)
{
	return emu_Read_Page_Main(read_data, block, page, page_count);
}

u16 GLOB_LLD_Write_Page_Main_Spare(u8 *write_data, u32 block,
				      u16 Page, u16 PageCount)
{
	return emu_Write_Page_Main_Spare(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Main_Spare(u8 *read_data, u32 block,
				     u16 Page, u16 PageCount)
{
	return emu_Read_Page_Main_Spare(read_data, block, Page, PageCount);
}

u16 GLOB_LLD_Write_Page_Spare(u8 *write_data, u32 block, u16 Page,
				 u16 PageCount)
{
	return emu_Write_Page_Spare(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Spare(u8 *read_data, u32 block, u16 Page,
				u16 PageCount)
{
	return emu_Read_Page_Spare(read_data, block, Page, PageCount);
}

u16  GLOB_LLD_Get_Bad_Block(u32 block)
{
    return  emu_Get_Bad_Block(block);
}

#endif /* FLASH_EMU */

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
#if FLASH_MTD		/* vector all the LLD calls to the LLD_MTD code */
#include "lld_mtd.h"
#include "lld_cdma.h"

/* common functions: */
u16 GLOB_LLD_Flash_Reset(void)
{
	return mtd_Flash_Reset();
}

u16 GLOB_LLD_Read_Device_ID(void)
{
	return mtd_Read_Device_ID();
}

int GLOB_LLD_Flash_Release(void)
{
	return mtd_Flash_Release();
}

u16 GLOB_LLD_Flash_Init(void)
{
	return mtd_Flash_Init();
}

u16 GLOB_LLD_Erase_Block(u32 block_add)
{
	return mtd_Erase_Block(block_add);
}

u16 GLOB_LLD_Write_Page_Main(u8 *write_data, u32 block, u16 Page,
				u16 PageCount)
{
	return mtd_Write_Page_Main(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Main(u8 *read_data, u32 block, u16 Page,
			       u16 PageCount)
{
	return mtd_Read_Page_Main(read_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Main_Polling(u8 *read_data,
			u32 block, u16 page, u16 page_count)
{
	return mtd_Read_Page_Main(read_data, block, page, page_count);
}

u16 GLOB_LLD_Write_Page_Main_Spare(u8 *write_data, u32 block,
				      u16 Page, u16 PageCount)
{
	return mtd_Write_Page_Main_Spare(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Main_Spare(u8 *read_data, u32 block,
				     u16 Page, u16 PageCount)
{
	return mtd_Read_Page_Main_Spare(read_data, block, Page, PageCount);
}

u16 GLOB_LLD_Write_Page_Spare(u8 *write_data, u32 block, u16 Page,
				 u16 PageCount)
{
	return mtd_Write_Page_Spare(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Spare(u8 *read_data, u32 block, u16 Page,
				u16 PageCount)
{
	return mtd_Read_Page_Spare(read_data, block, Page, PageCount);
}

u16  GLOB_LLD_Get_Bad_Block(u32 block)
{
    return  mtd_Get_Bad_Block(block);
}

#endif /* FLASH_MTD */

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
#if FLASH_NAND	/* vector all the LLD calls to the NAND controller code */
#include "lld_nand.h"
#include "lld_cdma.h"
#include "flash.h"

/* common functions for LLD_NAND */
void GLOB_LLD_ECC_Control(int enable)
{
	NAND_ECC_Ctrl(enable);
}

/* common functions for LLD_NAND */
u16 GLOB_LLD_Flash_Reset(void)
{
	return NAND_Flash_Reset();
}

u16 GLOB_LLD_Read_Device_ID(void)
{
	return NAND_Read_Device_ID();
}

u16 GLOB_LLD_UnlockArrayAll(void)
{
	return NAND_UnlockArrayAll();
}

u16 GLOB_LLD_Flash_Init(void)
{
	return NAND_Flash_Init();
}

int GLOB_LLD_Flash_Release(void)
{
	return nand_release_spectra();
}

u16 GLOB_LLD_Erase_Block(u32 block_add)
{
	return NAND_Erase_Block(block_add);
}


u16 GLOB_LLD_Write_Page_Main(u8 *write_data, u32 block, u16 Page,
				u16 PageCount)
{
	return NAND_Write_Page_Main(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Main(u8 *read_data, u32 block, u16 page,
			       u16 page_count)
{
	if (page_count == 1) /* Using polling to improve read speed */
		return NAND_Read_Page_Main_Polling(read_data, block, page, 1);
	else
		return NAND_Read_Page_Main(read_data, block, page, page_count);
}

u16 GLOB_LLD_Read_Page_Main_Polling(u8 *read_data,
			u32 block, u16 page, u16 page_count)
{
	return NAND_Read_Page_Main_Polling(read_data,
			block, page, page_count);
}

u16 GLOB_LLD_Write_Page_Main_Spare(u8 *write_data, u32 block,
				      u16 Page, u16 PageCount)
{
	return NAND_Write_Page_Main_Spare(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Write_Page_Spare(u8 *write_data, u32 block, u16 Page,
				 u16 PageCount)
{
	return NAND_Write_Page_Spare(write_data, block, Page, PageCount);
}

u16 GLOB_LLD_Read_Page_Main_Spare(u8 *read_data, u32 block,
				     u16 page, u16 page_count)
{
	return NAND_Read_Page_Main_Spare(read_data, block, page, page_count);
}

u16 GLOB_LLD_Read_Page_Spare(u8 *read_data, u32 block, u16 Page,
				u16 PageCount)
{
	return NAND_Read_Page_Spare(read_data, block, Page, PageCount);
}

u16  GLOB_LLD_Get_Bad_Block(u32 block)
{
	return  NAND_Get_Bad_Block(block);
}

#if CMD_DMA
u16 GLOB_LLD_Event_Status(void)
{
	return CDMA_Event_Status();
}

u16 glob_lld_execute_cmds(void)
{
	return CDMA_Execute_CMDs();
}

u16 GLOB_LLD_MemCopy_CMD(u8 *dest, u8 *src,
			u32 ByteCount, u16 flag)
{
	/* Replace the hardware memcopy with software memcpy function */
	if (CDMA_Execute_CMDs())
		return FAIL;
	memcpy(dest, src, ByteCount);
	return PASS;

	/* return CDMA_MemCopy_CMD(dest, src, ByteCount, flag); */
}

u16 GLOB_LLD_Erase_Block_cdma(u32 block, u16 flags)
{
	return CDMA_Data_CMD(ERASE_CMD, 0, block, 0, 0, flags);
}

u16 GLOB_LLD_Write_Page_Main_cdma(u8 *data, u32 block, u16 page, u16 count)
{
	return CDMA_Data_CMD(WRITE_MAIN_CMD, data, block, page, count, 0);
}

u16 GLOB_LLD_Read_Page_Main_cdma(u8 *data, u32 block, u16 page,
				u16 count, u16 flags)
{
	return CDMA_Data_CMD(READ_MAIN_CMD, data, block, page, count, flags);
}

u16 GLOB_LLD_Write_Page_Main_Spare_cdma(u8 *data, u32 block, u16 page,
					u16 count, u16 flags)
{
	return CDMA_Data_CMD(WRITE_MAIN_SPARE_CMD,
			data, block, page, count, flags);
}

u16 GLOB_LLD_Read_Page_Main_Spare_cdma(u8 *data,
				u32 block, u16 page, u16 count)
{
	return CDMA_Data_CMD(READ_MAIN_SPARE_CMD, data, block, page, count,
			LLD_CMD_FLAG_MODE_CDMA);
}

#endif /* CMD_DMA */
#endif /* FLASH_NAND */

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/

/* end of LLD.c */
