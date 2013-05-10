/*
 * arch/arm/plat-sunxi/include/plat/irqs.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
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

#ifndef __SW_IRQS_H
#define __SW_IRQS_H


#include <mach/platform.h>

#define NR_IRQS					(96+32)

/*----------- interrupt register list -------------------------------------------*/


/* registers */

/* mask */
#define SW_INT_START	                  0

#define SW_INT_IRQNO_ENMI               0
#define SW_INT_IRQNO_UART0              1
#define SW_INT_IRQNO_UART1              2
#define SW_INT_IRQNO_UART2              3
#define SW_INT_IRQNO_UART3              4
#define SW_INT_IRQNO_IR0                5
#define SW_INT_IRQNO_IR1                6
#define SW_INT_IRQNO_TWI0               7
#define SW_INT_IRQNO_TWI1               8
#define SW_INT_IRQNO_TWI2               9
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
#define SW_INT_IRQNO_KEYPAD             21
#define SW_INT_IRQNO_TIMER0             22
#define SW_INT_IRQNO_TIMER1             23
#define SW_INT_IRQNO_ALARM              24
#define SW_INT_IRQNO_TIMER2             24
#define SW_INT_IRQNO_TIMER3             25
#define SW_INT_IRQNO_CAN                26
#define SW_INT_IRQNO_DMA                27
#define SW_INT_IRQNO_PIO                28
#define SW_INT_IRQNO_TOUCH_PANEL        29
#define SW_INT_IRQNO_AUDIO_CODEC        30
#define SW_INT_IRQNO_LRADC              31
#define SW_INT_IRQNO_SDMC0              32
#define SW_INT_IRQNO_SDMC1              33
#define SW_INT_IRQNO_SDMC2              34
#define SW_INT_IRQNO_SDMC3              35
#define SW_INT_IRQNO_MEMSTICK           36
#define SW_INT_IRQNO_NAND               37

#define SW_INTC_IRQNO_USB0              38
#define SW_INTC_IRQNO_USB1              39
#define SW_INTC_IRQNO_USB2              40
#define SW_INTC_IRQNO_SCR               41
#define SW_INTC_IRQNO_CSI0              42
#define SW_INTC_IRQNO_CSI1              43

#define SW_INT_IRQNO_LCDCTRL0           44
#define SW_INT_IRQNO_LCDCTRL1           45
#define SW_INT_IRQNO_MP                 46
#define SW_INT_IRQNO_DEFEBE0            47
#define SW_INT_IRQNO_DEFEBE1            48
#define SW_INT_IRQNO_PMU                49
#define SW_INT_IRQNO_SPI3               50
#define SW_INT_IRQNO_TZASC              51
#define SW_INT_IRQNO_PATA               52
#define SW_INT_IRQNO_VE                 53
#define SW_INT_IRQNO_SS                 54
#define SW_INT_IRQNO_EMAC               55
#define SW_INT_IRQNO_SATA               56
#define SW_INT_IRQNO_GPS                57
#define SW_INT_IRQNO_HDMI               58
#define SW_INT_IRQNO_TVE                59
#define SW_INT_IRQNO_ACE                60
#define SW_INT_IRQNO_TVD                61
#define SW_INT_IRQNO_PS2_0              62
#define SW_INT_IRQNO_PS2_1              63
#define SW_INT_IRQNO_USB3               64
#define SW_INT_IRQNO_USB4               65
#define SW_INT_IRQNO_PLE_PFM            66
#define SW_INT_IRQNO_TIMER4             67
#define SW_INT_IRQNO_TIMER5             68
#define SW_INT_IRQNO_GPU_GP             69
#define SW_INT_IRQNO_GPU_GPMMU          70
#define SW_INT_IRQNO_GPU_PP0            71
#define SW_INT_IRQNO_GPU_PPMMU0         72
#define SW_INT_IRQNO_GPU_PMU            73
#define SW_INT_IRQNO_GPU_RSV0           74
#define SW_INT_IRQNO_GPU_RSV1           75
#define SW_INT_IRQNO_GPU_RSV2           76
#define SW_INT_IRQNO_GPU_RSV3           77
#define SW_INT_IRQNO_GPU_RSV4           78
#define SW_INT_IRQNO_GPU_RSV5           79
#define SW_INT_IRQNO_GPU_RSV6           80

#ifdef CONFIG_ARCH_SUN5I
#define SW_INT_IRQNO_SYNC_TIMER0	82
#define SW_INT_IRQNO_SYNC_TIMER1	83
#endif

#define SW_INT_END		                  95

#endif
