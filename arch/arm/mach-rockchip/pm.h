/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Tony Xie <tony.xie@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __MACH_ROCKCHIP_PM_H
#define __MACH_ROCKCHIP_PM_H

extern unsigned long rkpm_bootdata_cpusp;
extern unsigned long rkpm_bootdata_cpu_code;
extern unsigned long rkpm_bootdata_l2ctlr_f;
extern unsigned long rkpm_bootdata_l2ctlr;
extern unsigned long rkpm_bootdata_ddr_code;
extern unsigned long rkpm_bootdata_ddr_data;
extern unsigned long rk3288_bootram_sz;

void rockchip_slp_cpu_resume(void);
void __init rockchip_suspend_init(void);

/****** following is rk3288 defined **********/
#define RK3288_PMU_WAKEUP_CFG0		0x00
#define RK3288_PMU_WAKEUP_CFG1		0x04
#define RK3288_PMU_PWRMODE_CON		0x18
#define RK3288_PMU_OSC_CNT		0x20
#define RK3288_PMU_PLL_CNT		0x24
#define RK3288_PMU_STABL_CNT		0x28
#define RK3288_PMU_DDR0IO_PWRON_CNT	0x2c
#define RK3288_PMU_DDR1IO_PWRON_CNT	0x30
#define RK3288_PMU_CORE_PWRDWN_CNT	0x34
#define RK3288_PMU_CORE_PWRUP_CNT	0x38
#define RK3288_PMU_GPU_PWRDWN_CNT	0x3c
#define RK3288_PMU_GPU_PWRUP_CNT	0x40
#define RK3288_PMU_WAKEUP_RST_CLR_CNT	0x44
#define RK3288_PMU_PWRMODE_CON1		0x90

#define RK3288_SGRF_SOC_CON0		(0x0000)
#define RK3288_SGRF_FAST_BOOT_ADDR	(0x0120)
#define SGRF_FAST_BOOT_EN		BIT(8)
#define SGRF_FAST_BOOT_EN_WRITE		BIT(24)

#define RK3288_CRU_MODE_CON		0x50
#define RK3288_CRU_SEL0_CON		0x60
#define RK3288_CRU_SEL1_CON		0x64
#define RK3288_CRU_SEL10_CON		0x88
#define RK3288_CRU_SEL33_CON		0xe4
#define RK3288_CRU_SEL37_CON		0xf4

/* PMU_WAKEUP_CFG1 bits */
#define PMU_ARMINT_WAKEUP_EN		BIT(0)

enum rk3288_pwr_mode_con {
	PMU_PWR_MODE_EN = 0,
	PMU_CLK_CORE_SRC_GATE_EN,
	PMU_GLOBAL_INT_DISABLE,
	PMU_L2FLUSH_EN,
	PMU_BUS_PD_EN,
	PMU_A12_0_PD_EN,
	PMU_SCU_EN,
	PMU_PLL_PD_EN,
	PMU_CHIP_PD_EN, /* POWER OFF PIN ENABLE */
	PMU_PWROFF_COMB,
	PMU_ALIVE_USE_LF,
	PMU_PMU_USE_LF,
	PMU_OSC_24M_DIS,
	PMU_INPUT_CLAMP_EN,
	PMU_WAKEUP_RESET_EN,
	PMU_SREF0_ENTER_EN,
	PMU_SREF1_ENTER_EN,
	PMU_DDR0IO_RET_EN,
	PMU_DDR1IO_RET_EN,
	PMU_DDR0_GATING_EN,
	PMU_DDR1_GATING_EN,
	PMU_DDR0IO_RET_DE_REQ,
	PMU_DDR1IO_RET_DE_REQ
};

enum rk3288_pwr_mode_con1 {
	PMU_CLR_BUS = 0,
	PMU_CLR_CORE,
	PMU_CLR_CPUP,
	PMU_CLR_ALIVE,
	PMU_CLR_DMA,
	PMU_CLR_PERI,
	PMU_CLR_GPU,
	PMU_CLR_VIDEO,
	PMU_CLR_HEVC,
	PMU_CLR_VIO,
};

#endif /* __MACH_ROCKCHIP_PM_H */
