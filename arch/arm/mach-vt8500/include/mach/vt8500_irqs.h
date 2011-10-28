/*
 *  arch/arm/mach-vt8500/include/mach/vt8500_irqs.h
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

/* VT8500 Interrupt Sources */

#define IRQ_JPEGENC	0	/* JPEG Encoder */
#define IRQ_JPEGDEC	1	/* JPEG Decoder */
				/* Reserved */
#define IRQ_PATA	3	/* PATA Controller */
				/* Reserved */
#define IRQ_DMA		5	/* DMA Controller */
#define IRQ_EXT0	6	/* External Interrupt 0 */
#define IRQ_EXT1	7	/* External Interrupt 1 */
#define IRQ_GE		8	/* Graphic Engine */
#define IRQ_GOV		9	/* Graphic Overlay Engine */
#define IRQ_ETHER	10	/* Ethernet MAC */
#define IRQ_MPEGTS	11	/* Transport Stream Interface */
#define IRQ_LCDC	12	/* LCD Controller */
#define IRQ_EXT2	13	/* External Interrupt 2 */
#define IRQ_EXT3	14	/* External Interrupt 3 */
#define IRQ_EXT4	15	/* External Interrupt 4 */
#define IRQ_CIPHER	16	/* Cipher */
#define IRQ_VPP		17	/* Video Post-Processor */
#define IRQ_I2C1	18	/* I2C 1 */
#define IRQ_I2C0	19	/* I2C 0 */
#define IRQ_SDMMC	20	/* SD/MMC Controller */
#define IRQ_SDMMC_DMA	21	/* SD/MMC Controller DMA */
#define IRQ_PMC_WU	22	/* Power Management Controller Wakeup */
				/* Reserved */
#define IRQ_SPI0	24	/* SPI 0 */
#define IRQ_SPI1	25	/* SPI 1 */
#define IRQ_SPI2	26	/* SPI 2 */
#define IRQ_LCDDF	27	/* LCD Data Formatter */
#define IRQ_NAND	28	/* NAND Flash Controller */
#define IRQ_NAND_DMA	29	/* NAND Flash Controller DMA */
#define IRQ_MS		30	/* MemoryStick Controller */
#define IRQ_MS_DMA	31	/* MemoryStick Controller DMA */
#define IRQ_UART0	32	/* UART 0 */
#define IRQ_UART1	33	/* UART 1 */
#define IRQ_I2S		34	/* I2S */
#define IRQ_PCM		35	/* PCM */
#define IRQ_PMCOS0	36	/* PMC OS Timer 0 */
#define IRQ_PMCOS1	37	/* PMC OS Timer 1 */
#define IRQ_PMCOS2	38	/* PMC OS Timer 2 */
#define IRQ_PMCOS3	39	/* PMC OS Timer 3 */
#define IRQ_VPU		40	/* Video Processing Unit */
#define IRQ_VID		41	/* Video Digital Input Interface */
#define IRQ_AC97	42	/* AC97 Interface */
#define IRQ_EHCI	43	/* USB */
#define IRQ_NOR		44	/* NOR Flash Controller */
#define IRQ_PS2MOUSE	45	/* PS/2 Mouse */
#define IRQ_PS2KBD	46	/* PS/2 Keyboard */
#define IRQ_UART2	47	/* UART 2 */
#define IRQ_RTC		48	/* RTC Interrupt */
#define IRQ_RTCSM	49	/* RTC Second/Minute Update Interrupt */
#define IRQ_UART3	50	/* UART 3 */
#define IRQ_ADC		51	/* ADC */
#define IRQ_EXT5	52	/* External Interrupt 5 */
#define IRQ_EXT6	53	/* External Interrupt 6 */
#define IRQ_EXT7	54	/* External Interrupt 7 */
#define IRQ_CIR		55	/* CIR */
#define IRQ_DMA0	56	/* DMA Channel 0 */
#define IRQ_DMA1	57	/* DMA Channel 1 */
#define IRQ_DMA2	58	/* DMA Channel 2 */
#define IRQ_DMA3	59	/* DMA Channel 3 */
#define IRQ_DMA4	60	/* DMA Channel 4 */
#define IRQ_DMA5	61	/* DMA Channel 5 */
#define IRQ_DMA6	62	/* DMA Channel 6 */
#define IRQ_DMA7	63	/* DMA Channel 7 */

#define VT8500_NR_IRQS		64
