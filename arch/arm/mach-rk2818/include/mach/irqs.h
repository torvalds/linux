/* arch/arm/mach-rk2818/include/mach/irqs.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */


#ifndef __ARCH_ARM_MACH_RK2818_IRQS_H
#define __ARCH_ARM_MACH_RK2818_IRQS_H



#define IRQ_REG_INTEN_L              0x00//IRQ interrupt source enable register (low)
#define IRQ_REG_INTEN_H              0x04//IRQ interrupt source enable register (high)
#define IRQ_REG_INTMASK_L            0x08//IRQ interrupt source mask register (low).
#define IRQ_REG_INTMASK_H            0x0c//IRQ interrupt source mask register (high).
#define IRQ_REG_INTFORCE_L           0x10//IRQ interrupt force register
#define IRQ_REG_INTFORCE_H           0x14//
#define IRQ_REG_RAWSTATUS_L          0x18//IRQ raw status register
#define IRQ_REG_RAWSTATUS_H          0x1c//
#define IRQ_REG_STATUS_L             0x20//IRQ status register
#define IRQ_REG_STATUS_H             0x24//
#define IRQ_REG_MASKSTATUS_L         0x28//IRQ interrupt mask status register
#define IRQ_REG_MASKSTATUS_H         0x2c//
#define IRQ_REG_FINALSTATUS_L        0x30//IRQ interrupt final status
#define IRQ_REG_FINALSTATUS_H        0x34 
#define FIQ_REG_INTEN                0xc0//Fast interrupt enable register
#define FIQ_REG_INTMASK              0xc4//Fast interrupt mask register
#define FIQ_REG_INTFORCE             0xc8//Fast interrupt force register
#define FIQ_REG_RAWSTATUS            0xcc//Fast interrupt source raw status register
#define FIQ_REG_STATUS               0xd0//Fast interrupt status register
#define FIQ_REG_FINALSTATUS          0xd4//Fast interrupt final status register
#define IRQ_REG_PLEVEL               0xd8//IRQ System Priority Level Register


/*定义RK2818的最大中断数目。NR_AIC_IRQS表示RK2818的中断寄存器支持的最大中断数目，
CONFIG_RK28_GPIO_IRQ表示RK2818的GPIO复用的最大中断数目，CONFIG_EXTEND_GPIO_IRQ表示RK2818的
扩展IO复用的最大中断数目。*/
#define NR_AIC_IRQS 	48
#define	NR_IRQS		(NR_AIC_IRQS + CONFIG_RK28_GPIO_IRQ+CONFIG_EXPANDED_GPIO_IRQ_NUM+CONFIG_SPI_FPGA_GPIO_IRQ_NUM)                                   
                                   
                                   
/*irq number*/                                   
#define IRQ_NR_DWDMA                 0 // -- low
#define IRQ_NR_HOST                  1 // -- Host Interface
#define IRQ_NR_NANDC                 2
#define IRQ_NR_LCDC                  3
#define IRQ_NR_SDMMC0                4
#define IRQ_NR_VIP                   5
#define IRQ_NR_GPIO0                 6
#define IRQ_NR_GPIO1                 7
#define IRQ_NR_OTG                   8 // -- USB OTG
#define IRQ_NR_ABTARMD               9 // -- Arbiter in ARMD BUS
#define IRQ_NR_ABTEXP                10//-- Arbiter in EXP BUS
#define IRQ_NR_I2C0                  11
#define IRQ_NR_I2C1                  12
#define IRQ_NR_I2S                   13
#define IRQ_NR_SPIM                  14// -- SPI Master
#define IRQ_NR_SPIS                  15//-- SPI Slave
#define IRQ_NR_TIMER1                16
#define IRQ_NR_TIMER2                17
#define IRQ_NR_TIMER3                18
#define IRQ_NR_UART0                 19
#define IRQ_NR_UART1                 20
#define IRQ_NR_WDT                   21
#define IRQ_NR_PWM0                  22
#define IRQ_NR_PWM1                  23
#define IRQ_NR_PWM2                  24
#define IRQ_NR_PWM3                  25
#define IRQ_NR_ADC                   26
#define IRQ_NR_RTC                   27
#define IRQ_NR_PIUSEM0               28// -- PIU Semphore 0
#define IRQ_NR_PIUSEM1               29
#define IRQ_NR_PIUSEM3               30
#define IRQ_NR_PIUCMD                31// -- PIU command/reply
#define IRQ_NR_XDMA                  32
#define IRQ_NR_SDMMC1                33
#define IRQ_NR_DSPSEI                34// -- DSP slave interface error interrupt
#define IRQ_NR_DSPSWI                35// -- DSP interrupt by software set
#define IRQ_NR_SCU                   36
#define IRQ_NR_SWI                   37// -- Software Interrupt
#define IRQ_NR_DSPMEI                38// -- DSP master interface error interrupt
#define IRQ_NR_DSPSAEI               39// -- DSP system access error interrupt
#define IRQ_NR_GPU_M55               40
#define IRQ_NR_GPU_MMU               41
#define IRQ_NR_DDRII_MOBILE_CTR      42
#define IRQ_NR_MC_DMA                43
#define IRQ_NR_NAND_FLASH_RDY        44
#define IRQ_NR_UART2                 45
#define IRQ_NR_UART3                 46
#define IRQ_NR_USB_HOST              47


#endif
