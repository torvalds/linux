/*
 *  arch/arm/mach-vt8500/include/mach/wm8505_regs.h
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
#ifndef __ASM_ARM_ARCH_WM8505_REGS_H
#define __ASM_ARM_ARCH_WM8505_REGS_H

/* WM8505 Registers Map */

#define WM8505_REGS_START_PHYS	0xd8000000	/* Start of MMIO registers */
#define WM8505_REGS_START_VIRT	0xf8000000	/* Virtual mapping start */

#define WM8505_DDR_BASE		0xd8000400	/* 1k	DDR/DDR2 Memory
							Controller */
#define WM8505_DMA_BASE		0xd8001800	/* 1k	DMA Controller */
#define WM8505_VDMA_BASE	0xd8001c00	/* 1k	VDMA */
#define WM8505_SFLASH_BASE	0xd8002000	/* 1k	Serial Flash Memory
							Controller */
#define WM8505_ETHER_BASE	0xd8004000	/* 1k	Ethernet MAC 0 */
#define WM8505_CIPHER_BASE	0xd8006000	/* 4k	Cipher */
#define WM8505_USB_BASE		0xd8007000	/* 2k	USB 2.0 Host */
# define WM8505_EHCI_BASE	0xd8007100	/*	EHCI */
# define WM8505_UHCI_BASE	0xd8007301	/*	UHCI */
#define WM8505_PS2_BASE		0xd8008800	/* 1k	PS/2 */
#define WM8505_NAND_BASE	0xd8009000	/* 1k	NAND Controller */
#define WM8505_NOR_BASE		0xd8009400	/* 1k	NOR Controller */
#define WM8505_SDMMC_BASE	0xd800a000	/* 1k	SD/MMC Controller */
#define WM8505_VPU_BASE		0xd8050000	/* 256	VPU */
#define WM8505_GOV_BASE		0xd8050300	/* 256	GOV */
#define WM8505_GEGEA_BASE	0xd8050400	/* 768	GE/GE Alpha Mixing */
#define WM8505_GOVR_BASE	0xd8050800	/* 512	GOVR (frambuffer) */
#define WM8505_VID_BASE		0xd8050a00	/* 256	VID */
#define WM8505_SCL_BASE		0xd8050d00	/* 256	SCL */
#define WM8505_VPP_BASE		0xd8050f00	/* 256	VPP */
#define WM8505_JPEGDEC_BASE	0xd80fe000	/* 4k	JPEG Decoder */
#define WM8505_RTC_BASE		0xd8100000	/* 64k	RTC */
#define WM8505_GPIO_BASE	0xd8110000	/* 64k	GPIO Configuration */
#define WM8505_SCC_BASE		0xd8120000	/* 64k	System Configuration*/
#define WM8505_PMC_BASE		0xd8130000	/* 64k	PMC Configuration */
#define WM8505_IC_BASE		0xd8140000	/* 64k	Interrupt Controller*/
#define WM8505_SIC_BASE		0xd8150000	/* 64k	Secondary IC */
#define WM8505_UART0_BASE	0xd8200000	/* 64k	UART 0 */
#define WM8505_UART2_BASE	0xd8210000	/* 64k	UART 2 */
#define WM8505_PWM_BASE		0xd8220000	/* 64k	PWM Configuration */
#define WM8505_SPI0_BASE	0xd8240000	/* 64k	SPI 0 */
#define WM8505_SPI1_BASE	0xd8250000	/* 64k	SPI 1 */
#define WM8505_KEYPAD_BASE	0xd8260000	/* 64k	Keypad control */
#define WM8505_CIR_BASE		0xd8270000	/* 64k	CIR */
#define WM8505_I2C0_BASE	0xd8280000	/* 64k	I2C 0 */
#define WM8505_AC97_BASE	0xd8290000	/* 64k	AC97 */
#define WM8505_SPI2_BASE	0xd82a0000	/* 64k	SPI 2 */
#define WM8505_UART1_BASE	0xd82b0000	/* 64k	UART 1 */
#define WM8505_UART3_BASE	0xd82c0000	/* 64k	UART 3 */
#define WM8505_I2C1_BASE	0xd8320000	/* 64k	I2C 1 */
#define WM8505_I2S_BASE		0xd8330000	/* 64k	I2S */
#define WM8505_UART4_BASE	0xd8370000	/* 64k	UART 4 */
#define WM8505_UART5_BASE	0xd8380000	/* 64k	UART 5 */

#define WM8505_REGS_END_PHYS	0xd838ffff	/* End of MMIO registers */
#define WM8505_REGS_LENGTH	(WM8505_REGS_END_PHYS \
				- WM8505_REGS_START_PHYS + 1)

#endif
