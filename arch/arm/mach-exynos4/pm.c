/* linux/arch/arm/mach-exynos4/pm.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4210 - Power Management support
 *
 * Based on arch/arm/mach-s3c2410/pm.c
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>

#include <plat/cpu.h>
#include <plat/pm.h>

#include <mach/regs-irq.h>
#include <mach/regs-gpio.h>
#include <mach/regs-clock.h>
#include <mach/regs-pmu.h>
#include <mach/pm-core.h>

static struct sleep_save exynos4_sleep[] = {
	{ .reg = S5P_ARM_CORE0_LOWPWR			, .val = 0x2, },
	{ .reg = S5P_DIS_IRQ_CORE0			, .val = 0x0, },
	{ .reg = S5P_DIS_IRQ_CENTRAL0			, .val = 0x0, },
	{ .reg = S5P_ARM_CORE1_LOWPWR			, .val = 0x2, },
	{ .reg = S5P_DIS_IRQ_CORE1			, .val = 0x0, },
	{ .reg = S5P_DIS_IRQ_CENTRAL1			, .val = 0x0, },
	{ .reg = S5P_ARM_COMMON_LOWPWR			, .val = 0x2, },
	{ .reg = S5P_L2_0_LOWPWR			, .val = 0x3, },
	{ .reg = S5P_L2_1_LOWPWR			, .val = 0x3, },
	{ .reg = S5P_CMU_ACLKSTOP_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_SCLKSTOP_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_RESET_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_APLL_SYSCLK_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_MPLL_SYSCLK_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_VPLL_SYSCLK_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_EPLL_SYSCLK_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_CMU_CLKSTOP_GPS_ALIVE_LOWPWR	, .val = 0x0, },
	{ .reg = S5P_CMU_RESET_GPSALIVE_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_CLKSTOP_CAM_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_CLKSTOP_TV_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_CLKSTOP_MFC_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_CLKSTOP_G3D_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_CLKSTOP_LCD0_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_CLKSTOP_LCD1_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_CLKSTOP_MAUDIO_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_CLKSTOP_GPS_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_RESET_CAM_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_RESET_TV_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_RESET_MFC_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_RESET_G3D_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_RESET_LCD0_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_RESET_LCD1_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_RESET_MAUDIO_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CMU_RESET_GPS_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_TOP_BUS_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_TOP_RETENTION_LOWPWR		, .val = 0x1, },
	{ .reg = S5P_TOP_PWR_LOWPWR			, .val = 0x3, },
	{ .reg = S5P_LOGIC_RESET_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_ONENAND_MEM_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_MODIMIF_MEM_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_G2D_ACP_MEM_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_USBOTG_MEM_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_HSMMC_MEM_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_CSSYS_MEM_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_SECSS_MEM_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_PCIE_MEM_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_SATA_MEM_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_PAD_RETENTION_DRAM_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_PAD_RETENTION_MAUDIO_LOWPWR	, .val = 0x0, },
	{ .reg = S5P_PAD_RETENTION_GPIO_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_PAD_RETENTION_UART_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_PAD_RETENTION_MMCA_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_PAD_RETENTION_MMCB_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_PAD_RETENTION_EBIA_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_PAD_RETENTION_EBIB_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_PAD_RETENTION_ISOLATION_LOWPWR	, .val = 0x0, },
	{ .reg = S5P_PAD_RETENTION_ALV_SEL_LOWPWR	, .val = 0x0, },
	{ .reg = S5P_XUSBXTI_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_XXTI_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_EXT_REGULATOR_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_GPIO_MODE_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_GPIO_MODE_MAUDIO_LOWPWR		, .val = 0x0, },
	{ .reg = S5P_CAM_LOWPWR				, .val = 0x0, },
	{ .reg = S5P_TV_LOWPWR				, .val = 0x0, },
	{ .reg = S5P_MFC_LOWPWR				, .val = 0x0, },
	{ .reg = S5P_G3D_LOWPWR				, .val = 0x0, },
	{ .reg = S5P_LCD0_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_LCD1_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_MAUDIO_LOWPWR			, .val = 0x0, },
	{ .reg = S5P_GPS_LOWPWR				, .val = 0x0, },
	{ .reg = S5P_GPS_ALIVE_LOWPWR			, .val = 0x0, },
};

static struct sleep_save exynos4_set_clksrc[] = {
	{ .reg = S5P_CLKSRC_MASK_TOP			, .val = 0x00000001, },
	{ .reg = S5P_CLKSRC_MASK_CAM			, .val = 0x11111111, },
	{ .reg = S5P_CLKSRC_MASK_TV			, .val = 0x00000111, },
	{ .reg = S5P_CLKSRC_MASK_LCD0			, .val = 0x00001111, },
	{ .reg = S5P_CLKSRC_MASK_LCD1			, .val = 0x00001111, },
	{ .reg = S5P_CLKSRC_MASK_MAUDIO			, .val = 0x00000001, },
	{ .reg = S5P_CLKSRC_MASK_FSYS			, .val = 0x01011111, },
	{ .reg = S5P_CLKSRC_MASK_PERIL0			, .val = 0x01111111, },
	{ .reg = S5P_CLKSRC_MASK_PERIL1			, .val = 0x01110111, },
	{ .reg = S5P_CLKSRC_MASK_DMC			, .val = 0x00010000, },
};

static struct sleep_save exynos4_core_save[] = {
	/* CMU side */
	SAVE_ITEM(S5P_CLKDIV_LEFTBUS),
	SAVE_ITEM(S5P_CLKGATE_IP_LEFTBUS),
	SAVE_ITEM(S5P_CLKDIV_RIGHTBUS),
	SAVE_ITEM(S5P_CLKGATE_IP_RIGHTBUS),
	SAVE_ITEM(S5P_EPLL_CON0),
	SAVE_ITEM(S5P_EPLL_CON1),
	SAVE_ITEM(S5P_VPLL_CON0),
	SAVE_ITEM(S5P_VPLL_CON1),
	SAVE_ITEM(S5P_CLKSRC_TOP0),
	SAVE_ITEM(S5P_CLKSRC_TOP1),
	SAVE_ITEM(S5P_CLKSRC_CAM),
	SAVE_ITEM(S5P_CLKSRC_MFC),
	SAVE_ITEM(S5P_CLKSRC_IMAGE),
	SAVE_ITEM(S5P_CLKSRC_LCD0),
	SAVE_ITEM(S5P_CLKSRC_LCD1),
	SAVE_ITEM(S5P_CLKSRC_MAUDIO),
	SAVE_ITEM(S5P_CLKSRC_FSYS),
	SAVE_ITEM(S5P_CLKSRC_PERIL0),
	SAVE_ITEM(S5P_CLKSRC_PERIL1),
	SAVE_ITEM(S5P_CLKDIV_CAM),
	SAVE_ITEM(S5P_CLKDIV_TV),
	SAVE_ITEM(S5P_CLKDIV_MFC),
	SAVE_ITEM(S5P_CLKDIV_G3D),
	SAVE_ITEM(S5P_CLKDIV_IMAGE),
	SAVE_ITEM(S5P_CLKDIV_LCD0),
	SAVE_ITEM(S5P_CLKDIV_LCD1),
	SAVE_ITEM(S5P_CLKDIV_MAUDIO),
	SAVE_ITEM(S5P_CLKDIV_FSYS0),
	SAVE_ITEM(S5P_CLKDIV_FSYS1),
	SAVE_ITEM(S5P_CLKDIV_FSYS2),
	SAVE_ITEM(S5P_CLKDIV_FSYS3),
	SAVE_ITEM(S5P_CLKDIV_PERIL0),
	SAVE_ITEM(S5P_CLKDIV_PERIL1),
	SAVE_ITEM(S5P_CLKDIV_PERIL2),
	SAVE_ITEM(S5P_CLKDIV_PERIL3),
	SAVE_ITEM(S5P_CLKDIV_PERIL4),
	SAVE_ITEM(S5P_CLKDIV_PERIL5),
	SAVE_ITEM(S5P_CLKDIV_TOP),
	SAVE_ITEM(S5P_CLKSRC_MASK_CAM),
	SAVE_ITEM(S5P_CLKSRC_MASK_TV),
	SAVE_ITEM(S5P_CLKSRC_MASK_LCD0),
	SAVE_ITEM(S5P_CLKSRC_MASK_LCD1),
	SAVE_ITEM(S5P_CLKSRC_MASK_MAUDIO),
	SAVE_ITEM(S5P_CLKSRC_MASK_FSYS),
	SAVE_ITEM(S5P_CLKSRC_MASK_PERIL0),
	SAVE_ITEM(S5P_CLKSRC_MASK_PERIL1),
	SAVE_ITEM(S5P_CLKGATE_SCLKCAM),
	SAVE_ITEM(S5P_CLKGATE_IP_CAM),
	SAVE_ITEM(S5P_CLKGATE_IP_TV),
	SAVE_ITEM(S5P_CLKGATE_IP_MFC),
	SAVE_ITEM(S5P_CLKGATE_IP_G3D),
	SAVE_ITEM(S5P_CLKGATE_IP_IMAGE),
	SAVE_ITEM(S5P_CLKGATE_IP_LCD0),
	SAVE_ITEM(S5P_CLKGATE_IP_LCD1),
	SAVE_ITEM(S5P_CLKGATE_IP_FSYS),
	SAVE_ITEM(S5P_CLKGATE_IP_GPS),
	SAVE_ITEM(S5P_CLKGATE_IP_PERIL),
	SAVE_ITEM(S5P_CLKGATE_IP_PERIR),
	SAVE_ITEM(S5P_CLKGATE_BLOCK),
	SAVE_ITEM(S5P_CLKSRC_MASK_DMC),
	SAVE_ITEM(S5P_CLKSRC_DMC),
	SAVE_ITEM(S5P_CLKDIV_DMC0),
	SAVE_ITEM(S5P_CLKDIV_DMC1),
	SAVE_ITEM(S5P_CLKGATE_IP_DMC),
	SAVE_ITEM(S5P_CLKSRC_CPU),
	SAVE_ITEM(S5P_CLKDIV_CPU),
	SAVE_ITEM(S5P_CLKGATE_SCLKCPU),
	SAVE_ITEM(S5P_CLKGATE_IP_CPU),
	/* GIC side */
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x000),
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x004),
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x008),
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x00C),
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x014),
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x018),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x000),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x004),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x100),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x104),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x108),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x300),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x304),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x308),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x400),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x404),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x408),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x40C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x410),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x414),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x418),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x41C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x420),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x424),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x428),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x42C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x430),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x434),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x438),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x43C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x440),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x444),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x448),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x44C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x450),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x454),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x458),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x45C),

	SAVE_ITEM(S5P_VA_GIC_DIST + 0x800),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x804),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x808),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x80C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x810),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x814),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x818),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x81C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x820),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x824),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x828),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x82C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x830),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x834),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x838),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x83C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x840),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x844),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x848),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x84C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x850),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x854),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x858),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x85C),

	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC00),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC04),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC08),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC0C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC10),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC14),

	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x000),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x010),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x020),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x030),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x040),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x050),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x060),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x070),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x080),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x090),
};

static struct sleep_save exynos4_l2cc_save[] = {
	SAVE_ITEM(S5P_VA_L2CC + L2X0_TAG_LATENCY_CTRL),
	SAVE_ITEM(S5P_VA_L2CC + L2X0_DATA_LATENCY_CTRL),
	SAVE_ITEM(S5P_VA_L2CC + L2X0_PREFETCH_CTRL),
	SAVE_ITEM(S5P_VA_L2CC + L2X0_POWER_CTRL),
	SAVE_ITEM(S5P_VA_L2CC + L2X0_AUX_CTRL),
};

void exynos4_cpu_suspend(void)
{
	unsigned long tmp;
	unsigned long mask = 0xFFFFFFFF;

	/* Setting Central Sequence Register for power down mode */

	tmp = __raw_readl(S5P_CENTRAL_SEQ_CONFIGURATION);
	tmp &= ~(S5P_CENTRAL_LOWPWR_CFG);
	__raw_writel(tmp, S5P_CENTRAL_SEQ_CONFIGURATION);

	/* Setting Central Sequence option Register */

	tmp = __raw_readl(S5P_CENTRAL_SEQ_OPTION);
	tmp &= ~(S5P_USE_MASK);
	tmp |= S5P_USE_STANDBY_WFI0;
	__raw_writel(tmp, S5P_CENTRAL_SEQ_OPTION);

	/* Clear all interrupt pending to avoid early wakeup */

	__raw_writel(mask, (S5P_VA_GIC_DIST + 0x280));
	__raw_writel(mask, (S5P_VA_GIC_DIST + 0x284));
	__raw_writel(mask, (S5P_VA_GIC_DIST + 0x288));

	/* Disable all interrupt */

	__raw_writel(0x0, (S5P_VA_GIC_CPU + 0x000));
	__raw_writel(0x0, (S5P_VA_GIC_DIST + 0x000));
	__raw_writel(mask, (S5P_VA_GIC_DIST + 0x184));
	__raw_writel(mask, (S5P_VA_GIC_DIST + 0x188));

	outer_flush_all();

	/* issue the standby signal into the pm unit. */
	cpu_do_idle();

	/* we should never get past here */
	panic("sleep resumed to originator?");
}

static void exynos4_pm_prepare(void)
{
	u32 tmp;

	s3c_pm_do_save(exynos4_core_save, ARRAY_SIZE(exynos4_core_save));
	s3c_pm_do_save(exynos4_l2cc_save, ARRAY_SIZE(exynos4_l2cc_save));

	tmp = __raw_readl(S5P_INFORM1);

	/* Set value of power down register for sleep mode */

	s3c_pm_do_restore_core(exynos4_sleep, ARRAY_SIZE(exynos4_sleep));
	__raw_writel(S5P_CHECK_SLEEP, S5P_INFORM1);

	/* ensure at least INFORM0 has the resume address */

	__raw_writel(virt_to_phys(s3c_cpu_resume), S5P_INFORM0);

	/* Before enter central sequence mode, clock src register have to set */

	s3c_pm_do_restore_core(exynos4_set_clksrc, ARRAY_SIZE(exynos4_set_clksrc));

}

static int exynos4_pm_add(struct sys_device *sysdev)
{
	pm_cpu_prep = exynos4_pm_prepare;
	pm_cpu_sleep = exynos4_cpu_suspend;

	return 0;
}

/* This function copy from linux/arch/arm/kernel/smp_scu.c */

void exynos4_scu_enable(void __iomem *scu_base)
{
	u32 scu_ctrl;

	scu_ctrl = __raw_readl(scu_base);
	/* already enabled? */
	if (scu_ctrl & 1)
		return;

	scu_ctrl |= 1;
	__raw_writel(scu_ctrl, scu_base);

	/*
	 * Ensure that the data accessed by CPU0 before the SCU was
	 * initialised is visible to the other CPUs.
	 */
	flush_cache_all();
}

static struct sysdev_driver exynos4_pm_driver = {
	.add		= exynos4_pm_add,
};

static __init int exynos4_pm_drvinit(void)
{
	unsigned int tmp;

	s3c_pm_init();

	/* All wakeup disable */

	tmp = __raw_readl(S5P_WAKEUP_MASK);
	tmp |= ((0xFF << 8) | (0x1F << 1));
	__raw_writel(tmp, S5P_WAKEUP_MASK);

	return sysdev_driver_register(&exynos4_sysclass, &exynos4_pm_driver);
}
arch_initcall(exynos4_pm_drvinit);

static void exynos4_pm_resume(void)
{
	/* For release retention */

	__raw_writel((1 << 28), S5P_PAD_RET_MAUDIO_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_GPIO_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_UART_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_MMCA_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_MMCB_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_EBIA_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_EBIB_OPTION);

	s3c_pm_do_restore_core(exynos4_core_save, ARRAY_SIZE(exynos4_core_save));

	exynos4_scu_enable(S5P_VA_SCU);

#ifdef CONFIG_CACHE_L2X0
	s3c_pm_do_restore_core(exynos4_l2cc_save, ARRAY_SIZE(exynos4_l2cc_save));
	outer_inv_all();
	/* enable L2X0*/
	writel_relaxed(1, S5P_VA_L2CC + L2X0_CTRL);
#endif
}

static struct syscore_ops exynos4_pm_syscore_ops = {
	.resume		= exynos4_pm_resume,
};

static __init int exynos4_pm_syscore_init(void)
{
	register_syscore_ops(&exynos4_pm_syscore_ops);
	return 0;
}
arch_initcall(exynos4_pm_syscore_init);
