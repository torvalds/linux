/*
 * arch/arch/mach-sun7i/include/mach/uart.h
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * liugang <liugang@allwinnertech.com>
 * csjamesdeng <csjamesdeng@allwinnertech.com>
 *
 * uart head file
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __SW_UART_H
#define __SW_UART_H

#define AW_UART_RBR 		0x00 /* Receive Buffer Register */
#define AW_UART_THR 		0x00 /* Transmit Holding Register */
#define AW_UART_DLL 		0x00 /* Divisor Latch Low Register */
#define AW_UART_DLH 		0x04 /* Diviso Latch High Register */
#define AW_UART_IER 		0x04 /* Interrupt Enable Register */
#define AW_UART_IIR 		0x08 /* Interrrupt Identity Register */
#define AW_UART_FCR 		0x08 /* FIFO Control Register */
#define AW_UART_LCR 		0x0c /* Line Control Register */
#define AW_UART_MCR 		0x10 /* Modem Control Register */
#define AW_UART_LSR 		0x14 /* Line Status Register */
#define AW_UART_MSR 		0x18 /* Modem Status Register */
#define AW_UART_SCH 		0x1c /* Scratch Register */
#define AW_UART_USR 		0x7c /* Status Register */
#define AW_UART_TFL 		0x80 /* Transmit FIFO Level */
#define AW_UART_RFL 		0x84 /* RFL */
#define AW_UART_HALT		0xa4 /* Halt TX Register */

#define UART_USR            (AW_UART_USR >> 2)
#define UART_HALT           (AW_UART_HALT >> 2)
#define UART_SCH            (AW_UART_SCH >> 2)
#define UART_FORCE_CFG      (1 << 1)
#define UART_FORCE_UPDATE   (1 << 2)

#define AW_UART_LOG(fmt, args...) do{} while(0)

#endif

