/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : mem_serial.h
* By      : 
* Version : v1.0
* Date    : 2011-5-31 14:34
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __MEM_SERIAL_H__
#define __MEM_SERIAL_H__
#include "pm.h"


#define SUART_BASE_PA	SW_PA_UART0_IO_BASE
#define SUART_BASE_VA	SW_VA_UART0_IO_BASE

#define SUART_RBR_PA	(SUART_BASE_PA + 0x00)
#define SUART_THR_PA	(SUART_BASE_PA + 0x00)
#define SUART_DLL_PA	(SUART_BASE_PA + 0x00)
#define SUART_DLH_PA	(SUART_BASE_PA + 0x04)
#define SUART_FCR_PA	(SUART_BASE_PA + 0x08)
#define SUART_LCR_PA	(SUART_BASE_PA + 0x0c)
#define SUART_LSR_PA	(SUART_BASE_PA + 0x14)
#define SUART_USR_PA	(SUART_BASE_PA + 0x7c)
#define SUART_TX_FIFO_LEVEL_PA	(SUART_BASE_PA + 0x80)

#define SUART_HALT_PA	(SUART_BASE_PA + 0xa4)

#define SUART_RBR	(SUART_BASE_VA + 0x00)
#define SUART_THR	(SUART_BASE_VA + 0x00)
#define SUART_DLL	(SUART_BASE_VA + 0x00)
#define SUART_DLH	(SUART_BASE_VA + 0x04)
#define SUART_FCR	(SUART_BASE_VA + 0x08)
#define SUART_LCR	(SUART_BASE_VA + 0x0c)
#define SUART_LSR	(SUART_BASE_VA + 0x14)
#define SUART_USR	(SUART_BASE_VA + 0x7c)
#define SUART_TX_FIFO_LEVEL_VA	(SUART_BASE_VA + 0x80)

#define SUART_HALT	(SUART_BASE_VA + 0xa4)

#define SUART_BAUDRATE	(115200)
#define CCU_UART_PA		(0x01c2006C)
#define CCU_UART_VA		(0xF1c2006C)

void serial_init(void);
__s32 serial_puts(const char *string);
__u32 serial_gets(char* buf, __u32 n);
void serial_init_nommu(void);
__s32 serial_puts_nommu(const char *string);
__u32 serial_gets_nommu(char* buf, __u32 n);


#endif  /* __MEM_SERIAL_H__ */

