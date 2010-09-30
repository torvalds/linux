/* arch/arm/mach-rk2818/include/mach/scu.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ASM_ARCH_RK2818_SCU_H

enum scu_clk_gate
{
	/* SCU CLK GATE 0 CON */
	CLK_GATE_ARM = 0,
	CLK_GATE_DSP,
	CLK_GATE_DMA,
	CLK_GATE_SRAMARM,
	CLK_GATE_SRAMDSP,
	CLK_GATE_HIF,
	CLK_GATE_OTGBUS,
	CLK_GATE_OTGPHY,
	CLK_GATE_NANDC,
	CLK_GATE_INTC,
	CLK_GATE_DEBLK,		/* 10 */
	CLK_GATE_LCDC,
	CLK_GATE_VIP,		/* as sensor */
	CLK_GATE_I2S,
	CLK_GATE_SDMMC0,
	CLK_GATE_EBROM,
	CLK_GATE_GPIO0,
	CLK_GATE_GPIO1,
	CLK_GATE_UART0,
	CLK_GATE_UART1,
	CLK_GATE_I2C0,		/* 20 */
	CLK_GATE_I2C1,
	CLK_GATE_SPI0,
	CLK_GATE_SPI1,
	CLK_GATE_PWM,
	CLK_GATE_TIMER,
	CLK_GATE_WDT,
	CLK_GATE_RTC,
	CLK_GATE_LSADC,
	CLK_GATE_UART2,
	CLK_GATE_UART3,		/* 30 */
	CLK_GATE_SDMMC1,

	/* SCU CLK GATE 1 CON */
	CLK_GATE_HSADC = 32,
	CLK_GATE_SDRAM_COMMON = 47,
	CLK_GATE_SDRAM_CONTROLLER,
	CLK_GATE_MOBILE_SDRAM_CONTROLLER,
	CLK_GATE_LCDC_SHARE_MEMORY,	/* 50 */
	CLK_GATE_LCDC_HCLK,
	CLK_GATE_DEBLK_H264,
	CLK_GATE_GPU,
	CLK_GATE_DDR_HCLK,
	CLK_GATE_DDR,
	CLK_GATE_CUSTOMIZED_SDRAM_CONTROLLER,
	CLK_GATE_MCDMA,
	CLK_GATE_SDRAM,
	CLK_GATE_DDR_AXI,
	CLK_GATE_DSP_TIMER,	/* 60 */
	CLK_GATE_DSP_SLAVE,
	CLK_GATE_DSP_MASTER,
	CLK_GATE_USB_HOST,

	/* SCU CLK GATE 2 CON */
	CLK_GATE_ARMIBUS = 64,
	CLK_GATE_ARMDBUS,
	CLK_GATE_DSPBUS,
	CLK_GATE_EXPBUS,
	CLK_GATE_APBBUS,
	CLK_GATE_EFUSE,
	CLK_GATE_DTCM1,		/* 70 */
	CLK_GATE_DTCM0,
	CLK_GATE_ITCM,
	CLK_GATE_VIDEOBUS,

	CLK_GATE_MAX,
};

/* Register definitions */
#define SCU_APLL_CON		0x00
#define SCU_DPLL_CON		0x04
#define SCU_CPLL_CON		0x08
#define SCU_MODE_CON		0x0c
#define SCU_PMU_CON		0x10
#define SCU_CLKSEL0_CON		0x14
#define SCU_CLKSEL1_CON		0x18
#define SCU_CLKGATE0_CON	0x1c
#define SCU_CLKGATE1_CON	0x20
#define SCU_CLKGATE2_CON	0x24
#define SCU_SOFTRST_CON		0x28
#define SCU_CHIPCFG_CON		0x2c
#define SCU_CPUPD		0x30
#define SCU_CLKSEL2_CON		0x34

#include <asm/tcm.h>

#define DDR_SAVE_SP		do { save_sp = ddr_save_sp((DTCM_END&(~7))); } while (0)
#define DDR_RESTORE_SP		do { ddr_save_sp(save_sp); } while (0)
unsigned long ddr_save_sp( unsigned long new_sp );
extern unsigned long save_sp;
extern int __tcmdata ddr_disabled;

/*
 * delay at itcm. one loops == 6 arm instruction 
 * at 600M,about 6,000,0 for 1ms delay.
 */
extern void __tcmfunc ddr_pll_delay( int loops ) ;

/**
 * tcm_udelay - delay usecs microseconds in tcm
 * @usecs: in microseconds
 * @arm_freq_mhz: arm frequency in MHz
 *
 * for example when arm run at slow mode, call tcm_udelay(usecs, 24)
 */
extern void __tcmfunc tcm_udelay(unsigned long usecs, unsigned long arm_freq_mhz);
void scu_set_clk_for_reboot( void );
void ddr_change_mode( int performance );
void change_ddr_freq(int freq_MHZ);
extern void kernel_restart(char *cmd);
#endif
