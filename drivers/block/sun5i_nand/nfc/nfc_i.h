/*
*********************************************************************************************************
*											        eBIOS
*						                the Base Input Output Subrutines
*									           dma controller sub system
*
*						        (c) Copyright 2006-2007, RICHARD,China
*											All	Rights Reserved
*
* File    : nfc_i.h
* By      : Richard.x
* Version : V1.00
*********************************************************************************************************
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
#define READ_RETRY_MAX_REG_NUM	4
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
extern __s32 NAND_QueryDmaStat(__hdle hDma);
extern __s32 NAND_SettingDMA(__hdle hDMA, void * pArg);
extern __s32 NAND_StartDMA(__u8 rw,__hdle hDMA, __u32 saddr, __u32 daddr, __u32 bytes);
extern __s32 NAND_GetPin(void);
extern __s32 NAND_ReleasePin(void);
extern __u32 NAND_GetBoardVersion(void);
extern __s32 NAND_WaitDmaFinish(void);
extern __s32 NAND_DMAEqueueBuf(__hdle hDma,  __u32 buff_addr, __u32 len);
extern void NAND_ClearRbInt(void);
extern void NAND_EnRbInt(void);

extern void NAND_RbInterrupt(void);
extern __s32 NAND_WaitRbReady(void);

#endif	/* _NFC_I_H_ */

