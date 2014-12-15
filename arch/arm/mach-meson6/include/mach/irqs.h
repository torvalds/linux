/*
 *  arch/arm/mach-meson3/include/mach/irqs.h
 *
 *  Copyright (C) 2010 AMLOGIC, INC.
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

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H

#define IRQ_BIT(irq)            ((irq) & 0x1f)
#define IRQ_INDEX(irq)          ((irq) >> 5)
#define IRQ_MASK_REG(irq)       (SYS_CPU_0_IRQ_IN0_INTR_MASK + (((irq) >> 5) << 2))
#define IRQ_STATUS_REG(irq)     (SYS_CPU_0_IRQ_IN0_INTR_STAT + (((irq) >> 5) << 2))
#define IRQ_CLR_REG(irq)        (SYS_CPU_0_IRQ_IN0_INTR_STAT_CLR + (((irq) >> 5) << 2))
#define IRQ_FIQSEL_REG(irq)     (SYS_CPU_0_IRQ_IN0_INTR_FIRQ_SEL + (((irq) >> 5) << 2))
#ifdef CONFIG_ARM_GIC
#define BASE_IRQ 32
#define NR_IRQS         256
#else
#define BASE_IRQ 0
#define NR_IRQS         AM_IRQ3(32)
#endif
#define AM_IRQ(reg,v)   ((reg<<5)+(v) + BASE_IRQ)
#define AM_IRQ0(v)      AM_IRQ(0,v)
#define AM_IRQ1(v)      AM_IRQ(1,v)
#define AM_IRQ2(v)      AM_IRQ(2,v)
#define AM_IRQ3(v)      AM_IRQ(3,v)
#define AM_IRQ4(v)      AM_IRQ(4,v)



#define INT_WATCHDOG                AM_IRQ0(0)
#define INT_MAILBOX                 AM_IRQ0(1)
#define INT_VIU_HSYNC               AM_IRQ0(2)
#define INT_VIU_VSYNC               AM_IRQ0(3)
#define INT_DEMUX_1                 AM_IRQ0(5)
#define INT_TIMER_C                 AM_IRQ0(6)
#define INT_AUDIO_IN                AM_IRQ0(7)
#define INT_ETHERNET                AM_IRQ0(8)
#define INT_SYS_ARC_SLEEP_RATIO     AM_IRQ0(9)
#define INT_TIMER_A                 AM_IRQ0(10)
#define INT_TIMER_B                 AM_IRQ0(11)
#define INT_VIU2_HSYNC              AM_IRQ0(12)
#define INT_VIU2_VSYNC              AM_IRQ0(13)
#define INT_MIPI_PHY				AM_IRQ2(14)
#define INT_REMOTE                  AM_IRQ0(15)
#define INT_ABUF_WR                 AM_IRQ0(16)
#define INT_ABUF_RD                 AM_IRQ0(17)
#define INT_ASYNC_FIFO_FILL         AM_IRQ0(18)
#define INT_ASYNC_FIFO_FLUSH        AM_IRQ0(19)
#define INT_BT656                   AM_IRQ0(20)
#define INT_I2C_MASTER              AM_IRQ0(21)
#define INT_ENCODER                 AM_IRQ0(22)
#define INT_DEMUX                   AM_IRQ0(23)
#define INT_ASYNC_FIFO2_FILL        AM_IRQ0(24)
#define INT_ASYNC_FIFO2_FLUSH       AM_IRQ0(25)
#define INT_UART_0                  AM_IRQ0(26)
#define INT_SDIO                    AM_IRQ0(28)
#define INT_TIMER_D                 AM_IRQ0(29)
#define INT_USB_A                   AM_IRQ0(30)
#define INT_USB_B                   AM_IRQ0(31)

#define INT_PARSER                  AM_IRQ1(0)
#define INT_VIFF_EMPTY              AM_IRQ1(1)
#define INT_NAND                    AM_IRQ1(2)
#define INT_SPDIF                   AM_IRQ1(3)
#define INT_NDMA                    AM_IRQ1(4)
#define INT_SMART_CARD              AM_IRQ1(5)
#define INT_MEASURE_CLK             AM_IRQ1(6)
#define INT_I2C_SLAVE               AM_IRQ1(7)
#define INT_MAILBOX_2B              AM_IRQ1(8)
#define INT_MAILBOX_1B              AM_IRQ1(9)
#define INT_MAILBOX_0B              AM_IRQ1(10)
#define INT_MAILBOX_2A              AM_IRQ1(11)
#define INT_MAILBOX_1A              AM_IRQ1(12)
#define INT_MAILBOX_0A              AM_IRQ1(13)
#define INT_DEINTERLACE             AM_IRQ1(14)
#define INT_MMC                     AM_IRQ1(15)
#define INT_MALI_GP                 AM_IRQ1(16)
#define INT_MALI_GP_MMU             AM_IRQ1(17)
#define INT_MALI_PP                 AM_IRQ1(18)
#define INT_MALI_PP_MMU             AM_IRQ1(19)
#define INT_MALI_PMU                AM_IRQ1(20)
#define INT_DEMUX_2                 AM_IRQ1(21)
#define INT_AUDIO_ARC_SLEEP_RATIO   AM_IRQ1(22)
#define INT_HDMI_CEC                AM_IRQ1(23)
#define INT_HDMI_TX                 AM_IRQ1(25)
#define INT_A9_PMU                  AM_IRQ1(26)
#define INT_A9_DEBUG_TX             AM_IRQ1(27)
#define INT_A9_DEBUG_RX             AM_IRQ1(28)
#define INT_A9_L2_CC                AM_IRQ1(29)
#define INT_SATA                    AM_IRQ1(30)
#define INT_DEMOD                   AM_IRQ1(31)

#define INT_GPIO_0                  AM_IRQ2(0)
#define INT_GPIO_1                  AM_IRQ2(1)
#define INT_GPIO_2                  AM_IRQ2(2)
#define INT_GPIO_3                  AM_IRQ2(3)
#define INT_GPIO_4                  AM_IRQ2(4)
#define INT_GPIO_5                  AM_IRQ2(5)
#define INT_GPIO_6                  AM_IRQ2(6)
#define INT_GPIO_7                  AM_IRQ2(7)
#define INT_RTC                     AM_IRQ2(8)
#define INT_SAR_ADC                 AM_IRQ2(9)
#define INT_UART_1                  AM_IRQ2(11)
#define INT_LED_PWM                 AM_IRQ2(12)
#define INT_VGHL_PWM                AM_IRQ2(13)
#define INT_WIFI_WATCHDOG           AM_IRQ2(14)
#define INT_VIDEO_WR                AM_IRQ2(15)
#define INT_SPI                     AM_IRQ2(16)
#define INT_SPI_1                   AM_IRQ2(17)
#define INT_VDIN_HSYNC              AM_IRQ2(18)
#define INT_VDIN_VSYNC              AM_IRQ2(19)
#define INT_CSI2_HOST				AM_IRQ2(23)
#define INT_I2C_CBUS_DDR            AM_IRQ2(24)
#define INT_RDMA                    AM_IRQ2(25)
#define INT_UART_AO                 AM_IRQ2(26)
#define INT_I2C_SLAVE_AO            AM_IRQ2(27)
#define INT_I2C_MASTER_AO           AM_IRQ2(28)
#define INT_UART_2                  AM_IRQ2(29)
#define INT_UART_3                  AM_IRQ2(30)
#define INT_CSI2_ADAPTER			AM_IRQ2(31)

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
#define INT_GE2D                    AM_IRQ3(15)
#define INT_AMRISC_CPU1_STOP        AM_IRQ3(16)
#define INT_AMRISC_CPU2_STOP        AM_IRQ3(17)
#define INT_AMRISC_VENC_INT         AM_IRQ3(19)
#define INT_AMRISC_TIMER0           AM_IRQ3(26)
#define INT_AMRISC_TIMER1           AM_IRQ3(27)

/* All interrupts are FIQ capable */
#define FIQ_START                   AM_IRQ0(0)
extern void request_fiq(unsigned fiq, void (*isr)(void));
extern void free_fiq(unsigned fiq, void (*isr)(void));
#endif
