/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright (c) 2016 AmLogic, Inc.
 * Author: Michael Turquette <mturquette@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING
 *
 * BSD LICENSE
 *
 * Copyright (c) 2016 BayLibre, Inc.
 * Author: Michael Turquette <mturquette@baylibre.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __GXBB_H
#define __GXBB_H

/*
 * Clock controller register offsets
 *
 * Register offsets from the data sheet are listed in comment blocks below.
 * Those offsets must be multiplied by 4 before adding them to the base address
 * to get the right value
 */
#define SCR				0x2C /* 0x0b offset in data sheet */
#define TIMEOUT_VALUE			0x3c /* 0x0f offset in data sheet */

#define HHI_GP0_PLL_CNTL		0x40 /* 0x10 offset in data sheet */
#define HHI_GP0_PLL_CNTL2		0x44 /* 0x11 offset in data sheet */
#define HHI_GP0_PLL_CNTL3		0x48 /* 0x12 offset in data sheet */
#define HHI_GP0_PLL_CNTL4		0x4c /* 0x13 offset in data sheet */

#define HHI_XTAL_DIVN_CNTL		0xbc /* 0x2f offset in data sheet */
#define HHI_TIMER90K			0xec /* 0x3b offset in data sheet */

#define HHI_MEM_PD_REG0			0x100 /* 0x40 offset in data sheet */
#define HHI_MEM_PD_REG1			0x104 /* 0x41 offset in data sheet */
#define HHI_VPU_MEM_PD_REG1		0x108 /* 0x42 offset in data sheet */
#define HHI_VIID_CLK_DIV		0x128 /* 0x4a offset in data sheet */
#define HHI_VIID_CLK_CNTL		0x12c /* 0x4b offset in data sheet */

#define HHI_GCLK_MPEG0			0x140 /* 0x50 offset in data sheet */
#define HHI_GCLK_MPEG1			0x144 /* 0x51 offset in data sheet */
#define HHI_GCLK_MPEG2			0x148 /* 0x52 offset in data sheet */
#define HHI_GCLK_OTHER			0x150 /* 0x54 offset in data sheet */
#define HHI_GCLK_AO			0x154 /* 0x55 offset in data sheet */
#define HHI_SYS_OSCIN_CNTL		0x158 /* 0x56 offset in data sheet */
#define HHI_SYS_CPU_CLK_CNTL1		0x15c /* 0x57 offset in data sheet */
#define HHI_SYS_CPU_RESET_CNTL		0x160 /* 0x58 offset in data sheet */
#define HHI_VID_CLK_DIV			0x164 /* 0x59 offset in data sheet */

#define HHI_MPEG_CLK_CNTL		0x174 /* 0x5d offset in data sheet */
#define HHI_AUD_CLK_CNTL		0x178 /* 0x5e offset in data sheet */
#define HHI_VID_CLK_CNTL		0x17c /* 0x5f offset in data sheet */
#define HHI_AUD_CLK_CNTL2		0x190 /* 0x64 offset in data sheet */
#define HHI_VID_CLK_CNTL2		0x194 /* 0x65 offset in data sheet */
#define HHI_SYS_CPU_CLK_CNTL0		0x19c /* 0x67 offset in data sheet */
#define HHI_VID_PLL_CLK_DIV		0x1a0 /* 0x68 offset in data sheet */
#define HHI_AUD_CLK_CNTL3		0x1a4 /* 0x69 offset in data sheet */
#define HHI_MALI_CLK_CNTL		0x1b0 /* 0x6c offset in data sheet */
#define HHI_VPU_CLK_CNTL		0x1bC /* 0x6f offset in data sheet */

#define HHI_HDMI_CLK_CNTL		0x1CC /* 0x73 offset in data sheet */
#define HHI_VDEC_CLK_CNTL		0x1E0 /* 0x78 offset in data sheet */
#define HHI_VDEC2_CLK_CNTL		0x1E4 /* 0x79 offset in data sheet */
#define HHI_VDEC3_CLK_CNTL		0x1E8 /* 0x7a offset in data sheet */
#define HHI_VDEC4_CLK_CNTL		0x1EC /* 0x7b offset in data sheet */
#define HHI_HDCP22_CLK_CNTL		0x1F0 /* 0x7c offset in data sheet */
#define HHI_VAPBCLK_CNTL		0x1F4 /* 0x7d offset in data sheet */

#define HHI_VPU_CLKB_CNTL		0x20C /* 0x83 offset in data sheet */
#define HHI_USB_CLK_CNTL		0x220 /* 0x88 offset in data sheet */
#define HHI_32K_CLK_CNTL		0x224 /* 0x89 offset in data sheet */
#define HHI_GEN_CLK_CNTL		0x228 /* 0x8a offset in data sheet */
#define HHI_GEN_CLK_CNTL		0x228 /* 0x8a offset in data sheet */

#define HHI_PCM_CLK_CNTL		0x258 /* 0x96 offset in data sheet */
#define HHI_NAND_CLK_CNTL		0x25C /* 0x97 offset in data sheet */
#define HHI_SD_EMMC_CLK_CNTL		0x264 /* 0x99 offset in data sheet */

#define HHI_MPLL_CNTL			0x280 /* 0xa0 offset in data sheet */
#define HHI_MPLL_CNTL2			0x284 /* 0xa1 offset in data sheet */
#define HHI_MPLL_CNTL3			0x288 /* 0xa2 offset in data sheet */
#define HHI_MPLL_CNTL4			0x28C /* 0xa3 offset in data sheet */
#define HHI_MPLL_CNTL5			0x290 /* 0xa4 offset in data sheet */
#define HHI_MPLL_CNTL6			0x294 /* 0xa5 offset in data sheet */
#define HHI_MPLL_CNTL7			0x298 /* MP0, 0xa6 offset in data sheet */
#define HHI_MPLL_CNTL8			0x29C /* MP1, 0xa7 offset in data sheet */
#define HHI_MPLL_CNTL9			0x2A0 /* MP2, 0xa8 offset in data sheet */
#define HHI_MPLL_CNTL10			0x2A4 /* MP2, 0xa9 offset in data sheet */

#define HHI_MPLL3_CNTL0			0x2E0 /* 0xb8 offset in data sheet */
#define HHI_MPLL3_CNTL1			0x2E4 /* 0xb9 offset in data sheet */
#define HHI_VDAC_CNTL0			0x2F4 /* 0xbd offset in data sheet */
#define HHI_VDAC_CNTL1			0x2F8 /* 0xbe offset in data sheet */

#define HHI_SYS_PLL_CNTL		0x300 /* 0xc0 offset in data sheet */
#define HHI_SYS_PLL_CNTL2		0x304 /* 0xc1 offset in data sheet */
#define HHI_SYS_PLL_CNTL3		0x308 /* 0xc2 offset in data sheet */
#define HHI_SYS_PLL_CNTL4		0x30c /* 0xc3 offset in data sheet */
#define HHI_SYS_PLL_CNTL5		0x310 /* 0xc4 offset in data sheet */
#define HHI_DPLL_TOP_I			0x318 /* 0xc6 offset in data sheet */
#define HHI_DPLL_TOP2_I			0x31C /* 0xc7 offset in data sheet */
#define HHI_HDMI_PLL_CNTL		0x320 /* 0xc8 offset in data sheet */
#define HHI_HDMI_PLL_CNTL2		0x324 /* 0xc9 offset in data sheet */
#define HHI_HDMI_PLL_CNTL3		0x328 /* 0xca offset in data sheet */
#define HHI_HDMI_PLL_CNTL4		0x32C /* 0xcb offset in data sheet */
#define HHI_HDMI_PLL_CNTL5		0x330 /* 0xcc offset in data sheet */
#define HHI_HDMI_PLL_CNTL6		0x334 /* 0xcd offset in data sheet */
#define HHI_HDMI_PLL_CNTL_I		0x338 /* 0xce offset in data sheet */
#define HHI_HDMI_PLL_CNTL7		0x33C /* 0xcf offset in data sheet */

#define HHI_HDMI_PHY_CNTL0		0x3A0 /* 0xe8 offset in data sheet */
#define HHI_HDMI_PHY_CNTL1		0x3A4 /* 0xe9 offset in data sheet */
#define HHI_HDMI_PHY_CNTL2		0x3A8 /* 0xea offset in data sheet */
#define HHI_HDMI_PHY_CNTL3		0x3AC /* 0xeb offset in data sheet */

#define HHI_VID_LOCK_CLK_CNTL		0x3C8 /* 0xf2 offset in data sheet */
#define HHI_BT656_CLK_CNTL		0x3D4 /* 0xf5 offset in data sheet */
#define HHI_SAR_CLK_CNTL		0x3D8 /* 0xf6 offset in data sheet */

/*
 * CLKID index values
 *
 * These indices are entirely contrived and do not map onto the hardware.
 * Migrate them out of this header and into the DT header file when they need
 * to be exposed to client nodes in DT: include/dt-bindings/clock/gxbb-clkc.h
 */
#define CLKID_SYS_PLL		  0
/* CLKID_CPUCLK */
#define CLKID_HDMI_PLL		  2
#define CLKID_FIXED_PLL		  3
/* CLKID_FCLK_DIV2 */
#define CLKID_FCLK_DIV3		  5
#define CLKID_FCLK_DIV4		  6
#define CLKID_FCLK_DIV5		  7
#define CLKID_FCLK_DIV7		  8
#define CLKID_GP0_PLL		  9
#define CLKID_MPEG_SEL		  10
#define CLKID_MPEG_DIV		  11
/* CLKID_CLK81 */
#define CLKID_MPLL0		  13
#define CLKID_MPLL1		  14
#define CLKID_MPLL2		  15
#define CLKID_DDR		  16
#define CLKID_DOS		  17
#define CLKID_ISA		  18
#define CLKID_PL301		  19
#define CLKID_PERIPHS		  20
#define CLKID_SPICC		  21
#define CLKID_I2C		  22
#define CLKID_SAR_ADC		  23
#define CLKID_SMART_CARD	  24
#define CLKID_RNG0		  25
#define CLKID_UART0		  26
#define CLKID_SDHC		  27
#define CLKID_STREAM		  28
#define CLKID_ASYNC_FIFO	  29
#define CLKID_SDIO		  30
#define CLKID_ABUF		  31
#define CLKID_HIU_IFACE		  32
#define CLKID_ASSIST_MISC	  33
#define CLKID_SPI		  34
#define CLKID_I2S_SPDIF		  35
#define CLKID_ETH		  36
#define CLKID_DEMUX		  37
#define CLKID_AIU_GLUE		  38
#define CLKID_IEC958		  39
#define CLKID_I2S_OUT		  40
#define CLKID_AMCLK		  41
#define CLKID_AIFIFO2		  42
#define CLKID_MIXER		  43
#define CLKID_MIXER_IFACE	  44
#define CLKID_ADC		  45
#define CLKID_BLKMV		  46
#define CLKID_AIU		  47
#define CLKID_UART1		  48
#define CLKID_G2D		  49
#define CLKID_USB0		  50
#define CLKID_USB1		  51
#define CLKID_RESET		  52
#define CLKID_NAND		  53
#define CLKID_DOS_PARSER	  54
#define CLKID_USB		  55
#define CLKID_VDIN1		  56
#define CLKID_AHB_ARB0		  57
#define CLKID_EFUSE		  58
#define CLKID_BOOT_ROM		  59
#define CLKID_AHB_DATA_BUS	  60
#define CLKID_AHB_CTRL_BUS	  61
#define CLKID_HDMI_INTR_SYNC	  62
#define CLKID_HDMI_PCLK		  63
#define CLKID_USB1_DDR_BRIDGE	  64
#define CLKID_USB0_DDR_BRIDGE	  65
#define CLKID_MMC_PCLK		  66
#define CLKID_DVIN		  67
#define CLKID_UART2		  68
#define CLKID_SANA		  69
#define CLKID_VPU_INTR		  70
#define CLKID_SEC_AHB_AHB3_BRIDGE 71
#define CLKID_CLK81_A53		  72
#define CLKID_VCLK2_VENCI0	  73
#define CLKID_VCLK2_VENCI1	  74
#define CLKID_VCLK2_VENCP0	  75
#define CLKID_VCLK2_VENCP1	  76
#define CLKID_GCLK_VENCI_INT0	  77
#define CLKID_GCLK_VENCI_INT	  78
#define CLKID_DAC_CLK		  79
#define CLKID_AOCLK_GATE	  80
#define CLKID_IEC958_GATE	  81
#define CLKID_ENC480P		  82
#define CLKID_RNG1		  83
#define CLKID_GCLK_VENCI_INT1	  84
#define CLKID_VCLK2_VENCLMCC	  85
#define CLKID_VCLK2_VENCL	  86
#define CLKID_VCLK_OTHER	  87
#define CLKID_EDP		  88
#define CLKID_AO_MEDIA_CPU	  89
#define CLKID_AO_AHB_SRAM	  90
#define CLKID_AO_AHB_BUS	  91
#define CLKID_AO_IFACE		  92
#define CLKID_AO_I2C		  93
/* CLKID_SD_EMMC_A */
/* CLKID_SD_EMMC_B */
/* CLKID_SD_EMMC_C */

#define NR_CLKS			  97

/* include the CLKIDs that have been made part of the stable DT binding */
#include <dt-bindings/clock/gxbb-clkc.h>

#endif /* __GXBB_H */
