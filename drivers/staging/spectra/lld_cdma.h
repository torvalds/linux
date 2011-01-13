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

/* header for LLD_CDMA.c module */

#ifndef _LLD_CDMA_
#define _LLD_CDMA_

#include "flash.h"

#define  DEBUG_SYNC    1

/*///////////   CDMA specific MACRO definition */
#define MAX_DESCS         (255)
#define MAX_CHANS  (4)
#define MAX_SYNC_POINTS         (16)
#define MAX_DESC_PER_CHAN     (MAX_DESCS * 3 + MAX_SYNC_POINTS + 2)

#define CHANNEL_SYNC_MASK       (0x000F)
#define CHANNEL_DMA_MASK        (0x00F0)
#define CHANNEL_ID_MASK         (0x0300)
#define CHANNEL_CONT_MASK       (0x4000)
#define CHANNEL_INTR_MASK       (0x8000)

#define CHANNEL_SYNC_OFFSET     (0)
#define CHANNEL_DMA_OFFSET      (4)
#define CHANNEL_ID_OFFSET       (8)
#define CHANNEL_CONT_OFFSET     (14)
#define CHANNEL_INTR_OFFSET     (15)

u16 CDMA_Data_CMD(u8 cmd, u8 *data, u32 block, u16 page, u16 num, u16 flags);
u16 CDMA_MemCopy_CMD(u8 *dest, u8 *src, u32 byte_cnt, u16 flags);
u16 CDMA_Execute_CMDs(void);
void print_pending_cmds(void);
void print_cdma_descriptors(void);

extern u8 g_SBDCmdIndex;
extern struct mrst_nand_info info;


/*///////////   prototypes: APIs for LLD_CDMA */
int is_cdma_interrupt(void);
u16 CDMA_Event_Status(void);

/* CMD-DMA Descriptor Struct.  These are defined by the CMD_DMA HW */
struct cdma_descriptor {
	u32 NxtPointerHi;
	u32 NxtPointerLo;
	u32 FlashPointerHi;
	u32 FlashPointerLo;
	u32 CommandType;
	u32 MemAddrHi;
	u32 MemAddrLo;
	u32 CommandFlags;
	u32 Channel;
	u32 Status;
	u32 MemCopyPointerHi;
	u32 MemCopyPointerLo;
	u32 Reserved12;
	u32 Reserved13;
	u32 Reserved14;
	u32 pcmd; /* pending cmd num related to this descriptor */
};

/* This struct holds one MemCopy descriptor as defined by the HW */
struct memcpy_descriptor {
	u32 NxtPointerHi;
	u32 NxtPointerLo;
	u32 SrcAddrHi;
	u32 SrcAddrLo;
	u32 DestAddrHi;
	u32 DestAddrLo;
	u32 XferSize;
	u32 MemCopyFlags;
	u32 MemCopyStatus;
	u32 reserved9;
	u32 reserved10;
	u32 reserved11;
	u32 reserved12;
	u32 reserved13;
	u32 reserved14;
	u32 reserved15;
};

/* Pending CMD table entries (includes MemCopy parameters */
struct pending_cmd {
	u8 CMD;
	u8 *DataAddr;
	u32 Block;
	u16 Page;
	u16 PageCount;
	u8 *DataDestAddr;
	u8 *DataSrcAddr;
	u32 MemCopyByteCnt;
	u16 Flags;
	u16 Status;
};

#if DEBUG_SYNC
extern u32 debug_sync_cnt;
#endif

/* Definitions for CMD DMA descriptor chain fields */
#define     CMD_DMA_DESC_COMP   0x8000
#define     CMD_DMA_DESC_FAIL   0x4000

#endif /*_LLD_CDMA_*/
