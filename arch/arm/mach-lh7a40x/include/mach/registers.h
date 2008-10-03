/* arch/arm/mach-lh7a40x/include/mach/registers.h
 *
 *  Copyright (C) 2004 Coastal Environmental Systems
 *  Copyright (C) 2004 Logic Product Development
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

#include <mach/constants.h>

#ifndef __ASM_ARCH_REGISTERS_H
#define __ASM_ARCH_REGISTERS_H


	/* Physical register base addresses */

#define AC97C_PHYS	(0x80000000)	/* AC97 Controller */
#define MMC_PHYS	(0x80000100)	/* Multimedia Card Controller */
#define USB_PHYS	(0x80000200)	/* USB Client */
#define SCI_PHYS	(0x80000300)	/* Secure Card Interface */
#define CSC_PHYS	(0x80000400)	/* Clock/State Controller  */
#define INTC_PHYS	(0x80000500)	/* Interrupt Controller */
#define UART1_PHYS	(0x80000600)	/* UART1 Controller */
#define SIR_PHYS	(0x80000600)	/* IR Controller, same are UART1 */
#define UART2_PHYS	(0x80000700)	/* UART2 Controller */
#define UART3_PHYS	(0x80000800)	/* UART3 Controller */
#define DCDC_PHYS	(0x80000900)	/* DC to DC Controller */
#define ACI_PHYS	(0x80000a00)	/* Audio Codec Interface */
#define SSP_PHYS	(0x80000b00)	/* Synchronous ... */
#define TIMER_PHYS	(0x80000c00)	/* Timer Controller */
#define RTC_PHYS	(0x80000d00)	/* Real-time Clock */
#define GPIO_PHYS	(0x80000e00)	/* General Purpose IO */
#define BMI_PHYS	(0x80000f00)	/* Battery Monitor Interface */
#define HRTFTC_PHYS	(0x80001000)	/* High-res TFT Controller (LH7A400) */
#define ALI_PHYS	(0x80001000)	/* Advanced LCD Interface (LH7A404) */
#define WDT_PHYS	(0x80001400)	/* Watchdog Timer */
#define SMC_PHYS	(0x80002000)	/* Static Memory Controller */
#define SDRC_PHYS	(0x80002400)	/* SDRAM Controller */
#define DMAC_PHYS	(0x80002800)	/* DMA Controller */
#define CLCDC_PHYS	(0x80003000)	/* Color LCD Controller */

	/* Physical registers of the LH7A404 */

#define ADC_PHYS	(0x80001300)	/* A/D & Touchscreen Controller */
#define VIC1_PHYS	(0x80008000)	/* Vectored Interrupt Controller 1 */
#define USBH_PHYS	(0x80009000)	/* USB OHCI host controller */
#define VIC2_PHYS	(0x8000a000)	/* Vectored Interrupt Controller 2 */

/*#define KBD_PHYS	(0x80000e00) */
/*#define LCDICP_PHYS	(0x80001000) */


	/* Clock/State Controller register */

#define CSC_PWRSR	__REG(CSC_PHYS + 0x00) /* Reset register & ID */
#define CSC_PWRCNT	__REG(CSC_PHYS + 0x04) /* Power control */
#define CSC_CLKSET	__REG(CSC_PHYS + 0x20) /* Clock speed control */
#define CSC_USBDRESET	__REG(CSC_PHYS + 0x4c) /* USB Device resets */

#define CSC_PWRCNT_USBH_EN	(1<<28)	/* USB Host power enable */
#define CSC_PWRCNT_DMAC_M2M1_EN	(1<<27)
#define CSC_PWRCNT_DMAC_M2M0_EN	(1<<26)
#define CSC_PWRCNT_DMAC_M2P8_EN	(1<<25)
#define CSC_PWRCNT_DMAC_M2P9_EN	(1<<24)
#define CSC_PWRCNT_DMAC_M2P6_EN	(1<<23)
#define CSC_PWRCNT_DMAC_M2P7_EN	(1<<22)
#define CSC_PWRCNT_DMAC_M2P4_EN	(1<<21)
#define CSC_PWRCNT_DMAC_M2P5_EN	(1<<20)
#define CSC_PWRCNT_DMAC_M2P2_EN	(1<<19)
#define CSC_PWRCNT_DMAC_M2P3_EN	(1<<18)
#define CSC_PWRCNT_DMAC_M2P0_EN	(1<<17)
#define CSC_PWRCNT_DMAC_M2P1_EN	(1<<16)

#define CSC_PWRSR_CHIPMAN_SHIFT	(24)
#define CSC_PWRSR_CHIPMAN_MASK	(0xff)
#define CSC_PWRSR_CHIPID_SHIFT	(16)
#define CSC_PWRSR_CHIPID_MASK	(0xff)

#define CSC_USBDRESET_APBRESETREG	(1<<1)
#define CSC_USBDRESET_IORESETREG	(1<<0)

	/* Interrupt Controller registers */

#define INTC_INTSR	__REG(INTC_PHYS + 0x00)	/* Status */
#define INTC_INTRSR	__REG(INTC_PHYS + 0x04)	/* Raw Status */
#define INTC_INTENS	__REG(INTC_PHYS + 0x08)	/* Enable Set */
#define INTC_INTENC	__REG(INTC_PHYS + 0x0c)	/* Enable Clear */


	/* Vectored Interrupted Controller registers */

#define VIC1_IRQSTATUS	__REG(VIC1_PHYS + 0x00)
#define VIC1_FIQSTATUS	__REG(VIC1_PHYS + 0x04)
#define VIC1_RAWINTR	__REG(VIC1_PHYS + 0x08)
#define VIC1_INTSEL	__REG(VIC1_PHYS + 0x0c)
#define VIC1_INTEN	__REG(VIC1_PHYS + 0x10)
#define VIC1_INTENCLR	__REG(VIC1_PHYS + 0x14)
#define VIC1_SOFTINT	__REG(VIC1_PHYS + 0x18)
#define VIC1_SOFTINTCLR	__REG(VIC1_PHYS + 0x1c)
#define VIC1_PROTECT	__REG(VIC1_PHYS + 0x20)
#define VIC1_VECTADDR	__REG(VIC1_PHYS + 0x30)
#define VIC1_NVADDR	__REG(VIC1_PHYS + 0x34)
#define VIC1_VAD0	__REG(VIC1_PHYS + 0x100)
#define VIC1_VECTCNTL0	__REG(VIC1_PHYS + 0x200)
#define VIC2_IRQSTATUS	__REG(VIC2_PHYS + 0x00)
#define VIC2_FIQSTATUS	__REG(VIC2_PHYS + 0x04)
#define VIC2_RAWINTR	__REG(VIC2_PHYS + 0x08)
#define VIC2_INTSEL	__REG(VIC2_PHYS + 0x0c)
#define VIC2_INTEN	__REG(VIC2_PHYS + 0x10)
#define VIC2_INTENCLR	__REG(VIC2_PHYS + 0x14)
#define VIC2_SOFTINT	__REG(VIC2_PHYS + 0x18)
#define VIC2_SOFTINTCLR	__REG(VIC2_PHYS + 0x1c)
#define VIC2_PROTECT	__REG(VIC2_PHYS + 0x20)
#define VIC2_VECTADDR	__REG(VIC2_PHYS + 0x30)
#define VIC2_NVADDR	__REG(VIC2_PHYS + 0x34)
#define VIC2_VAD0	__REG(VIC2_PHYS + 0x100)
#define VIC2_VECTCNTL0	__REG(VIC2_PHYS + 0x200)

#define VIC_CNTL_ENABLE	(0x20)

	/* USB Host registers (Open HCI compatible) */

#define USBH_CMDSTATUS	__REG(USBH_PHYS + 0x08)


	/* GPIO registers */

#define GPIO_INTTYPE1	__REG(GPIO_PHYS + 0x4c)	/* Interrupt Type 1 (Edge) */
#define GPIO_INTTYPE2	__REG(GPIO_PHYS + 0x50)	/* Interrupt Type 2 */
#define GPIO_GPIOFEOI	__REG(GPIO_PHYS + 0x54)	/* GPIO End-of-Interrupt */
#define GPIO_GPIOINTEN	__REG(GPIO_PHYS + 0x58)	/* GPIO Interrupt Enable */
#define GPIO_INTSTATUS	__REG(GPIO_PHYS + 0x5c)	/* GPIO Interrupt Status */
#define GPIO_PINMUX	__REG(GPIO_PHYS + 0x2c)
#define GPIO_PADD	__REG(GPIO_PHYS + 0x10)
#define GPIO_PAD	__REG(GPIO_PHYS + 0x00)
#define GPIO_PCD	__REG(GPIO_PHYS + 0x08)
#define GPIO_PCDD	__REG(GPIO_PHYS + 0x18)
#define GPIO_PEDD	__REG(GPIO_PHYS + 0x24)
#define GPIO_PED	__REG(GPIO_PHYS + 0x20)


	/* Static Memory Controller registers */

#define SMC_BCR0	__REG(SMC_PHYS + 0x00)	/* Bank 0 Configuration */
#define SMC_BCR1	__REG(SMC_PHYS + 0x04)	/* Bank 1 Configuration */
#define SMC_BCR2	__REG(SMC_PHYS + 0x08)	/* Bank 2 Configuration */
#define SMC_BCR3	__REG(SMC_PHYS + 0x0C)	/* Bank 3 Configuration */
#define SMC_BCR6	__REG(SMC_PHYS + 0x18)	/* Bank 6 Configuration */
#define SMC_BCR7	__REG(SMC_PHYS + 0x1c)	/* Bank 7 Configuration */


#ifdef CONFIG_MACH_KEV7A400
# define CPLD_RD_OPT_DIP_SW	__REG16(CPLD_PHYS + 0x00) /* Read Option SW */
# define CPLD_WR_IO_BRD_CTL	__REG16(CPLD_PHYS + 0x00) /* Write Control */
# define CPLD_RD_PB_KEYS	__REG16(CPLD_PHYS + 0x02) /* Read Btn Keys */
# define CPLD_LATCHED_INTS	__REG16(CPLD_PHYS + 0x04) /* Read INTR stat. */
# define CPLD_CL_INT		__REG16(CPLD_PHYS + 0x04) /* Clear INTR stat */
# define CPLD_BOOT_MMC_STATUS	__REG16(CPLD_PHYS + 0x06) /* R/O */
# define CPLD_RD_KPD_ROW_SENSE	__REG16(CPLD_PHYS + 0x08)
# define CPLD_WR_PB_INT_MASK	__REG16(CPLD_PHYS + 0x08)
# define CPLD_RD_BRD_DISP_SW	__REG16(CPLD_PHYS + 0x0a)
# define CPLD_WR_EXT_INT_MASK	__REG16(CPLD_PHYS + 0x0a)
# define CPLD_LCD_PWR_CNTL	__REG16(CPLD_PHYS + 0x0c)
# define CPLD_SEVEN_SEG		__REG16(CPLD_PHYS + 0x0e) /* 7 seg. LED mask */

#endif

#if defined (CONFIG_MACH_LPD7A400) || defined (CONFIG_MACH_LPD7A404)

# define CPLD_CONTROL		__REG16(CPLD02_PHYS)
# define CPLD_SPI_DATA		__REG16(CPLD06_PHYS)
# define CPLD_SPI_CONTROL	__REG16(CPLD08_PHYS)
# define CPLD_SPI_EEPROM	__REG16(CPLD0A_PHYS)
# define CPLD_INTERRUPTS	__REG16(CPLD0C_PHYS) /* IRQ mask/status */
# define CPLD_BOOT_MODE		__REG16(CPLD0E_PHYS)
# define CPLD_FLASH		__REG16(CPLD10_PHYS)
# define CPLD_POWER_MGMT	__REG16(CPLD12_PHYS)
# define CPLD_REVISION		__REG16(CPLD14_PHYS)
# define CPLD_GPIO_EXT		__REG16(CPLD16_PHYS)
# define CPLD_GPIO_DATA		__REG16(CPLD18_PHYS)
# define CPLD_GPIO_DIR		__REG16(CPLD1A_PHYS)

#endif

	/* Timer registers */

#define TIMER_LOAD1	__REG(TIMER_PHYS + 0x00) /* Timer 1 initial value */
#define TIMER_VALUE1	__REG(TIMER_PHYS + 0x04) /* Timer 1 current value */
#define TIMER_CONTROL1	__REG(TIMER_PHYS + 0x08) /* Timer 1 control word */
#define TIMER_EOI1	__REG(TIMER_PHYS + 0x0c) /* Timer 1 interrupt clear */

#define TIMER_LOAD2	__REG(TIMER_PHYS + 0x20) /* Timer 2 initial value */
#define TIMER_VALUE2	__REG(TIMER_PHYS + 0x24) /* Timer 2 current value */
#define TIMER_CONTROL2	__REG(TIMER_PHYS + 0x28) /* Timer 2 control word */
#define TIMER_EOI2	__REG(TIMER_PHYS + 0x2c) /* Timer 2 interrupt clear */

#define TIMER_BUZZCON	__REG(TIMER_PHYS + 0x40) /* Buzzer configuration */

#define TIMER_LOAD3	__REG(TIMER_PHYS + 0x80) /* Timer 3 initial value */
#define TIMER_VALUE3	__REG(TIMER_PHYS + 0x84) /* Timer 3 current value */
#define TIMER_CONTROL3	__REG(TIMER_PHYS + 0x88) /* Timer 3 control word */
#define TIMER_EOI3	__REG(TIMER_PHYS + 0x8c) /* Timer 3 interrupt clear */

#define TIMER_C_ENABLE		(1<<7)
#define TIMER_C_PERIODIC	(1<<6)
#define TIMER_C_FREERUNNING	(0)
#define TIMER_C_2KHZ		(0x00)		/* 1.986 kHz */
#define TIMER_C_508KHZ		(0x08)

	/* GPIO registers */

#define GPIO_PFDD		__REG(GPIO_PHYS + 0x34)	/* PF direction */
#define GPIO_INTTYPE1		__REG(GPIO_PHYS + 0x4c)	/* IRQ edge or lvl  */
#define GPIO_INTTYPE2		__REG(GPIO_PHYS + 0x50)	/* IRQ activ hi/lo */
#define GPIO_GPIOFEOI		__REG(GPIO_PHYS + 0x54)	/* GPIOF end of IRQ */
#define GPIO_GPIOFINTEN		__REG(GPIO_PHYS + 0x58)	/* GPIOF IRQ enable */
#define GPIO_INTSTATUS		__REG(GPIO_PHYS + 0x5c)	/* GPIOF IRQ latch */
#define GPIO_RAWINTSTATUS	__REG(GPIO_PHYS + 0x60)	/* GPIOF IRQ raw */


#endif  /* _ASM_ARCH_REGISTERS_H */
