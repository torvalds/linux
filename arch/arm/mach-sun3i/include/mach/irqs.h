/*
 * arch/arm/mach-sun3i/include/mach/irqs.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __SW_MACH_IRQ_H
#define __SW_MACH_IRQ_H

#include <mach/platform.h>

#define NR_IRQS			64

/*----------- interrupt register list -------------------------------------------*/


/* registers */

/* mask */
#define SW_INT_START		         0

#define SW_INT_IRQNO_ENMI               0
#define SW_INT_IRQNO_UART0              1
#define SW_INT_IRQNO_UART1              2
#define SW_INT_IRQNO_UART2              3
#define SW_INT_IRQNO_UART3              4
#define SW_INT_IRQNO_CAN                5
#define SW_INT_IRQNO_IR                 6
#define SW_INT_IRQNO_TWI0               7
#define SW_INT_IRQNO_TWI1               8
#define SW_INT_IRQNO_RERSEV0            9
#define SW_INT_IRQNO_SPI00              10
#define SW_INT_IRQNO_SPI01              11
#define SW_INT_IRQNO_SPI02              12
#define SW_INT_IRQNO_SPDIF              13
#define SW_INT_IRQNO_AC97               14
#define SW_INT_IRQNO_TS                 15
#define SW_INT_IRQNO_I2S                16
#define SW_INT_IRQNO_UART4              17
#define SW_INT_IRQNO_UART5              18
#define SW_INT_IRQNO_UART6              19
#define SW_INT_IRQNO_UART7              20
#define SW_INT_IRQNO_PS2_0              21
#define SW_INT_IRQNO_TIMER0             22
#define SW_INT_IRQNO_TIMER1             23
#define SW_INT_IRQNO_TIMER2t5           24
#define SW_INT_IRQNO_ALARM              25
#define SW_INT_IRQNO_PS2_1              26
#define SW_INT_IRQNO_DMA                27
#define SW_INT_IRQNO_PIO                28
#define SW_INT_IRQNO_TOUCH_PANEL        29
#define SW_INT_IRQNO_AUDIO_CODEC        30
#define SW_INT_IRQNO_LRADC              31
#define SW_INT_IRQNO_SDMC0              32

#define SW_INTC_IRQNO_USB0              38
#define SW_INTC_IRQNO_USB1              39
#define SW_INTC_IRQNO_USB2              40
#define SW_INTC_IRQNO_CSI0              42


#define INTC_IRQNO_EMAC                 51
#define SW_INT_IRQNO_KEYPAD	53
#define SW_INT_END		 63


#endif

