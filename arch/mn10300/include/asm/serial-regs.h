/* MN10300 on-board serial port module registers
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_SERIAL_REGS_H
#define _ASM_SERIAL_REGS_H

#include <asm/cpu-regs.h>
#include <asm/intctl-regs.h>

#ifdef __KERNEL__

/* serial port 0 */
#define	SC0CTR			__SYSREG(0xd4002000, u16)	/* control reg */
#define	SC01CTR_CK		0x0007	/* clock source select */
#define	SC0CTR_CK_TM8UFLOW_8	0x0000	/* - 1/8 timer 8 underflow (serial port 0 only) */
#define	SC1CTR_CK_TM9UFLOW_8	0x0000	/* - 1/8 timer 9 underflow (serial port 1 only) */
#define	SC01CTR_CK_IOCLK_8	0x0001	/* - 1/8 IOCLK */
#define	SC01CTR_CK_IOCLK_32	0x0002	/* - 1/32 IOCLK */
#define	SC0CTR_CK_TM2UFLOW_2	0x0003	/* - 1/2 timer 2 underflow (serial port 0 only) */
#define	SC1CTR_CK_TM3UFLOW_2	0x0003	/* - 1/2 timer 3 underflow (serial port 1 only) */
#define	SC0CTR_CK_TM0UFLOW_8	0x0004	/* - 1/8 timer 1 underflow (serial port 0 only) */
#define	SC1CTR_CK_TM1UFLOW_8	0x0004	/* - 1/8 timer 2 underflow (serial port 1 only) */
#define	SC0CTR_CK_TM2UFLOW_8	0x0005	/* - 1/8 timer 2 underflow (serial port 0 only) */
#define	SC1CTR_CK_TM3UFLOW_8	0x0005	/* - 1/8 timer 3 underflow (serial port 1 only) */
#define	SC01CTR_CK_EXTERN_8	0x0006	/* - 1/8 external closk */
#define	SC01CTR_CK_EXTERN	0x0007	/* - external closk */
#define	SC01CTR_STB		0x0008	/* stop bit select */
#define	SC01CTR_STB_1BIT	0x0000	/* - 1 stop bit */
#define	SC01CTR_STB_2BIT	0x0008	/* - 2 stop bits */
#define	SC01CTR_PB		0x0070	/* parity bit select */
#define	SC01CTR_PB_NONE		0x0000	/* - no parity */
#define	SC01CTR_PB_FIXED0	0x0040	/* - fixed at 0 */
#define	SC01CTR_PB_FIXED1	0x0050	/* - fixed at 1 */
#define	SC01CTR_PB_EVEN		0x0060	/* - even parity */
#define	SC01CTR_PB_ODD		0x0070	/* - odd parity */
#define	SC01CTR_CLN		0x0080	/* character length */
#define	SC01CTR_CLN_7BIT	0x0000	/* - 7 bit chars */
#define	SC01CTR_CLN_8BIT	0x0080	/* - 8 bit chars */
#define	SC01CTR_TOE		0x0100	/* T input output enable */
#define	SC01CTR_OD		0x0200	/* bit order select */
#define	SC01CTR_OD_LSBFIRST	0x0000	/* - LSB first */
#define	SC01CTR_OD_MSBFIRST	0x0200	/* - MSB first */
#define	SC01CTR_MD		0x0c00	/* mode select */
#define SC01CTR_MD_STST_SYNC	0x0000	/* - start-stop synchronous */
#define SC01CTR_MD_CLOCK_SYNC1	0x0400	/* - clock synchronous 1 */
#define SC01CTR_MD_I2C		0x0800	/* - I2C mode */
#define SC01CTR_MD_CLOCK_SYNC2	0x0c00	/* - clock synchronous 2 */
#define	SC01CTR_IIC		0x1000	/* I2C mode select */
#define	SC01CTR_BKE		0x2000	/* break transmit enable */
#define	SC01CTR_RXE		0x4000	/* receive enable */
#define	SC01CTR_TXE		0x8000	/* transmit enable */

#define	SC0ICR			__SYSREG(0xd4002004, u8)	/* interrupt control reg */
#define SC01ICR_DMD		0x80	/* output data mode */
#define SC01ICR_TD		0x20	/* transmit DMA trigger cause */
#define SC01ICR_TI		0x10	/* transmit interrupt cause */
#define SC01ICR_RES		0x04	/* receive error select */
#define SC01ICR_RI		0x01	/* receive interrupt cause */

#define	SC0TXB			__SYSREG(0xd4002008, u8)	/* transmit buffer reg */
#define	SC0RXB			__SYSREG(0xd4002009, u8)	/* receive buffer reg */

#define	SC0STR			__SYSREG(0xd400200c, u16)	/* status reg */
#define SC01STR_OEF		0x0001	/* overrun error found */
#define SC01STR_PEF		0x0002	/* parity error found */
#define SC01STR_FEF		0x0004	/* framing error found */
#define SC01STR_RBF		0x0010	/* receive buffer status */
#define SC01STR_TBF		0x0020	/* transmit buffer status */
#define SC01STR_RXF		0x0040	/* receive status */
#define SC01STR_TXF		0x0080	/* transmit status */
#define SC01STR_STF		0x0100	/* I2C start sequence found */
#define SC01STR_SPF		0x0200	/* I2C stop sequence found */

#define SC0RXIRQ		20	/* timer 0 Receive IRQ */
#define SC0TXIRQ		21	/* timer 0 Transmit IRQ */

#define	SC0RXICR		GxICR(SC0RXIRQ)	/* serial 0 receive intr ctrl reg */
#define	SC0TXICR		GxICR(SC0TXIRQ)	/* serial 0 transmit intr ctrl reg */

/* serial port 1 */
#define	SC1CTR			__SYSREG(0xd4002010, u16)	/* serial port 1 control */
#define	SC1ICR			__SYSREG(0xd4002014, u8)	/* interrupt control reg */
#define	SC1TXB			__SYSREG(0xd4002018, u8)	/* transmit buffer reg */
#define	SC1RXB			__SYSREG(0xd4002019, u8)	/* receive buffer reg */
#define	SC1STR			__SYSREG(0xd400201c, u16)	/* status reg */

#define SC1RXIRQ		22	/* timer 1 Receive IRQ */
#define SC1TXIRQ		23	/* timer 1 Transmit IRQ */

#define	SC1RXICR		GxICR(SC1RXIRQ)	/* serial 1 receive intr ctrl reg */
#define	SC1TXICR		GxICR(SC1TXIRQ)	/* serial 1 transmit intr ctrl reg */

/* serial port 2 */
#define	SC2CTR			__SYSREG(0xd4002020, u16)	/* control reg */
#define	SC2CTR_CK		0x0003	/* clock source select */
#define	SC2CTR_CK_TM10UFLOW	0x0000	/* - timer 10 underflow */
#define	SC2CTR_CK_TM2UFLOW	0x0001	/* - timer 2 underflow */
#define	SC2CTR_CK_EXTERN	0x0002	/* - external closk */
#define	SC2CTR_CK_TM3UFLOW	0x0003	/* - timer 3 underflow */
#define	SC2CTR_STB		0x0008	/* stop bit select */
#define	SC2CTR_STB_1BIT		0x0000	/* - 1 stop bit */
#define	SC2CTR_STB_2BIT		0x0008	/* - 2 stop bits */
#define	SC2CTR_PB		0x0070	/* parity bit select */
#define	SC2CTR_PB_NONE		0x0000	/* - no parity */
#define	SC2CTR_PB_FIXED0	0x0040	/* - fixed at 0 */
#define	SC2CTR_PB_FIXED1	0x0050	/* - fixed at 1 */
#define	SC2CTR_PB_EVEN		0x0060	/* - even parity */
#define	SC2CTR_PB_ODD		0x0070	/* - odd parity */
#define	SC2CTR_CLN		0x0080	/* character length */
#define	SC2CTR_CLN_7BIT		0x0000	/* - 7 bit chars */
#define	SC2CTR_CLN_8BIT		0x0080	/* - 8 bit chars */
#define	SC2CTR_TWE		0x0100	/* transmit wait enable (enable XCTS control) */
#define	SC2CTR_OD		0x0200	/* bit order select */
#define	SC2CTR_OD_LSBFIRST	0x0000	/* - LSB first */
#define	SC2CTR_OD_MSBFIRST	0x0200	/* - MSB first */
#define	SC2CTR_TWS		0x1000	/* transmit wait select */
#define	SC2CTR_TWS_XCTS_HIGH	0x0000	/* - interrupt TX when XCTS high */
#define	SC2CTR_TWS_XCTS_LOW	0x1000	/* - interrupt TX when XCTS low */
#define	SC2CTR_BKE		0x2000	/* break transmit enable */
#define	SC2CTR_RXE		0x4000	/* receive enable */
#define	SC2CTR_TXE		0x8000	/* transmit enable */

#define	SC2ICR			__SYSREG(0xd4002024, u8)	/* interrupt control reg */
#define SC2ICR_TD		0x20	/* transmit DMA trigger cause */
#define SC2ICR_TI		0x10	/* transmit interrupt cause */
#define SC2ICR_RES		0x04	/* receive error select */
#define SC2ICR_RI		0x01	/* receive interrupt cause */

#define	SC2TXB			__SYSREG(0xd4002018, u8)	/* transmit buffer reg */
#define	SC2RXB			__SYSREG(0xd4002019, u8)	/* receive buffer reg */
#define	SC2STR			__SYSREG(0xd400201c, u8)	/* status reg */
#define SC2STR_OEF		0x0001	/* overrun error found */
#define SC2STR_PEF		0x0002	/* parity error found */
#define SC2STR_FEF		0x0004	/* framing error found */
#define SC2STR_CTS		0x0008	/* XCTS input pin status (0 means high) */
#define SC2STR_RBF		0x0010	/* receive buffer status */
#define SC2STR_TBF		0x0020	/* transmit buffer status */
#define SC2STR_RXF		0x0040	/* receive status */
#define SC2STR_TXF		0x0080	/* transmit status */

#define	SC2TIM			__SYSREG(0xd400202d, u8)	/* status reg */

#define SC2RXIRQ		24	/* serial 2 Receive IRQ */
#define SC2TXIRQ		25	/* serial 2 Transmit IRQ */

#define	SC2RXICR		GxICR(SC2RXIRQ)	/* serial 2 receive intr ctrl reg */
#define	SC2TXICR		GxICR(SC2TXIRQ)	/* serial 2 transmit intr ctrl reg */


#endif /* __KERNEL__ */

#endif /* _ASM_SERIAL_REGS_H */
