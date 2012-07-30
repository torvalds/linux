/*
 * drivers/block/sunxi_nand/src/include/nand_simple.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __PHY_BOOT__
#define  __PHY_BOOT__

#include "nand_oal.h"

#define SUCESS	0
#define FAIL	-1;
#define BADBLOCK -2

struct boot_physical_param{
	__u8   chip; //chip no
	__u16  block; // block no within chip
	__u16  page; // apge no within block
	__u16  sectorbitmap; //done't care
	void   *mainbuf; //data buf
	void   *oobbuf; //oob buf
};

extern __s32 PHY_SimpleErase(struct boot_physical_param * eraseop);
extern __s32 PHY_SimpleRead(struct boot_physical_param * readop);
extern __s32 PHY_SimpleWrite(struct boot_physical_param * writeop);
extern __s32 PHY_SimpleWrite_1K(struct boot_physical_param * writeop);
extern __s32 PHY_SimpleWrite_Seq(struct boot_physical_param * writeop);
extern __s32 PHY_SimpleRead_Seq(struct boot_physical_param * readop);
extern __s32 PHY_SimpleRead_1K(struct boot_physical_param * readop);
#endif
