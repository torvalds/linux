/* linux/arch/arm/mach-exynos/cpuidle.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/time.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/suspend.h>

#include <asm/proc-fns.h>
#include <asm/smp_scu.h>
#include <asm/suspend.h>
#include <asm/unified.h>
#include <asm/cputype.h>
#include <asm/cacheflush.h>
#include <asm/system_misc.h>
#include <asm/tlbflush.h>

#include <mach/regs-pmu.h>
#include <mach/regs-clock.h>
#include <mach/pmu.h>
#include <mach/smc.h>
#include <mach/asv.h>

#include <plat/pm.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/regs-serial.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-core.h>
#include <plat/usb-phy.h>

#ifdef CONFIG_ARM_TRUSTZONE
#define REG_DIRECTGO_ADDR	(soc_is_exynos4210() ? samsung_rev() == EXYNOS4210_REV_1_1 ? \
			EXYNOS_INFORM7 : (samsung_rev() == EXYNOS4210_REV_1_0 ? \
			(S5P_VA_SYSRAM_NS + 0x24) : EXYNOS_INFORM0) : (S5P_VA_SYSRAM_NS + 0x24))
#define REG_DIRECTGO_FLAG	(soc_is_exynos4210() ? samsung_rev() == EXYNOS4210_REV_1_1 ? \
			EXYNOS_INFORM6 : (samsung_rev() == EXYNOS4210_REV_1_0 ? \
			(S5P_VA_SYSRAM_NS + 0x20) : EXYNOS_INFORM1) : (S5P_VA_SYSRAM_NS + 0x20))
#else
#define REG_DIRECTGO_ADDR	(samsung_rev() == EXYNOS4210_REV_1_1 ? \
			EXYNOS_INFORM7 : (samsung_rev() == EXYNOS4210_REV_1_0 ? \
			(S5P_VA_SYSRAM + 0x24) : EXYNOS_INFORM0))
#define REG_DIRECTGO_FLAG	(samsung_rev() == EXYNOS4210_REV_1_1 ? \
			EXYNOS_INFORM6 : (samsung_rev() == EXYNOS4210_REV_1_0 ? \
			(S5P_VA_SYSRAM + 0x20) : EXYNOS_INFORM1))
#endif

#define EXYNOS_GPIO_END		(soc_is_exynos5410() ? \
					EXYNOS5410_GPIO_END : EXYNOS4_GPIO_END)
#define EXYNOS_CHECK_DIRECTGO		0xFCBA0D10

extern unsigned int scu_save[2];

static int exynos_enter_idle(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			      int index);
static int exynos_enter_lowpower(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index);
static int exynos5410_enter_lowpower(struct cpuidle_device *dev,
				 struct cpuidle_driver *drv,
				 int index);

struct check_reg_lpa {
	void __iomem	*check_reg;
	unsigned int	check_bit;
};

/*
 * List of check power domain list for LPA mode
 * These register are have to power off to enter LPA mode
 */
static struct check_reg_lpa exynos5_power_domain[] = {
	{.check_reg = EXYNOS5_GSCL_STATUS,	.check_bit = 0x7},
	{.check_reg = EXYNOS5_ISP_STATUS,	.check_bit = 0x7},
	{.check_reg = EXYNOS5410_G3D_STATUS,	.check_bit = 0x7},
	{.check_reg = EXYNOS5410_MFC_STATUS,	.check_bit = 0x7},
};

/*
 * List of check clock gating list for LPA mode
 * If clock of list is not gated, system can not enter LPA mode.
 */
static struct check_reg_lpa exynos5_clock_gating[] = {
	{.check_reg = EXYNOS5_CLKGATE_IP_DISP1,		.check_bit = 0x00000008},
	{.check_reg = EXYNOS5_CLKGATE_IP_MFC,		.check_bit = 0x00000001},
	{.check_reg = EXYNOS5_CLKGATE_IP_GEN,		.check_bit = 0x00004016},
	{.check_reg = EXYNOS5_CLKGATE_BUS_FSYS0,	.check_bit = 0x00000002},
	{.check_reg = EXYNOS5_CLKGATE_IP_PERIC,		.check_bit = 0x003F7FC0},
};

#if defined(CONFIG_EXYNOS_DEV_DWMCI)
enum hc_type {
	HC_SDHC,
	HC_MSHC,
};

struct check_device_op {
	void __iomem		*base;
	struct platform_device	*pdev;
	enum hc_type		type;
};

#if defined(CONFIG_ARCH_EXYNOS4)
static struct check_device_op chk_sdhc_op[] = {
#if defined(CONFIG_MMC_DW)
	{.base = 0, .pdev = &exynos4_device_dwmci, .type = HC_MSHC},
#endif
#if defined(CONFIG_S5P_DEV_MSHC)
	{.base = 0, .pdev = &s3c_device_mshci, .type = HC_MSHC},
#endif
#if defined(CONFIG_S3C_DEV_HSMMC)
	{.base = 0, .pdev = &s3c_device_hsmmc0, .type = HC_SDHC},
#endif
#if defined(CONFIG_S3C_DEV_HSMMC1)
	{.base = 0, .pdev = &s3c_device_hsmmc1, .type = HC_SDHC},
#endif
#if defined(CONFIG_S3C_DEV_HSMMC2)
	{.base = 0, .pdev = &s3c_device_hsmmc2, .type = HC_SDHC},
#endif
#if defined(CONFIG_S3C_DEV_HSMMC3)
	{.base = 0, .pdev = &s3c_device_hsmmc3, .type = HC_SDHC},
#endif
};
#else
static struct check_device_op chk_sdhc_op[] = {
	{.base = 0, .pdev = &exynos5_device_dwmci0, .type = HC_MSHC},
	{.base = 0, .pdev = &exynos5_device_dwmci1, .type = HC_MSHC},
	{.base = 0, .pdev = &exynos5_device_dwmci2, .type = HC_MSHC},
};
#endif

#define S3C_HSMMC_PRNSTS	(0x24)
#define S3C_HSMMC_CLKCON	(0x2c)
#define S3C_HSMMC_CMD_INHIBIT	0x00000001
#define S3C_HSMMC_DATA_INHIBIT	0x00000002
#define S3C_HSMMC_CLOCK_CARD_EN	0x0004

#define MSHCI_CLKENA	(0x10)  /* Clock enable */
#define MSHCI_STATUS	(0x48)  /* Status */
#define MSHCI_DATA_BUSY	(0x1<<9)
#define MSHCI_DATA_STAT_BUSY	(0x1<<10)
#define MSHCI_ENCLK	(0x1)

static int sdmmc_dev_num;
/* If SD/MMC interface is working: return = 1 or not 0 */
static int check_sdmmc_op(unsigned int ch)
{
	unsigned int reg1;
	void __iomem *base_addr;
	if (soc_is_exynos5410()) {
		if (unlikely(ch >= sdmmc_dev_num)) {
			pr_err("Invalid ch[%d] for SD/MMC\n", ch);
			return 0;
		}

		return (__raw_readl(EXYNOS5_CLKSRC_MASK_FSYS) & (1 << (ch * 4))) ? 1 : 0;
	} else {
		unsigned int reg2;
		if (unlikely(ch >= sdmmc_dev_num)) {
			printk(KERN_ERR "Invalid ch[%d] for SD/MMC\n", ch);
			return 0;
		}

		if (chk_sdhc_op[ch].type == HC_SDHC) {
			base_addr = chk_sdhc_op[ch].base;
			/* Check CLKCON [2]: ENSDCLK */
			reg2 = readl(base_addr + S3C_HSMMC_CLKCON);
			return !!(reg2 & (S3C_HSMMC_CLOCK_CARD_EN));
		} else if (chk_sdhc_op[ch].type == HC_MSHC) {
			base_addr = chk_sdhc_op[ch].base;
			/* Check STATUS [9] for data busy */
			reg1 = readl(base_addr + MSHCI_STATUS);
			return (reg1 & (MSHCI_DATA_BUSY)) ||
				(reg1 & (MSHCI_DATA_STAT_BUSY));
		}
	}
	/* should not be here */
	return 0;
}

/* Check all sdmmc controller */
static int loop_sdmmc_check(void)
{
	unsigned int iter;

	for (iter = 0; iter < sdmmc_dev_num; iter++) {
		if (check_sdmmc_op(iter)) {
			pr_debug("SDMMC [%d] working\n", iter);
			return 1;
		}
	}
	return 0;
}
#endif

static int exynos_check_reg_status(struct check_reg_lpa *reg_list,
				    unsigned int list_cnt)
{
	unsigned int i;
	unsigned int tmp;

	for (i = 0; i < list_cnt; i++) {
		tmp = __raw_readl(reg_list[i].check_reg);
		if (tmp & reg_list[i].check_bit)
			return -EBUSY;
	}

	return 0;
}

static int exynos_uart_fifo_check(void)
{
	unsigned int ret;
	unsigned int check_val;

	ret = 0;

	/* Check UART for console is empty */
	check_val = __raw_readl(S5P_VA_UART(CONFIG_S3C_LOWLEVEL_UART_PORT) +
				0x18);

	ret = ((check_val >> 16) & 0xff);

	return ret;
}

static int check_power_domain(void)
{
	unsigned long tmp;

	tmp = __raw_readl(EXYNOS4_LCD0_CONFIGURATION);
	if ((tmp & EXYNOS_INT_LOCAL_PWR_EN) == EXYNOS_INT_LOCAL_PWR_EN)
		return 1;

	/*
	 * from REV 1.1, MFC power domain can turn off
	 */
	if (((soc_is_exynos4412()) && (samsung_rev() >= EXYNOS4412_REV_1_1)) ||
	    ((soc_is_exynos4212()) && (samsung_rev() >= EXYNOS4212_REV_1_0)) ||
	     soc_is_exynos4210()) {
		tmp = __raw_readl(EXYNOS4_MFC_CONFIGURATION);
		if ((tmp & EXYNOS_INT_LOCAL_PWR_EN) == EXYNOS_INT_LOCAL_PWR_EN)
			return 1;
	}

	tmp = __raw_readl(EXYNOS4_MFC_CONFIGURATION);
	if ((tmp & EXYNOS_INT_LOCAL_PWR_EN) == EXYNOS_INT_LOCAL_PWR_EN)
		return 1;

	tmp = __raw_readl(EXYNOS4_CAM_CONFIGURATION);
	if ((tmp & EXYNOS_INT_LOCAL_PWR_EN) == EXYNOS_INT_LOCAL_PWR_EN)
		return 1;

	tmp = __raw_readl(EXYNOS4_TV_CONFIGURATION);
	if ((tmp & EXYNOS_INT_LOCAL_PWR_EN) == EXYNOS_INT_LOCAL_PWR_EN)
		return 1;

	tmp = __raw_readl(EXYNOS4_GPS_CONFIGURATION);
	if ((tmp & EXYNOS_INT_LOCAL_PWR_EN) == EXYNOS_INT_LOCAL_PWR_EN)
		return 1;

	return 0;
}

static int exynos4_check_operation(void)
{
	if (check_power_domain())
		return 1;

	return 0;
}

static int __maybe_unused exynos_check_enter_mode(void)
{
	if (soc_is_exynos5410()) {
		/* Check power domain */
		if (exynos_check_reg_status(exynos5_power_domain,
				    ARRAY_SIZE(exynos5_power_domain)))
			return EXYNOS_CHECK_DIDLE;

		/* Check clock gating */
		if (exynos_check_reg_status(exynos5_clock_gating,
				    ARRAY_SIZE(exynos5_clock_gating)))
			return EXYNOS_CHECK_DIDLE;
	} else {
		if (exynos4_check_operation())
			return EXYNOS_CHECK_DIDLE;
	}

#if defined(CONFIG_EXYNOS_DEV_DWMCI)
	if (loop_sdmmc_check())
		return EXYNOS_CHECK_DIDLE;
#endif
	if (exynos_check_usb_op())
		return EXYNOS_CHECK_DIDLE;

	return EXYNOS_CHECK_LPA;
}

static struct cpuidle_state exynos_cpuidle_set[] __initdata = {
	[0] = {
		.enter			= exynos_enter_idle,
		.exit_latency		= 1,
		.target_residency	= 10000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "C0",
		.desc			= "ARM clock gating(WFI)",
	},
	[1] = {
		.enter			= exynos_enter_lowpower,
		.exit_latency		= 300,
		.target_residency	= 10000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "C1",
		.desc			= "ARM power down",
	},
};

static struct cpuidle_state exynos5410_cpuidle_set[] __initdata = {
	[0] = {
		.enter			= exynos_enter_idle,
		.exit_latency		= 1,
		.target_residency	= 1000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "C1",
		.desc			= "ARM clock gating(WFI)",
	},
	[1] = {
		.enter			= exynos5410_enter_lowpower,
		.exit_latency		= 30,
		.target_residency	= 1000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "C2",
		.desc			= "ARM power down",
	},
	[2] = {
		.enter                  = exynos_enter_lowpower,
		.exit_latency           = 300,
		.target_residency       = 5000,
		.flags                  = CPUIDLE_FLAG_TIME_VALID,
		.name                   = "C3",
		.desc                   = "ARM power down",
	},
};

static DEFINE_PER_CPU(struct cpuidle_device, exynos_cpuidle_device);

static struct cpuidle_driver exynos_idle_driver = {
	.name		= "exynos_idle",
	.owner		= THIS_MODULE,
};

/*
 * To keep value of gpio on power down mode
 * set Power down register of gpio
 */
static void exynos_gpio_set_pd_reg(void)
{
	struct samsung_gpio_chip *target_chip;
	unsigned int gpio_nr;
	unsigned int tmp;

	for (gpio_nr = 0; gpio_nr < EXYNOS_GPIO_END; gpio_nr++) {
		target_chip = samsung_gpiolib_getchip(gpio_nr);

		if (!target_chip)
			continue;

		if (!target_chip->pm)
			continue;

		/* Keep the previous state in LPA mode */
		s5p_gpio_set_pd_cfg(gpio_nr, 0x3);

		/* Pull up-down state in LPA mode is same as normal */
		tmp = s3c_gpio_getpull(gpio_nr);
		s5p_gpio_set_pd_pull(gpio_nr, tmp);
	}
}

/* Ext-GIC nIRQ/nFIQ is the only wakeup source in AFTR */
static void exynos_set_wakeupmask(void)
{
	if (soc_is_exynos5410())
		__raw_writel(0x40003ffe, EXYNOS_WAKEUP_MASK);
	else
		__raw_writel(0x0000ff3e, EXYNOS_WAKEUP_MASK);
}

#if !defined(CONFIG_ARM_TRUSTZONE)
static unsigned int g_pwr_ctrl, g_diag_reg;

static void save_cpu_arch_register(void)
{
	/*read power control register*/
	asm("mrc p15, 0, %0, c15, c0, 0" : "=r"(g_pwr_ctrl) : : "cc");
	/*read diagnostic register*/
	asm("mrc p15, 0, %0, c15, c0, 1" : "=r"(g_diag_reg) : : "cc");
	return;
}

static void restore_cpu_arch_register(void)
{
	/*write power control register*/
	asm("mcr p15, 0, %0, c15, c0, 0" : : "r"(g_pwr_ctrl) : "cc");
	/*write diagnostic register*/
	asm("mcr p15, 0, %0, c15, c0, 1" : : "r"(g_diag_reg) : "cc");
	return;
}
#else
static void save_cpu_arch_register(void)
{
}

static void restore_cpu_arch_register(void)
{
}
#endif

static int idle_finisher(unsigned long flags)
{

#ifdef CONFIG_CACHE_L2X0
	outer_flush_all();
#endif

#if defined(CONFIG_ARM_TRUSTZONE)
	if (soc_is_exynos5410()) {
		exynos_smc(SMC_CMD_SAVE, OP_TYPE_CORE, SMC_POWERSTATE_IDLE, 0);
		exynos_smc(SMC_CMD_SHUTDOWN, OP_TYPE_CLUSTER, SMC_POWERSTATE_IDLE, 0);
	} else {
		exynos_smc(SMC_CMD_CPU0AFTR, 0, 0, 0);
	}
#else
	cpu_do_idle();
#endif
	return 1;
}

static int c2_finisher(unsigned long flags)
{
#if defined(CONFIG_ARM_TRUSTZONE)
	exynos_smc(SMC_CMD_SAVE, OP_TYPE_CORE, SMC_POWERSTATE_IDLE, 0);
	exynos_smc(SMC_CMD_SHUTDOWN, OP_TYPE_CORE, SMC_POWERSTATE_IDLE, 0);
	/*
	 * Secure monitor disables the SMP bit and takes the CPU out of the
	 * coherency domain.
	 */
	local_flush_tlb_all();
#else
	cpu_do_idle();
#endif
	return 1;
}

static int exynos_enter_core0_aftr(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	struct timeval before, after;
	int idle_time;
	unsigned long tmp, abb_val;
	unsigned int ret = 0;
	unsigned int cpuid = smp_processor_id();

	local_irq_disable();
	do_gettimeofday(&before);

	exynos_set_wakeupmask();

	/* Set value of power down register for aftr mode */
	exynos_sys_powerdown_conf(SYS_AFTR);

	__raw_writel(virt_to_phys(s3c_cpu_resume), REG_DIRECTGO_ADDR);
	__raw_writel(EXYNOS_CHECK_DIRECTGO, REG_DIRECTGO_FLAG);

	if (!soc_is_exynos5410()) {
		exynos_reset_assert_ctrl(0);

		abb_val = exynos4x12_get_abb_member(ABB_ARM);
		exynos4x12_set_abb_member(ABB_ARM, ABB_MODE_085V);
	}

	if (soc_is_exynos5410())
		exynos_disable_idle_clock_down(KFC);

	save_cpu_arch_register();

	/* Setting Central Sequence Register for power down mode */
	tmp = __raw_readl(EXYNOS_CENTRAL_SEQ_CONFIGURATION);
	tmp &= ~EXYNOS_CENTRAL_LOWPWR_CFG;
	__raw_writel(tmp, EXYNOS_CENTRAL_SEQ_CONFIGURATION);

	if (soc_is_exynos5410())
		set_boot_flag(cpuid, C2_STATE);

	cpu_pm_enter();

	ret = cpu_suspend(0, idle_finisher);
	if (ret) {
		tmp = __raw_readl(EXYNOS_CENTRAL_SEQ_CONFIGURATION);
		tmp |= EXYNOS_CENTRAL_LOWPWR_CFG;
		__raw_writel(tmp, EXYNOS_CENTRAL_SEQ_CONFIGURATION);
	}

#ifdef CONFIG_SMP
#if !defined(CONFIG_ARM_TRUSTZONE)
	scu_enable(S5P_VA_SCU);
#endif
#endif
	if (soc_is_exynos5410())
		clear_boot_flag(cpuid, C2_STATE);

	cpu_pm_exit();

	restore_cpu_arch_register();

	if (soc_is_exynos5410())
		exynos_enable_idle_clock_down(KFC);

	/* Clear wakeup state register */
	__raw_writel(0x0, EXYNOS_WAKEUP_STAT);

	if (!soc_is_exynos5410())
		exynos_reset_assert_ctrl(1);

	do_gettimeofday(&after);

	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
		    (after.tv_usec - before.tv_usec);

	dev->last_residency = idle_time;
	return index;
}

static struct sleep_save exynos4_lpa_save[] = {
	/* CMU side */
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_TOP),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_CAM),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_TV),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_LCD0),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_LCD1),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_MAUDIO),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_FSYS),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_PERIL0),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_PERIL1),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_DMC),
};

static struct sleep_save exynos4_set_clksrc[] = {
	{ .reg = EXYNOS4_CLKSRC_MASK_TOP	, .val = 0x00000001, },
	{ .reg = EXYNOS4_CLKSRC_MASK_CAM	, .val = 0x11111111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_TV		, .val = 0x00000111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_LCD0	, .val = 0x00001111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_MAUDIO	, .val = 0x00000001, },
	{ .reg = EXYNOS4_CLKSRC_MASK_FSYS	, .val = 0x01011111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_PERIL0	, .val = 0x01111111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_PERIL1	, .val = 0x01110111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_DMC	, .val = 0x00010000, },
};

static struct sleep_save exynos5_lpa_save[] = {
	/* CMU side */
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_TOP),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_GSCL),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_DISP1_0),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_MAUDIO),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_FSYS),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_PERIC0),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_PERIC1),
	SAVE_ITEM(EXYNOS5_CLKSRC_TOP3),
};

static struct sleep_save exynos5_set_clksrc[] = {
	{ .reg = EXYNOS5_CLKSRC_MASK_TOP		, .val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_GSCL		, .val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_DISP1_0		, .val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_MAUDIO		, .val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_FSYS		, .val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_PERIC0		, .val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_PERIC1		, .val = 0xffffffff, },
};

static int exynos_enter_core0_lpa(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	struct timeval before, after;
	int idle_time, ret = 0;
	unsigned long tmp, abb_val;
	unsigned int cpuid = smp_processor_id();

	/*
	 * Before enter central sequence mode, clock src register have to set
	 */
	if (soc_is_exynos5410()) {
		s3c_pm_do_save(exynos5_lpa_save, ARRAY_SIZE(exynos5_lpa_save));
		s3c_pm_do_restore_core(exynos5_set_clksrc,
				       ARRAY_SIZE(exynos5_set_clksrc));
	} else {
		s3c_pm_do_save(exynos4_lpa_save, ARRAY_SIZE(exynos4_lpa_save));
		s3c_pm_do_restore_core(exynos4_set_clksrc,
					ARRAY_SIZE(exynos4_set_clksrc));
	}

	local_irq_disable();
	do_gettimeofday(&before);

	/*
	 * Unmasking all wakeup source.
	 */
	if (soc_is_exynos5410())
		__raw_writel(0x7FFFE000, EXYNOS_WAKEUP_MASK);
	else
		__raw_writel(0x3FF0000, EXYNOS_WAKEUP_MASK);

	/* Configure GPIO Power down control register */
	exynos_gpio_set_pd_reg();

	__raw_writel(virt_to_phys(s3c_cpu_resume), REG_DIRECTGO_ADDR);
	__raw_writel(EXYNOS_CHECK_DIRECTGO, REG_DIRECTGO_FLAG);

	if (!soc_is_exynos5410()) {
		exynos_reset_assert_ctrl(0);

		abb_val = exynos4x12_get_abb_member(ABB_ARM);
		exynos4x12_set_abb_member(ABB_ARM, ABB_MODE_085V);
	}

	/* Set value of power down register for aftr mode */
	exynos_sys_powerdown_conf(SYS_LPA);

	if (soc_is_exynos5410())
		exynos_disable_idle_clock_down(KFC);

	save_cpu_arch_register();

	/* Setting Central Sequence Register for power down mode */
	tmp = __raw_readl(EXYNOS_CENTRAL_SEQ_CONFIGURATION);
	tmp &= ~EXYNOS_CENTRAL_LOWPWR_CFG;
	__raw_writel(tmp, EXYNOS_CENTRAL_SEQ_CONFIGURATION);

	do {
		/* Waiting for flushing UART fifo */
	} while (exynos_uart_fifo_check());

	if (soc_is_exynos5410())
		set_boot_flag(cpuid, C2_STATE);

	cpu_pm_enter();

	ret = cpu_suspend(0, idle_finisher);
	if (ret) {
		tmp = __raw_readl(EXYNOS_CENTRAL_SEQ_CONFIGURATION);
		tmp |= EXYNOS_CENTRAL_LOWPWR_CFG;
		__raw_writel(tmp, EXYNOS_CENTRAL_SEQ_CONFIGURATION);

		goto early_wakeup;
	}

#ifdef CONFIG_SMP
#if !defined(CONFIG_ARM_TRUSTZONE)
	scu_enable(S5P_VA_SCU);
#endif
#endif
	/* For release retention */
	if (!soc_is_exynos5410()) {
		__raw_writel((1 << 28), EXYNOS_PAD_RET_GPIO_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_UART_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_MMCA_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_MMCB_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_EBIA_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_EBIB_OPTION);
	} else {
		__raw_writel((1 << 28), EXYNOS5410_PAD_RET_GPIO_OPTION);
		__raw_writel((1 << 28), EXYNOS5410_PAD_RET_UART_OPTION);
		__raw_writel((1 << 28), EXYNOS5410_PAD_RET_MMCA_OPTION);
		__raw_writel((1 << 28), EXYNOS5410_PAD_RET_MMCB_OPTION);
		__raw_writel((1 << 28), EXYNOS5410_PAD_RET_MMCC_OPTION);
		__raw_writel((1 << 28), EXYNOS5410_PAD_RET_SPI_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_EBIA_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_EBIB_OPTION);
	}

early_wakeup:
	if (soc_is_exynos5410())
		clear_boot_flag(cpuid, C2_STATE);

	cpu_pm_exit();

	restore_cpu_arch_register();

	if (soc_is_exynos5410())
		exynos_enable_idle_clock_down(KFC);

	if (soc_is_exynos5410())
		s3c_pm_do_restore_core(exynos5_lpa_save,
				       ARRAY_SIZE(exynos5_lpa_save));
	else {
		s3c_pm_do_restore_core(exynos4_lpa_save,
			       ARRAY_SIZE(exynos4_lpa_save));
		exynos_reset_assert_ctrl(1);
	}

	/* Clear wakeup state register */
	__raw_writel(0x0, EXYNOS_WAKEUP_STAT);

	do_gettimeofday(&after);

	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
		    (after.tv_usec - before.tv_usec);

	dev->last_residency = idle_time;
	return index;
}

static int exynos_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	struct timeval before, after;
	int idle_time;

	local_irq_disable();
	do_gettimeofday(&before);

	cpu_do_idle();

	do_gettimeofday(&after);
	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
		    (after.tv_usec - before.tv_usec);

	dev->last_residency = idle_time;
	return index;
}

static unsigned int exynos_get_core_num(void)
{
	unsigned int cluster_id = read_cpuid_mpidr() & 0x100;
	unsigned int pwr_offset = 0;
	unsigned int cpu;
	unsigned int tmp;

	struct cpumask cpu_power_on_mask;

	cpumask_clear(&cpu_power_on_mask);

	if (samsung_rev() < EXYNOS5410_REV_1_0) {
		if (cluster_id == 0)
			pwr_offset = 4;
	} else {
		if (cluster_id != 0)
			pwr_offset = 4;
	}

	for (cpu = 0; cpu < num_possible_cpus(); cpu++) {
		tmp = __raw_readl(EXYNOS_ARM_CORE_STATUS(cpu + pwr_offset));

		if (tmp & EXYNOS_CORE_LOCAL_PWR_EN)
			cpumask_set_cpu(cpu, &cpu_power_on_mask);
	}

	return cpumask_weight(&cpu_power_on_mask);
}

static int exynos_enter_lowpower(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	int new_index = index;

	if (!soc_is_exynos5410())
		__raw_writel((EXYNOS4_USE_STANDBY_WFI0 |
				EXYNOS4_USE_STANDBY_WFE0),
				EXYNOS_CENTRAL_SEQ_OPTION);

	/* This mode only can be entered when other core's are offline */
	if (num_online_cpus() > 1) {
		if (soc_is_exynos5410())
			return exynos5410_enter_lowpower(dev, drv, (new_index - 1));
		else
			return exynos_enter_idle(dev, drv, 0);
	}

	if (soc_is_exynos5410() && (exynos_get_core_num() > 1))
		return exynos5410_enter_lowpower(dev, drv, (new_index - 1));

	if (exynos_check_enter_mode() == EXYNOS_CHECK_DIDLE)
		return exynos_enter_core0_aftr(dev, drv, new_index);
	else
		return exynos_enter_core0_lpa(dev, drv, new_index);
}

static int exynos5410_enter_lowpower(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	struct timeval before, after;
	int idle_time, ret = 0;
	unsigned int cpuid = smp_processor_id(), cpu_offset = 0;
	unsigned int cluster_id = read_cpuid(CPUID_MPIDR) >> 8 & 0xf;
	unsigned int value;

	local_irq_disable();
	do_gettimeofday(&before);

	__raw_writel(virt_to_phys(s3c_cpu_resume), REG_DIRECTGO_ADDR);
	__raw_writel(EXYNOS_CHECK_DIRECTGO, REG_DIRECTGO_FLAG);

	set_boot_flag(cpuid, C2_STATE);
	cpu_pm_enter();

	if (samsung_rev() < EXYNOS5410_REV_1_0) {
		if (cluster_id == 0)
			cpu_offset = cpuid + 4;
		else
			cpu_offset = cpuid;
	} else {
		if (cluster_id == 0)
			cpu_offset = cpuid;
		else
			cpu_offset = cpuid + 4;
	}
	__raw_writel(0x0, EXYNOS_ARM_CORE_CONFIGURATION(cpu_offset));

	value = __raw_readl(EXYNOS5410_ARM_INTR_SPREAD_ENABLE);
	value &= ~(0x1 << cpu_offset);
	__raw_writel(value, EXYNOS5410_ARM_INTR_SPREAD_ENABLE);

	ret = cpu_suspend(0, c2_finisher);
	if (ret)
		__raw_writel(0x3, EXYNOS_ARM_CORE_CONFIGURATION(cpu_offset));

	value = __raw_readl(EXYNOS5410_ARM_INTR_SPREAD_ENABLE);
	value |= (0x1 << cpu_offset);
	__raw_writel(value, EXYNOS5410_ARM_INTR_SPREAD_ENABLE);

	clear_boot_flag(cpuid, C2_STATE);
	cpu_pm_exit();

	do_gettimeofday(&after);
	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
		    (after.tv_usec - before.tv_usec);

	dev->last_residency = idle_time;
	return index;
}

void exynos_enable_idle_clock_down(unsigned int cluster)
{
	unsigned int tmp;

	if (cluster) {
		/* For A15 core */
		tmp = __raw_readl(EXYNOS5_PWR_CTRL1);
		tmp &= ~((0x7 << 28) | (0x7 << 16) | (1 << 9) | (1 << 8));
		tmp |= (0x7 << 28) | (0x7 << 16) | 0x3ff;
		__raw_writel(tmp, EXYNOS5_PWR_CTRL1);

		tmp = __raw_readl(EXYNOS5_PWR_CTRL2);
		tmp &= ~((0x3 << 24) | (0xffff << 8) | (0x77));
		tmp |= (1 << 16) | (1 << 8) | (1 << 4) | (1 << 0);
		tmp |= (1 << 25) | (1 << 24);
		__raw_writel(tmp, EXYNOS5_PWR_CTRL2);
	} else {
		/* For A7 core */
		tmp = __raw_readl(EXYNOS5_PWR_CTRL_KFC);
		tmp &= ~((0x7 << 16) | (1 << 8));
		tmp |= (0x7 << 16) | 0x1ff;
		__raw_writel(tmp, EXYNOS5_PWR_CTRL_KFC);

		tmp = __raw_readl(EXYNOS5_PWR_CTRL2_KFC);
		tmp &= ~((0x1 << 24) | (0xffff << 8) | (0x7));
		tmp |= (1 << 16) | (1 << 8) | (1 << 0);
		tmp |= 1 << 24;
		__raw_writel(tmp, EXYNOS5_PWR_CTRL2_KFC);
	}

	pr_debug("%s idle clock down is enabled\n", cluster ? "ARM" : "KFC");
}

void exynos_disable_idle_clock_down(unsigned int cluster)
{
	unsigned int tmp;

	if (cluster) {
		/* For A15 core */
		tmp = __raw_readl(EXYNOS5_PWR_CTRL1);
		tmp &= ~((0x7 << 28) | (0x7 << 16) | (1 << 9) | (1 << 8));
		__raw_writel(tmp, EXYNOS5_PWR_CTRL1);

		tmp = __raw_readl(EXYNOS5_PWR_CTRL2);
		tmp &= ~((0x3 << 24) | (0xffff << 8) | (0x77));
		__raw_writel(tmp, EXYNOS5_PWR_CTRL2);
	} else {
		/* For A7 core */
		tmp = __raw_readl(EXYNOS5_PWR_CTRL_KFC);
		tmp &= ~((0x7 << 16) | (1 << 8));
		__raw_writel(tmp, EXYNOS5_PWR_CTRL_KFC);

		tmp = __raw_readl(EXYNOS5_PWR_CTRL2_KFC);
		tmp &= ~((0x1 << 24) | (0xffff << 8) | (0x7));
		__raw_writel(tmp, EXYNOS5_PWR_CTRL2_KFC);
	}

	pr_debug("%s idle clock down is disabled\n", cluster ? "ARM" : "KFC");
}

static int exynos_cpuidle_notifier_event(struct notifier_block *this,
					  unsigned long event,
					  void *ptr)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		disable_hlt();
		pr_debug("PM_SUSPEND_PREPARE for CPUIDLE\n");
		return NOTIFY_OK;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		enable_hlt();
		pr_debug("PM_POST_SUSPEND for CPUIDLE\n");
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static struct notifier_block exynos_cpuidle_notifier = {
	.notifier_call = exynos_cpuidle_notifier_event,
};

static int __init exynos_init_cpuidle(void)
{
	int i, max_cpuidle_state, cpu_id;
	struct cpuidle_device *device;
	struct cpuidle_driver *drv = &exynos_idle_driver;
	struct cpuidle_state *idle_set;
	struct platform_device *pdev;
	struct resource *res;

	if (soc_is_exynos5410()) {
		exynos_enable_idle_clock_down(ARM);
		exynos_enable_idle_clock_down(KFC);
	}

	/* Setup cpuidle driver */
	if (soc_is_exynos5410()) {
		idle_set = exynos5410_cpuidle_set;
		drv->state_count = ARRAY_SIZE(exynos5410_cpuidle_set);
	} else {
		idle_set = exynos_cpuidle_set;
		drv->state_count = ARRAY_SIZE(exynos_cpuidle_set);
	}

	max_cpuidle_state = drv->state_count;
	for (i = 0; i < max_cpuidle_state; i++) {
		memcpy(&drv->states[i], &idle_set[i],
				sizeof(struct cpuidle_state));
	}
	drv->safe_state_index = 0;
	cpuidle_register_driver(&exynos_idle_driver);

	for_each_cpu(cpu_id, cpu_online_mask) {
		device = &per_cpu(exynos_cpuidle_device, cpu_id);
		device->cpu = cpu_id;

	if (cpu_id != 0 && !soc_is_exynos5410())
		device->state_count = 1;
	else
		 device->state_count = max_cpuidle_state;

	if (cpuidle_register_device(device)) {
		printk(KERN_ERR "CPUidle register device failed\n,");
		return -EIO;
		}
	}

#if defined(CONFIG_EXYNOS_DEV_DWMCI)
	sdmmc_dev_num = ARRAY_SIZE(chk_sdhc_op);

	for (i = 0; i < sdmmc_dev_num; i++) {

		pdev = chk_sdhc_op[i].pdev;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res) {
			pr_err("failed to get iomem region\n");
			return -EINVAL;
		}

		chk_sdhc_op[i].base = ioremap(res->start, resource_size(res));

		if (!chk_sdhc_op[i].base) {
			pr_err("failed to map io region\n");
			return -EINVAL;
		}
	}
#endif
	if (soc_is_exynos4412()) {
		/* Save register value for SCU */
		scu_save[0] = __raw_readl(S5P_VA_SCU + 0x30);
		scu_save[1] = __raw_readl(S5P_VA_SCU + 0x0);

		flush_cache_all();
		outer_clean_range(virt_to_phys(scu_save), ARRAY_SIZE(scu_save));
	}

	register_pm_notifier(&exynos_cpuidle_notifier);

	return 0;
}
device_initcall(exynos_init_cpuidle);
