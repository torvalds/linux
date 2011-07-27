/* linux/arch/arm/mach-exynos4/pmu.c
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

static void __iomem *sys_powerdown_reg[] = {
	S5P_ARM_CORE0_LOWPWR,
	S5P_DIS_IRQ_CORE0,
	S5P_DIS_IRQ_CENTRAL0,
	S5P_ARM_CORE1_LOWPWR,
	S5P_DIS_IRQ_CORE1,
	S5P_DIS_IRQ_CENTRAL1,
	S5P_ARM_COMMON_LOWPWR,
	S5P_L2_0_LOWPWR,
	S5P_L2_1_LOWPWR,
	S5P_CMU_ACLKSTOP_LOWPWR,
	S5P_CMU_SCLKSTOP_LOWPWR,
	S5P_CMU_RESET_LOWPWR,
	S5P_APLL_SYSCLK_LOWPWR,
	S5P_MPLL_SYSCLK_LOWPWR,
	S5P_VPLL_SYSCLK_LOWPWR,
	S5P_EPLL_SYSCLK_LOWPWR,
	S5P_CMU_CLKSTOP_GPS_ALIVE_LOWPWR,
	S5P_CMU_RESET_GPSALIVE_LOWPWR,
	S5P_CMU_CLKSTOP_CAM_LOWPWR,
	S5P_CMU_CLKSTOP_TV_LOWPWR,
	S5P_CMU_CLKSTOP_MFC_LOWPWR,
	S5P_CMU_CLKSTOP_G3D_LOWPWR,
	S5P_CMU_CLKSTOP_LCD0_LOWPWR,
	S5P_CMU_CLKSTOP_LCD1_LOWPWR,
	S5P_CMU_CLKSTOP_MAUDIO_LOWPWR,
	S5P_CMU_CLKSTOP_GPS_LOWPWR,
	S5P_CMU_RESET_CAM_LOWPWR,
	S5P_CMU_RESET_TV_LOWPWR,
	S5P_CMU_RESET_MFC_LOWPWR,
	S5P_CMU_RESET_G3D_LOWPWR,
	S5P_CMU_RESET_LCD0_LOWPWR,
	S5P_CMU_RESET_LCD1_LOWPWR,
	S5P_CMU_RESET_MAUDIO_LOWPWR,
	S5P_CMU_RESET_GPS_LOWPWR,
	S5P_TOP_BUS_LOWPWR,
	S5P_TOP_RETENTION_LOWPWR,
	S5P_TOP_PWR_LOWPWR,
	S5P_LOGIC_RESET_LOWPWR,
	S5P_ONENAND_MEM_LOWPWR,
	S5P_MODIMIF_MEM_LOWPWR,
	S5P_G2D_ACP_MEM_LOWPWR,
	S5P_USBOTG_MEM_LOWPWR,
	S5P_HSMMC_MEM_LOWPWR,
	S5P_CSSYS_MEM_LOWPWR,
	S5P_SECSS_MEM_LOWPWR,
	S5P_PCIE_MEM_LOWPWR,
	S5P_SATA_MEM_LOWPWR,
	S5P_PAD_RETENTION_DRAM_LOWPWR,
	S5P_PAD_RETENTION_MAUDIO_LOWPWR,
	S5P_PAD_RETENTION_GPIO_LOWPWR,
	S5P_PAD_RETENTION_UART_LOWPWR,
	S5P_PAD_RETENTION_MMCA_LOWPWR,
	S5P_PAD_RETENTION_MMCB_LOWPWR,
	S5P_PAD_RETENTION_EBIA_LOWPWR,
	S5P_PAD_RETENTION_EBIB_LOWPWR,
	S5P_PAD_RETENTION_ISOLATION_LOWPWR,
	S5P_PAD_RETENTION_ALV_SEL_LOWPWR,
	S5P_XUSBXTI_LOWPWR,
	S5P_XXTI_LOWPWR,
	S5P_EXT_REGULATOR_LOWPWR,
	S5P_GPIO_MODE_LOWPWR,
	S5P_GPIO_MODE_MAUDIO_LOWPWR,
	S5P_CAM_LOWPWR,
	S5P_TV_LOWPWR,
	S5P_MFC_LOWPWR,
	S5P_G3D_LOWPWR,
	S5P_LCD0_LOWPWR,
	S5P_LCD1_LOWPWR,
	S5P_MAUDIO_LOWPWR,
	S5P_GPS_LOWPWR,
	S5P_GPS_ALIVE_LOWPWR,
};

static const unsigned int sys_powerdown_val[][NUM_SYS_POWERDOWN] = {
	/* { AFTR, LPA, SLEEP }*/
	{ 0, 0, 2 },	/* ARM_CORE0 */
	{ 0, 0, 0 },	/* ARM_DIS_IRQ_CORE0 */
	{ 0, 0, 0 },	/* ARM_DIS_IRQ_CENTRAL0 */
	{ 0, 0, 2 },	/* ARM_CORE1 */
	{ 0, 0, 0 },	/* ARM_DIS_IRQ_CORE1 */
	{ 0, 0, 0 },	/* ARM_DIS_IRQ_CENTRAL1 */
	{ 0, 0, 2 },	/* ARM_COMMON */
	{ 2, 2, 3 },	/* ARM_CPU_L2_0 */
	{ 2, 2, 3 },	/* ARM_CPU_L2_1 */
	{ 1, 0, 0 },	/* CMU_ACLKSTOP */
	{ 1, 0, 0 },	/* CMU_SCLKSTOP */
	{ 1, 1, 0 },	/* CMU_RESET */
	{ 1, 0, 0 },	/* APLL_SYSCLK */
	{ 1, 0, 0 },	/* MPLL_SYSCLK */
	{ 1, 0, 0 },	/* VPLL_SYSCLK */
	{ 1, 1, 0 },	/* EPLL_SYSCLK */
	{ 1, 1, 0 },	/* CMU_CLKSTOP_GPS_ALIVE */
	{ 1, 1, 0 },	/* CMU_RESET_GPS_ALIVE */
	{ 1, 1, 0 },	/* CMU_CLKSTOP_CAM */
	{ 1, 1, 0 },	/* CMU_CLKSTOP_TV */
	{ 1, 1, 0 },	/* CMU_CLKSTOP_MFC */
	{ 1, 1, 0 },	/* CMU_CLKSTOP_G3D */
	{ 1, 1, 0 },	/* CMU_CLKSTOP_LCD0 */
	{ 1, 1, 0 },	/* CMU_CLKSTOP_LCD1 */
	{ 1, 1, 0 },	/* CMU_CLKSTOP_MAUDIO */
	{ 1, 1, 0 },	/* CMU_CLKSTOP_GPS */
	{ 1, 1, 0 },	/* CMU_RESET_CAM */
	{ 1, 1, 0 },	/* CMU_RESET_TV */
	{ 1, 1, 0 },	/* CMU_RESET_MFC */
	{ 1, 1, 0 },	/* CMU_RESET_G3D */
	{ 1, 1, 0 },	/* CMU_RESET_LCD0 */
	{ 1, 1, 0 },	/* CMU_RESET_LCD1 */
	{ 1, 1, 0 },	/* CMU_RESET_MAUDIO */
	{ 1, 1, 0 },	/* CMU_RESET_GPS */
	{ 3, 0, 0 },	/* TOP_BUS */
	{ 1, 0, 1 },	/* TOP_RETENTION */
	{ 3, 0, 3 },	/* TOP_PWR */
	{ 1, 1, 0 },	/* LOGIC_RESET */
	{ 3, 0, 0 },	/* ONENAND_MEM */
	{ 3, 0, 0 },	/* MODIMIF_MEM */
	{ 3, 0, 0 },	/* G2D_ACP_MEM */
	{ 3, 0, 0 },	/* USBOTG_MEM */
	{ 3, 0, 0 },	/* HSMMC_MEM */
	{ 3, 0, 0 },	/* CSSYS_MEM */
	{ 3, 0, 0 },	/* SECSS_MEM */
	{ 3, 0, 0 },	/* PCIE_MEM */
	{ 3, 0, 0 },	/* SATA_MEM */
	{ 1, 0, 0 },	/* PAD_RETENTION_DRAM */
	{ 1, 1, 0 },	/* PAD_RETENTION_MAUDIO */
	{ 1, 0, 0 },	/* PAD_RETENTION_GPIO */
	{ 1, 0, 0 },	/* PAD_RETENTION_UART */
	{ 1, 0, 0 },	/* PAD_RETENTION_MMCA */
	{ 1, 0, 0 },	/* PAD_RETENTION_MMCB */
	{ 1, 0, 0 },	/* PAD_RETENTION_EBIA */
	{ 1, 0, 0 },	/* PAD_RETENTION_EBIB */
	{ 1, 0, 0 },	/* PAD_RETENTION_ISOLATION */
	{ 1, 0, 0 },	/* PAD_RETENTION_ALV_SEL */
	{ 1, 1, 0 },	/* XUSBXTI */
	{ 1, 1, 0 },	/* XXTI */
	{ 1, 1, 0 },	/* EXT_REGULATOR */
	{ 1, 0, 0 },	/* GPIO_MODE */
	{ 1, 1, 0 },	/* GPIO_MODE_MAUDIO */
	{ 7, 0, 0 },	/* CAM */
	{ 7, 0, 0 },	/* TV */
	{ 7, 0, 0 },	/* MFC */
	{ 7, 0, 0 },	/* G3D */
	{ 7, 0, 0 },	/* LCD0 */
	{ 7, 0, 0 },	/* LCD1 */
	{ 7, 7, 0 },	/* MAUDIO */
	{ 7, 0, 0 },	/* GPS */
	{ 7, 0, 0 },	/* GPS_ALIVE */
};

void exynos4_sys_powerdown_conf(enum sys_powerdown mode)
{
	unsigned int count = ARRAY_SIZE(sys_powerdown_reg);

	for (; count > 0; count--)
		__raw_writel(sys_powerdown_val[count - 1][mode],
				sys_powerdown_reg[count - 1]);
}
