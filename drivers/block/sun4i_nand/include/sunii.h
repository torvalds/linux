/*
************************************************************************************************************************
*                                                     suni define
*                            suni CPU hardware registers, memory, interrupts, ... define
*
*                             Copyright(C), 2009-2010, uLive Microelectronic Co., Ltd.
*											       All Rights Reserved
*
* File Name : sunii.h
*
* Author : kevin.z
*
* Version : 1.1.0
*
* Date : 2009-9-7 10:53
*
* Description : This file provides some defination of suni's hardware registers, memory, interrupt
*             and so on. This file is very similar to file "sunii.inc"; the two files should be
*             modified at the same time to keep coherence of information.
*
* Others : None at present.
*
*
* History :
*
*  <Author>        <time>       <version>      <description>
*
* kevin.z      2009-9-7 10:53    1.0.0        build the file
*
************************************************************************************************************************
*/

#ifndef  __SUNII_H_
#define  __SUNII_H_

extern void * nand_base;

#define __REG(x)    (*(volatile unsigned int   *)(nand_base + x))
#define __REGw(x)   (*(volatile unsigned int   *)(nand_base + x))
#define __REGhw(x)  (*(volatile unsigned short *)(nand_base + x))
#define __REGb(x)   (*(volatile unsigned char  *)(nand_base + x))
/*
*********************************************************************************************************
*   hardware registers base define
*********************************************************************************************************
*/
#define REGS_pBASE		0x01C00000		//寄存器物理地址
#define REGS_pSIZE      0x00300000      //寄存器物理空间大小
#define DRAM_pBASE      0x80000000
#define SRAM_pBASE      0x00000000
//#define SRAM_SIZE      (32 * 1024)

//#define	REGS_vBASE  0xf0000000      //??′??÷Dé?aμ??·
//#define DRAM_vBASE	0xc2000000
//#define SRAM_vBASE  0xffe00000

//#ifdef  USE_PHYSICAL_ADDRESS
//	#define SRAM_BASE               SRAM_pBASE
//	#define DRAM_BASE               DRAM_pBASE
//#else
//	#define SRAM_BASE               SRAM_vBASE
//	#define DRAM_BASE               DRAM_vBASE
//#endif    // #ifdef  USE_PHYSICAL_ADDRESS
//
// 物理地址
#define SRAM_REGS_pBASE         ( REGS_pBASE + 0x00000 )    //SRAM controller
#define DRAM_REGS_pBASE         ( REGS_pBASE + 0x01000 )    //SDRAM/DDR controller
#define DMAC_REGS_pBASE         ( REGS_pBASE + 0x02000 )    //DMA controller
#define NAFC_REGS_pBASE         ( REGS_pBASE + 0x03000 )    //nand flash controller
#define TSC_REGS_pBASE          ( REGS_pBASE + 0x04000 )    //transport stream interface
#define SPIC0_REGS_pBASE        ( REGS_pBASE + 0x05000 )    //spi
#define SPIC1_REGS_pBASE        ( REGS_pBASE + 0x06000 )    //spi
#define MSHC_REGS_pBASE         ( REGS_pBASE + 0x07000 )    //ms
#define CSIC_REGS_pBASE         ( REGS_pBASE + 0x09000 )    //csi controller
#define TVEC_REGS_pBASE         ( REGS_pBASE + 0x0a000 )    //tv
#define LCDC_REGS_pBASE         ( REGS_pBASE + 0x0c000 )    //lcd
#define MACC_REGS_pBASE         ( REGS_pBASE + 0x0e000 )    //media accelerate
#define SDMC0_REGS_pBASE        ( REGS_pBASE + 0x0f000 )    //sdmmc0 controller
#define SDMC1_REGS_pBASE        ( REGS_pBASE + 0x10000 )    //sdmmc1 controller
#define SDMC2_REGS_pBASE        ( REGS_pBASE + 0x11000 )    //sdmmc2 controller
#define SDMC3_REGS_pBASE        ( REGS_pBASE + 0x12000 )    //sdmmc3 controller
#define USBC0_REGS_pBASE        ( REGS_pBASE + 0x13000 )    //usb/otg 0 controller
#define USBC1_REGS_pBASE        ( REGS_pBASE + 0x14000 )    //usb/otg 1 controller
#define CCMU_REGS_pBASE         ( REGS_pBASE + 0x20000 )    //clock manager unit
#define INTC_REGS_pBASE         ( REGS_pBASE + 0x20400 )    //arm interrupt controller
#define PIOC_REGS_pBASE         ( REGS_pBASE + 0x20800 )    //general perpose I/O
#define TMRC_REGS_pBASE         ( REGS_pBASE + 0x20c00 )    //timer
#define UART0_REGS_pBASE        ( REGS_pBASE + 0x21000 )    //uart0
#define UART1_REGS_pBASE        ( REGS_pBASE + 0x21400 )    //uart1
#define UART2_REGS_pBASE        ( REGS_pBASE + 0x21800 )    //uart2
#define UART3_REGS_pBASE        ( REGS_pBASE + 0x21c00 )    //uart3
#define SPDIF_REGS_pBASE        ( REGS_pBASE + 0x22000 )    //SPDIF interface
#define PS2_REGS_pBASE          ( REGS_pBASE + 0x22400 )    //media accelerate
#define AC97_REGS_pBASE         ( REGS_pBASE + 0x22800 )    //AC97 interface
#define IRCC_REGS_pBASE         ( REGS_pBASE + 0x22c00 )    //fir
#define I2SC_REGS_pBASE         ( REGS_pBASE + 0x23000 )    //i2s
#define LRAC_REGS_pBASE         ( REGS_pBASE + 0x23400 )    //lradc
#define ADDA_REGS_pBASE         ( REGS_pBASE + 0x23c00 )    //AD/DA
#define TWIC0_REGS_pBASE        ( REGS_pBASE + 0x24000 )    //twi0
#define TWIC1_REGS_pBASE        ( REGS_pBASE + 0x24400 )    //twi1
#define TPC_REGS_pBASE          ( REGS_pBASE + 0x24800 )    //touch panel controller
#define DISE_REGS_pBASE         ( REGS_pBASE + 0x200000)    //display engine


#endif // end of #ifndef __SUNII_H_
