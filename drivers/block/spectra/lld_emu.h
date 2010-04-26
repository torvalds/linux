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

#ifndef _LLD_EMU_
#define _LLD_EMU_

#include "ffsport.h"
#include "ffsdefs.h"

/* prototypes: emulator API functions */
extern u16 emu_Flash_Reset(void);
extern u16 emu_Flash_Init(void);
extern int emu_Flash_Release(void);
extern u16 emu_Read_Device_ID(void);
extern u16 emu_Erase_Block(u32 block_addr);
extern u16 emu_Write_Page_Main(u8 *write_data, u32 Block,
				u16 Page, u16 PageCount);
extern u16 emu_Read_Page_Main(u8 *read_data, u32 Block, u16 Page,
				 u16 PageCount);
extern u16 emu_Event_Status(void);
extern void emu_Enable_Disable_Interrupts(u16 INT_ENABLE);
extern u16 emu_Write_Page_Main_Spare(u8 *write_data, u32 Block,
					u16 Page, u16 PageCount);
extern u16 emu_Write_Page_Spare(u8 *write_data, u32 Block,
					u16 Page, u16 PageCount);
extern u16 emu_Read_Page_Main_Spare(u8 *read_data, u32 Block,
				       u16 Page, u16 PageCount);
extern u16 emu_Read_Page_Spare(u8 *read_data, u32 Block, u16 Page,
				  u16 PageCount);
extern u16 emu_Get_Bad_Block(u32 block);

u16 emu_CDMA_Flash_Init(void);
u16 emu_CDMA_Execute_CMDs(u16 tag_count);
u16 emu_CDMA_Event_Status(void);
#endif /*_LLD_EMU_*/
