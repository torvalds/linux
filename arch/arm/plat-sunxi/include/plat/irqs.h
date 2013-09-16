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

#ifndef __PLAT_IRQS_H
#define __PLAT_IRQS_H

#ifndef __MACH_IRQS_H__
#error plat/irqs.h may only be included from arch/irqs.h
#endif

#include <mach/platform.h>

/*----------- interrupt register list -------------------------------------------*/


/* registers */

/* mask */
#ifdef CONFIG_ARCH_SUN7I
#define AW_IRQ_GIC_START	32
#define SW_INT_START		AW_IRQ_GIC_START
#define NR_IRQS			(AW_IRQ_GIC_START + 128 + 32)
#define MAX_GIC_NR		1
#else
#define SW_INT_START		0
#define NR_IRQS			(96+32)
#endif

/*
 * sgi and ppi irq sources
 */
#ifdef CONFIG_ARCH_SUN7I

#define IRQ_SGI0		0
#define IRQ_SGI1		1
#define IRQ_SGI2		2
#define IRQ_SGI3		3
#define IRQ_SGI4		4
#define IRQ_SGI5		5
#define IRQ_SGI6		6
#define IRQ_SGI7		7
#define IRQ_SGI8		8
#define IRQ_SGI9		9
#define IRQ_SGI10		10
#define IRQ_SGI11		11
#define IRQ_SGI12		12
#define IRQ_SGI13		13
#define IRQ_SGI14		14
#define IRQ_SGI15		15
#define IRQ_PPI0		0
#define IRQ_PPI1		1
#define IRQ_PPI2		2
#define IRQ_PPI3		3
#define IRQ_PPI4		4
#define IRQ_PPI5		5
#define IRQ_PPI6		6
#define IRQ_PPI7		7
#define IRQ_PPI8		8
#define IRQ_PPI9		9
#define IRQ_PPI10		10
#define IRQ_PPI11		11
#define IRQ_PPI12		12
#define IRQ_PPI13		13
#define IRQ_PPI14		14
#define IRQ_PPI15		15

#endif /* CONFIG_ARCH_SUN7I */

#define SW_INT_IRQNO_ENMI		(0 + SW_INT_START)
#define SW_INT_IRQNO_NMI		(0 + SW_INT_START)
#define SW_INT_IRQNO_UART0		(1 + SW_INT_START)
#define SW_INT_IRQNO_UART1		(2 + SW_INT_START)
#define SW_INT_IRQNO_UART2		(3 + SW_INT_START)
#define SW_INT_IRQNO_UART3		(4 + SW_INT_START)
#define SW_INT_IRQNO_IR0		(5 + SW_INT_START)
#define SW_INT_IRQNO_IR1		(6 + SW_INT_START)
#define SW_INT_IRQNO_TWI0		(7 + SW_INT_START)
#define SW_INT_IRQNO_TWI1		(8 + SW_INT_START)
#define SW_INT_IRQNO_TWI2		(9 + SW_INT_START)
#define SW_INT_IRQNO_SPI00		(10 + SW_INT_START)
#define SW_INT_IRQNO_SPI01		(11 + SW_INT_START)
#define SW_INT_IRQNO_SPI02		(12 + SW_INT_START)
#define SW_INT_IRQNO_SPDIF		(13 + SW_INT_START)
#define SW_INT_IRQNO_AC97		(14 + SW_INT_START)
#define SW_INT_IRQNO_TS			(15 + SW_INT_START)
#define SW_INT_IRQNO_I2S		(16 + SW_INT_START)
#define SW_INT_IRQNO_UART4		(17 + SW_INT_START)
#define SW_INT_IRQNO_UART5		(18 + SW_INT_START)
#define SW_INT_IRQNO_UART6		(19 + SW_INT_START)
#define SW_INT_IRQNO_UART7		(20 + SW_INT_START)
#define SW_INT_IRQNO_KEYPAD		(21 + SW_INT_START)
#define SW_INT_IRQNO_TIMER0		(22 + SW_INT_START)
#define SW_INT_IRQNO_TIMER1		(23 + SW_INT_START)
#define SW_INT_IRQNO_ALARM		(24 + SW_INT_START)
#define SW_INT_IRQNO_TIMER2		(24 + SW_INT_START)
#define SW_INT_IRQNO_TIMER3		(25 + SW_INT_START)
#define SW_INT_IRQNO_CAN		(26 + SW_INT_START)
#define SW_INT_IRQNO_DMA		(27 + SW_INT_START)
#define SW_INT_IRQNO_PIO		(28 + SW_INT_START)
#define SW_INT_IRQNO_TOUCH_PANEL	(29 + SW_INT_START)
#define SW_INT_IRQNO_AUDIO_CODEC	(30 + SW_INT_START)
#define SW_INT_IRQNO_LRADC		(31 + SW_INT_START)
#define SW_INT_IRQNO_SDMC0		(32 + SW_INT_START)
#define SW_INT_IRQNO_SDMC1		(33 + SW_INT_START)
#define SW_INT_IRQNO_SDMC2		(34 + SW_INT_START)
#define SW_INT_IRQNO_SDMC3		(35 + SW_INT_START)
#define SW_INT_IRQNO_MEMSTICK		(36 + SW_INT_START)
#define SW_INT_IRQNO_NAND		(37 + SW_INT_START)

#define SW_INT_IRQNO_USB0		(38 + SW_INT_START)
#define SW_INT_IRQNO_USB1		(39 + SW_INT_START)
#define SW_INT_IRQNO_USB2		(40 + SW_INT_START)
#define SW_INTC_IRQNO_SCR		(41 + SW_INT_START)
#define SW_INTC_IRQNO_CSI0		(42 + SW_INT_START)
#define SW_INTC_IRQNO_CSI1		(43 + SW_INT_START)

#define SW_INT_IRQNO_LCDCTRL0		(44 + SW_INT_START)
#define SW_INT_IRQNO_LCDCTRL1		(45 + SW_INT_START)
#define SW_INT_IRQNO_MP			(46 + SW_INT_START)
#define SW_INT_IRQNO_DEFEBE0		(47 + SW_INT_START)
#define SW_INT_IRQNO_DEFEBE1		(48 + SW_INT_START)
#define SW_INT_IRQNO_PMU		(49 + SW_INT_START)
#define SW_INT_IRQNO_SPI3		(50 + SW_INT_START)
#define SW_INT_IRQNO_TZASC		(51 + SW_INT_START)
#define SW_INT_IRQNO_PATA		(52 + SW_INT_START)
#define SW_INT_IRQNO_VE			(53 + SW_INT_START)
#define SW_INT_IRQNO_SS			(54 + SW_INT_START)
#define SW_INT_IRQNO_EMAC		(55 + SW_INT_START)
#define SW_INT_IRQNO_SATA		(56 + SW_INT_START)
#define SW_INT_IRQNO_GPS		(57 + SW_INT_START)
#define SW_INT_IRQNO_HDMI		(58 + SW_INT_START)
#define SW_INT_IRQNO_TVE		(59 + SW_INT_START)
#define SW_INT_IRQNO_ACE		(60 + SW_INT_START)
#define SW_INT_IRQNO_TVD		(61 + SW_INT_START)
#define SW_INT_IRQNO_PS2_0		(62 + SW_INT_START)
#define SW_INT_IRQNO_PS2_1		(63 + SW_INT_START)
#define SW_INT_IRQNO_USB3		(64 + SW_INT_START)
#define SW_INT_IRQNO_USB4		(65 + SW_INT_START)
#define SW_INT_IRQNO_PLE_PFM		(66 + SW_INT_START)
#define SW_INT_IRQNO_TIMER4		(67 + SW_INT_START)
#define SW_INT_IRQNO_TIMER5		(68 + SW_INT_START)
#define SW_INT_IRQNO_GPU_GP		(69 + SW_INT_START)
#define SW_INT_IRQNO_GPU_GPMMU		(70 + SW_INT_START)
#define SW_INT_IRQNO_GPU_PP0		(71 + SW_INT_START)
#define SW_INT_IRQNO_GPU_PPMMU0		(72 + SW_INT_START)
#define SW_INT_IRQNO_GPU_PMU		(73 + SW_INT_START)

#ifdef CONFIG_ARCH_SUN7I
#define SW_INT_IRQNO_GPU_PP1            (74 + SW_INT_START)
#define SW_INT_IRQNO_GPU_PPMMU1         (75 + SW_INT_START)
#define SW_INT_IRQNO_GPU_RSV0           (76 + SW_INT_START)
#define SW_INT_IRQNO_GPU_RSV1           (77 + SW_INT_START)
#define SW_INT_IRQNO_GPU_RSV2           (78 + SW_INT_START)
#define SW_INT_IRQNO_GPU_RSV3           (79 + SW_INT_START)
#define SW_INT_IRQNO_GPU_RSV4           (80 + SW_INT_START)
#else
#define SW_INT_IRQNO_GPU_RSV0		(74 + SW_INT_START)
#define SW_INT_IRQNO_GPU_RSV1		(75 + SW_INT_START)
#define SW_INT_IRQNO_GPU_RSV2		(76 + SW_INT_START)
#define SW_INT_IRQNO_GPU_RSV3		(77 + SW_INT_START)
#define SW_INT_IRQNO_GPU_RSV4		(78 + SW_INT_START)
#define SW_INT_IRQNO_GPU_RSV5		(79 + SW_INT_START)
#define SW_INT_IRQNO_GPU_RSV6		(80 + SW_INT_START)
#endif

/* sun5i only */
#define SW_INT_IRQNO_SYNC_TIMER0	(82 + SW_INT_START)
#define SW_INT_IRQNO_SYNC_TIMER1	(83 + SW_INT_START)

/* sun7i only */
#define SW_INT_IRQNO_GMAC		(85 + SW_INT_START)
#define SW_INT_IRQNO_TWI3		(88 + SW_INT_START)
#define SW_INT_IRQNO_TWI4		(89 + SW_INT_START)
#ifdef CONFIG_ARCH_SUN7I
#define SW_INT_END				  (127 + SW_INT_START)
#else
#define SW_INT_END				  (95 + SW_INT_START)
#endif /* sun7i */

#endif
