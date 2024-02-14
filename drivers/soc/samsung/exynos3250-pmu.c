// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2011-2015 Samsung Electronics Co., Ltd.
//		http://www.samsung.com/
//
// Exynos3250 - CPU PMU (Power Management Unit) support

#include <linux/soc/samsung/exynos-regs-pmu.h>
#include <linux/soc/samsung/exynos-pmu.h>

#include "exynos-pmu.h"

static const struct exynos_pmu_conf exynos3250_pmu_config[] = {
	/* { .offset = offset, .val = { AFTR, W-AFTR, SLEEP } */
	{ EXYNOS3_ARM_CORE0_SYS_PWR_REG,		{ 0x0, 0x0, 0x2} },
	{ EXYNOS3_DIS_IRQ_ARM_CORE0_LOCAL_SYS_PWR_REG,	{ 0x0, 0x0, 0x0} },
	{ EXYNOS3_DIS_IRQ_ARM_CORE0_CENTRAL_SYS_PWR_REG, { 0x0, 0x0, 0x0} },
	{ EXYNOS3_ARM_CORE1_SYS_PWR_REG,		{ 0x0, 0x0, 0x2} },
	{ EXYNOS3_DIS_IRQ_ARM_CORE1_LOCAL_SYS_PWR_REG,	{ 0x0, 0x0, 0x0} },
	{ EXYNOS3_DIS_IRQ_ARM_CORE1_CENTRAL_SYS_PWR_REG, { 0x0, 0x0, 0x0} },
	{ EXYNOS3_ISP_ARM_SYS_PWR_REG,			{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_DIS_IRQ_ISP_ARM_LOCAL_SYS_PWR_REG,	{ 0x0, 0x0, 0x0} },
	{ EXYNOS3_DIS_IRQ_ISP_ARM_CENTRAL_SYS_PWR_REG,	{ 0x0, 0x0, 0x0} },
	{ EXYNOS3_ARM_COMMON_SYS_PWR_REG,		{ 0x0, 0x0, 0x2} },
	{ EXYNOS3_ARM_L2_SYS_PWR_REG,			{ 0x0, 0x0, 0x3} },
	{ EXYNOS3_CMU_ACLKSTOP_SYS_PWR_REG,		{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_CMU_SCLKSTOP_SYS_PWR_REG,		{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_CMU_RESET_SYS_PWR_REG,		{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_DRAM_FREQ_DOWN_SYS_PWR_REG,		{ 0x1, 0x1, 0x1} },
	{ EXYNOS3_DDRPHY_DLLOFF_SYS_PWR_REG,		{ 0x1, 0x1, 0x1} },
	{ EXYNOS3_LPDDR_PHY_DLL_LOCK_SYS_PWR_REG,	{ 0x1, 0x1, 0x1} },
	{ EXYNOS3_CMU_ACLKSTOP_COREBLK_SYS_PWR_REG,	{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_SCLKSTOP_COREBLK_SYS_PWR_REG,	{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_RESET_COREBLK_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_APLL_SYSCLK_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_MPLL_SYSCLK_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_BPLL_SYSCLK_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_VPLL_SYSCLK_SYS_PWR_REG,		{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_EPLL_SYSCLK_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_UPLL_SYSCLK_SYS_PWR_REG,		{ 0x1, 0x1, 0x1} },
	{ EXYNOS3_EPLLUSER_SYSCLK_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_MPLLUSER_SYSCLK_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_BPLLUSER_SYSCLK_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_CLKSTOP_CAM_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_CLKSTOP_MFC_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_CLKSTOP_G3D_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_CLKSTOP_LCD0_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_CLKSTOP_ISP_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_CLKSTOP_MAUDIO_SYS_PWR_REG,	{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_RESET_CAM_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_RESET_MFC_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_RESET_G3D_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_RESET_LCD0_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_RESET_ISP_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_CMU_RESET_MAUDIO_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ EXYNOS3_TOP_BUS_SYS_PWR_REG,			{ 0x3, 0x0, 0x0} },
	{ EXYNOS3_TOP_RETENTION_SYS_PWR_REG,		{ 0x1, 0x1, 0x1} },
	{ EXYNOS3_TOP_PWR_SYS_PWR_REG,			{ 0x3, 0x3, 0x3} },
	{ EXYNOS3_TOP_BUS_COREBLK_SYS_PWR_REG,		{ 0x3, 0x0, 0x0} },
	{ EXYNOS3_TOP_RETENTION_COREBLK_SYS_PWR_REG,	{ 0x1, 0x1, 0x1} },
	{ EXYNOS3_TOP_PWR_COREBLK_SYS_PWR_REG,		{ 0x3, 0x3, 0x3} },
	{ EXYNOS3_LOGIC_RESET_SYS_PWR_REG,		{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_OSCCLK_GATE_SYS_PWR_REG,		{ 0x1, 0x1, 0x1} },
	{ EXYNOS3_LOGIC_RESET_COREBLK_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_OSCCLK_GATE_COREBLK_SYS_PWR_REG,	{ 0x1, 0x0, 0x1} },
	{ EXYNOS3_PAD_RETENTION_DRAM_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_PAD_RETENTION_MAUDIO_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_PAD_RETENTION_GPIO_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_PAD_RETENTION_UART_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_PAD_RETENTION_MMC0_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_PAD_RETENTION_MMC1_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_PAD_RETENTION_MMC2_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_PAD_RETENTION_SPI_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_PAD_RETENTION_EBIA_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_PAD_RETENTION_EBIB_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_PAD_RETENTION_JTAG_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_PAD_ISOLATION_SYS_PWR_REG,		{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_PAD_ALV_SEL_SYS_PWR_REG,		{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_XUSBXTI_SYS_PWR_REG,			{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_XXTI_SYS_PWR_REG,			{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_EXT_REGULATOR_SYS_PWR_REG,		{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_EXT_REGULATOR_COREBLK_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_GPIO_MODE_SYS_PWR_REG,		{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_GPIO_MODE_MAUDIO_SYS_PWR_REG,		{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_TOP_ASB_RESET_SYS_PWR_REG,		{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_TOP_ASB_ISOLATION_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_TOP_ASB_RESET_COREBLK_SYS_PWR_REG,	{ 0x1, 0x1, 0x0} },
	{ EXYNOS3_TOP_ASB_ISOLATION_COREBLK_SYS_PWR_REG, { 0x1, 0x1, 0x0} },
	{ EXYNOS3_CAM_SYS_PWR_REG,			{ 0x7, 0x0, 0x0} },
	{ EXYNOS3_MFC_SYS_PWR_REG,			{ 0x7, 0x0, 0x0} },
	{ EXYNOS3_G3D_SYS_PWR_REG,			{ 0x7, 0x0, 0x0} },
	{ EXYNOS3_LCD0_SYS_PWR_REG,			{ 0x7, 0x0, 0x0} },
	{ EXYNOS3_ISP_SYS_PWR_REG,			{ 0x7, 0x0, 0x0} },
	{ EXYNOS3_MAUDIO_SYS_PWR_REG,			{ 0x7, 0x0, 0x0} },
	{ EXYNOS3_CMU_SYSCLK_ISP_SYS_PWR_REG,		{ 0x1, 0x0, 0x0} },
	{ PMU_TABLE_END,},
};

static unsigned int const exynos3250_list_feed[] = {
	EXYNOS3_ARM_CORE_OPTION(0),
	EXYNOS3_ARM_CORE_OPTION(1),
	EXYNOS3_ARM_CORE_OPTION(2),
	EXYNOS3_ARM_CORE_OPTION(3),
	EXYNOS3_ARM_COMMON_OPTION,
	EXYNOS3_TOP_PWR_OPTION,
	EXYNOS3_CORE_TOP_PWR_OPTION,
	S5P_CAM_OPTION,
	S5P_MFC_OPTION,
	S5P_G3D_OPTION,
	S5P_LCD0_OPTION,
	S5P_ISP_OPTION,
};

static void exynos3250_powerdown_conf_extra(enum sys_powerdown mode)
{
	unsigned int i;
	unsigned int tmp;

	/* Enable only SC_FEEDBACK */
	for (i = 0; i < ARRAY_SIZE(exynos3250_list_feed); i++) {
		tmp = pmu_raw_readl(exynos3250_list_feed[i]);
		tmp &= ~(EXYNOS3_OPTION_USE_SC_COUNTER);
		tmp |= EXYNOS3_OPTION_USE_SC_FEEDBACK;
		pmu_raw_writel(tmp, exynos3250_list_feed[i]);
	}

	if (mode != SYS_SLEEP)
		return;

	pmu_raw_writel(XUSBXTI_DURATION, EXYNOS3_XUSBXTI_DURATION);
	pmu_raw_writel(XXTI_DURATION, EXYNOS3_XXTI_DURATION);
	pmu_raw_writel(EXT_REGULATOR_DURATION, EXYNOS3_EXT_REGULATOR_DURATION);
	pmu_raw_writel(EXT_REGULATOR_COREBLK_DURATION,
		       EXYNOS3_EXT_REGULATOR_COREBLK_DURATION);
}

static void exynos3250_pmu_init(void)
{
	unsigned int value;

	/*
	 * To prevent from issuing new bus request form L2 memory system
	 * If core status is power down, should be set '1' to L2 power down
	 */
	value = pmu_raw_readl(EXYNOS3_ARM_COMMON_OPTION);
	value |= EXYNOS3_OPTION_SKIP_DEACTIVATE_ACEACP_IN_PWDN;
	pmu_raw_writel(value, EXYNOS3_ARM_COMMON_OPTION);

	/* Enable USE_STANDBY_WFI for all CORE */
	pmu_raw_writel(S5P_USE_STANDBY_WFI_ALL, S5P_CENTRAL_SEQ_OPTION);

	/*
	 * Set PSHOLD port for output high
	 */
	value = pmu_raw_readl(S5P_PS_HOLD_CONTROL);
	value |= S5P_PS_HOLD_OUTPUT_HIGH;
	pmu_raw_writel(value, S5P_PS_HOLD_CONTROL);

	/*
	 * Enable signal for PSHOLD port
	 */
	value = pmu_raw_readl(S5P_PS_HOLD_CONTROL);
	value |= S5P_PS_HOLD_EN;
	pmu_raw_writel(value, S5P_PS_HOLD_CONTROL);
}

const struct exynos_pmu_data exynos3250_pmu_data = {
	.pmu_config	= exynos3250_pmu_config,
	.pmu_init	= exynos3250_pmu_init,
	.powerdown_conf_extra	= exynos3250_powerdown_conf_extra,
};
