/* linux/arch/arm/mach-exynos/cpuidle-exynos5.c
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

#include <plat/pm.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-core.h>
#include <plat/regs-otg.h>
#include <plat/devs.h>
#include <plat/cpu.h>

#include <mach/regs-pmu5.h>
#include <mach/pm-core.h>
#include <mach/pmu.h>
#include <mach/regs-clock.h>
#include <mach/smc.h>
#include <mach/clock-domain.h>
#include <mach/regs-usb-phy.h>

#ifdef CONFIG_ARM_TRUSTZONE
#define REG_DIRECTGO_ADDR       (S5P_VA_SYSRAM_NS + 0x24)
#define REG_DIRECTGO_FLAG       (S5P_VA_SYSRAM_NS + 0x20)
#else
#define REG_DIRECTGO_ADDR	(S5P_VA_SYSRAM + 0x24)
#define REG_DIRECTGO_FLAG	(S5P_VA_SYSRAM + 0x20)
#endif

extern unsigned long sys_pwr_conf_addr;

static int exynos5_enter_idle(struct cpuidle_device *dev,
			      struct cpuidle_state *state);

static int __maybe_unused exynos5_enter_lowpower(struct cpuidle_device *dev,
						 struct cpuidle_state *state);

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
	{.check_reg = EXYNOS5_G3D_STATUS,	.check_bit = 0x7},
};

/*
 * List of check clock gating list for LPA mode
 * If clock of list is not gated, system can not enter LPA mode.
 */
static struct check_reg_lpa exynos5_clock_gating[] = {
	{.check_reg = EXYNOS5_CLKSRC_MASK_DISP1_0,	.check_bit = 0x00000001},
	{.check_reg = EXYNOS5_CLKGATE_IP_DISP1,		.check_bit = 0x00000010},
	{.check_reg = EXYNOS5_CLKGATE_IP_MFC,		.check_bit = 0x00000001},
	{.check_reg = EXYNOS5_CLKGATE_IP_GEN,		.check_bit = 0x00004016},
	{.check_reg = EXYNOS5_CLKGATE_IP_FSYS,		.check_bit = 0x00000002},
	{.check_reg = EXYNOS5_CLKGATE_IP_PERIC,		.check_bit = 0x00377FC0},
};

enum hc_type {
	HC_SDHC,
	HC_MSHC,
};

struct check_device_op {
	void __iomem		*base;
	struct platform_device	*pdev;
	enum hc_type		type;
};

static struct check_device_op chk_sdhc_op[] = {
#if defined(CONFIG_EXYNOS4_DEV_DWMCI)
	{.base = 0, .pdev = &exynos_device_dwmci, .type = HC_MSHC},
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

static struct check_device_op chk_sdhc_op_exynos5250_rev1[] = {
#if defined(CONFIG_EXYNOS4_DEV_DWMCI)
	{.base = 0, .pdev = &exynos_device_dwmci0, .type = HC_MSHC},
	{.base = 0, .pdev = &exynos_device_dwmci1, .type = HC_MSHC},
	{.base = 0, .pdev = &exynos_device_dwmci2, .type = HC_MSHC},
	{.base = 0, .pdev = &exynos_device_dwmci3, .type = HC_MSHC},
#endif
};

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
	unsigned int reg1, reg2;
	void __iomem *base_addr;

	if (unlikely(ch >= sdmmc_dev_num)) {
		printk(KERN_ERR "Invalid ch[%d] for SD/MMC\n", ch);
		return 0;
	}

	if (soc_is_exynos5250() && (samsung_rev() >= EXYNOS5250_REV_1_0)) {
		if (chk_sdhc_op_exynos5250_rev1[ch].type == HC_MSHC) {
			base_addr = chk_sdhc_op_exynos5250_rev1[ch].base;
			/* Check STATUS [9] for data busy */
			reg1 = readl(base_addr + MSHCI_STATUS);
			return (reg1 & (MSHCI_DATA_BUSY)) ||
			       (reg1 & (MSHCI_DATA_STAT_BUSY));
		}
	} else {
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
			printk(KERN_DEBUG "SDMMC [%d] working\n", iter);
			return 1;
		}
	}
	return 0;
}

static int exynos5_check_reg_status(struct check_reg_lpa *reg_list,
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

static int exynos5_uart_fifo_check(void)
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

static struct cpuidle_state exynos5_cpuidle_set[] = {
	[0] = {
		.enter			= exynos5_enter_idle,
		.exit_latency		= 1,
		.target_residency	= 10000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "IDLE",
		.desc			= "ARM clock gating(WFI)",
	},
#ifdef CONFIG_EXYNOS5_LOWPWR_IDLE
	[1] = {
		.enter			= exynos5_enter_lowpower,
		.exit_latency		= 300,
		.target_residency	= 10000,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "LOW_POWER",
		.desc			= "ARM power down",
	},
#endif
};

static DEFINE_PER_CPU(struct cpuidle_device, exynos5_cpuidle_device);

static struct cpuidle_driver exynos5_idle_driver = {
	.name		= "exynos5_idle",
	.owner		= THIS_MODULE,
};

/*
 * To keep value of gpio on power down mode
 * set Power down register of gpio
 */
static void exynos5_gpio_set_pd_reg(void)
{
	struct s3c_gpio_chip *target_chip;
	unsigned int gpio_nr;
	unsigned int tmp;

	for (gpio_nr = 0; gpio_nr < EXYNOS5_GPIO_END; gpio_nr++) {
		target_chip = s3c_gpiolib_getchip(gpio_nr);

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

static int exynos5_enter_idle(struct cpuidle_device *dev,
			      struct cpuidle_state *state)
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

	return idle_time;
}

static void exynos5_set_wakeupmask(void)
{
	__raw_writel(0x0000ff3e, EXYNOS5_WAKEUP_MASK);
}

static inline void vfp_enable(void *unused)
{
	u32 access = get_copro_access();

	/*
	 * Enable full access to VFP (cp10 and cp11)
	 */
	set_copro_access(access | CPACC_FULL(10) | CPACC_FULL(11));
}

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

static int exynos5_enter_core0_lpa(struct cpuidle_device *dev,
				   struct cpuidle_state *state)
{
	struct timeval before, after;
	int idle_time;

	unsigned long tmp;

	s3c_pm_do_save(exynos5_lpa_save, ARRAY_SIZE(exynos5_lpa_save));
	/*
	 * Before enter central sequence mode, clock src register have to set
	 */
	s3c_pm_do_restore_core(exynos5_set_clksrc,
			       ARRAY_SIZE(exynos5_set_clksrc));

	local_irq_disable();

	do_gettimeofday(&before);

	/*
	 * Unmasking all wakeup source.
	 */
	__raw_writel(0x0, S5P_WAKEUP_MASK);

	/* Configure GPIO Power down control register */
	exynos5_gpio_set_pd_reg();

	/* ensure at least INFORM0 has the resume address */
	__raw_writel(virt_to_phys(exynos5_idle_resume), REG_DIRECTGO_ADDR);
	__raw_writel(0xfcba0d10, REG_DIRECTGO_FLAG);

	__raw_writel(S5P_CHECK_LPA, EXYNOS5_INFORM1);

	exynos5_sys_powerdown_conf(SYS_LPA);

	/* Disable USE_RETENTION of JPEG_MEM_OPTION */
	tmp = __raw_readl(EXYNOS5_JPEG_MEM_OPTION);
	tmp |= EXYNOS5_OPTION_USE_RETENTION;
	__raw_writel(tmp, EXYNOS5_JPEG_MEM_OPTION);

	do {
		/* Waiting for flushing UART fifo */
	} while (exynos5_uart_fifo_check());

	/*
	 * GPS can not turn off.
	 */
	if (samsung_rev() < EXYNOS5250_REV_1_0)
		__raw_writel(0x10000, EXYNOS5_GPS_LPI);

	if (exynos5_enter_lp(0, PLAT_PHYS_OFFSET - PAGE_OFFSET) == 0) {
		/*
		 * Clear Central Sequence Register in exiting early wakeup
		 */
		tmp = __raw_readl(EXYNOS5_CENTRAL_SEQ_CONFIGURATION);
		tmp |= (EXYNOS5_CENTRAL_LOWPWR_CFG);
		__raw_writel(tmp, EXYNOS5_CENTRAL_SEQ_CONFIGURATION);

		goto early_wakeup;
	}

	flush_tlb_all();

	cpu_init();

	vfp_enable(NULL);

	/* For release retention */
	__raw_writel((1 << 28), EXYNOS5_PAD_RETENTION_MAU_OPTION);
	__raw_writel((1 << 28), EXYNOS5_PAD_RETENTION_GPIO_OPTION);
	__raw_writel((1 << 28), EXYNOS5_PAD_RETENTION_UART_OPTION);
	__raw_writel((1 << 28), EXYNOS5_PAD_RETENTION_MMCA_OPTION);
	__raw_writel((1 << 28), EXYNOS5_PAD_RETENTION_MMCB_OPTION);
	__raw_writel((1 << 28), EXYNOS5_PAD_RETENTION_EBIA_OPTION);
	__raw_writel((1 << 28), EXYNOS5_PAD_RETENTION_EBIB_OPTION);
	__raw_writel((1 << 28), EXYNOS5_PAD_RETENTION_SPI_OPTION);
	__raw_writel((1 << 28), EXYNOS5_PAD_RETENTION_GPIO_SYSMEM_OPTION);

early_wakeup:
	s3c_pm_do_restore_core(exynos5_lpa_save,
			       ARRAY_SIZE(exynos5_lpa_save));

	/* Clear wakeup state register */
	__raw_writel(0x0, EXYNOS5_WAKEUP_STAT);

	__raw_writel(0x0, EXYNOS5_WAKEUP_MASK);

	do_gettimeofday(&after);

	local_irq_enable();

	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
		    (after.tv_usec - before.tv_usec);

	return idle_time;
}

static int exynos5_enter_core0_aftr(struct cpuidle_device *dev,
				    struct cpuidle_state *state)
{
	struct timeval before, after;
	int idle_time;
	unsigned long tmp;

	local_irq_disable();
	do_gettimeofday(&before);

	exynos5_set_wakeupmask();

	__raw_writel(virt_to_phys(exynos5_idle_resume), REG_DIRECTGO_ADDR);
	__raw_writel(0xfcba0d10, REG_DIRECTGO_FLAG);

	/* Set value of power down register for aftr mode */
	exynos5_sys_powerdown_conf(SYS_AFTR);

	if (exynos5_enter_lp(0, PLAT_PHYS_OFFSET - PAGE_OFFSET) == 0) {
		/*
		 * Clear Central Sequence Register in exiting early wakeup
		 */
		tmp = __raw_readl(EXYNOS5_CENTRAL_SEQ_CONFIGURATION);
		tmp |= EXYNOS5_CENTRAL_LOWPWR_CFG;
		__raw_writel(tmp, EXYNOS5_CENTRAL_SEQ_CONFIGURATION);

		goto early_wakeup;
	}

	flush_tlb_all();

	cpu_init();

	vfp_enable(NULL);

early_wakeup:
	/* Clear wakeup state register */
	__raw_writel(0x0, EXYNOS5_WAKEUP_STAT);

	do_gettimeofday(&after);

	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
		    (after.tv_usec - before.tv_usec);

	return idle_time;
}

static int __maybe_unused exynos5_check_enter_mode(void)
{
	/* Check power domain */
	if (exynos5_check_reg_status(exynos5_power_domain,
				    ARRAY_SIZE(exynos5_power_domain)))
		return S5P_CHECK_DIDLE;

	/* Check clock gating */
	if (exynos5_check_reg_status(exynos5_clock_gating,
				    ARRAY_SIZE(exynos5_clock_gating)))
		return S5P_CHECK_DIDLE;

	if (clock_domain_enabled(LPA_DOMAIN))
		return S5P_CHECK_DIDLE;

	if (loop_sdmmc_check())
		return S5P_CHECK_DIDLE;

        if (exynos_check_usb_op())
                return S5P_CHECK_DIDLE;

	return S5P_CHECK_LPA;
}

static int __maybe_unused exynos5_enter_lowpower(struct cpuidle_device *dev,
						 struct cpuidle_state *state)
{
	struct cpuidle_state *new_state = state;
	unsigned int tmp;

	/* This mode only can be entered when only Core0 is online */
	if (num_online_cpus() != 1) {
		BUG_ON(!dev->safe_state);
		new_state = dev->safe_state;
	}
	dev->last_state = new_state;

	if (new_state == &dev->states[0])
		return exynos5_enter_idle(dev, new_state);

	tmp = __raw_readl(EXYNOS5_CENTRAL_SEQ_OPTION);
	tmp = (EXYNOS5_USE_STANDBYWFI_ARM_CORE0 |
		EXYNOS5_USE_STANDBYWFE_ARM_CORE0);
	__raw_writel(tmp, EXYNOS5_CENTRAL_SEQ_OPTION);

	if (exynos5_check_enter_mode() == S5P_CHECK_DIDLE)
		return exynos5_enter_core0_aftr(dev, new_state);
	else
		return exynos5_enter_core0_aftr(dev, new_state);
		//return exynos5_enter_core0_lpa(dev, new_state);
}

static int exynos5_cpuidle_notifier_event(struct notifier_block *this,
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

static struct notifier_block exynos5_cpuidle_notifier = {
	.notifier_call = exynos5_cpuidle_notifier_event,
};

#ifdef CONFIG_EXYNOS5_ENABLE_CLOCK_DOWN
static void __init exynos5_core_down_clk(void)
{
	unsigned int tmp;

	tmp = __raw_readl(EXYNOS5_PWR_CTRL1);

	tmp &= ~(PWR_CTRL1_CORE2_DOWN_MASK | PWR_CTRL1_CORE1_DOWN_MASK);

	/* set arm clock divider value on idle state */
	tmp |= ((0x7 << PWR_CTRL1_CORE2_DOWN_RATIO) |
		(0x7 << PWR_CTRL1_CORE1_DOWN_RATIO));

	tmp |= (PWR_CTRL1_DIV2_DOWN_EN |
		PWR_CTRL1_DIV1_DOWN_EN |
		PWR_CTRL1_USE_CORE1_WFE |
		PWR_CTRL1_USE_CORE0_WFE |
		PWR_CTRL1_USE_CORE1_WFI |
		PWR_CTRL1_USE_CORE0_WFI);

	__raw_writel(tmp, EXYNOS5_PWR_CTRL1);

	tmp = __raw_readl(EXYNOS5_PWR_CTRL2);

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

	__raw_writel(tmp, EXYNOS5_PWR_CTRL2);
	printk(KERN_INFO "Exynos5 : ARM Clock down on idle mode is enabled\n");
}
#else
#define exynos5_core_down_clk()	do { } while (0)
#endif

static int __init exynos5_init_cpuidle(void)
{
	int i, max_cpuidle_state, cpu_id, ret;
	struct cpuidle_device *device;
	struct platform_device *pdev;
	struct resource *res;
	void __iomem *base;

	if (samsung_rev() >= EXYNOS5250_REV_1_0)
		exynos4_reset_assert_ctrl(1);

	exynos5_core_down_clk();

	ret = cpuidle_register_driver(&exynos5_idle_driver);
	
	if(ret < 0){
		printk(KERN_ERR "exynos5 idle register driver failed\n");
		return ret;
	}


	for_each_cpu(cpu_id, cpu_online_mask) {
		device = &per_cpu(exynos5_cpuidle_device, cpu_id);
		device->cpu = cpu_id;

		if (cpu_id == 0)
			device->state_count = ARRAY_SIZE(exynos5_cpuidle_set);
		else
			device->state_count = 1;	/* Support IDLE only */

		max_cpuidle_state = device->state_count;

		for (i = 0; i < max_cpuidle_state; i++) {
			memcpy(&device->states[i], &exynos5_cpuidle_set[i],
					sizeof(struct cpuidle_state));
		}

		device->safe_state = &device->states[0];

		if (cpuidle_register_device(device)) {
			cpuidle_unregister_driver(&exynos5_idle_driver);
			printk(KERN_ERR "CPUidle register device failed\n,");
			return -EIO;
		}
	}

	if (soc_is_exynos5250() && (samsung_rev() >= EXYNOS5250_REV_1_0))
		sdmmc_dev_num = ARRAY_SIZE(chk_sdhc_op_exynos5250_rev1);
	else
		sdmmc_dev_num = ARRAY_SIZE(chk_sdhc_op);

	for (i = 0; i < sdmmc_dev_num; i++) {

		if (soc_is_exynos5250() && (samsung_rev() >= EXYNOS5250_REV_1_0))
			pdev = chk_sdhc_op_exynos5250_rev1[i].pdev;
		else
			pdev = chk_sdhc_op[i].pdev;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res) {
			printk(KERN_ERR "failed to get iomem region\n");
			return -EINVAL;
		}

		if (soc_is_exynos5250() && (samsung_rev() >= EXYNOS5250_REV_1_0)) {
			chk_sdhc_op_exynos5250_rev1[i].base = ioremap(res->start, resource_size(res));
			base = chk_sdhc_op_exynos5250_rev1[i].base;
		} else {
			chk_sdhc_op[i].base = ioremap(res->start, resource_size(res));
			base = chk_sdhc_op[i].base;
		}


		if (!base) {
			printk(KERN_ERR "failed to map io region\n");
			return -EINVAL;
		}
	}

	register_pm_notifier(&exynos5_cpuidle_notifier);
	sys_pwr_conf_addr = (unsigned long)EXYNOS5_CENTRAL_SEQ_CONFIGURATION;

	return 0;
}
device_initcall(exynos5_init_cpuidle);
