/*
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * Copyright (c) 2016 BayLibre, Inc.
 * Michael Turquette <mturquette@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MESON8B_H
#define __MESON8B_H

/*
 * Clock controller register offsets
 *
 * Register offsets from the HardKernel[0] data sheet are listed in comment
 * blocks below. Those offsets must be multiplied by 4 before adding them to
 * the base address to get the right value
 *
 * [0] http://dn.odroid.com/S805/Datasheet/S805_Datasheet%20V0.8%2020150126.pdf
 */
#define HHI_GCLK_MPEG0			0x140 /* 0x50 offset in data sheet */
#define HHI_GCLK_MPEG1			0x144 /* 0x51 offset in data sheet */
#define HHI_GCLK_MPEG2			0x148 /* 0x52 offset in data sheet */
#define HHI_GCLK_OTHER			0x150 /* 0x54 offset in data sheet */
#define HHI_GCLK_AO			0x154 /* 0x55 offset in data sheet */
#define HHI_SYS_CPU_CLK_CNTL1		0x15c /* 0x57 offset in data sheet */
#define HHI_MPEG_CLK_CNTL		0x174 /* 0x5d offset in data sheet */
#define HHI_MPLL_CNTL			0x280 /* 0xa0 offset in data sheet */
#define HHI_SYS_PLL_CNTL		0x300 /* 0xc0 offset in data sheet */
#define HHI_VID_PLL_CNTL		0x320 /* 0xc8 offset in data sheet */

/*
 * MPLL register offeset taken from the S905 datasheet. Vendor kernel source
 * confirm these are the same for the S805.
 */
#define HHI_MPLL_CNTL			0x280 /* 0xa0 offset in data sheet */
#define HHI_MPLL_CNTL2			0x284 /* 0xa1 offset in data sheet */
#define HHI_MPLL_CNTL3			0x288 /* 0xa2 offset in data sheet */
#define HHI_MPLL_CNTL4			0x28C /* 0xa3 offset in data sheet */
#define HHI_MPLL_CNTL5			0x290 /* 0xa4 offset in data sheet */
#define HHI_MPLL_CNTL6			0x294 /* 0xa5 offset in data sheet */
#define HHI_MPLL_CNTL7			0x298 /* 0xa6 offset in data sheet */
#define HHI_MPLL_CNTL8			0x29C /* 0xa7 offset in data sheet */
#define HHI_MPLL_CNTL9			0x2A0 /* 0xa8 offset in data sheet */
#define HHI_MPLL_CNTL10			0x2A4 /* 0xa9 offset in data sheet */

/*
 * CLKID index values
 *
 * These indices are entirely contrived and do not map onto the hardware.
 * Migrate them out of this header and into the DT header file when they need
 * to be exposed to client nodes in DT: include/dt-bindings/clock/meson8b-clkc.h
 */

/* CLKID_UNUSED */
/* CLKID_XTAL */
/* CLKID_PLL_FIXED */
/* CLKID_PLL_VID */
/* CLKID_PLL_SYS */
/* CLKID_FCLK_DIV2 */
/* CLKID_FCLK_DIV3 */
/* CLKID_FCLK_DIV4 */
/* CLKID_FCLK_DIV5 */
/* CLKID_FCLK_DIV7 */
/* CLKID_CLK81 */
/* CLKID_MALI */
/* CLKID_CPUCLK */
/* CLKID_ZERO */
/* CLKID_MPEG_SEL */
/* CLKID_MPEG_DIV */
#define CLKID_DDR		16
#define CLKID_DOS		17
#define CLKID_ISA		18
#define CLKID_PL301		19
#define CLKID_PERIPHS		20
#define CLKID_SPICC		21
#define CLKID_I2C		22
#define CLKID_SAR_ADC		23
#define CLKID_SMART_CARD	24
#define CLKID_RNG0		25
#define CLKID_UART0		26
#define CLKID_SDHC		27
#define CLKID_STREAM		28
#define CLKID_ASYNC_FIFO	29
#define CLKID_SDIO		30
#define CLKID_ABUF		31
#define CLKID_HIU_IFACE		32
#define CLKID_ASSIST_MISC	33
#define CLKID_SPI		34
#define CLKID_I2S_SPDIF		35
#define CLKID_ETH		36
#define CLKID_DEMUX		37
#define CLKID_AIU_GLUE		38
#define CLKID_IEC958		39
#define CLKID_I2S_OUT		40
#define CLKID_AMCLK		41
#define CLKID_AIFIFO2		42
#define CLKID_MIXER		43
#define CLKID_MIXER_IFACE	44
#define CLKID_ADC		45
#define CLKID_BLKMV		46
#define CLKID_AIU		47
#define CLKID_UART1		48
#define CLKID_G2D		49
#define CLKID_USB0		50
#define CLKID_USB1		51
#define CLKID_RESET		52
#define CLKID_NAND		53
#define CLKID_DOS_PARSER	54
#define CLKID_USB		55
#define CLKID_VDIN1		56
#define CLKID_AHB_ARB0		57
#define CLKID_EFUSE		58
#define CLKID_BOOT_ROM		59
#define CLKID_AHB_DATA_BUS	60
#define CLKID_AHB_CTRL_BUS	61
#define CLKID_HDMI_INTR_SYNC	62
#define CLKID_HDMI_PCLK		63
#define CLKID_USB1_DDR_BRIDGE	64
#define CLKID_USB0_DDR_BRIDGE	65
#define CLKID_MMC_PCLK		66
#define CLKID_DVIN		67
#define CLKID_UART2		68
#define CLKID_SANA		69
#define CLKID_VPU_INTR		70
#define CLKID_SEC_AHB_AHB3_BRIDGE	71
#define CLKID_CLK81_A9		72
#define CLKID_VCLK2_VENCI0	73
#define CLKID_VCLK2_VENCI1	74
#define CLKID_VCLK2_VENCP0	75
#define CLKID_VCLK2_VENCP1	76
#define CLKID_GCLK_VENCI_INT	77
#define CLKID_GCLK_VENCP_INT	78
#define CLKID_DAC_CLK		79
#define CLKID_AOCLK_GATE	80
#define CLKID_IEC958_GATE	81
#define CLKID_ENC480P		82
#define CLKID_RNG1		83
#define CLKID_GCLK_VENCL_INT	84
#define CLKID_VCLK2_VENCLMCC	85
#define CLKID_VCLK2_VENCL	86
#define CLKID_VCLK2_OTHER	87
#define CLKID_EDP		88
#define CLKID_AO_MEDIA_CPU	89
#define CLKID_AO_AHB_SRAM	90
#define CLKID_AO_AHB_BUS	91
#define CLKID_AO_IFACE		92
#define CLKID_MPLL0		93
#define CLKID_MPLL1		94
#define CLKID_MPLL2		95

#define CLK_NR_CLKS		96

/* include the CLKIDs that have been made part of the stable DT binding */
#include <dt-bindings/clock/meson8b-clkc.h>

#endif /* __MESON8B_H */
