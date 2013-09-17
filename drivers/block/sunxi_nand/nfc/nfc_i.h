/*
 * drivers/block/sunxi_nand/nfc/nfc_i.h
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

#ifndef	_NFC_I_H_
#define	_NFC_I_H_

#include "../include/type_def.h"
#include "nfc.h"
//#include "../nfd/dma_for_nand.h"
//#include "ebios_i.h"
//#define MAX_ECC_BIT_CNT	8

#define NFC_READ_REG(reg)   		(reg)
#define NFC_WRITE_REG(reg,data) 	(reg) = (data)


#define ERR_ECC 	12
#define ECC_LIMIT 	10
#define ERR_TIMEOUT 14
#define READ_RETRY_MAX_TYPE_NUM 5
#define READ_RETRY_MAX_REG_NUM	16
#define READ_RETRY_MAX_CYCLE	10
#define LSB_MODE_MAX_REG_NUM	8
/* define various unit data input or output*/
#define NFC_READ_RAM_B(ram)    		(*((volatile __u8 *)(NAND_IO_BASE + ram)))
#define NFC_WRITE_RAM_B(ram,data)  	(*((volatile __u8 *)(NAND_IO_BASE + ram)) = (data))
#define NFC_READ_RAM_HW(ram)   		(*((volatile __u16 *)(NAND_IO_BASE + ram)))
#define NFC_WRITE_RAM_HW(ram,data) 	(*((volatile __u16 *)(NAND_IO_BASE + ram)) = (data))
#define NFC_READ_RAM_W(ram)   		(*((volatile __u32 *)(NAND_IO_BASE + ram)))
#define NFC_WRITE_RAM_W(ram,data) 	(*((volatile __u32 *)(NAND_IO_BASE + ram)) = (data))



#ifdef USE_PHYSICAL_ADDRESS
#define NFC_IS_SDRAM(addr)			((addr >= DRAM_BASE)?1:0)
#else
#define NFC_IS_SDRAM(addr)			( ((addr >= DRAM_BASE))&&(addr < SRAM_BASE)?1:0)
#endif

extern __hdle NAND_RequestDMA(__u32 dmatype);
extern __s32 NAND_ReleaseDMA(__hdle hDma);
extern void NAND_Config_Start_DMA(__u8 rw, dma_addr_t buff_addr, __u32 len);
extern __s32 NAND_WaitDmaFinish(void);
extern void NAND_ClearRbInt(void);
extern void NAND_EnRbInt(void);

extern void NAND_RbInterrupt(void);
extern __s32 NAND_WaitRbReady(void);


#endif	/* _NFC_I_H_ */
