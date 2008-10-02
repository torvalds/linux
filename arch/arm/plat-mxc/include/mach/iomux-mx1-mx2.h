/*
 * Copyright (C) 2008 by Sascha Hauer <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef _MXC_GPIO_MX1_MX2_H
#define _MXC_GPIO_MX1_MX2_H

#include <linux/io.h>

#define MXC_GPIO_ALLOC_MODE_NORMAL	0
#define MXC_GPIO_ALLOC_MODE_NO_ALLOC	1
#define MXC_GPIO_ALLOC_MODE_TRY_ALLOC	2
#define MXC_GPIO_ALLOC_MODE_ALLOC_ONLY	4
#define MXC_GPIO_ALLOC_MODE_RELEASE	8

/*
 *  GPIO Module and I/O Multiplexer
 *  x = 0..3 for reg_A, reg_B, reg_C, reg_D
 */
#define VA_GPIO_BASE	IO_ADDRESS(GPIO_BASE_ADDR)
#define MXC_DDIR(x)    (0x00 + ((x) << 8))
#define MXC_OCR1(x)    (0x04 + ((x) << 8))
#define MXC_OCR2(x)    (0x08 + ((x) << 8))
#define MXC_ICONFA1(x) (0x0c + ((x) << 8))
#define MXC_ICONFA2(x) (0x10 + ((x) << 8))
#define MXC_ICONFB1(x) (0x14 + ((x) << 8))
#define MXC_ICONFB2(x) (0x18 + ((x) << 8))
#define MXC_DR(x)      (0x1c + ((x) << 8))
#define MXC_GIUS(x)    (0x20 + ((x) << 8))
#define MXC_SSR(x)     (0x24 + ((x) << 8))
#define MXC_ICR1(x)    (0x28 + ((x) << 8))
#define MXC_ICR2(x)    (0x2c + ((x) << 8))
#define MXC_IMR(x)     (0x30 + ((x) << 8))
#define MXC_ISR(x)     (0x34 + ((x) << 8))
#define MXC_GPR(x)     (0x38 + ((x) << 8))
#define MXC_SWR(x)     (0x3c + ((x) << 8))
#define MXC_PUEN(x)    (0x40 + ((x) << 8))

#ifdef CONFIG_ARCH_MX1
# define GPIO_PORT_MAX  3
#endif
#ifdef CONFIG_ARCH_MX2
# define GPIO_PORT_MAX  5
#endif

#ifndef GPIO_PORT_MAX
# error "GPIO config port count unknown!"
#endif

#define GPIO_PIN_MASK 0x1f

#define GPIO_PORT_SHIFT 5
#define GPIO_PORT_MASK (0x7 << GPIO_PORT_SHIFT)

#define GPIO_PORTA (0 << GPIO_PORT_SHIFT)
#define GPIO_PORTB (1 << GPIO_PORT_SHIFT)
#define GPIO_PORTC (2 << GPIO_PORT_SHIFT)
#define GPIO_PORTD (3 << GPIO_PORT_SHIFT)
#define GPIO_PORTE (4 << GPIO_PORT_SHIFT)
#define GPIO_PORTF (5 << GPIO_PORT_SHIFT)

#define GPIO_OUT   (1 << 8)
#define GPIO_IN    (0 << 8)
#define GPIO_PUEN  (1 << 9)

#define GPIO_PF    (1 << 10)
#define GPIO_AF    (1 << 11)

#define GPIO_OCR_SHIFT 12
#define GPIO_OCR_MASK (3 << GPIO_OCR_SHIFT)
#define GPIO_AIN   (0 << GPIO_OCR_SHIFT)
#define GPIO_BIN   (1 << GPIO_OCR_SHIFT)
#define GPIO_CIN   (2 << GPIO_OCR_SHIFT)
#define GPIO_GPIO  (3 << GPIO_OCR_SHIFT)

#define GPIO_AOUT_SHIFT 14
#define GPIO_AOUT_MASK (3 << GPIO_AOUT_SHIFT)
#define GPIO_AOUT     (0 << GPIO_AOUT_SHIFT)
#define GPIO_AOUT_ISR (1 << GPIO_AOUT_SHIFT)
#define GPIO_AOUT_0   (2 << GPIO_AOUT_SHIFT)
#define GPIO_AOUT_1   (3 << GPIO_AOUT_SHIFT)

#define GPIO_BOUT_SHIFT 16
#define GPIO_BOUT_MASK (3 << GPIO_BOUT_SHIFT)
#define GPIO_BOUT      (0 << GPIO_BOUT_SHIFT)
#define GPIO_BOUT_ISR  (1 << GPIO_BOUT_SHIFT)
#define GPIO_BOUT_0    (2 << GPIO_BOUT_SHIFT)
#define GPIO_BOUT_1    (3 << GPIO_BOUT_SHIFT)

extern void mxc_gpio_mode(int gpio_mode);
extern int mxc_gpio_setup_multiple_pins(const int *pin_list, unsigned count,
					int alloc_mode, const char *label);

/*-------------------------------------------------------------------------*/

/* assignements for GPIO alternate/primary functions */

/* FIXME: This list is not completed. The correct directions are
 * missing on some (many) pins
 */
#ifdef CONFIG_ARCH_MX1
#define PA0_AIN_SPI2_CLK     (GPIO_GIUS | GPIO_PORTA | GPIO_OUT | 0)
#define PA0_AF_ETMTRACESYNC  (GPIO_PORTA | GPIO_AF | 0)
#define PA1_AOUT_SPI2_RXD    (GPIO_GIUS | GPIO_PORTA | GPIO_IN | 1)
#define PA1_PF_TIN           (GPIO_PORTA | GPIO_PF | 1)
#define PA2_PF_PWM0          (GPIO_PORTA | GPIO_OUT | GPIO_PF | 2)
#define PA3_PF_CSI_MCLK      (GPIO_PORTA | GPIO_PF | 3)
#define PA4_PF_CSI_D0        (GPIO_PORTA | GPIO_PF | 4)
#define PA5_PF_CSI_D1        (GPIO_PORTA | GPIO_PF | 5)
#define PA6_PF_CSI_D2        (GPIO_PORTA | GPIO_PF | 6)
#define PA7_PF_CSI_D3        (GPIO_PORTA | GPIO_PF | 7)
#define PA8_PF_CSI_D4        (GPIO_PORTA | GPIO_PF | 8)
#define PA9_PF_CSI_D5        (GPIO_PORTA | GPIO_PF | 9)
#define PA10_PF_CSI_D6       (GPIO_PORTA | GPIO_PF | 10)
#define PA11_PF_CSI_D7       (GPIO_PORTA | GPIO_PF | 11)
#define PA12_PF_CSI_VSYNC    (GPIO_PORTA | GPIO_PF | 12)
#define PA13_PF_CSI_HSYNC    (GPIO_PORTA | GPIO_PF | 13)
#define PA14_PF_CSI_PIXCLK   (GPIO_PORTA | GPIO_PF | 14)
#define PA15_PF_I2C_SDA      (GPIO_PORTA | GPIO_OUT | GPIO_PF | 15)
#define PA16_PF_I2C_SCL      (GPIO_PORTA | GPIO_OUT | GPIO_PF | 16)
#define PA17_AF_ETMTRACEPKT4 (GPIO_PORTA | GPIO_AF | 17)
#define PA17_AIN_SPI2_SS     (GPIO_GIUS | GPIO_PORTA | GPIO_OUT | 17)
#define PA18_AF_ETMTRACEPKT5 (GPIO_PORTA | GPIO_AF | 18)
#define PA19_AF_ETMTRACEPKT6 (GPIO_PORTA | GPIO_AF | 19)
#define PA20_AF_ETMTRACEPKT7 (GPIO_PORTA | GPIO_AF | 20)
#define PA21_PF_A0           (GPIO_PORTA | GPIO_PF | 21)
#define PA22_PF_CS4          (GPIO_PORTA | GPIO_PF | 22)
#define PA23_PF_CS5          (GPIO_PORTA | GPIO_PF | 23)
#define PA24_PF_A16          (GPIO_PORTA | GPIO_PF | 24)
#define PA24_AF_ETMTRACEPKT0 (GPIO_PORTA | GPIO_AF | 24)
#define PA25_PF_A17          (GPIO_PORTA | GPIO_PF | 25)
#define PA25_AF_ETMTRACEPKT1 (GPIO_PORTA | GPIO_AF | 25)
#define PA26_PF_A18          (GPIO_PORTA | GPIO_PF | 26)
#define PA26_AF_ETMTRACEPKT2 (GPIO_PORTA | GPIO_AF | 26)
#define PA27_PF_A19          (GPIO_PORTA | GPIO_PF | 27)
#define PA27_AF_ETMTRACEPKT3 (GPIO_PORTA | GPIO_AF | 27)
#define PA28_PF_A20          (GPIO_PORTA | GPIO_PF | 28)
#define PA28_AF_ETMPIPESTAT0 (GPIO_PORTA | GPIO_AF | 28)
#define PA29_PF_A21          (GPIO_PORTA | GPIO_PF | 29)
#define PA29_AF_ETMPIPESTAT1 (GPIO_PORTA | GPIO_AF | 29)
#define PA30_PF_A22          (GPIO_PORTA | GPIO_PF | 30)
#define PA30_AF_ETMPIPESTAT2 (GPIO_PORTA | GPIO_AF | 30)
#define PA31_PF_A23          (GPIO_PORTA | GPIO_PF | 31)
#define PA31_AF_ETMTRACECLK  (GPIO_PORTA | GPIO_AF | 31)
#define PB8_PF_SD_DAT0       (GPIO_PORTB | GPIO_PF | GPIO_PUEN | 8)
#define PB8_AF_MS_PIO        (GPIO_PORTB | GPIO_AF | 8)
#define PB9_PF_SD_DAT1       (GPIO_PORTB | GPIO_PF | GPIO_PUEN  | 9)
#define PB9_AF_MS_PI1        (GPIO_PORTB | GPIO_AF | 9)
#define PB10_PF_SD_DAT2      (GPIO_PORTB | GPIO_PF | GPIO_PUEN  | 10)
#define PB10_AF_MS_SCLKI     (GPIO_PORTB | GPIO_AF | 10)
#define PB11_PF_SD_DAT3      (GPIO_PORTB | GPIO_PF | 11)
#define PB11_AF_MS_SDIO      (GPIO_PORTB | GPIO_AF | 11)
#define PB12_PF_SD_CLK       (GPIO_PORTB | GPIO_PF | 12)
#define PB12_AF_MS_SCLK0     (GPIO_PORTB | GPIO_AF | 12)
#define PB13_PF_SD_CMD       (GPIO_PORTB | GPIO_PF | GPIO_PUEN | 13)
#define PB13_AF_MS_BS        (GPIO_PORTB | GPIO_AF | 13)
#define PB14_AF_SSI_RXFS     (GPIO_PORTB | GPIO_AF | 14)
#define PB15_AF_SSI_RXCLK    (GPIO_PORTB | GPIO_AF | 15)
#define PB16_AF_SSI_RXDAT    (GPIO_PORTB | GPIO_IN | GPIO_AF | 16)
#define PB17_AF_SSI_TXDAT    (GPIO_PORTB | GPIO_OUT | GPIO_AF | 17)
#define PB18_AF_SSI_TXFS     (GPIO_PORTB | GPIO_AF | 18)
#define PB19_AF_SSI_TXCLK    (GPIO_PORTB | GPIO_AF | 19)
#define PB20_PF_USBD_AFE     (GPIO_PORTB | GPIO_PF | 20)
#define PB21_PF_USBD_OE      (GPIO_PORTB | GPIO_PF | 21)
#define PB22_PFUSBD_RCV      (GPIO_PORTB | GPIO_PF | 22)
#define PB23_PF_USBD_SUSPND  (GPIO_PORTB | GPIO_PF | 23)
#define PB24_PF_USBD_VP      (GPIO_PORTB | GPIO_PF | 24)
#define PB25_PF_USBD_VM      (GPIO_PORTB | GPIO_PF | 25)
#define PB26_PF_USBD_VPO     (GPIO_PORTB | GPIO_PF | 26)
#define PB27_PF_USBD_VMO     (GPIO_PORTB | GPIO_PF | 27)
#define PB28_PF_UART2_CTS    (GPIO_PORTB | GPIO_OUT | GPIO_PF | 28)
#define PB29_PF_UART2_RTS    (GPIO_PORTB | GPIO_IN | GPIO_PF | 29)
#define PB30_PF_UART2_TXD    (GPIO_PORTB | GPIO_OUT | GPIO_PF | 30)
#define PB31_PF_UART2_RXD    (GPIO_PORTB | GPIO_IN | GPIO_PF | 31)
#define PC3_PF_SSI_RXFS      (GPIO_PORTC | GPIO_PF | 3)
#define PC4_PF_SSI_RXCLK     (GPIO_PORTC | GPIO_PF | 4)
#define PC5_PF_SSI_RXDAT     (GPIO_PORTC | GPIO_IN | GPIO_PF | 5)
#define PC6_PF_SSI_TXDAT     (GPIO_PORTC | GPIO_OUT | GPIO_PF | 6)
#define PC7_PF_SSI_TXFS      (GPIO_PORTC | GPIO_PF | 7)
#define PC8_PF_SSI_TXCLK     (GPIO_PORTC | GPIO_PF | 8)
#define PC9_PF_UART1_CTS     (GPIO_PORTC | GPIO_OUT | GPIO_PF | 9)
#define PC10_PF_UART1_RTS    (GPIO_PORTC | GPIO_IN | GPIO_PF | 10)
#define PC11_PF_UART1_TXD    (GPIO_PORTC | GPIO_OUT | GPIO_PF | 11)
#define PC12_PF_UART1_RXD    (GPIO_PORTC | GPIO_IN | GPIO_PF | 12)
#define PC13_PF_SPI1_SPI_RDY (GPIO_PORTC | GPIO_PF | 13)
#define PC14_PF_SPI1_SCLK    (GPIO_PORTC | GPIO_PF | 14)
#define PC15_PF_SPI1_SS      (GPIO_PORTC | GPIO_PF | 15)
#define PC16_PF_SPI1_MISO    (GPIO_PORTC | GPIO_PF | 16)
#define PC17_PF_SPI1_MOSI    (GPIO_PORTC | GPIO_PF | 17)
#define PC24_BIN_UART3_RI    (GPIO_GIUS | GPIO_PORTC | GPIO_OUT | GPIO_BIN | 24)
#define PC25_BIN_UART3_DSR   (GPIO_GIUS | GPIO_PORTC | GPIO_OUT | GPIO_BIN | 25)
#define PC26_AOUT_UART3_DTR  (GPIO_GIUS | GPIO_PORTC | GPIO_IN | 26)
#define PC27_BIN_UART3_DCD   (GPIO_GIUS | GPIO_PORTC | GPIO_OUT | GPIO_BIN | 27)
#define PC28_BIN_UART3_CTS   (GPIO_GIUS | GPIO_PORTC | GPIO_OUT | GPIO_BIN | 28)
#define PC29_AOUT_UART3_RTS  (GPIO_GIUS | GPIO_PORTC | GPIO_IN | 29)
#define PC30_BIN_UART3_TX    (GPIO_GIUS | GPIO_PORTC | GPIO_BIN | 30)
#define PC31_AOUT_UART3_RX   (GPIO_GIUS | GPIO_PORTC | GPIO_IN | 31)
#define PD6_PF_LSCLK         (GPIO_PORTD | GPIO_OUT | GPIO_PF | 6)
#define PD7_PF_REV           (GPIO_PORTD | GPIO_PF | 7)
#define PD7_AF_UART2_DTR     (GPIO_GIUS | GPIO_PORTD | GPIO_IN | GPIO_AF | 7)
#define PD7_AIN_SPI2_SCLK    (GPIO_GIUS | GPIO_PORTD | GPIO_AIN | 7)
#define PD8_PF_CLS           (GPIO_PORTD | GPIO_PF | 8)
#define PD8_AF_UART2_DCD     (GPIO_PORTD | GPIO_OUT | GPIO_AF | 8)
#define PD8_AIN_SPI2_SS      (GPIO_GIUS | GPIO_PORTD | GPIO_AIN | 8)
#define PD9_PF_PS            (GPIO_PORTD | GPIO_PF | 9)
#define PD9_AF_UART2_RI      (GPIO_PORTD | GPIO_OUT | GPIO_AF | 9)
#define PD9_AOUT_SPI2_RXD    (GPIO_GIUS | GPIO_PORTD | GPIO_IN | 9)
#define PD10_PF_SPL_SPR      (GPIO_PORTD | GPIO_OUT | GPIO_PF | 10)
#define PD10_AF_UART2_DSR    (GPIO_PORTD | GPIO_OUT | GPIO_AF | 10)
#define PD10_AIN_SPI2_TXD    (GPIO_GIUS | GPIO_PORTD | GPIO_OUT | 10)
#define PD11_PF_CONTRAST     (GPIO_PORTD | GPIO_OUT | GPIO_PF | 11)
#define PD12_PF_ACD_OE       (GPIO_PORTD | GPIO_OUT | GPIO_PF | 12)
#define PD13_PF_LP_HSYNC     (GPIO_PORTD | GPIO_OUT | GPIO_PF | 13)
#define PD14_PF_FLM_VSYNC    (GPIO_PORTD | GPIO_OUT | GPIO_PF | 14)
#define PD15_PF_LD0          (GPIO_PORTD | GPIO_OUT | GPIO_PF | 15)
#define PD16_PF_LD1          (GPIO_PORTD | GPIO_OUT | GPIO_PF | 16)
#define PD17_PF_LD2          (GPIO_PORTD | GPIO_OUT | GPIO_PF | 17)
#define PD18_PF_LD3          (GPIO_PORTD | GPIO_OUT | GPIO_PF | 18)
#define PD19_PF_LD4          (GPIO_PORTD | GPIO_OUT | GPIO_PF | 19)
#define PD20_PF_LD5          (GPIO_PORTD | GPIO_OUT | GPIO_PF | 20)
#define PD21_PF_LD6          (GPIO_PORTD | GPIO_OUT | GPIO_PF | 21)
#define PD22_PF_LD7          (GPIO_PORTD | GPIO_OUT | GPIO_PF | 22)
#define PD23_PF_LD8          (GPIO_PORTD | GPIO_OUT | GPIO_PF | 23)
#define PD24_PF_LD9          (GPIO_PORTD | GPIO_OUT | GPIO_PF | 24)
#define PD25_PF_LD10         (GPIO_PORTD | GPIO_OUT | GPIO_PF | 25)
#define PD26_PF_LD11         (GPIO_PORTD | GPIO_OUT | GPIO_PF | 26)
#define PD27_PF_LD12         (GPIO_PORTD | GPIO_OUT | GPIO_PF | 27)
#define PD28_PF_LD13         (GPIO_PORTD | GPIO_OUT | GPIO_PF | 28)
#define PD29_PF_LD14         (GPIO_PORTD | GPIO_OUT | GPIO_PF | 29)
#define PD30_PF_LD15         (GPIO_PORTD | GPIO_OUT | GPIO_PF | 30)
#define PD31_PF_TMR2OUT      (GPIO_PORTD | GPIO_PF | 31)
#define PD31_BIN_SPI2_TXD    (GPIO_GIUS | GPIO_PORTD | GPIO_BIN | 31)
#endif

#ifdef CONFIG_ARCH_MX2
#define PA5_PF_LSCLK		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 5)
#define PA6_PF_LD0		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 6)
#define PA7_PF_LD1		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 7)
#define PA8_PF_LD2		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 8)
#define PA9_PF_LD3		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 9)
#define PA10_PF_LD4		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 10)
#define PA11_PF_LD5		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 11)
#define PA12_PF_LD6		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 12)
#define PA13_PF_LD7		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 13)
#define PA14_PF_LD8		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 14)
#define PA15_PF_LD9		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 15)
#define PA16_PF_LD10		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 16)
#define PA17_PF_LD11		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 17)
#define PA18_PF_LD12		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 18)
#define PA19_PF_LD13		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 19)
#define PA20_PF_LD14		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 20)
#define PA21_PF_LD15		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 21)
#define PA22_PF_LD16		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 22)
#define PA23_PF_LD17		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 23)
#define PA24_PF_REV		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 24)
#define PA25_PF_CLS		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 25)
#define PA26_PF_PS		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 26)
#define PA27_PF_SPL_SPR		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 27)
#define PA28_PF_HSYNC		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 28)
#define PA29_PF_VSYNC		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 29)
#define PA30_PF_CONTRAST	(GPIO_PORTA | GPIO_OUT | GPIO_PF | 30)
#define PA31_PF_OE_ACD		(GPIO_PORTA | GPIO_OUT | GPIO_PF | 31)
#define PB10_PF_CSI_D0		(GPIO_PORTB | GPIO_OUT | GPIO_PF | 10)
#define PB10_AF_UART6_TXD	(GPIO_PORTB | GPIO_OUT | GPIO_AF | 10)
#define PB11_PF_CSI_D1		(GPIO_PORTB | GPIO_OUT | GPIO_PF | 11)
#define PB11_AF_UART6_RXD	(GPIO_PORTB | GPIO_IN  | GPIO_AF | 11)
#define PB12_PF_CSI_D2		(GPIO_PORTB | GPIO_OUT | GPIO_PF | 12)
#define PB12_AF_UART6_CTS	(GPIO_PORTB | GPIO_OUT | GPIO_AF | 12)
#define PB13_PF_CSI_D3		(GPIO_PORTB | GPIO_OUT | GPIO_PF | 13)
#define PB13_AF_UART6_RTS	(GPIO_PORTB | GPIO_IN  | GPIO_AF | 13)
#define PB14_PF_CSI_D4		(GPIO_PORTB | GPIO_OUT | GPIO_PF | 14)
#define PB15_PF_CSI_MCLK	(GPIO_PORTB | GPIO_OUT | GPIO_PF | 15)
#define PB16_PF_CSI_PIXCLK	(GPIO_PORTB | GPIO_OUT | GPIO_PF | 16)
#define PB17_PF_CSI_D5		(GPIO_PORTB | GPIO_OUT | GPIO_PF | 17)
#define PB18_PF_CSI_D6		(GPIO_PORTB | GPIO_OUT | GPIO_PF | 18)
#define PB18_AF_UART5_TXD	(GPIO_PORTB | GPIO_OUT | GPIO_AF | 18)
#define PB19_PF_CSI_D7		(GPIO_PORTB | GPIO_OUT | GPIO_PF | 19)
#define PB19_AF_UART5_RXD	(GPIO_PORTB | GPIO_IN  | GPIO_AF | 19)
#define PB20_PF_CSI_VSYNC	(GPIO_PORTB | GPIO_OUT | GPIO_PF | 20)
#define PB20_AF_UART5_CTS	(GPIO_PORTB | GPIO_OUT | GPIO_AF | 20)
#define PB21_PF_CSI_HSYNC	(GPIO_PORTB | GPIO_OUT | GPIO_PF | 21)
#define PB21_AF_UART5_RTS	(GPIO_PORTB | GPIO_IN  | GPIO_AF | 21)
#define PB26_AF_UART4_RTS	(GPIO_PORTB | GPIO_IN  | GPIO_PF | 26)
#define PB28_AF_UART4_TXD	(GPIO_PORTB | GPIO_OUT | GPIO_AF | 28)
#define PB29_AF_UART4_CTS	(GPIO_PORTB | GPIO_OUT | GPIO_AF | 29)
#define PB31_AF_UART4_RXD	(GPIO_PORTB | GPIO_IN  | GPIO_AF | 31)
#define PC5_PF_I2C2_SDA		(GPIO_PORTC | GPIO_IN  | GPIO_PF | 5)
#define PC6_PF_I2C2_SCL		(GPIO_PORTC | GPIO_IN  | GPIO_PF | 6)
#define PC16_PF_SSI4_FS		(GPIO_PORTC | GPIO_IN  | GPIO_PF | 16)
#define PC17_PF_SSI4_RXD	(GPIO_PORTC | GPIO_IN  | GPIO_PF | 17)
#define PC18_PF_SSI4_TXD	(GPIO_PORTC | GPIO_IN  | GPIO_PF | 18)
#define PC19_PF_SSI4_CLK	(GPIO_PORTC | GPIO_IN  | GPIO_PF | 19)
#define PC20_PF_SSI1_FS		(GPIO_PORTC | GPIO_IN  | GPIO_PF | 20)
#define PC21_PF_SSI1_RXD	(GPIO_PORTC | GPIO_IN  | GPIO_PF | 21)
#define PC22_PF_SSI1_TXD	(GPIO_PORTC | GPIO_IN  | GPIO_PF | 22)
#define PC23_PF_SSI1_CLK	(GPIO_PORTC | GPIO_IN  | GPIO_PF | 23)
#define PC24_PF_SSI2_FS		(GPIO_PORTC | GPIO_IN  | GPIO_PF | 24)
#define PC25_PF_SSI2_RXD	(GPIO_PORTC | GPIO_IN  | GPIO_PF | 25)
#define PC26_PF_SSI2_TXD	(GPIO_PORTC | GPIO_IN  | GPIO_PF | 26)
#define PC27_PF_SSI2_CLK	(GPIO_PORTC | GPIO_IN  | GPIO_PF | 27)
#define PC28_PF_SSI3_FS		(GPIO_PORTC | GPIO_IN  | GPIO_PF | 28)
#define PC29_PF_SSI3_RXD	(GPIO_PORTC | GPIO_IN  | GPIO_PF | 29)
#define PC30_PF_SSI3_TXD	(GPIO_PORTC | GPIO_IN  | GPIO_PF | 30)
#define PC31_PF_SSI3_CLK	(GPIO_PORTC | GPIO_IN  | GPIO_PF | 31)
#define PD0_AIN_FEC_TXD0	(GPIO_PORTD | GPIO_OUT | GPIO_AIN | 0)
#define PD1_AIN_FEC_TXD1	(GPIO_PORTD | GPIO_OUT | GPIO_AIN | 1)
#define PD2_AIN_FEC_TXD2	(GPIO_PORTD | GPIO_OUT | GPIO_AIN | 2)
#define PD3_AIN_FEC_TXD3	(GPIO_PORTD | GPIO_OUT | GPIO_AIN | 3)
#define PD4_AOUT_FEC_RX_ER	(GPIO_PORTD | GPIO_IN | GPIO_AOUT | 4)
#define PD5_AOUT_FEC_RXD1	(GPIO_PORTD | GPIO_IN | GPIO_AOUT | 5)
#define PD6_AOUT_FEC_RXD2	(GPIO_PORTD | GPIO_IN | GPIO_AOUT | 6)
#define PD7_AOUT_FEC_RXD3	(GPIO_PORTD | GPIO_IN | GPIO_AOUT | 7)
#define PD8_AF_FEC_MDIO		(GPIO_PORTD | GPIO_IN | GPIO_AF | 8)
#define PD9_AIN_FEC_MDC		(GPIO_PORTD | GPIO_OUT | GPIO_AIN | 9)
#define PD10_AOUT_FEC_CRS	(GPIO_PORTD | GPIO_IN | GPIO_AOUT | 10)
#define PD11_AOUT_FEC_TX_CLK	(GPIO_PORTD | GPIO_IN | GPIO_AOUT | 11)
#define PD12_AOUT_FEC_RXD0	(GPIO_PORTD | GPIO_IN | GPIO_AOUT | 12)
#define PD13_AOUT_FEC_RX_DV	(GPIO_PORTD | GPIO_IN | GPIO_AOUT | 13)
#define PD14_AOUT_FEC_CLR	(GPIO_PORTD | GPIO_IN | GPIO_AOUT | 14)
#define PD15_AOUT_FEC_COL	(GPIO_PORTD | GPIO_IN | GPIO_AOUT | 15)
#define PD16_AIN_FEC_TX_ER	(GPIO_PORTD | GPIO_OUT | GPIO_AIN | 16)
#define PD17_PF_I2C_DATA	(GPIO_PORTD | GPIO_OUT | GPIO_PF | 17)
#define PD18_PF_I2C_CLK		(GPIO_PORTD | GPIO_OUT | GPIO_PF | 18)
#define PD25_PF_CSPI1_RDY	(GPIO_PORTD | GPIO_OUT | GPIO_PF  | 25)
#define PD26_PF_CSPI1_SS2	(GPIO_PORTD | GPIO_OUT | GPIO_PF  | 26)
#define PD27_PF_CSPI1_SS1	(GPIO_PORTD | GPIO_OUT | GPIO_PF  | 27)
#define PD28_PF_CSPI1_SS0	(GPIO_PORTD | GPIO_OUT | GPIO_PF  | 28)
#define PD29_PF_CSPI1_SCLK	(GPIO_PORTD | GPIO_OUT | GPIO_PF  | 29)
#define PD30_PF_CSPI1_MISO	(GPIO_PORTD | GPIO_IN | GPIO_PF  | 30)
#define PD31_PF_CSPI1_MOSI	(GPIO_PORTD | GPIO_OUT | GPIO_PF  | 31)
#define PF23_AIN_FEC_TX_EN	(GPIO_PORTF | GPIO_OUT | GPIO_AIN | 23)
#define PE3_PF_UART2_CTS	(GPIO_PORTE | GPIO_OUT | GPIO_PF | 3)
#define PE4_PF_UART2_RTS	(GPIO_PORTE | GPIO_IN  | GPIO_PF | 4)
#define PE6_PF_UART2_TXD	(GPIO_PORTE | GPIO_OUT | GPIO_PF | 6)
#define PE7_PF_UART2_RXD	(GPIO_PORTE | GPIO_IN  | GPIO_PF | 7)
#define PE8_PF_UART3_TXD	(GPIO_PORTE | GPIO_OUT | GPIO_PF | 8)
#define PE9_PF_UART3_RXD	(GPIO_PORTE | GPIO_IN  | GPIO_PF | 9)
#define PE10_PF_UART3_CTS	(GPIO_PORTE | GPIO_OUT | GPIO_PF | 10)
#define PE11_PF_UART3_RTS	(GPIO_PORTE | GPIO_IN  | GPIO_PF | 11)
#define PE12_PF_UART1_TXD	(GPIO_PORTE | GPIO_OUT | GPIO_PF | 12)
#define PE13_PF_UART1_RXD	(GPIO_PORTE | GPIO_IN  | GPIO_PF | 13)
#define PE14_PF_UART1_CTS	(GPIO_PORTE | GPIO_OUT | GPIO_PF | 14)
#define PE15_PF_UART1_RTS	(GPIO_PORTE | GPIO_IN  | GPIO_PF | 15)
#define PE18_AF_CSPI3_MISO	(GPIO_PORTE | GPIO_IN  | GPIO_AF | 18)
#define PE21_AF_CSPI3_SS	(GPIO_PORTE | GPIO_OUT | GPIO_AF | 21)
#define PE22_AF_CSPI3_MOSI	(GPIO_PORTE | GPIO_OUT | GPIO_AF | 22)
#define PE23_AF_CSPI3_SCLK	(GPIO_PORTE | GPIO_OUT | GPIO_AF | 23)
#endif

/* decode irq number to use with IMR(x), ISR(x) and friends */
#define IRQ_TO_REG(irq) ((irq - MXC_MAX_INT_LINES) >> 5)

#define IRQ_GPIOA(x)  (MXC_MAX_INT_LINES + x)
#define IRQ_GPIOB(x)  (IRQ_GPIOA(32) + x)
#define IRQ_GPIOC(x)  (IRQ_GPIOB(32) + x)
#define IRQ_GPIOD(x)  (IRQ_GPIOC(32) + x)

#endif /* _MXC_GPIO_MX1_MX2_H */
