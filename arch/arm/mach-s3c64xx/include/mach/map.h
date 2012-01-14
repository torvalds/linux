/* linux/arch/arm/mach-s3c6400/include/mach/map.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C64XX - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_MAP_H
#define __ASM_ARCH_MAP_H __FILE__

#include <plat/map-base.h>
#include <plat/map-s3c.h>

/*
 * Post-mux Chip Select Regions Xm0CSn_
 * These may be used by SROM, NAND or CF depending on settings
 */

#define S3C64XX_PA_XM0CSN0 (0x10000000)
#define S3C64XX_PA_XM0CSN1 (0x18000000)
#define S3C64XX_PA_XM0CSN2 (0x20000000)
#define S3C64XX_PA_XM0CSN3 (0x28000000)
#define S3C64XX_PA_XM0CSN4 (0x30000000)
#define S3C64XX_PA_XM0CSN5 (0x38000000)

/* HSMMC units */
#define S3C64XX_PA_HSMMC(x)	(0x7C200000 + ((x) * 0x100000))
#define S3C64XX_PA_HSMMC0	S3C64XX_PA_HSMMC(0)
#define S3C64XX_PA_HSMMC1	S3C64XX_PA_HSMMC(1)
#define S3C64XX_PA_HSMMC2	S3C64XX_PA_HSMMC(2)

#define S3C_PA_UART		(0x7F005000)
#define S3C_PA_UART0		(S3C_PA_UART + 0x00)
#define S3C_PA_UART1		(S3C_PA_UART + 0x400)
#define S3C_PA_UART2		(S3C_PA_UART + 0x800)
#define S3C_PA_UART3		(S3C_PA_UART + 0xC00)
#define S3C_UART_OFFSET		(0x400)

/* See notes on UART VA mapping in debug-macro.S */
#define S3C_VA_UARTx(x)	(S3C_VA_UART + (S3C_PA_UART & 0xfffff) + ((x) * S3C_UART_OFFSET))

#define S3C_VA_UART0		S3C_VA_UARTx(0)
#define S3C_VA_UART1		S3C_VA_UARTx(1)
#define S3C_VA_UART2		S3C_VA_UARTx(2)
#define S3C_VA_UART3		S3C_VA_UARTx(3)

#define S3C64XX_PA_SROM		(0x70000000)

#define S3C64XX_PA_ONENAND0	(0x70100000)
#define S3C64XX_PA_ONENAND0_BUF	(0x20000000)
#define S3C64XX_SZ_ONENAND0_BUF (SZ_64M)

/* NAND and OneNAND1 controllers occupy the same register region
   (depending on SoC POP version) */
#define S3C64XX_PA_ONENAND1	(0x70200000)
#define S3C64XX_PA_ONENAND1_BUF	(0x28000000)
#define S3C64XX_SZ_ONENAND1_BUF	(SZ_64M)

#define S3C64XX_PA_NAND		(0x70200000)
#define S3C64XX_PA_FB		(0x77100000)
#define S3C64XX_PA_USB_HSOTG	(0x7C000000)
#define S3C64XX_PA_WATCHDOG	(0x7E004000)
#define S3C64XX_PA_RTC		(0x7E005000)
#define S3C64XX_PA_KEYPAD	(0x7E00A000)
#define S3C64XX_PA_ADC		(0x7E00B000)
#define S3C64XX_PA_SYSCON	(0x7E00F000)
#define S3C64XX_PA_AC97		(0x7F001000)
#define S3C64XX_PA_IIS0		(0x7F002000)
#define S3C64XX_PA_IIS1		(0x7F003000)
#define S3C64XX_PA_TIMER	(0x7F006000)
#define S3C64XX_PA_IIC0		(0x7F004000)
#define S3C64XX_PA_SPI0		(0x7F00B000)
#define S3C64XX_PA_SPI1		(0x7F00C000)
#define S3C64XX_PA_PCM0		(0x7F009000)
#define S3C64XX_PA_PCM1		(0x7F00A000)
#define S3C64XX_PA_IISV4	(0x7F00D000)
#define S3C64XX_PA_IIC1		(0x7F00F000)

#define S3C64XX_PA_GPIO		(0x7F008000)
#define S3C64XX_SZ_GPIO		SZ_4K

#define S3C64XX_PA_SDRAM	(0x50000000)

#define S3C64XX_PA_CFCON	(0x70300000)

#define S3C64XX_PA_VIC0		(0x71200000)
#define S3C64XX_PA_VIC1		(0x71300000)

#define S3C64XX_PA_MODEM	(0x74108000)

#define S3C64XX_PA_USBHOST	(0x74300000)

#define S3C64XX_PA_USB_HSPHY	(0x7C100000)

/* compatibiltiy defines. */
#define S3C_PA_TIMER		S3C64XX_PA_TIMER
#define S3C_PA_HSMMC0		S3C64XX_PA_HSMMC0
#define S3C_PA_HSMMC1		S3C64XX_PA_HSMMC1
#define S3C_PA_HSMMC2		S3C64XX_PA_HSMMC2
#define S3C_PA_IIC		S3C64XX_PA_IIC0
#define S3C_PA_IIC1		S3C64XX_PA_IIC1
#define S3C_PA_NAND		S3C64XX_PA_NAND
#define S3C_PA_ONENAND		S3C64XX_PA_ONENAND0
#define S3C_PA_ONENAND_BUF	S3C64XX_PA_ONENAND0_BUF
#define S3C_SZ_ONENAND_BUF	S3C64XX_SZ_ONENAND0_BUF
#define S3C_PA_FB		S3C64XX_PA_FB
#define S3C_PA_USBHOST		S3C64XX_PA_USBHOST
#define S3C_PA_USB_HSOTG	S3C64XX_PA_USB_HSOTG
#define S3C_PA_RTC		S3C64XX_PA_RTC
#define S3C_PA_WDT		S3C64XX_PA_WATCHDOG
#define S3C_PA_SPI0		S3C64XX_PA_SPI0
#define S3C_PA_SPI1		S3C64XX_PA_SPI1

#define SAMSUNG_PA_ADC		S3C64XX_PA_ADC
#define SAMSUNG_PA_CFCON	S3C64XX_PA_CFCON
#define SAMSUNG_PA_KEYPAD	S3C64XX_PA_KEYPAD

#endif /* __ASM_ARCH_6400_MAP_H */
