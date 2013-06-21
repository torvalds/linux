/*
 * arch/arm/mach-sun7i/include/mach/irqs-sun7i.h
 *
 *  Copyright (C) 2012-2016 Allwinner Limited
 *  Benn Huang (benn@allwinnertech.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __MACH_IRQS_AW_H
#define __MACH_IRQS_AW_H

#define AW_IRQ_GIC_START        32

/*
 * sgi and ppi irq sources
 */
#define IRQ_SGI0       		0
#define IRQ_SGI1       		1
#define IRQ_SGI2       		2
#define IRQ_SGI3       		3
#define IRQ_SGI4       		4
#define IRQ_SGI5       		5
#define IRQ_SGI6       		6
#define IRQ_SGI7       		7
#define IRQ_SGI8       		8
#define IRQ_SGI9       		9
#define IRQ_SGI10      		10
#define IRQ_SGI11      		11
#define IRQ_SGI12      		12
#define IRQ_SGI13      		13
#define IRQ_SGI14      		14
#define IRQ_SGI15      		15
#define IRQ_PPI0       		0
#define IRQ_PPI1       		1
#define IRQ_PPI2       		2
#define IRQ_PPI3       		3
#define IRQ_PPI4       		4
#define IRQ_PPI5       		5
#define IRQ_PPI6       		6
#define IRQ_PPI7       		7
#define IRQ_PPI8       		8
#define IRQ_PPI9       		9
#define IRQ_PPI10      		10
#define IRQ_PPI11      		11
#define IRQ_PPI12      		12
#define IRQ_PPI13      		13
#define IRQ_PPI14      		14
#define IRQ_PPI15      		15

/*
 * AW on-board gic irq sources
 */
#if defined(CONFIG_AW_FPGA_PLATFORM)
/* NOTE: maybe err, need redifine irqs number on sun7i fpga */
#define AW_IRQ_NMI       	(AW_IRQ_GIC_START + 0 )    /* NMI    */
#define AW_IRQ_UART0     	(AW_IRQ_GIC_START + 1 )    /* UART0  */
#define AW_IRQ_UART1     	(AW_IRQ_GIC_START + 2 )    /* UART1  */
#define AW_IRQ_UART2     	(AW_IRQ_GIC_START + 3 )    /* UART2  */
#define AW_IRQ_UART3     	(AW_IRQ_GIC_START + 4 )    /* UART3  */
#define AW_IRQ_IR0       	(AW_IRQ_GIC_START + 2 )    /* IR0 (on fpga, irq2 is shared by IR0(37), twi0(39), keypad(53), can(58), GPIO(60), ps2-0(94)) */
#define AW_IRQ_IR1       	(AW_IRQ_GIC_START + 6 )    /* IR1    */
#define AW_IRQ_TWI0       	(AW_IRQ_GIC_START + 2 )    /* TWI0 (on fpga, irq2 is shared by IR0(37), twi0(39), keypad(53), can(58), GPIO(60), ps2-0(94)) */
#define AW_IRQ_TWI1       	(AW_IRQ_GIC_START + 8 )    /* TWI1   */
#define AW_IRQ_TWI2       	(AW_IRQ_GIC_START + 9 )    /* TWI2   */
#define AW_IRQ_SPI0       	(AW_IRQ_GIC_START + 15)    /* SPI0   */
#define AW_IRQ_SPI1       	(AW_IRQ_GIC_START + 11)    /* SPI1   */
#define AW_IRQ_SPI2       	(AW_IRQ_GIC_START + 12)    /* SPI2   */
#define AW_IRQ_SPDIF    	(AW_IRQ_GIC_START + 3 )    /* SPDIF (on fpga, irq3 is shared by spdif(45), ac97(46), ts(47), iis(48), ple(98)) */
#define AW_IRQ_AC97       	(AW_IRQ_GIC_START + 3 )    /* AC97 (on fpga, irq3 is shared by spdif(45), ac97(46), ts(47), iis(48), ple(98)) */
#define AW_IRQ_TS       	(AW_IRQ_GIC_START + 3 )    /* TS (on fpga, irq3 is shared by spdif(45), ac97(46), ts(47), iis(48), ple(98)) */
#define AW_IRQ_IIS       	(AW_IRQ_GIC_START + 3 )    /* IIS (on fpga, irq3 is shared by spdif(45), ac97(46), ts(47), iis(48), ple(98)) */
#define AW_IRQ_UART4       	(AW_IRQ_GIC_START + 17)    /* UART4  */
#define AW_IRQ_UART5       	(AW_IRQ_GIC_START + 18)    /* UART5  */
#define AW_IRQ_UART6       	(AW_IRQ_GIC_START + 19)    /* UART6  */
#define AW_IRQ_UART7       	(AW_IRQ_GIC_START + 20)    /* UART7  */
#define AW_IRQ_KEYPAD     	(AW_IRQ_GIC_START + 2 )    /* KEYPAD (on fpga, irq2 is shared by IR0(37), twi0(39), keypad(53), can(58), GPIO(60), ps2-0(94)) */
#define AW_IRQ_TIMER0    	(AW_IRQ_GIC_START + 4 )    /* Timer0 */
#define AW_IRQ_TIMER1    	(AW_IRQ_GIC_START + 5 )    /* Timer1 (on fpga, irq5 is shared by timer1(55), hstimer(113)) */
#define AW_IRQ_TIMER2    	(AW_IRQ_GIC_START + 6 )    /* Timer2 / alarm / watchdog  */
#define AW_IRQ_TIMER3    	(AW_IRQ_GIC_START + 25)    /* Timer3 */
#define AW_IRQ_CAN       	(AW_IRQ_GIC_START + 2 )    /* CAN (on fpga, irq2 is shared by IR0(37), twi0(39), keypad(53), can(58), GPIO(60), ps2-0(94)) */
#define AW_IRQ_DMA       	(AW_IRQ_GIC_START + 7 )    /* DMA irq on fpga   */
#define AW_IRQ_GPIO       	(AW_IRQ_GIC_START + 2 )    /* GPIO (on fpga, irq2 is shared by IR0(37), twi0(39), keypad(53), can(58), GPIO(60), ps2-0(94)) */
#define AW_IRQ_TOUCH_PANEL 	(AW_IRQ_GIC_START + 8 )    /* touch pannel  */
#define AW_IRQ_AUDIO_COEC 	(AW_IRQ_GIC_START + 8 )    /* AUDIO COEC  */
#define AW_IRQ_LRADC    	(AW_IRQ_GIC_START + 8 )    /* LRADC  */
#define AW_IRQ_MMC0       	(AW_IRQ_GIC_START + 9 )    /* MMC0   */
#define AW_IRQ_MMC1       	(AW_IRQ_GIC_START + 33)    /* MMC1   */
#define AW_IRQ_MMC2       	(AW_IRQ_GIC_START + 10)    /* MMC2   */
#define AW_IRQ_MMC3       	(AW_IRQ_GIC_START + 35)    /* MMC3   */
#define AW_IRQ_MS        	(AW_IRQ_GIC_START + 10)    /* MS  */
#define AW_IRQ_NAND        	(AW_IRQ_GIC_START + 11)    /* NAND  */
#define AW_IRQ_USB0        	(AW_IRQ_GIC_START + 12)    /* USB0  */
#define AW_IRQ_USB1        	(AW_IRQ_GIC_START + 13)    /* USB1  */
#define AW_IRQ_USB2        	(AW_IRQ_GIC_START + 14)    /* USB2  */
#define AW_IRQ_SCR       	(AW_IRQ_GIC_START + 15)    /* SCR   */
#define AW_IRQ_CSI0       	(AW_IRQ_GIC_START + 16)    /* CSI0  */
#define AW_IRQ_CSI1       	(AW_IRQ_GIC_START + 14)    /* CSI1  */
#define AW_IRQ_LCDC0       	(AW_IRQ_GIC_START + 17)    /* LCDC0 */
#define AW_IRQ_LCDC1       	(AW_IRQ_GIC_START + 18)    /* LCDC1 */
#define AW_IRQ_MP       	(AW_IRQ_GIC_START + 19)    /* MP    */
#define AW_IRQ_DE0       	(AW_IRQ_GIC_START + 20)    /* DE-FE0 / DE-BE0 */
#define AW_IRQ_DE1       	(AW_IRQ_GIC_START + 21)    /* DE-FE1 / DE-BE1 */
#define AW_IRQ_PMU       	(AW_IRQ_GIC_START + 22)    /* PMU   */
#define AW_IRQ_SPI3       	(AW_IRQ_GIC_START + 50)    /* SPI3  */

#define AW_IRQ_PATA       	(AW_IRQ_GIC_START + 22)    /* PATA  */
#define AW_IRQ_VE       	(AW_IRQ_GIC_START + 24)    /* VE    */
#define AW_IRQ_SS       	(AW_IRQ_GIC_START + 24)    /* SS    */
#define AW_IRQ_EMAC       	(AW_IRQ_GIC_START + 15)    /* EMAC  */
#define AW_IRQ_SATA       	(AW_IRQ_GIC_START + 22)    /* sata  */
#define AW_IRQ_GPS       	(AW_IRQ_GIC_START + 24)    /* GPS   */
#define AW_IRQ_HDMI       	(AW_IRQ_GIC_START + 23)    /* HDMI  */
#define AW_IRQ_TVE_0_1    	(AW_IRQ_GIC_START + 15)    /* TV encoder 0/1 interrupt  */
#define AW_IRQ_ACE       	(AW_IRQ_GIC_START + 24)    /* ACE   */
#define AW_IRQ_TVD       	(AW_IRQ_GIC_START + 14)    /* TVD   */
#define AW_IRQ_PS2_0       	(AW_IRQ_GIC_START + 2 )    /* PS2_0(on fpga, irq2 is shared by IR0(37), twi0(39), keypad(53), can(58), GPIO(60), ps2-0(94)) */
#define AW_IRQ_PS2_1       	(AW_IRQ_GIC_START + 63)    /* PS2_1 */
#define AW_IRQ_USB3       	(AW_IRQ_GIC_START + 15)    /* USB3  */
#define AW_IRQ_USB4       	(AW_IRQ_GIC_START + 16)    /* USB4  */
#define AW_IRQ_PLE_PERFMU 	(AW_IRQ_GIC_START + 3 )    /* PLE_PERFMU (on fpga, irq3 is shared by spdif(45), ac97(46), ts(47), iis(48), ple(98)) */
#define AW_IRQ_TIMER4    	(AW_IRQ_GIC_START + 67)    /* TIMER4  */
#define AW_IRQ_TIMER5    	(AW_IRQ_GIC_START + 68)    /* TIMER5  */
#define AW_IRQ_GPU_GP    	(AW_IRQ_GIC_START + 25)    /* GPU_GP  */
#define AW_IRQ_GPU_GPMMU 	(AW_IRQ_GIC_START + 26)    /* GPU_GPMMU */
#define AW_IRQ_GPU_PP0  	(AW_IRQ_GIC_START + 27)    /* GPU_PP0  */
#define AW_IRQ_GPU_PPMMU0 	(AW_IRQ_GIC_START + 28)    /* GPU_PPMMU0 */
#define AW_IRQ_GPU_PMU  	(AW_IRQ_GIC_START + 29)    /* GPU_PMU  */
#define AW_IRQ_GPU_PP1   	(AW_IRQ_GIC_START + 30)    /* GPU_PP1  */
#define AW_IRQ_GPU_PPMMU1  	(AW_IRQ_GIC_START + 31)    /* GPU_PPMMU1 */
#define AW_IRQ_GPU_RSV0  	(AW_IRQ_GIC_START + 76)    /* GPU_RSV0  */
#define AW_IRQ_GPU_RSV1  	(AW_IRQ_GIC_START + 77)    /* GPU_RSV1  */
#define AW_IRQ_GPU_RSV2  	(AW_IRQ_GIC_START + 78)    /* GPU_RSV2  */
#define AW_IRQ_GPU_RSV3  	(AW_IRQ_GIC_START + 79)    /* GPU_RSV3  */
#define AW_IRQ_GPU_RSV4  	(AW_IRQ_GIC_START + 80)    /* GPU_RSV4  */
#define AW_IRQ_HSTIMER0  	(AW_IRQ_GIC_START + 5 )    /* hr-timer0 */
#define AW_IRQ_HSTIMER1  	(AW_IRQ_GIC_START + 82)    /* hr-timer1 */
#define AW_IRQ_HSTIMER2  	(AW_IRQ_GIC_START + 83)    /* hr-timer2 */
#define AW_IRQ_HSTIMER3  	(AW_IRQ_GIC_START + 84)    /* hr-timer3 */
#define AW_IRQ_GMAC       	(AW_IRQ_GIC_START + 15)    /* GMAC  */
#elif defined(CONFIG_AW_ASIC_PLATFORM)
#define AW_IRQ_NMI       	(AW_IRQ_GIC_START + 0 )    /* NMI    */
#define AW_IRQ_UART0     	(AW_IRQ_GIC_START + 1 )    /* UART0  */
#define AW_IRQ_UART1     	(AW_IRQ_GIC_START + 2 )    /* UART1  */
#define AW_IRQ_UART2     	(AW_IRQ_GIC_START + 3 )    /* UART2  */
#define AW_IRQ_UART3     	(AW_IRQ_GIC_START + 4 )    /* UART3  */
#define AW_IRQ_IR0       	(AW_IRQ_GIC_START + 5 )    /* IR0    */
#define AW_IRQ_IR1       	(AW_IRQ_GIC_START + 6 )    /* IR1    */
#define AW_IRQ_TWI0       	(AW_IRQ_GIC_START + 7 )    /* TWI0   */
#define AW_IRQ_TWI1       	(AW_IRQ_GIC_START + 8 )    /* TWI1   */
#define AW_IRQ_TWI2       	(AW_IRQ_GIC_START + 9 )    /* TWI2   */
#define AW_IRQ_SPI0       	(AW_IRQ_GIC_START + 10)    /* SPI0   */
#define AW_IRQ_SPI1       	(AW_IRQ_GIC_START + 11)    /* SPI1   */
#define AW_IRQ_SPI2       	(AW_IRQ_GIC_START + 12)    /* SPI2   */
#define AW_IRQ_SPDIF    	(AW_IRQ_GIC_START + 13)    /* SPDIF  */
#define AW_IRQ_AC97       	(AW_IRQ_GIC_START + 14)    /* AC97   */
#define AW_IRQ_TS       	(AW_IRQ_GIC_START + 15)    /* TS     */
#define AW_IRQ_IIS       	(AW_IRQ_GIC_START + 16)    /* IIS    */
#define AW_IRQ_UART4       	(AW_IRQ_GIC_START + 17)    /* UART4  */
#define AW_IRQ_UART5       	(AW_IRQ_GIC_START + 18)    /* UART5  */
#define AW_IRQ_UART6       	(AW_IRQ_GIC_START + 19)    /* UART6  */
#define AW_IRQ_UART7       	(AW_IRQ_GIC_START + 20)    /* UART7  */
#define AW_IRQ_KEYPAD     	(AW_IRQ_GIC_START + 21)    /* KEYPAD */
#define AW_IRQ_TIMER0    	(AW_IRQ_GIC_START + 22)    /* Timer0 */
#define AW_IRQ_TIMER1    	(AW_IRQ_GIC_START + 23)    /* Timer1 */
#define AW_IRQ_TIMER2    	(AW_IRQ_GIC_START + 24)    /* Timer2 / alarm / watchdog  */
#define AW_IRQ_TIMER3    	(AW_IRQ_GIC_START + 25)    /* Timer3 */
#define AW_IRQ_CAN       	(AW_IRQ_GIC_START + 26)    /* CAN    */
#define AW_IRQ_DMA       	(AW_IRQ_GIC_START + 27)    /* DMA    */
#define AW_IRQ_GPIO       	(AW_IRQ_GIC_START + 28)    /* GPIO    */
#define AW_IRQ_TOUCH_PANEL 	(AW_IRQ_GIC_START + 29)    /* touch pannel  */
#define AW_IRQ_AUDIO_COEC 	(AW_IRQ_GIC_START + 30)    /* AUDIO COEC  */
#define AW_IRQ_LRADC    	(AW_IRQ_GIC_START + 31)    /* LRADC  */
#define AW_IRQ_MMC0       	(AW_IRQ_GIC_START + 32)    /* MMC0   */
#define AW_IRQ_MMC1       	(AW_IRQ_GIC_START + 33)    /* MMC1   */
#define AW_IRQ_MMC2       	(AW_IRQ_GIC_START + 34)    /* MMC2   */
#define AW_IRQ_MMC3       	(AW_IRQ_GIC_START + 35)    /* MMC3   */
#define AW_IRQ_MS        	(AW_IRQ_GIC_START + 36)    /* External NMI  */
#define AW_IRQ_NAND        	(AW_IRQ_GIC_START + 37)    /* NAND  */
#define AW_IRQ_USB0        	(AW_IRQ_GIC_START + 38)    /* USB0  */
#define AW_IRQ_USB1        	(AW_IRQ_GIC_START + 39)    /* USB1  */
#define AW_IRQ_USB2        	(AW_IRQ_GIC_START + 40)    /* USB2  */
#define AW_IRQ_SCR       	(AW_IRQ_GIC_START + 41)    /* SCR   */
#define AW_IRQ_CSI0       	(AW_IRQ_GIC_START + 42)    /* CSI0  */
#define AW_IRQ_CSI1       	(AW_IRQ_GIC_START + 43)    /* CSI1  */
#define AW_IRQ_LCDC0       	(AW_IRQ_GIC_START + 44)    /* LCDC0 */
#define AW_IRQ_LCDC1       	(AW_IRQ_GIC_START + 45)    /* LCDC1 */
#define AW_IRQ_MP       	(AW_IRQ_GIC_START + 46)    /* MP    */
#define AW_IRQ_DE0       	(AW_IRQ_GIC_START + 47)    /* DE-FE0 / DE-BE0 */
#define AW_IRQ_DE1       	(AW_IRQ_GIC_START + 48)    /* DE-FE1 / DE-BE1 */
#define AW_IRQ_PMU       	(AW_IRQ_GIC_START + 49)    /* PMU   */
#define AW_IRQ_SPI3       	(AW_IRQ_GIC_START + 50)    /* SPI3  */

#define AW_IRQ_PATA       	(AW_IRQ_GIC_START + 52)    /* PATA  */
#define AW_IRQ_VE       	(AW_IRQ_GIC_START + 53)    /* VE    */
#define AW_IRQ_SS       	(AW_IRQ_GIC_START + 54)    /* SS    */
#define AW_IRQ_EMAC       	(AW_IRQ_GIC_START + 55)    /* EMAC  */
#define AW_IRQ_SATA       	(AW_IRQ_GIC_START + 56)    /* sata  */
#define AW_IRQ_GPS       	(AW_IRQ_GIC_START + 57)    /* GPS   */
#define AW_IRQ_HDMI       	(AW_IRQ_GIC_START + 58)    /* HDMI  */
#define AW_IRQ_TVE_0_1    	(AW_IRQ_GIC_START + 59)    /* TV encoder 0/1 interrupt  */
#define AW_IRQ_ACE       	(AW_IRQ_GIC_START + 60)    /* ACE   */
#define AW_IRQ_TVD       	(AW_IRQ_GIC_START + 61)    /* TVD   */
#define AW_IRQ_PS2_0       	(AW_IRQ_GIC_START + 62)    /* PS2_0 */
#define AW_IRQ_PS2_1       	(AW_IRQ_GIC_START + 63)    /* PS2_1 */
#define AW_IRQ_USB3       	(AW_IRQ_GIC_START + 64)    /* USB3  */
#define AW_IRQ_USB4       	(AW_IRQ_GIC_START + 65)    /* USB4  */
#define AW_IRQ_PLE_PERFMU 	(AW_IRQ_GIC_START + 66)    /* PLE_PERFMU */
#define AW_IRQ_TIMER4    	(AW_IRQ_GIC_START + 67)    /* TIMER4  */
#define AW_IRQ_TIMER5    	(AW_IRQ_GIC_START + 68)    /* TIMER5  */
#define AW_IRQ_GPU_GP    	(AW_IRQ_GIC_START + 69)    /* GPU_GP  */
#define AW_IRQ_GPU_GPMMU 	(AW_IRQ_GIC_START + 70)    /* GPU_GPMMU */
#define AW_IRQ_GPU_PP0  	(AW_IRQ_GIC_START + 71)    /* GPU_PP0  */
#define AW_IRQ_GPU_PPMMU0 	(AW_IRQ_GIC_START + 72)    /* GPU_PPMMU0 */
#define AW_IRQ_GPU_PMU  	(AW_IRQ_GIC_START + 73)    /* GPU_PMU  */
#define AW_IRQ_GPU_PP1   	(AW_IRQ_GIC_START + 74)    /* GPU_PP1  */
#define AW_IRQ_GPU_PPMMU1  	(AW_IRQ_GIC_START + 75)    /* GPU_PPMMU1 */
#define AW_IRQ_GPU_RSV0  	(AW_IRQ_GIC_START + 76)    /* GPU_RSV0  */
#define AW_IRQ_GPU_RSV1  	(AW_IRQ_GIC_START + 77)    /* GPU_RSV1  */
#define AW_IRQ_GPU_RSV2  	(AW_IRQ_GIC_START + 78)    /* GPU_RSV2  */
#define AW_IRQ_GPU_RSV3  	(AW_IRQ_GIC_START + 79)    /* GPU_RSV3  */
#define AW_IRQ_GPU_RSV4  	(AW_IRQ_GIC_START + 80)    /* GPU_RSV4  */
#define AW_IRQ_HSTIMER0  	(AW_IRQ_GIC_START + 81)    /* hr-timer0 */
#define AW_IRQ_HSTIMER1  	(AW_IRQ_GIC_START + 82)    /* hr-timer1 */
#define AW_IRQ_HSTIMER2  	(AW_IRQ_GIC_START + 83)    /* hr-timer2 */
#define AW_IRQ_HSTIMER3  	(AW_IRQ_GIC_START + 84)    /* hr-timer3 */
#define AW_IRQ_GMAC       	(AW_IRQ_GIC_START + 85)    /* GMAC  */
#define AW_IRQ_TWI3       	(AW_IRQ_GIC_START + 88 )   /* TWI3   */
#define AW_IRQ_TWI4       	(AW_IRQ_GIC_START + 89)    /* TWI4   */
#else
#error "ERROR: please select a valid platform\n"
#endif

/*
 * GIC
 */
#define NR_IRQS           (AW_IRQ_GIC_START + 128)
#define MAX_GIC_NR        1


#endif    /* __MACH_IRQS_AW_H */
