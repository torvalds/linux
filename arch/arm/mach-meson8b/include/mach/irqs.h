/*
 *  arch/arm/mach-meson8b/include/mach/irqs.h
 *
 *  Copyright (C) 2010-2013 AMLOGIC, INC.
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

#ifndef __ASM_ARCH_MESON8B_IRQS_H
#define __ASM_ARCH_MESON8B_IRQS_H

#define IRQ_BIT(irq)            ((irq) & 0x1f)
#define IRQ_INDEX(irq)          ((irq) >> 5)

#define BASE_IRQ 32
#define NR_IRQS         256


#define AM_IRQ(reg)   (reg + BASE_IRQ)

#define INT_WATCHDOG                AM_IRQ(0)
#define INT_MAILBOX                 AM_IRQ(1)
#define INT_VIU_HSYNC               AM_IRQ(2)
#define INT_VIU_VSYNC               AM_IRQ(3)
#define INT_DEMUX_1                 AM_IRQ(5)
#define INT_TIMER_C                 AM_IRQ(6)
#define INT_AUDIO_IN                AM_IRQ(7)
#define INT_ETHERNET                AM_IRQ(8)
#define INT_TIMER_A                 AM_IRQ(10)
#define INT_TIMER_B                 AM_IRQ(11)
#define INT_VIU2_HSYNC              AM_IRQ(12)
#define INT_VIU2_VSYNC              AM_IRQ(13)
#define INT_MIPI_PHY                AM_IRQ(14)
#define INT_REMOTE                  AM_IRQ(15)
#define INT_ABUF_WR                 AM_IRQ(16)
#define INT_ABUF_RD                 AM_IRQ(17)
#define INT_ASYNC_FIFO_FILL         AM_IRQ(18)
#define INT_ASYNC_FIFO_FLUSH        AM_IRQ(19)
#define INT_BT656                   AM_IRQ(20)
#define INT_I2C_MASTER0             AM_IRQ(21)
#define INT_ENCODER                 AM_IRQ(22)
#define INT_DEMUX                   AM_IRQ(23)
#define INT_ASYNC_FIFO2_FILL        AM_IRQ(24)
#define INT_ASYNC_FIFO2_FLUSH       AM_IRQ(25)
#define INT_UART_0                  AM_IRQ(26)
#define INT_MIPI_DSI_PHY            AM_IRQ(27)
#define INT_SDIO                    AM_IRQ(28)
#define INT_TIMER_D                 AM_IRQ(29)
#define INT_USB_A                   AM_IRQ(30)
#define INT_USB_B                   AM_IRQ(31)

#define INT_PARSER                  AM_IRQ(32)
#define INT_VIFF_EMPTY              AM_IRQ(33)
#define INT_NAND                    AM_IRQ(34)
#define INT_SPDIF                   AM_IRQ(35)
#define INT_NDMA                    AM_IRQ(36)
#define INT_SMART_CARD              AM_IRQ(37)
#define INT_UART_AO_2               AM_IRQ(38)
#define INT_I2C_MASTER3             AM_IRQ(39)
#define INT_DOS_MAILBOX_0           AM_IRQ(43)
#define INT_DOS_MAILBOX_1           AM_IRQ(44)
#define INT_DOS_MAILBOX_2           AM_IRQ(45)
#define INT_DEINTERLACE	            AM_IRQ(46)
#define INT_AIU_CRC                 AM_IRQ(47)
#define INT_I2S_DDR                 AM_IRQ(48)
#define INT_IEC958_DDR	            AM_IRQ(49)
#define INT_AI_IEC958               AM_IRQ(50)
#define INT_DMC_SEC                 AM_IRQ(51)
#define INT_DMC                     AM_IRQ(52)
#define INT_DEMUX_2                 AM_IRQ(53)
#define INT_HDMI_CEC                AM_IRQ(55)
#define INT_HDMI_TX                 AM_IRQ(57)
#define INT_TIMER_F                 AM_IRQ(60)
#define INT_TIMER_G                 AM_IRQ(61)
#define INT_TIMER_H                 AM_IRQ(62)
#define INT_TIMER_I                 AM_IRQ(63)

#define INT_GPIO_0                  AM_IRQ(64)
#define INT_GPIO_1                  AM_IRQ(65)
#define INT_GPIO_2                  AM_IRQ(66)
#define INT_GPIO_3                  AM_IRQ(67)
#define INT_GPIO_4                  AM_IRQ(68)
#define INT_GPIO_5                  AM_IRQ(69)
#define INT_GPIO_6                  AM_IRQ(70)
#define INT_GPIO_7                  AM_IRQ(71)
#define INT_RTC                     AM_IRQ(72)
#define INT_SAR_ADC                 AM_IRQ(73)
#define INT_CSI2_HOST               AM_IRQ(74)
#define INT_UART_1                  AM_IRQ(75)
#define INT_ACODEC_LEVEL            AM_IRQ(77)
#define INT_SDHC                    AM_IRQ(78)
#define INT_VIDEO_0_WR              AM_IRQ(79)
#define INT_SPI                     AM_IRQ(80)
#define INT_SPI_2                   AM_IRQ(81)
#define INT_VDIN0_HSYNC             AM_IRQ(82)
#define INT_VDIN0_VSYNC             AM_IRQ(83)
#define INT_VDIN1_HSYNC             AM_IRQ(84)
#define INT_VDIN1_VSYNC             AM_IRQ(85)
#define INT_VIDEO_1_WR              AM_IRQ(86)
#define INT_CSI2_HOST_2             AM_IRQ(87)
#define INT_I2S_CBUS                AM_IRQ(88)
#define INT_RDMA                    AM_IRQ(89)
#define INT_UART_AO                 AM_IRQ(90)
#define INT_I2C_SLAVE_AO            AM_IRQ(91)
#define INT_I2C_MASTER_AO           AM_IRQ(92)
#define INT_UART_2                  AM_IRQ(93)
#define INT_UART_3                  AM_IRQ(94)
#define INT_CSI2_ADAPTER            AM_IRQ(95)

/*
#define INT_AMRISC_DC_PCMLAST       AM_IRQ3(0)
#define INT_AMRISC_VIU_VSYNC        AM_IRQ3(1)
#define INT_AMRISC_H2TMR            AM_IRQ3(3)
#define INT_AMRISC_H2CPAR           AM_IRQ3(4)
#define INT_AMRISC_HI_ABX           AM_IRQ3(5)
#define INT_AMRISC_H2CMD            AM_IRQ3(6)
#define INT_AMRISC_AI_IEC958        AM_IRQ3(7)
#define INT_AMRISC_VL_CP            AM_IRQ3(8)
#define INT_AMRISC_DC_MBDONE        AM_IRQ3(9)
#define INT_AMRISC_VIU_HSYNC        AM_IRQ3(10)
#define INT_AMRISC_R2C              AM_IRQ3(11)
#define INT_AMRISC_AIFIFO           AM_IRQ3(13)
#define INT_AMRISC_HST_INTP         AM_IRQ3(14)
#define INT_AMRISC_CPU1_STOP        AM_IRQ3(16)
#define INT_AMRISC_CPU2_STOP        AM_IRQ3(17)
#define INT_AMRISC_VENC_INT         AM_IRQ3(19)
#define INT_AMRISC_TIMER0           AM_IRQ3(26)
#define INT_AMRISC_TIMER1           AM_IRQ3(27)
*/

#define INT_I2C_MASTER1             AM_IRQ(128)
#define INT_I2C_MASTER2             AM_IRQ(129)
#define INT_MMC	                    AM_IRQ(130)
#define INT_MIPI_DSI_ERR            AM_IRQ(133)
#define INT_MIPI_DSI_TE	            AM_IRQ(134)
#define INT_IR_BLASTER_AO           AM_IRQ(135)
#define INT_A9_PMU0                 AM_IRQ(137)
#define INT_A9_PMU1                 AM_IRQ(138)
#define INT_A9_DBG_COMTX0           AM_IRQ(139)
#define INT_A9_DBG_COMTX1           AM_IRQ(140)
#define INT_A9_DBG_COMRX0           AM_IRQ(141)
#define INT_A9_DBG_COMRX1           AM_IRQ(142)
#define INT_L2_CACHE                AM_IRQ(143)
#define INT_ASSIST_MBOX0            AM_IRQ(145)
#define INT_ASSIST_MBOX1            AM_IRQ(146)
#define INT_ASSIST_MBOX2            AM_IRQ(147)
#define INT_ASSIST_MBOX3            AM_IRQ(148)
#define INT_CUSAD                   AM_IRQ(149)
#define INT_GE2D                    AM_IRQ(150)
#define INT_AO_CEC                  AM_IRQ(151)
#define INT_VIU1_LINE_N             AM_IRQ(152)
#define INT_A9_PMU2                 AM_IRQ(153)
#define INT_A9_PMU3                 AM_IRQ(154)
#define INT_A9_DBG_COMTX2           AM_IRQ(155)
#define INT_A9_DBG_COMTX3           AM_IRQ(156)
#define INT_A9_DBG_COMRX2           AM_IRQ(157)
#define INT_A9_DBG_COMRX3           AM_IRQ(158)
#define INT_VIU2_LINE_N             AM_IRQ(159)

#define INT_MALI_GP                 AM_IRQ(160)
#define INT_MALI_GP_MMU             AM_IRQ(161)
#define INT_MALI_PP                 AM_IRQ(162)
#define INT_MALI_PMU                AM_IRQ(163)
#define INT_MALI_PP0                AM_IRQ(164)
#define INT_MALI_PP0_MMU            AM_IRQ(165)
#define INT_MALI_PP1                AM_IRQ(166)
#define INT_MALI_PP1_MMU            AM_IRQ(167)
#define INT_MALI_PP2                AM_IRQ(168)
#define INT_MALI_PP2_MMU            AM_IRQ(169)
#define INT_MALI_PP3                AM_IRQ(170)
#define INT_MALI_PP3_MMU            AM_IRQ(171)
#define INT_MALI_PP4                AM_IRQ(172)
#define INT_MALI_PP4_MMU            AM_IRQ(173)
#define INT_MALI_PP5                AM_IRQ(174)
#define INT_MALI_PP5_MMU            AM_IRQ(175)
#define INT_MALI_PP6                AM_IRQ(176)
#define INT_MALI_PP6_MMU            AM_IRQ(177)
#define INT_MALI_PP7	            AM_IRQ(178)
#define INT_MALI_PP7_MMU            AM_IRQ(179)



/* All interrupts are FIQ capable */
#define FIQ_START                   AM_IRQ0(0)
extern void request_fiq(unsigned fiq, void (*isr)(void));
extern void free_fiq(unsigned fiq, void (*isr)(void));

#endif
