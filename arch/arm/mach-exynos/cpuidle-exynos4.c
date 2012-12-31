/* linux/arch/arm/mach-exynos/cpuidle-exynos4.c
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
#include <linux/io.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <asm/proc-fns.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

#include <mach/regs-clock.h>
#include <mach/regs-pmu.h>
#include <mach/pmu.h>
#include <mach/gpio.h>
#include <mach/smc.h>
#include <mach/clock-domain.h>
#include <mach/regs-audss.h>
#include <mach/asv.h>
#include <mach/regs-usb-phy.h>

#include <plat/regs-otg.h>
#include <plat/exynos4.h>
#include <plat/pm.h>
#include <plat/devs.h>
#include <plat/cpu.h>

#ifdef CONFIG_ARM_TRUSTZONE
#define REG_DIRECTGO_ADDR	(S5P_VA_SYSRAM_NS + 0x24)
#define REG_DIRECTGO_FLAG	(S5P_VA_SYSRAM_NS + 0x20)
#else
#define REG_DIRECTGO_ADDR	(samsung_rev() != EXYNOS4210_REV_1_1 ?\
				(S5P_VA_SYSRAM + 0x24) : S5P_INFORM7)
#define REG_DIRECTGO_FLAG	(samsung_rev() != EXYNOS4210_REV_1_1 ?\
				(S5P_VA_SYSRAM + 0x20) : S5P_INFORM6)
#endif

extern unsigned long sys_pwr_conf_addr;
extern unsigned int l2x0_save[3];

enum hc_type {
	HC_SDHC,
	HC_MSHC,
};

enum idle_clock_down {
	HW_CLK_DWN,
	SW_CLK_DWN,
};

unsigned int use_clock_down;

struct check_device_op {
	void __iomem		*base;
	struct platform_device	*pdev;
	enum hc_type		type;
};

static struct check_device_op chk_sdhc_op[] = {
#if defined(CONFIG_EXYNOS4_DEV_DWMCI)
	{.base = 0, .pdev = &exynos_device_dwmci, .type = HC_MSHC},
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

#if defined(CONFIG_USB_S3C_OTGD) && !defined(CONFIG_USB_EXYNOS_SWITCH)
static struct check_device_op chk_usbotg_op = {
	.base = 0, .pdev = &s3c_device_usbgadget, .type = 0
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

#define GPIO_OFFSET		0x20
#define GPIO_PUD_OFFSET		0x08
#define GPIO_CON_PDN_OFFSET	0x10
#define GPIO_PUD_PDN_OFFSET	0x14
#define GPIO_END_OFFSET		0x200

static void exynos4_gpio_conpdn_reg(void)
{
	void __iomem *gpio_base = S5P_VA_GPIO;
	unsigned int val;

	do {
		/* Keep the previous state in didle mode */
		__raw_writel(0xffff, gpio_base + GPIO_CON_PDN_OFFSET);

		/* Pull up-down state in didle is same as normal */
		val = __raw_readl(gpio_base + GPIO_PUD_OFFSET);
		__raw_writel(val, gpio_base + GPIO_PUD_PDN_OFFSET);

		gpio_base += GPIO_OFFSET;

		if (gpio_base == S5P_VA_GPIO + GPIO_END_OFFSET)
			gpio_base = S5P_VA_GPIO2;

	} while (gpio_base <= S5P_VA_GPIO2 + GPIO_END_OFFSET);

	/* set the GPZ */
	gpio_base = S5P_VA_GPIO3;
	__raw_writel(0xffff, gpio_base + GPIO_CON_PDN_OFFSET);

	val = __raw_readl(gpio_base + GPIO_PUD_OFFSET);
	__raw_writel(val, gpio_base + GPIO_PUD_PDN_OFFSET);
}

static int check_power_domain(void)
{
	unsigned long tmp;

	tmp = __raw_readl(S5P_PMU_LCD0_CONF);
	if ((tmp & S5P_INT_LOCAL_PWR_EN) == S5P_INT_LOCAL_PWR_EN)
		return 1;

	/*
	 * from REV 1.1, MFC power domain can turn off
	 */
	if (((soc_is_exynos4412()) && (samsung_rev() >= EXYNOS4412_REV_1_1)) ||
	    ((soc_is_exynos4212()) && (samsung_rev() >= EXYNOS4212_REV_1_0)) ||
	     soc_is_exynos4210()) {
		tmp = __raw_readl(S5P_PMU_MFC_CONF);
		if ((tmp & S5P_INT_LOCAL_PWR_EN) == S5P_INT_LOCAL_PWR_EN)
			return 1;
	}

	tmp = __raw_readl(S5P_PMU_G3D_CONF);
	if ((tmp & S5P_INT_LOCAL_PWR_EN) == S5P_INT_LOCAL_PWR_EN)
		return 1;

	tmp = __raw_readl(S5P_PMU_CAM_CONF);
	if ((tmp & S5P_INT_LOCAL_PWR_EN) == S5P_INT_LOCAL_PWR_EN)
		return 1;

	tmp = __raw_readl(S5P_PMU_TV_CONF);
	if ((tmp & S5P_INT_LOCAL_PWR_EN) == S5P_INT_LOCAL_PWR_EN)
		return 1;

	tmp = __raw_readl(S5P_PMU_GPS_CONF);
	if ((tmp & S5P_INT_LOCAL_PWR_EN) == S5P_INT_LOCAL_PWR_EN)
		return 1;

	return 0;
}

static int __maybe_unused check_clock_gating(void)
{
	unsigned long tmp;

	tmp = __raw_readl(EXYNOS4_CLKGATE_IP_IMAGE);
	if (tmp & (EXYNOS4_CLKGATE_IP_IMAGE_MDMA | EXYNOS4_CLKGATE_IP_IMAGE_SMMUMDMA
		| EXYNOS4_CLKGATE_IP_IMAGE_QEMDMA))
		return 1;

	tmp = __raw_readl(EXYNOS4_CLKGATE_IP_FSYS);
	if (tmp & (EXYNOS4_CLKGATE_IP_FSYS_PDMA0 | EXYNOS4_CLKGATE_IP_FSYS_PDMA1))
		return 1;

	tmp = __raw_readl(EXYNOS4_CLKGATE_IP_PERIL);
	if (tmp & EXYNOS4_CLKGATE_IP_PERIL_I2C0_7)
		return 1;

	return 0;
}

static int sdmmc_dev_num;
/* If SD/MMC interface is working: return = 1 or not 0 */
static int check_sdmmc_op(unsigned int ch)
{
	unsigned int reg1, reg2;
	void __iomem *base_addr;

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
	/* should not be here */
	return 0;
}

/* Check all sdmmc controller */
static int loop_sdmmc_check(void)
{
	unsigned int iter;

	for (iter = 0; iter < sdmmc_dev_num; iter++) {
		if (check_sdmmc_op(iter)) {
			printk(KERN_DEBUG "SDMMC [%d] working\n", iter);
			return 1;
		}
	}
	return 0;
}

/*
 * Check USB Device and Host is working or not
 * USB_S3C-OTGD can check GOTGCTL register
 * GOTGCTL(0xEC000000)
 * BSesVld (Indicates the Device mode transceiver status)
 * BSesVld =	1b : B-session is valid
 *		0b : B-session is not valiid
 * USB_EXYNOS_SWITCH can check Both Host and Device status.
 */
static int check_usb_op(void)
{
#if defined(CONFIG_USB_S3C_OTGD) && !defined(CONFIG_USB_EXYNOS_SWITCH)
	void __iomem *base_addr;
	unsigned int val;

	base_addr = chk_usbotg_op.base;
	val = __raw_readl(base_addr + S3C_UDC_OTG_GOTGCTL);

	return val & (A_SESSION_VALID | B_SESSION_VALID);
#elif defined(CONFIG_USB_EXYNOS_SWITCH)
	return exynos_check_usb_op();
#else
	return 0;
#endif
}

#ifdef CONFIG_SND_SAMSUNG_RP
extern int srp_get_op_level(void);	/* By srp driver */
#endif

static int exynos4_check_operation(void)
{
	if (check_power_domain())
		return 1;

	if (clock_domain_enabled(LPA_DOMAIN))
		return 1;

	if (loop_sdmmc_check())
		return 1;

#ifdef CONFIG_SND_SAMSUNG_RP
	if (srp_get_op_level())
		return 1;
#endif
	if (check_usb_op())
		return 1;

	return 0;
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
	{ .reg = EXYNOS4_CLKSRC_MASK_TOP			, .val = 0x00000001, },
	{ .reg = EXYNOS4_CLKSRC_MASK_CAM			, .val = 0x11111111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_TV				, .val = 0x00000111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_LCD0			, .val = 0x00001111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_MAUDIO			, .val = 0x00000001, },
	{ .reg = EXYNOS4_CLKSRC_MASK_FSYS			, .val = 0x01011111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_PERIL0			, .val = 0x01111111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_PERIL1			, .val = 0x01110111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_DMC			, .val = 0x00010000, },
};

static struct sleep_save exynos4210_set_clksrc[] = {
	{ .reg = EXYNOS4_CLKSRC_MASK_LCD1			, .val = 0x00001111, },
};

static int exynos4_check_enter(void)
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

void exynos4_flush_cache(void *addr, phys_addr_t phy_ttb_base)
{
	outer_clean_range(virt_to_phys(addr - 0x40),
			  virt_to_phys(addr + 0x40));
	outer_clean_range(virt_to_phys(cpu_resume),
			  virt_to_phys(cpu_resume + 0x40));
	outer_clean_range(phy_ttb_base, phy_ttb_base + 0xffff);
	flush_cache_all();
}

static void exynos4_set_wakeupmask(void)
{
	__raw_writel(0x0000ff3e, S5P_WAKEUP_MASK);
}

static void vfp_enable(void *unused)
{
	u32 access = get_copro_access();

	/*
	 * Enable full access to VFP (cp10 and cp11)
	 */
	set_copro_access(access | CPACC_FULL(10) | CPACC_FULL(11));
}

static int exynos4_enter_core0_aftr(struct cpuidle_device *dev,
				    struct cpuidle_state *state)
{
	struct timeval before, after;
	int idle_time;
	unsigned long tmp, abb_val;

	local_irq_disable();
	do_gettimeofday(&before);

	exynos4_set_wakeupmask();

	__raw_writel(virt_to_phys(exynos4_idle_resume), REG_DIRECTGO_ADDR);
	__raw_writel(0xfcba0d10, REG_DIRECTGO_FLAG);

	/* Set value of power down register for aftr mode */
	exynos4_sys_powerdown_conf(SYS_AFTR);

	if (!soc_is_exynos4210())
		exynos4_reset_assert_ctrl(0);

	if (!soc_is_exynos4210()) {
		abb_val = exynos4x12_get_abb_member(ABB_ARM);
		exynos4x12_set_abb_member(ABB_ARM, ABB_MODE_085V);
	}
	if (exynos4_enter_lp(0, PLAT_PHYS_OFFSET - PAGE_OFFSET) == 0) {

		/*
		 * Clear Central Sequence Register in exiting early wakeup
		 */
		tmp = __raw_readl(S5P_CENTRAL_SEQ_CONFIGURATION);
		tmp |= (S5P_CENTRAL_LOWPWR_CFG);
		__raw_writel(tmp, S5P_CENTRAL_SEQ_CONFIGURATION);

		goto early_wakeup;
	}
	flush_tlb_all();

	cpu_init();

	vfp_enable(NULL);

early_wakeup:
#ifdef CONFIG_EXYNOS4_CPUFREQ
	if ((exynos_result_of_asv > 1) && !soc_is_exynos4210())
		exynos4x12_set_abb_member(ABB_ARM, abb_val);
#endif
	if (!soc_is_exynos4210())
		exynos4_reset_assert_ctrl(1);

	/* Clear wakeup state register */
	__raw_writel(0x0, S5P_WAKEUP_STAT);

	do_gettimeofday(&after);

	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
		    (after.tv_usec - before.tv_usec);

	return idle_time;
}

static int exynos4_enter_core0_lpa(struct cpuidle_device *dev,
				   struct cpuidle_state *state)
{
	struct timeval before, after;
	int idle_time;
	unsigned long tmp, abb_val;

	s3c_pm_do_save(exynos4_lpa_save, ARRAY_SIZE(exynos4_lpa_save));

	/*
	 * Before enter central sequence mode, clock src register have to set
	 */
	s3c_pm_do_restore_core(exynos4_set_clksrc,
			       ARRAY_SIZE(exynos4_set_clksrc));

	if (soc_is_exynos4210())
		s3c_pm_do_restore_core(exynos4210_set_clksrc, ARRAY_SIZE(exynos4210_set_clksrc));

	local_irq_disable();

	do_gettimeofday(&before);

	/*
	 * Unmasking all wakeup source.
	 */
	__raw_writel(0x3ff0000, S5P_WAKEUP_MASK);

	/* Configure GPIO Power down control register */
	exynos4_gpio_conpdn_reg();

	/* ensure at least INFORM0 has the resume address */
	__raw_writel(virt_to_phys(exynos4_idle_resume), S5P_INFORM0);

	__raw_writel(virt_to_phys(exynos4_idle_resume), REG_DIRECTGO_ADDR);
	__raw_writel(0xfcba0d10, REG_DIRECTGO_FLAG);

	__raw_writel(S5P_CHECK_LPA, S5P_INFORM1);
	exynos4_sys_powerdown_conf(SYS_LPA);

	/* Should be fixed on EVT1 */
	if (!soc_is_exynos4210())
		exynos4_reset_assert_ctrl(0);

	do {
		/* Waiting for flushing UART fifo */
	} while (exynos4_check_enter());

	if (!soc_is_exynos4210()) {
		abb_val = exynos4x12_get_abb_member(ABB_ARM);
		exynos4x12_set_abb_member(ABB_ARM, ABB_MODE_085V);
	}

	if (exynos4_enter_lp(0, PLAT_PHYS_OFFSET - PAGE_OFFSET) == 0) {

		/*
		 * Clear Central Sequence Register in exiting early wakeup
		 */
		tmp = __raw_readl(S5P_CENTRAL_SEQ_CONFIGURATION);
		tmp |= (S5P_CENTRAL_LOWPWR_CFG);
		__raw_writel(tmp, S5P_CENTRAL_SEQ_CONFIGURATION);

		goto early_wakeup;
	}
	flush_tlb_all();

	cpu_init();

	vfp_enable(NULL);

	s3c_pm_do_restore_core(exynos4_lpa_save,
			       ARRAY_SIZE(exynos4_lpa_save));

	/* For release retention */
	__raw_writel((1 << 28), S5P_PAD_RET_GPIO_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_UART_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_MMCA_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_MMCB_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_EBIA_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_EBIB_OPTION);

early_wakeup:
#ifdef CONFIG_EXYNOS4_CPUFREQ
	if ((exynos_result_of_asv > 1) && !soc_is_exynos4210())
		exynos4x12_set_abb_member(ABB_ARM, abb_val);
#endif
	if (!soc_is_exynos4210())
		exynos4_reset_assert_ctrl(1);

	/* Clear wakeup state register */
	__raw_writel(0x0, S5P_WAKEUP_STAT);

	__raw_writel(0x0, S5P_WAKEUP_MASK);

	do_gettimeofday(&after);

	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
		    (after.tv_usec - before.tv_usec);

	return idle_time;
}

static int exynos4_enter_idle(struct cpuidle_device *dev,
			      struct cpuidle_state *state);

static int exynos4_enter_lowpower(struct cpuidle_device *dev,
				  struct cpuidle_state *state);

static struct cpuidle_state exynos4_cpuidle_set[] = {
	[0] = {
		.enter			= exynos4_enter_idle,
		.exit_latency		= 1,
		.target_residency	= 10000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "IDLE",
		.desc			= "ARM clock gating(WFI)",
	},
#ifdef CONFIG_EXYNOS4_LOWPWR_IDLE
	[1] = {
		.enter			= exynos4_enter_lowpower,
		.exit_latency		= 300,
		.target_residency	= 10000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "LOW_POWER",
		.desc			= "ARM power down",
	},
#endif
};

static DEFINE_PER_CPU(struct cpuidle_device, exynos4_cpuidle_device);

static struct cpuidle_driver exynos4_idle_driver = {
	.name		= "exynos4_idle",
	.owner		= THIS_MODULE,
};

static unsigned int cpu_core;
static unsigned int old_div;
static DEFINE_SPINLOCK(idle_lock);

static int exynos4_enter_idle(struct cpuidle_device *dev,
			      struct cpuidle_state *state)
{
	struct timeval before, after;
	int idle_time;
	int cpu;
	unsigned int tmp;

	local_irq_disable();
	do_gettimeofday(&before);

	if (use_clock_down == SW_CLK_DWN) {
		/* USE SW Clock Down */
		cpu = get_cpu();

		spin_lock(&idle_lock);
		cpu_core |= (1 << cpu);

		if ((cpu_core == 0x3) || (cpu_online(1) == 0)) {
			old_div = __raw_readl(EXYNOS4_CLKDIV_CPU);
			tmp = old_div;
			tmp |= ((0x7 << 28) | (0x7 << 0));
			__raw_writel(tmp, EXYNOS4_CLKDIV_CPU);

			do {
				tmp = __raw_readl(EXYNOS4_CLKDIV_STATCPU);
			} while (tmp & 0x10000001);

		}

		spin_unlock(&idle_lock);

		cpu_do_idle();

		spin_lock(&idle_lock);

		if ((cpu_core == 0x3) || (cpu_online(1) == 0)) {
			__raw_writel(old_div, EXYNOS4_CLKDIV_CPU);

			do {
				tmp = __raw_readl(EXYNOS4_CLKDIV_STATCPU);
			} while (tmp & 0x10000001);

		}

		cpu_core &= ~(1 << cpu);
		spin_unlock(&idle_lock);

		put_cpu();
	} else
		cpu_do_idle();

	do_gettimeofday(&after);
	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
		    (after.tv_usec - before.tv_usec);

	return idle_time;
}

static int exynos4_check_entermode(void)
{
	unsigned int ret;

	if (exynos4_check_operation())
		ret = S5P_CHECK_DIDLE;
	else
		ret = S5P_CHECK_LPA;

	return ret;
}

static int exynos4_enter_lowpower(struct cpuidle_device *dev,
				  struct cpuidle_state *state)
{
	struct cpuidle_state *new_state = state;
	unsigned int enter_mode;
	unsigned int tmp;

	/* This mode only can be entered when only Core0 is online */
	if (num_online_cpus() != 1) {
		BUG_ON(!dev->safe_state);
		new_state = dev->safe_state;
	}
	dev->last_state = new_state;

	if (!soc_is_exynos4210()) {
		tmp = S5P_USE_STANDBY_WFI0 | S5P_USE_STANDBY_WFE0;
		__raw_writel(tmp, S5P_CENTRAL_SEQ_OPTION);
	}

	if (new_state == &dev->states[0])
		return exynos4_enter_idle(dev, new_state);

	enter_mode = exynos4_check_entermode();
	if (enter_mode == S5P_CHECK_DIDLE) {
		return exynos4_enter_core0_aftr(dev, new_state);
	} else
		return exynos4_enter_core0_lpa(dev, new_state);
}

static int exynos4_cpuidle_notifier_event(struct notifier_block *this,
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

static struct notifier_block exynos4_cpuidle_notifier = {
	.notifier_call = exynos4_cpuidle_notifier_event,
};

#ifdef CONFIG_EXYNOS4_ENABLE_CLOCK_DOWN
static void __init exynos4_core_down_clk(void)
{
	unsigned int tmp;

	tmp = __raw_readl(EXYNOS4_PWR_CTRL1);

	tmp &= ~(PWR_CTRL1_CORE2_DOWN_MASK | PWR_CTRL1_CORE1_DOWN_MASK);

	/* set arm clock divider value on idle state */
	tmp |= ((0x7 << PWR_CTRL1_CORE2_DOWN_RATIO) |
		(0x7 << PWR_CTRL1_CORE1_DOWN_RATIO));

	if (soc_is_exynos4212()) {
	/* Set PWR_CTRL1 register to use clock down feature */
		tmp |= (PWR_CTRL1_DIV2_DOWN_EN |
			PWR_CTRL1_DIV1_DOWN_EN |
			PWR_CTRL1_USE_CORE1_WFE |
			PWR_CTRL1_USE_CORE0_WFE |
			PWR_CTRL1_USE_CORE1_WFI |
			PWR_CTRL1_USE_CORE0_WFI);
	} else if (soc_is_exynos4412()) {
		tmp |= (PWR_CTRL1_DIV2_DOWN_EN |
			PWR_CTRL1_DIV1_DOWN_EN |
			PWR_CTRL1_USE_CORE3_WFE |
			PWR_CTRL1_USE_CORE2_WFE |
			PWR_CTRL1_USE_CORE1_WFE |
			PWR_CTRL1_USE_CORE0_WFE |
			PWR_CTRL1_USE_CORE3_WFI |
			PWR_CTRL1_USE_CORE2_WFI |
			PWR_CTRL1_USE_CORE1_WFI |
			PWR_CTRL1_USE_CORE0_WFI);
	}

	__raw_writel(tmp, EXYNOS4_PWR_CTRL1);

	tmp = __raw_readl(EXYNOS4_PWR_CTRL2);

	tmp &= ~(PWR_CTRL2_DUR_STANDBY2_MASK | PWR_CTRL2_DUR_STANDBY1_MASK |
		PWR_CTRL2_CORE2_UP_MASK | PWR_CTRL2_CORE1_UP_MASK);

	/* set duration value on middle wakeup step */
	tmp |=  ((0x1 << PWR_CTRL2_DUR_STANDBY2) |
		 (0x1 << PWR_CTRL2_DUR_STANDBY1));

	/* set arm clock divier value on middle wakeup step */
	tmp |= ((0x1 << PWR_CTRL2_CORE2_UP_RATIO) |
		(0x1 << PWR_CTRL2_CORE1_UP_RATIO));

	/* Set PWR_CTRL2 register to use step up for arm clock */
	tmp |= (PWR_CTRL2_DIV2_UP_EN | PWR_CTRL2_DIV1_UP_EN);

	__raw_writel(tmp, EXYNOS4_PWR_CTRL2);

	printk(KERN_INFO "Exynos4 : ARM Clock down on idle mode is enabled\n");
}
#else
#define exynos4_core_down_clk()	do { } while (0)
#endif

static int __init exynos4_init_cpuidle(void)
{
	int i, max_cpuidle_state, cpu_id;
	struct cpuidle_device *device;
	struct platform_device *pdev;
	struct resource *res;

	if (soc_is_exynos4210())
		use_clock_down = SW_CLK_DWN;
	else
		use_clock_down = HW_CLK_DWN;

	/* Clock down feature can use only EXYNOS4212 */
	if (use_clock_down == HW_CLK_DWN)
		exynos4_core_down_clk();

	cpuidle_register_driver(&exynos4_idle_driver);

	for_each_cpu(cpu_id, cpu_online_mask) {
		device = &per_cpu(exynos4_cpuidle_device, cpu_id);
		device->cpu = cpu_id;

		if (cpu_id == 0)
			device->state_count = ARRAY_SIZE(exynos4_cpuidle_set);
		else
			device->state_count = 1;	/* Support IDLE only */

		max_cpuidle_state = device->state_count;

		for (i = 0; i < max_cpuidle_state; i++) {
			memcpy(&device->states[i], &exynos4_cpuidle_set[i],
					sizeof(struct cpuidle_state));
		}

		device->safe_state = &device->states[0];

		if (cpuidle_register_device(device)) {
			printk(KERN_ERR "CPUidle register device failed\n,");
			return -EIO;
		}
	}

	sdmmc_dev_num = ARRAY_SIZE(chk_sdhc_op);

	for (i = 0; i < sdmmc_dev_num; i++) {

		pdev = chk_sdhc_op[i].pdev;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res) {
			printk(KERN_ERR "failed to get iomem region\n");
			return -EINVAL;
		}

		chk_sdhc_op[i].base = ioremap(res->start, resource_size(res));

		if (!chk_sdhc_op[i].base) {
			printk(KERN_ERR "failed to map io region\n");
			return -EINVAL;
		}
	}

#if defined(CONFIG_USB_S3C_OTGD) && !defined(CONFIG_USB_EXYNOS_SWITCH)
	pdev = chk_usbotg_op.pdev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		printk(KERN_ERR "failed to get iomem region\n");
		return -EINVAL;
	}

	chk_usbotg_op.base = ioremap(res->start, resource_size(res));

	if (!chk_usbotg_op.base) {
		printk(KERN_ERR "failed to map io region\n");
		return -EINVAL;
	}
#endif
	register_pm_notifier(&exynos4_cpuidle_notifier);
	sys_pwr_conf_addr = (unsigned long)S5P_CENTRAL_SEQ_CONFIGURATION;

	/* Save register value for L2X0 */
	l2x0_save[0] = __raw_readl(S5P_VA_L2CC + 0x108);
	l2x0_save[1] = __raw_readl(S5P_VA_L2CC + 0x10C);
	l2x0_save[2] = __raw_readl(S5P_VA_L2CC + 0xF60);

	flush_cache_all();
	outer_clean_range(virt_to_phys(l2x0_save), ARRAY_SIZE(l2x0_save));

	return 0;
}
device_initcall(exynos4_init_cpuidle);
