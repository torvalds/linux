/*
 * uart.h
 *
 *  Created on: 2012-4-25
 *      Author: Benn Huang (benn@allwinnertech.com)
 */

#ifndef UART_H_
#define UART_H_

/*
 * UART
 */
#define AW_UART_RBR                 0x00 /* Receive Buffer Register */
#define AW_UART_THR                 0x00 /* Transmit Holding Register */
#define AW_UART_DLL                 0x00 /* Divisor Latch Low Register */
#define AW_UART_DLH                 0x04 /* Diviso Latch High Register */
#define AW_UART_IER                 0x04 /* Interrupt Enable Register */
#define AW_UART_IIR                 0x08 /* Interrrupt Identity Register */
#define AW_UART_FCR                 0x08 /* FIFO Control Register */
#define AW_UART_LCR                 0x0c /* Line Control Register */
#define AW_UART_MCR                 0x10 /* Modem Control Register */
#define AW_UART_LSR                 0x14 /* Line Status Register */
#define AW_UART_MSR                 0x18 /* Modem Status Register */
#define AW_UART_SCH                 0x1c /* Scratch Register */
#define AW_UART_USR                 0x7c /* Status Register */
#define AW_UART_TFL                 0x80 /* Transmit FIFO Level */
#define AW_UART_RFL                 0x84 /* RFL */
#define AW_UART_HALT                0xa4 /* Halt TX Register */

#endif /* UART_H_ */
