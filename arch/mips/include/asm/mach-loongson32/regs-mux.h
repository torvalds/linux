/*
 * Copyright (c) 2014 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * Loongson 1 MUX Register Definitions.
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_MACH_LOONGSON32_REGS_MUX_H
#define __ASM_MACH_LOONGSON32_REGS_MUX_H

#define LS1X_MUX_REG(x) \
		((void __iomem *)KSEG1ADDR(LS1X_MUX_BASE + (x)))

#define LS1X_MUX_CTRL0			LS1X_MUX_REG(0x0)
#define LS1X_MUX_CTRL1			LS1X_MUX_REG(0x4)

/* MUX CTRL0 Register Bits */
#define UART0_USE_PWM23			(0x1 << 28)
#define UART0_USE_PWM01			(0x1 << 27)
#define UART1_USE_LCD0_5_6_11		(0x1 << 26)
#define I2C2_USE_CAN1			(0x1 << 25)
#define I2C1_USE_CAN0			(0x1 << 24)
#define NAND3_USE_UART5			(0x1 << 23)
#define NAND3_USE_UART4			(0x1 << 22)
#define NAND3_USE_UART1_DAT		(0x1 << 21)
#define NAND3_USE_UART1_CTS		(0x1 << 20)
#define NAND3_USE_PWM23			(0x1 << 19)
#define NAND3_USE_PWM01			(0x1 << 18)
#define NAND2_USE_UART5			(0x1 << 17)
#define NAND2_USE_UART4			(0x1 << 16)
#define NAND2_USE_UART1_DAT		(0x1 << 15)
#define NAND2_USE_UART1_CTS		(0x1 << 14)
#define NAND2_USE_PWM23			(0x1 << 13)
#define NAND2_USE_PWM01			(0x1 << 12)
#define NAND1_USE_UART5			(0x1 << 11)
#define NAND1_USE_UART4			(0x1 << 10)
#define NAND1_USE_UART1_DAT		(0x1 << 9)
#define NAND1_USE_UART1_CTS		(0x1 << 8)
#define NAND1_USE_PWM23			(0x1 << 7)
#define NAND1_USE_PWM01			(0x1 << 6)
#define GMAC1_USE_UART1			(0x1 << 4)
#define GMAC1_USE_UART0			(0x1 << 3)
#define LCD_USE_UART0_DAT		(0x1 << 2)
#define LCD_USE_UART15			(0x1 << 1)
#define LCD_USE_UART0			0x1

/* MUX CTRL1 Register Bits */
#define USB_RESET			(0x1 << 31)
#define SPI1_CS_USE_PWM01		(0x1 << 24)
#define SPI1_USE_CAN			(0x1 << 23)
#define DISABLE_DDR_CONFSPACE		(0x1 << 20)
#define DDR32TO16EN			(0x1 << 16)
#define GMAC1_SHUT			(0x1 << 13)
#define GMAC0_SHUT			(0x1 << 12)
#define USB_SHUT			(0x1 << 11)
#define UART1_3_USE_CAN1		(0x1 << 5)
#define UART1_2_USE_CAN0		(0x1 << 4)
#define GMAC1_USE_TXCLK			(0x1 << 3)
#define GMAC0_USE_TXCLK			(0x1 << 2)
#define GMAC1_USE_PWM23			(0x1 << 1)
#define GMAC0_USE_PWM01			0x1

#endif /* __ASM_MACH_LOONGSON32_REGS_MUX_H */
