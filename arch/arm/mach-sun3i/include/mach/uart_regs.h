/*
**********************************************************************************************************************
*                                                  CCMU BSP for sun
*                                 CCMU hardware registers definition and BSP interfaces
*
*                             Copyright(C), 2006-2009, uLIVE
*											       All Rights Reserved
*
* File Name : ccmu.h
*
* Author : Jerry
*
* Version : 1.1.0
*
* Date : 2009-8-20 11:10:01
*
* Description : This file provides some definition of CCMU's hardware registers and BSP interfaces.
*             This file is very similar to file "ccmu.inc"; the two files should be modified at the
*             same time to keep coherence of information.
*
* Others : None at present.
*
*
* History :
*
*  <Author>        <time>       <version>      <description>
*
* Jerry      2008.05.23       1.1.0        build the file
*
* Jerry      2009-8-20        2.1.0        updata for new vision
*
**********************************************************************************************************************
*/
#ifndef _UART_REGS_
#define _UART_REGS_


#define PA_UARTS_BASE		0x01c21400
#define VA_UARTS_BASE		IO_ADDRESS(PA_UARTS_BASE)

#define UART_BASE0 0xf1c21000
#define UART_BASE1 0xf1c21400
#define UART_BASE2 0xf1c21800
#define UART_BASE3 0xf1c21c00


#endif    // #ifndef _UART_REGS_
