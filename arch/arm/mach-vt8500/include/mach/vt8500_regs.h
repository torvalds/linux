/*
 *  arch/arm/mach-vt8500/include/mach/vt8500_regs.h
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
#ifndef __ASM_ARM_ARCH_VT8500_REGS_H
#define __ASM_ARM_ARCH_VT8500_REGS_H

/* VT8500 Registers Map */

#define VT8500_REGS_START_PHYS	0xd8000000	/* Start of MMIO registers */
#define VT8500_REGS_START_VIRT	0xf8000000	/* Virtual mapping start */

#define VT8500_DDR_BASE		0xd8000000	/* 1k	DDR/DDR2 Memory
							Controller */
#define VT8500_DMA_BASE		0xd8001000	/* 1k	DMA Controller */
#define VT8500_SFLASH_BASE	0xd8002000	/* 1k	Serial Flash Memory
							Controller */
#define VT8500_ETHER_BASE	0xd8004000	/* 1k	Ethernet MAC 0 */
#define VT8500_CIPHER_BASE	0xd8006000	/* 4k	Cipher */
#define VT8500_USB_BASE		0xd8007800	/* 2k	USB OTG */
# define VT8500_EHCI_BASE	0xd8007900	/*	EHCI */
# define VT8500_UHCI_BASE	0xd8007b01	/*	UHCI */
#define VT8500_PATA_BASE	0xd8008000	/* 512	PATA */
#define VT8500_PS2_BASE		0xd8008800	/* 1k	PS/2 */
#define VT8500_NAND_BASE	0xd8009000	/* 1k	NAND Controller */
#define VT8500_NOR_BASE		0xd8009400	/* 1k	NOR Controller */
#define VT8500_SDMMC_BASE	0xd800a000	/* 1k	SD/MMC Controller */
#define VT8500_MS_BASE		0xd800b000	/* 1k	MS/MSPRO Controller */
#define VT8500_LCDC_BASE	0xd800e400	/* 1k	LCD Controller */
#define VT8500_VPU_BASE		0xd8050000	/* 256	VPU */
#define VT8500_GOV_BASE		0xd8050300	/* 256	GOV */
#define VT8500_GEGEA_BASE	0xd8050400	/* 768	GE/GE Alpha Mixing */
#define VT8500_LCDF_BASE	0xd8050900	/* 256	LCD Formatter */
#define VT8500_VID_BASE		0xd8050a00	/* 256	VID */
#define VT8500_VPP_BASE		0xd8050b00	/* 256	VPP */
#define VT8500_TSBK_BASE	0xd80f4000	/* 4k	TSBK */
#define VT8500_JPEGDEC_BASE	0xd80fe000	/* 4k	JPEG Decoder */
#define VT8500_JPEGENC_BASE	0xd80ff000	/* 4k	JPEG Encoder */
#define VT8500_RTC_BASE		0xd8100000	/* 64k	RTC */
#define VT8500_GPIO_BASE	0xd8110000	/* 64k	GPIO Configuration */
#define VT8500_SCC_BASE		0xd8120000	/* 64k	System Configuration*/
#define VT8500_PMC_BASE		0xd8130000	/* 64k	PMC Configuration */
#define VT8500_IC_BASE		0xd8140000	/* 64k	Interrupt Controller*/
#define VT8500_UART0_BASE	0xd8200000	/* 64k	UART 0 */
#define VT8500_UART2_BASE	0xd8210000	/* 64k	UART 2 */
#define VT8500_PWM_BASE		0xd8220000	/* 64k	PWM Configuration */
#define VT8500_SPI0_BASE	0xd8240000	/* 64k	SPI 0 */
#define VT8500_SPI1_BASE	0xd8250000	/* 64k	SPI 1 */
#define VT8500_CIR_BASE		0xd8270000	/* 64k	CIR */
#define VT8500_I2C0_BASE	0xd8280000	/* 64k	I2C 0 */
#define VT8500_AC97_BASE	0xd8290000	/* 64k	AC97 */
#define VT8500_SPI2_BASE	0xd82a0000	/* 64k	SPI 2 */
#define VT8500_UART1_BASE	0xd82b0000	/* 64k	UART 1 */
#define VT8500_UART3_BASE	0xd82c0000	/* 64k	UART 3 */
#define VT8500_PCM_BASE		0xd82d0000	/* 64k	PCM */
#define VT8500_I2C1_BASE	0xd8320000	/* 64k	I2C 1 */
#define VT8500_I2S_BASE		0xd8330000	/* 64k	I2S */
#define VT8500_ADC_BASE		0xd8340000	/* 64k	ADC */

#define VT8500_REGS_END_PHYS	0xd834ffff	/* End of MMIO registers */
#define VT8500_REGS_LENGTH	(VT8500_REGS_END_PHYS \
				- VT8500_REGS_START_PHYS + 1)

#endif
