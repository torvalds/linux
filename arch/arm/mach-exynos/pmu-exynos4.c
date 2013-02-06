/* linux/arch/arm/mach-exynos/pmu-exynos4.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS4210 - CPU PMU(Power Management Unit) support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/kernel.h>

#include <mach/regs-clock.h>
#include <mach/pmu.h>
#include <mach/regs-pmu.h>

#include <plat/cpu.h>

static struct exynos4_pmu_conf *exynos4_pmu_config;

static unsigned int entry_cnt;

static struct exynos4_pmu_conf exynos4210_pmu_config[] = {
	/* { .reg = address, .val = { AFTR, LPA, SLEEP } */
	{ S5P_ARM_CORE0_SYS,			{ 0, 0, 2 } },
	{ S5P_DIS_IRQ_ARM_CORE0_LOCAL_SYS,	{ 0, 0, 0 } },
	{ S5P_DIS_IRQ_ARM_CORE0_CENTRAL_SYS,	{ 0, 0, 0 } },
	{ S5P_ARM_CORE1_SYS,			{ 0, 0, 2 } },
	{ S5P_DIS_IRQ_ARM_CORE1_LOCAL_SYS,	{ 0, 0, 0 } },
	{ S5P_DIS_IRQ_ARM_CORE1_CENTRAL_SYS,	{ 0, 0, 0 } },
	{ S5P_ARM_COMMON_SYS,			{ 0, 0, 2 } },
	{ S5P_ARM_L2_0_SYS,			{ 2, 2, 3 } },
	{ S5P_ARM_L2_1_SYS,			{ 2, 2, 3 } },
	{ S5P_CMU_ACLKSTOP_SYS,			{ 1, 0, 0 } },
	{ S5P_CMU_SCLKSTOP_SYS,			{ 1, 0, 0 } },
	{ S5P_CMU_RESET_SYS,			{ 1, 1, 0 } },
	{ S5P_APLL_SYSCLK_SYS,			{ 1, 0, 0 } },
	{ S5P_MPLL_SYSCLK_SYS,			{ 1, 0, 0 } },
	{ S5P_VPLL_SYSCLK_SYS,			{ 1, 0, 0 } },
	{ S5P_EPLL_SYSCLK_SYS,			{ 1, 1, 0 } },
	{ S5P_CMU_CLKSTOP_GPS_ALIVE_SYS,	{ 1, 1, 0 } },
	{ S5P_CMU_RESET_GPSALIVE_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_CLKSTOP_CAM_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_CLKSTOP_TV_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_CLKSTOP_MFC_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_CLKSTOP_G3D_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_CLKSTOP_LCD0_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_CLKSTOP_LCD1_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_CLKSTOP_MAUDIO_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_CLKSTOP_GPS_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_RESET_CAM_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_RESET_TV_SYS,			{ 1, 1, 0 } },
	{ S5P_CMU_RESET_MFC_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_RESET_G3D_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_RESET_LCD0_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_RESET_LCD1_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_RESET_MAUDIO_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_RESET_GPS_SYS,		{ 1, 1, 0 } },
	{ S5P_TOP_BUS_SYS,			{ 3, 0, 0 } },
	{ S5P_TOP_RETENTION_SYS,		{ 1, 0, 1 } },
	{ S5P_TOP_PWR_SYS,			{ 3, 0, 3 } },
	{ S5P_LOGIC_RESET_SYS,			{ 1, 1, 0 } },
	{ S5P_ONENAND_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_MODIMIF_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_G2D_ACP_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_USBOTG_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_SDMMC_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_CSSYS_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_SECSS_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_PCIE_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_SATA_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_PAD_RETENTION_DRAM_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_MAUDIO_SYS,		{ 1, 1, 0 } },
	{ S5P_PAD_RETENTION_GPIO_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_UART_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_MMCA_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_MMCB_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_EBIA_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_EBIB_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_ISOLATION_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_ALV_SEL_SYS,			{ 1, 0, 0 } },
	{ S5P_XXTI_SYS,				{ 1, 1, 0 } },
	{ S5P_EXT_REGULATOR_SYS,		{ 1, 1, 0 } },
	{ S5P_GPIO_MODE_SYS,			{ 1, 0, 0 } },
	{ S5P_GPIO_MODE_MAUDIO_SYS,		{ 1, 1, 0 } },
	{ S5P_CAM_SYS,				{ 7, 0, 0 } },
	{ S5P_TV_SYS,				{ 7, 0, 0 } },
	{ S5P_MFC_SYS,				{ 7, 0, 0 } },
	{ S5P_G3D_SYS,				{ 7, 0, 0 } },
	{ S5P_LCD0_SYS,				{ 7, 0, 0 } },
	{ S5P_LCD1_SYS,				{ 7, 0, 0 } },
	{ S5P_MAUDIO_SYS,			{ 7, 7, 0 } },
	{ S5P_GPS_SYS,				{ 7, 0, 0 } },
	{ S5P_GPS_ALIVE_SYS,			{ 7, 0, 0 } },
	{ S5P_XUSBXTI_SYS,			{ 1, 1, 0 } },
};

static struct exynos4_pmu_conf exynos4212_pmu_config[] = {
	{ S5P_ARM_CORE0_SYS,			{ 0, 0, 2 } },
	{ S5P_DIS_IRQ_ARM_CORE0_LOCAL_SYS,	{ 0, 0, 0 } },
	{ S5P_DIS_IRQ_ARM_CORE0_CENTRAL_SYS,	{ 0, 0, 0 } },
	{ S5P_ARM_CORE1_SYS,			{ 0, 0, 2 } },
	{ S5P_DIS_IRQ_ARM_CORE1_LOCAL_SYS,	{ 0, 0, 0 } },
	{ S5P_DIS_IRQ_ARM_CORE1_CENTRAL_SYS,	{ 0, 0, 0 } },
	{ S5P_ISP_ARM_SYS,			{ 1, 0, 0 } },
	{ S5P_DIS_IRQ_ISP_ARM_LOCAL_SYS,	{ 0, 0, 0 } },
	{ S5P_DIS_IRQ_ISP_ARM_CENTRAL_SYS,	{ 1, 0, 0 } },
	{ S5P_ARM_COMMON_SYS,			{ 0, 0, 2 } },
	{ S5P_ARM_L2_0_SYS,			{ 0, 0, 3 } },
	{ S5P_ARM_L2_0_OPTION,			{ 0x10, 0x10, 0 } },
	{ S5P_ARM_L2_1_SYS,			{ 0, 0, 3 } },
	{ S5P_ARM_L2_1_OPTION,			{ 0x10, 0x10, 0 } },
	{ S5P_CMU_ACLKSTOP_SYS,			{ 1, 0, 0 } },
	{ S5P_CMU_SCLKSTOP_SYS,			{ 1, 0, 0 } },
	{ S5P_CMU_RESET_SYS,			{ 1, 1, 0 } },
	{ S5P_DRAM_FREQ_DOWN_SYS,		{ 1, 1, 1 } },
	{ S5P_DDRPHY_DLLOFF_SYS,		{ 1, 1, 1 } },
	{ S5P_LPDDR_PHY_DLL_LOCK_SYS,		{ 1, 1, 1 } },
	{ S5P_CMU_ACLKSTOP_COREBLK_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_SCLKSTOP_COREBLK_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_RESET_COREBLK_SYS,		{ 1, 1, 0 } },
	{ S5P_APLL_SYSCLK_SYS,			{ 1, 0, 0 } },
	{ S5P_MPLL_SYSCLK_SYS,			{ 1, 0, 0 } },
	{ S5P_VPLL_SYSCLK_SYS,			{ 1, 0, 0 } },
	{ S5P_EPLL_SYSCLK_SYS,			{ 1, 1, 0 } },
	{ S5P_MPLLUSER_SYSCLK_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_GPS_ALIVE_SYS,	{ 1, 0, 0 } },
	{ S5P_CMU_RESET_GPSALIVE_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_CAM_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_TV_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_MFC_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_G3D_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_LCD0_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_ISP_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_MAUDIO_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_GPS_SYS,		{ 0, 0, 0 } },

	{ S5P_CMU_RESET_CAM_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_RESET_TV_SYS,			{ 1, 0, 0 } },
	{ S5P_CMU_RESET_MFC_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_RESET_G3D_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_RESET_LCD0_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_RESET_ISP_SYS,		{ 0, 0, 0 } },
	{ S5P_CMU_RESET_MAUDIO_SYS,		{ 1, 1, 0 } },
	{ S5P_CMU_RESET_GPS_SYS,		{ 1, 0, 0 } },
	{ S5P_TOP_BUS_SYS,			{ 3, 0, 0 } },
	{ S5P_TOP_RETENTION_SYS,		{ 1, 0, 1 } },
	{ S5P_TOP_PWR_SYS,			{ 3, 0, 3 } },
	{ S5P_TOP_BUS_COREBLK_SYS,		{ 3, 0, 0 } },
	{ S5P_TOP_RETENTION_COREBLK_SYS,	{ 1, 0, 1 } },
	{ S5P_TOP_PWR_COREBLK_SYS,		{ 3, 0, 3 } },
	{ S5P_LOGIC_RESET_SYS,			{ 1, 1, 0 } },
	{ S5P_OSCCLK_GATE_SYS,			{ 1, 0, 1 } },
	{ S5P_LOGIC_RESET_COREBLK_SYS,		{ 1, 1, 0 } },
	{ S5P_OSCCLK_GATE_COREBLK_SYS,		{ 1, 0, 1 } },
	{ S5P_ONENAND_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_ONENAND_MEM_OPTION,		{ 0x10, 0x10, 0 } },
	{ S5P_HSI_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_HSI_MEM_OPTION,			{ 0x10, 0x10, 0 } },
	{ S5P_G2D_ACP_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_G2D_ACP_MEM_OPTION,		{ 0x10, 0x10, 0 } },
	{ S5P_USBOTG_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_USBOTG_MEM_OPTION,		{ 0x10, 0x10, 0 } },
	{ S5P_SDMMC_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_SDMMC_MEM_OPTION,			{ 0x10, 0x10, 0 } },
	{ S5P_CSSYS_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_CSSYS_MEM_OPTION,			{ 0x10, 0x10, 0 } },
	{ S5P_SECSS_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_SECSS_MEM_OPTION,			{ 0x10, 0x10, 0 } },
	{ S5P_ROTATOR_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_ROTATOR_MEM_OPTION,		{ 0x10, 0x10, 0 } },
	{ S5P_PAD_RETENTION_DRAM_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_MAUDIO_SYS,		{ 1, 1, 0 } },
	{ S5P_PAD_RETENTION_GPIO_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_UART_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_MMCA_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_MMCB_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_EBIA_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_EBIB_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_GPIO_COREBLK_SYS,	{ 1, 0, 0 } },
	{ S5P_PAD_ISOLATION_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_ISOLATION_COREBLK_SYS,	{ 1, 0, 0 } },
	{ S5P_PAD_ALV_SEL_SYS,			{ 1, 0, 0 } },
	{ S5P_XXTI_SYS,				{ 1, 1, 0 } },
	{ S5P_EXT_REGULATOR_SYS,		{ 1, 1, 0 } },
	{ S5P_GPIO_MODE_SYS,			{ 1, 0, 0 } },
	{ S5P_GPIO_MODE_COREBLK_SYS,		{ 1, 0, 0 } },
	{ S5P_GPIO_MODE_MAUDIO_SYS,		{ 1, 1, 0 } },
	{ S5P_TOP_ASB_RESET_SYS,		{ 1, 1, 1 } },
	{ S5P_TOP_ASB_ISOLATION_SYS,		{ 1, 0, 1 } },
	{ S5P_CAM_SYS,				{ 7, 0, 0 } },
	{ S5P_TV_SYS,				{ 7, 0, 0 } },
	{ S5P_MFC_SYS,				{ 7, 0, 0 } },
	{ S5P_G3D_SYS,				{ 7, 0, 0 } },
	{ S5P_LCD0_SYS,				{ 7, 0, 0 } },
	{ S5P_ISP_SYS,				{ 7, 0, 0 } },
	{ S5P_MAUDIO_SYS,			{ 7, 7, 0 } },
	{ S5P_GPS_SYS,				{ 7, 0, 0 } },
	{ S5P_GPS_ALIVE_SYS,			{ 7, 0, 0 } },
	{ S5P_CMU_SYSCLK_ISP_SYS,		{ 0, 0, 0 } },
	{ S5P_CMU_SYSCLK_GPS_SYS,		{ 1, 0, 0 } },
	{ S5P_XUSBXTI_SYS,			{ 1, 1, 0 } },
};

static struct exynos4_pmu_conf exynos4412_pmu_config[] = {
	{ S5P_ARM_CORE0_SYS,			{ 0, 0, 2 } },
	{ S5P_ARM_CORE1_SYS,			{ 0, 0, 2 } },
	{ S5P_ARM_CORE2_SYS,			{ 0, 0, 2 } },
	{ S5P_ARM_CORE3_SYS,			{ 0, 0, 2 } },
	{ S5P_ISP_ARM_SYS,			{ 1, 0, 0 } },
	{ S5P_DIS_IRQ_ISP_ARM_LOCAL_SYS,	{ 0, 0, 0 } },
	{ S5P_DIS_IRQ_ISP_ARM_CENTRAL_SYS,	{ 1, 0, 0 } },
	{ S5P_ARM_COMMON_SYS,			{ 0, 0, 2 } },
	{ S5P_ARM_L2_0_SYS,			{ 0, 0, 3 } },
	{ S5P_ARM_L2_0_OPTION,			{ 0x10, 0x10, 0 } },
	{ S5P_ARM_L2_1_SYS,			{ 0, 0, 3 } },
	{ S5P_ARM_L2_1_OPTION,			{ 0x10, 0x10, 0 } },
	{ S5P_CMU_ACLKSTOP_SYS,			{ 1, 0, 0 } },
	{ S5P_CMU_SCLKSTOP_SYS,			{ 1, 0, 0 } },
	{ S5P_CMU_RESET_SYS,			{ 1, 1, 0 } },
	{ S5P_CMU_ACLKSTOP_COREBLK_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_SCLKSTOP_COREBLK_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_RESET_COREBLK_SYS,		{ 1, 1, 0 } },
	{ S5P_APLL_SYSCLK_SYS,			{ 1, 0, 0 } },
	{ S5P_MPLL_SYSCLK_SYS,			{ 1, 0, 0 } },
	{ S5P_VPLL_SYSCLK_SYS,			{ 1, 0, 0 } },
	{ S5P_EPLL_SYSCLK_SYS,			{ 1, 1, 0 } },
	{ S5P_MPLLUSER_SYSCLK_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_GPS_ALIVE_SYS,	{ 1, 0, 0 } },
	{ S5P_CMU_RESET_GPSALIVE_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_CAM_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_TV_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_MFC_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_G3D_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_LCD0_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_ISP_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_MAUDIO_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_CLKSTOP_GPS_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_RESET_CAM_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_RESET_TV_SYS,			{ 1, 0, 0 } },
	{ S5P_CMU_RESET_MFC_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_RESET_G3D_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_RESET_LCD0_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_RESET_ISP_SYS,		{ 0, 0, 0 } },
	{ S5P_CMU_RESET_MAUDIO_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_RESET_GPS_SYS,		{ 1, 0, 0 } },
	{ S5P_TOP_BUS_SYS,			{ 3, 0, 0 } },
	{ S5P_TOP_RETENTION_SYS,		{ 1, 0, 1 } },
	{ S5P_TOP_PWR_SYS,			{ 3, 0, 3 } },
	{ S5P_TOP_BUS_COREBLK_SYS,		{ 3, 0, 0 } },
	{ S5P_TOP_RETENTION_COREBLK_SYS,	{ 1, 0, 1 } },
	{ S5P_TOP_PWR_COREBLK_SYS,		{ 3, 0, 3 } },
	{ S5P_LOGIC_RESET_SYS,			{ 1, 1, 0 } },
	{ S5P_OSCCLK_GATE_SYS,			{ 1, 0, 1 } },
	{ S5P_LOGIC_RESET_COREBLK_SYS,		{ 1, 1, 0 } },
	{ S5P_OSCCLK_GATE_COREBLK_SYS,		{ 1, 0, 1 } },
	{ S5P_HSI_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_HSI_MEM_OPTION,			{ 0x10, 0x10, 0 } },
	{ S5P_G2D_ACP_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_G2D_ACP_MEM_OPTION,		{ 0x10, 0x10, 0 } },
	{ S5P_USBOTG_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_USBOTG_MEM_OPTION,		{ 0x10, 0x10, 0 } },
	{ S5P_SDMMC_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_SDMMC_MEM_OPTION,			{ 0x10, 0x10, 0 } },
	{ S5P_CSSYS_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_CSSYS_MEM_OPTION,			{ 0x10, 0x10, 0 } },
	{ S5P_SECSS_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_SECSS_MEM_OPTION,			{ 0x10, 0x10, 0 } },
	{ S5P_ROTATOR_MEM_SYS,			{ 3, 0, 0 } },
	{ S5P_ROTATOR_MEM_OPTION,		{ 0x10, 0x10, 0 } },
	{ S5P_PAD_RETENTION_DRAM_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_MAUDIO_SYS,		{ 1, 1, 0 } },
	{ S5P_PAD_RETENTION_GPIO_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_UART_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_MMCA_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_MMCB_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_EBIA_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_EBIB_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_GPIO_COREBLK_SYS,	{ 1, 0, 0 } },
	{ S5P_PAD_ISOLATION_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_ISOLATION_COREBLK_SYS,	{ 1, 0, 0 } },
	{ S5P_PAD_ALV_SEL_SYS,			{ 1, 0, 0 } },
	{ S5P_XXTI_SYS,				{ 1, 1, 0 } },
	{ S5P_EXT_REGULATOR_SYS,		{ 1, 1, 0 } },
	{ S5P_GPIO_MODE_SYS,			{ 1, 0, 0 } },
	{ S5P_GPIO_MODE_COREBLK_SYS,		{ 1, 0, 0 } },
	{ S5P_GPIO_MODE_MAUDIO_SYS,		{ 1, 1, 0 } },
	{ S5P_TOP_ASB_RESET_SYS,		{ 1, 1, 1 } },
	{ S5P_TOP_ASB_ISOLATION_SYS,		{ 1, 0, 1 } },
	{ S5P_CAM_SYS,				{ 7, 0, 0 } },
	{ S5P_TV_SYS,				{ 7, 0, 0 } },
	{ S5P_MFC_SYS,				{ 7, 0, 0 } },
	{ S5P_G3D_SYS,				{ 7, 0, 0 } },
	{ S5P_LCD0_SYS,				{ 7, 0, 0 } },
	{ S5P_ISP_SYS,				{ 7, 0, 0 } },
	{ S5P_MAUDIO_SYS,			{ 7, 7, 0 } },
	{ S5P_GPS_SYS,				{ 7, 0, 0 } },
	{ S5P_GPS_ALIVE_SYS,			{ 7, 0, 0 } },
	{ S5P_CMU_SYSCLK_ISP_SYS,		{ 1, 0, 0 } },
	{ S5P_CMU_SYSCLK_GPS_SYS,		{ 1, 0, 0 } },
	{ S5P_XUSBXTI_SYS,			{ 1, 1, 0 } },
};

static struct exynos4_pmu_conf exynos4x12_c2c_pmu_conf[] = {
	{ S5P_CMU_RESET_COREBLK_SYS,		{ 1, 1, 1 } },
	{ S5P_MPLLUSER_SYSCLK_SYS,		{ 1, 0, 0 } },
	{ S5P_TOP_RETENTION_COREBLK_SYS,	{ 1, 0, 0 } },
	{ S5P_TOP_PWR_COREBLK_SYS,		{ 3, 0, 0 } },
	{ S5P_LOGIC_RESET_COREBLK_SYS,		{ 1, 1, 1 } },
	{ S5P_OSCCLK_GATE_COREBLK_SYS,		{ 1, 0, 0 } },
	{ S5P_PAD_RETENTION_GPIO_COREBLK_SYS,	{ 1, 1, 1 } },
	{ S5P_TOP_ASB_RESET_SYS,		{ 1, 1, 0 } },
	{ S5P_TOP_ASB_ISOLATION_SYS,		{ 1, 0, 0 } },
};

static struct exynos4_pmu_conf exynos4212_c2c_pmu_conf[] = {
	{ S5P_LPDDR_PHY_DLL_LOCK_SYS,		{ 1, 0, 0 } },
};

static struct exynos4_c2c_pmu_conf exynos4_config_for_c2c[] = {
	/* Register Address	       Value */
	{ S5P_TOP_BUS_COREBLK_SYS,      0x0},
	{ S5P_TOP_PWR_COREBLK_SYS,      0x0},
	{ S5P_MPLL_SYSCLK_SYS,          0x0},
#ifdef CONFIG_MACH_SMDK4212
	{ S5P_XUSBXTI_SYS,              0x0},
#endif
};

void exynos4_pmu_xclkout_set(unsigned int enable, enum xclkout_select source)
{
	unsigned int tmp;

	if (enable) {
		tmp = __raw_readl(S5P_PMU_DEBUG);
		/* CLKOUT enable */
		tmp &= ~(0xF << S5P_PMU_CLKOUT_SEL_SHIFT | S5P_CLKOUT_DISABLE);
		tmp |= (source << S5P_PMU_CLKOUT_SEL_SHIFT);
		__raw_writel(tmp, S5P_PMU_DEBUG);
	} else {
		tmp = __raw_readl(S5P_PMU_DEBUG);
		tmp |= S5P_CLKOUT_DISABLE; /* CLKOUT disable */
		__raw_writel(tmp, S5P_PMU_DEBUG);
	}
	printk(KERN_DEBUG "pmu_debug: 0x%08x\n", __raw_readl(S5P_PMU_DEBUG));
}
EXPORT_SYMBOL_GPL(exynos4_pmu_xclkout_set);

void exynos4_sys_powerdown_xusbxti_control(unsigned int enable)
{
	unsigned int count = entry_cnt;

	if (enable)
		exynos4_pmu_config[count - 1].val[SYS_SLEEP] = 0x1;
	else
		exynos4_pmu_config[count - 1].val[SYS_SLEEP] = 0x0;

	printk(KERN_DEBUG "xusbxti_control: %ld\n",
			exynos4_pmu_config[count - 1].val[SYS_SLEEP]);
}
EXPORT_SYMBOL_GPL(exynos4_sys_powerdown_xusbxti_control);

void exynos4_sys_powerdown_conf(enum sys_powerdown mode)
{
	unsigned int count = entry_cnt;
	unsigned int tmp;

	for (; count > 0; count--)
		__raw_writel(exynos4_pmu_config[count - 1].val[mode],
				exynos4_pmu_config[count - 1].reg);

	if ((!soc_is_exynos4210()) && (exynos4_is_c2c_use())) {
		for (count = 0 ; count < ARRAY_SIZE(exynos4x12_c2c_pmu_conf) ; count++)
			__raw_writel(exynos4x12_c2c_pmu_conf[count].val[mode],
					exynos4x12_c2c_pmu_conf[count].reg);

		if (soc_is_exynos4212())
			__raw_writel(exynos4212_c2c_pmu_conf[0].val[mode],
					exynos4212_c2c_pmu_conf[0].reg);

		for (count = 0 ; count < ARRAY_SIZE(exynos4_config_for_c2c) ; count++) {
			tmp = __raw_readl(exynos4_config_for_c2c[count].reg);
			tmp |= exynos4_config_for_c2c[count].val;
			__raw_writel(tmp, exynos4_config_for_c2c[count].reg);
		}
	}
}

void exynos4_c2c_request_pwr_mode(enum c2c_pwr_mode mode)
{
	exynos4_config_for_c2c[0].val = 0x3;

	switch (mode) {
	/* If C2C mode is MAXIMAL LATENCY */
	case MAX_LATENCY:
		exynos4_config_for_c2c[1].val = 0x0;
		if (soc_is_exynos4412() && (samsung_rev() < EXYNOS4412_REV_1_0))
			exynos4_config_for_c2c[2].val = 0x1;
		else
			exynos4_config_for_c2c[2].val = 0x0;
#ifdef CONFIG_MACH_SMDK4212
		exynos4_config_for_c2c[3].val = 0x0;
#endif
		break;
	/* If C2C mode is Minimal or Short LATENCY */
	default:
		exynos4_config_for_c2c[1].val = 0x3;
		exynos4_config_for_c2c[2].val = 0x1;
#ifdef CONFIG_MACH_SMDK4212
		exynos4_config_for_c2c[3].val = 0x1;
#endif
		break;
	}
}

static int __init exynos4_pmu_init(void)
{
	unsigned int i;

	if(!soc_is_exynos4210())
		exynos4_reset_assert_ctrl(1);

	if (soc_is_exynos4210()) {
		exynos4_pmu_config = exynos4210_pmu_config;
		entry_cnt = ARRAY_SIZE(exynos4210_pmu_config);
		printk(KERN_INFO "%s: PMU supports 4210(%d)\n",
					__func__, entry_cnt);
	} else if (soc_is_exynos4212()) {
		exynos4_pmu_config = exynos4212_pmu_config;
		entry_cnt = ARRAY_SIZE(exynos4212_pmu_config);
		printk(KERN_INFO "%s: PMU supports 4212(%d)\n",
					__func__, entry_cnt);
	} else if (soc_is_exynos4412()) {
		exynos4_pmu_config = exynos4412_pmu_config;
		entry_cnt = ARRAY_SIZE(exynos4412_pmu_config);
		printk(KERN_INFO "%s: PMU supports 4412(%d)\n",
					__func__, entry_cnt);
	} else {
		printk(KERN_INFO "%s: PMU not supported\n", __func__);
	}

	return 0;
}
arch_initcall(exynos4_pmu_init);
