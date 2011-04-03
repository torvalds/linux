/*
 *  arch/arm/mach-vt8500/include/mach/wm8505_irqs.h
 *
 *  Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
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

/* WM8505 Interrupt Sources */

#define IRQ_UHCI	0	/* UHC FS (UHCI?) */
#define IRQ_EHCI	1	/* UHC HS */
#define IRQ_UDCDMA	2	/* UDC DMA */
				/* Reserved */
#define IRQ_PS2MOUSE	4	/* PS/2 Mouse */
#define IRQ_UDC		5	/* UDC */
#define IRQ_EXT0	6	/* External Interrupt 0 */
#define IRQ_EXT1	7	/* External Interrupt 1 */
#define IRQ_KEYPAD	8	/* Keypad */
#define IRQ_DMA		9	/* DMA Controller */
#define IRQ_ETHER	10	/* Ethernet MAC */
				/* Reserved */
				/* Reserved */
#define IRQ_EXT2	13	/* External Interrupt 2 */
#define IRQ_EXT3	14	/* External Interrupt 3 */
#define IRQ_EXT4	15	/* External Interrupt 4 */
#define IRQ_APB		16	/* APB Bridge */
#define IRQ_DMA0	17	/* DMA Channel 0 */
#define IRQ_I2C1	18	/* I2C 1 */
#define IRQ_I2C0	19	/* I2C 0 */
#define IRQ_SDMMC	20	/* SD/MMC Controller */
#define IRQ_SDMMC_DMA	21	/* SD/MMC Controller DMA */
#define IRQ_PMC_WU	22	/* Power Management Controller Wakeup */
#define IRQ_PS2KBD	23	/* PS/2 Keyboard */
#define IRQ_SPI0	24	/* SPI 0 */
#define IRQ_SPI1	25	/* SPI 1 */
#define IRQ_SPI2	26	/* SPI 2 */
#define IRQ_DMA1	27	/* DMA Channel 1 */
#define IRQ_NAND	28	/* NAND Flash Controller */
#define IRQ_NAND_DMA	29	/* NAND Flash Controller DMA */
#define IRQ_UART5	30	/* UART 5 */
#define IRQ_UART4	31	/* UART 4 */
#define IRQ_UART0	32	/* UART 0 */
#define IRQ_UART1	33	/* UART 1 */
#define IRQ_DMA2	34	/* DMA Channel 2 */
#define IRQ_I2S		35	/* I2S */
#define IRQ_PMCOS0	36	/* PMC OS Timer 0 */
#define IRQ_PMCOS1	37	/* PMC OS Timer 1 */
#define IRQ_PMCOS2	38	/* PMC OS Timer 2 */
#define IRQ_PMCOS3	39	/* PMC OS Timer 3 */
#define IRQ_DMA3	40	/* DMA Channel 3 */
#define IRQ_DMA4	41	/* DMA Channel 4 */
#define IRQ_AC97	42	/* AC97 Interface */
				/* Reserved */
#define IRQ_NOR		44	/* NOR Flash Controller */
#define IRQ_DMA5	45	/* DMA Channel 5 */
#define IRQ_DMA6	46	/* DMA Channel 6 */
#define IRQ_UART2	47	/* UART 2 */
#define IRQ_RTC		48	/* RTC Interrupt */
#define IRQ_RTCSM	49	/* RTC Second/Minute Update Interrupt */
#define IRQ_UART3	50	/* UART 3 */
#define IRQ_DMA7	51	/* DMA Channel 7 */
#define IRQ_EXT5	52	/* External Interrupt 5 */
#define IRQ_EXT6	53	/* External Interrupt 6 */
#define IRQ_EXT7	54	/* External Interrupt 7 */
#define IRQ_CIR		55	/* CIR */
#define IRQ_SIC0	56	/* SIC IRQ0 */
#define IRQ_SIC1	57	/* SIC IRQ1 */
#define IRQ_SIC2	58	/* SIC IRQ2 */
#define IRQ_SIC3	59	/* SIC IRQ3 */
#define IRQ_SIC4	60	/* SIC IRQ4 */
#define IRQ_SIC5	61	/* SIC IRQ5 */
#define IRQ_SIC6	62	/* SIC IRQ6 */
#define IRQ_SIC7	63	/* SIC IRQ7 */
				/* Reserved */
#define IRQ_JPEGDEC	65	/* JPEG Decoder */
#define IRQ_SAE		66	/* SAE (?) */
				/* Reserved */
#define IRQ_VPU		79	/* Video Processing Unit */
#define IRQ_VPP		80	/* Video Post-Processor */
#define IRQ_VID		81	/* Video Digital Input Interface */
#define IRQ_SPU		82	/* SPU (?) */
#define IRQ_PIP		83	/* PIP Error */
#define IRQ_GE		84	/* Graphic Engine */
#define IRQ_GOV		85	/* Graphic Overlay Engine */
#define IRQ_DVO		86	/* Digital Video Output */
				/* Reserved */
#define IRQ_DMA8	92	/* DMA Channel 8 */
#define IRQ_DMA9	93	/* DMA Channel 9 */
#define IRQ_DMA10	94	/* DMA Channel 10 */
#define IRQ_DMA11	95	/* DMA Channel 11 */
#define IRQ_DMA12	96	/* DMA Channel 12 */
#define IRQ_DMA13	97	/* DMA Channel 13 */
#define IRQ_DMA14	98	/* DMA Channel 14 */
#define IRQ_DMA15	99	/* DMA Channel 15 */
				/* Reserved */
#define IRQ_GOVW	111	/* GOVW (?) */
#define IRQ_GOVRSDSCD	112	/* GOVR SDSCD (?) */
#define IRQ_GOVRSDMIF	113	/* GOVR SDMIF (?) */
#define IRQ_GOVRHDSCD	114	/* GOVR HDSCD (?) */
#define IRQ_GOVRHDMIF	115	/* GOVR HDMIF (?) */

#define WM8505_NR_IRQS		116
