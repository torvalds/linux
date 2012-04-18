/* linux/arch/arm/plat-s3c64xx/pm.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C64XX CPU PM support.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/serial_core.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/pm_domain.h>

#include <mach/map.h>
#include <mach/irqs.h>

#include <plat/devs.h>
#include <plat/pm.h>
#include <plat/wakeup-mask.h>

#include <mach/regs-sys.h>
#include <mach/regs-gpio.h>
#include <mach/regs-clock.h>
#include <mach/regs-syscon-power.h>
#include <mach/regs-gpio-memport.h>
#include <mach/regs-modem.h>

struct s3c64xx_pm_domain {
	char *const name;
	u32 ena;
	u32 pwr_stat;
	struct generic_pm_domain pd;
};

static int s3c64xx_pd_off(struct generic_pm_domain *domain)
{
	struct s3c64xx_pm_domain *pd;
	u32 val;

	pd = container_of(domain, struct s3c64xx_pm_domain, pd);

	val = __raw_readl(S3C64XX_NORMAL_CFG);
	val &= ~(pd->ena);
	__raw_writel(val, S3C64XX_NORMAL_CFG);

	return 0;
}

static int s3c64xx_pd_on(struct generic_pm_domain *domain)
{
	struct s3c64xx_pm_domain *pd;
	u32 val;
	long retry = 1000000L;

	pd = container_of(domain, struct s3c64xx_pm_domain, pd);

	val = __raw_readl(S3C64XX_NORMAL_CFG);
	val |= pd->ena;
	__raw_writel(val, S3C64XX_NORMAL_CFG);

	/* Not all domains provide power status readback */
	if (pd->pwr_stat) {
		do {
			cpu_relax();
			if (__raw_readl(S3C64XX_BLK_PWR_STAT) & pd->pwr_stat)
				break;
		} while (retry--);

		if (!retry) {
			pr_err("Failed to start domain %s\n", pd->name);
			return -EBUSY;
		}
	}

	return 0;
}

static struct s3c64xx_pm_domain s3c64xx_pm_irom = {
	.name = "IROM",
	.ena = S3C64XX_NORMALCFG_IROM_ON,
	.pd = {
		.power_off = s3c64xx_pd_off,
		.power_on = s3c64xx_pd_on,
	},
};

static struct s3c64xx_pm_domain s3c64xx_pm_etm = {
	.name = "ETM",
	.ena = S3C64XX_NORMALCFG_DOMAIN_ETM_ON,
	.pwr_stat = S3C64XX_BLKPWRSTAT_ETM,
	.pd = {
		.power_off = s3c64xx_pd_off,
		.power_on = s3c64xx_pd_on,
	},
};

static struct s3c64xx_pm_domain s3c64xx_pm_s = {
	.name = "S",
	.ena = S3C64XX_NORMALCFG_DOMAIN_S_ON,
	.pwr_stat = S3C64XX_BLKPWRSTAT_S,
	.pd = {
		.power_off = s3c64xx_pd_off,
		.power_on = s3c64xx_pd_on,
	},
};

static struct s3c64xx_pm_domain s3c64xx_pm_f = {
	.name = "F",
	.ena = S3C64XX_NORMALCFG_DOMAIN_F_ON,
	.pwr_stat = S3C64XX_BLKPWRSTAT_F,
	.pd = {
		.power_off = s3c64xx_pd_off,
		.power_on = s3c64xx_pd_on,
	},
};

static struct s3c64xx_pm_domain s3c64xx_pm_p = {
	.name = "P",
	.ena = S3C64XX_NORMALCFG_DOMAIN_P_ON,
	.pwr_stat = S3C64XX_BLKPWRSTAT_P,
	.pd = {
		.power_off = s3c64xx_pd_off,
		.power_on = s3c64xx_pd_on,
	},
};

static struct s3c64xx_pm_domain s3c64xx_pm_i = {
	.name = "I",
	.ena = S3C64XX_NORMALCFG_DOMAIN_I_ON,
	.pwr_stat = S3C64XX_BLKPWRSTAT_I,
	.pd = {
		.power_off = s3c64xx_pd_off,
		.power_on = s3c64xx_pd_on,
	},
};

static struct s3c64xx_pm_domain s3c64xx_pm_g = {
	.name = "G",
	.ena = S3C64XX_NORMALCFG_DOMAIN_G_ON,
	.pd = {
		.power_off = s3c64xx_pd_off,
		.power_on = s3c64xx_pd_on,
	},
};

static struct s3c64xx_pm_domain s3c64xx_pm_v = {
	.name = "V",
	.ena = S3C64XX_NORMALCFG_DOMAIN_V_ON,
	.pwr_stat = S3C64XX_BLKPWRSTAT_V,
	.pd = {
		.power_off = s3c64xx_pd_off,
		.power_on = s3c64xx_pd_on,
	},
};

static struct s3c64xx_pm_domain *s3c64xx_always_on_pm_domains[] = {
	&s3c64xx_pm_irom,
};

static struct s3c64xx_pm_domain *s3c64xx_pm_domains[] = {
	&s3c64xx_pm_etm,
	&s3c64xx_pm_g,
	&s3c64xx_pm_v,
	&s3c64xx_pm_i,
	&s3c64xx_pm_p,
	&s3c64xx_pm_s,
	&s3c64xx_pm_f,
};

#ifdef CONFIG_S3C_PM_DEBUG_LED_SMDK
void s3c_pm_debug_smdkled(u32 set, u32 clear)
{
	unsigned long flags;
	int i;

	local_irq_save(flags);
	for (i = 0; i < 4; i++) {
		if (clear & (1 << i))
			gpio_set_value(S3C64XX_GPN(12 + i), 0);
		if (set & (1 << i))
			gpio_set_value(S3C64XX_GPN(12 + i), 1);
	}
	local_irq_restore(flags);
}
#endif

static struct sleep_save core_save[] = {
	SAVE_ITEM(S3C_APLL_LOCK),
	SAVE_ITEM(S3C_MPLL_LOCK),
	SAVE_ITEM(S3C_EPLL_LOCK),
	SAVE_ITEM(S3C_CLK_SRC),
	SAVE_ITEM(S3C_CLK_DIV0),
	SAVE_ITEM(S3C_CLK_DIV1),
	SAVE_ITEM(S3C_CLK_DIV2),
	SAVE_ITEM(S3C_CLK_OUT),
	SAVE_ITEM(S3C_HCLK_GATE),
	SAVE_ITEM(S3C_PCLK_GATE),
	SAVE_ITEM(S3C_SCLK_GATE),
	SAVE_ITEM(S3C_MEM0_GATE),

	SAVE_ITEM(S3C_EPLL_CON1),
	SAVE_ITEM(S3C_EPLL_CON0),

	SAVE_ITEM(S3C64XX_MEM0DRVCON),
	SAVE_ITEM(S3C64XX_MEM1DRVCON),

#ifndef CONFIG_CPU_FREQ
	SAVE_ITEM(S3C_APLL_CON),
	SAVE_ITEM(S3C_MPLL_CON),
#endif
};

static struct sleep_save misc_save[] = {
	SAVE_ITEM(S3C64XX_AHB_CON0),
	SAVE_ITEM(S3C64XX_AHB_CON1),
	SAVE_ITEM(S3C64XX_AHB_CON2),
	
	SAVE_ITEM(S3C64XX_SPCON),

	SAVE_ITEM(S3C64XX_MEM0CONSTOP),
	SAVE_ITEM(S3C64XX_MEM1CONSTOP),
	SAVE_ITEM(S3C64XX_MEM0CONSLP0),
	SAVE_ITEM(S3C64XX_MEM0CONSLP1),
	SAVE_ITEM(S3C64XX_MEM1CONSLP),

	SAVE_ITEM(S3C64XX_SDMA_SEL),
	SAVE_ITEM(S3C64XX_MODEM_MIFPCON),

	SAVE_ITEM(S3C64XX_NORMAL_CFG),
};

void s3c_pm_configure_extint(void)
{
	__raw_writel(s3c_irqwake_eintmask, S3C64XX_EINT_MASK);
}

void s3c_pm_restore_core(void)
{
	__raw_writel(0, S3C64XX_EINT_MASK);

	s3c_pm_debug_smdkled(1 << 2, 0);

	s3c_pm_do_restore_core(core_save, ARRAY_SIZE(core_save));
	s3c_pm_do_restore(misc_save, ARRAY_SIZE(misc_save));
}

void s3c_pm_save_core(void)
{
	s3c_pm_do_save(misc_save, ARRAY_SIZE(misc_save));
	s3c_pm_do_save(core_save, ARRAY_SIZE(core_save));
}

/* since both s3c6400 and s3c6410 share the same sleep pm calls, we
 * put the per-cpu code in here until any new cpu comes along and changes
 * this.
 */

static int s3c64xx_cpu_suspend(unsigned long arg)
{
	unsigned long tmp;

	/* set our standby method to sleep */

	tmp = __raw_readl(S3C64XX_PWR_CFG);
	tmp &= ~S3C64XX_PWRCFG_CFG_WFI_MASK;
	tmp |= S3C64XX_PWRCFG_CFG_WFI_SLEEP;
	__raw_writel(tmp, S3C64XX_PWR_CFG);

	/* clear any old wakeup */

	__raw_writel(__raw_readl(S3C64XX_WAKEUP_STAT),
		     S3C64XX_WAKEUP_STAT);

	/* set the LED state to 0110 over sleep */
	s3c_pm_debug_smdkled(3 << 1, 0xf);

	/* issue the standby signal into the pm unit. Note, we
	 * issue a write-buffer drain just in case */

	tmp = 0;

	asm("b 1f\n\t"
	    ".align 5\n\t"
	    "1:\n\t"
	    "mcr p15, 0, %0, c7, c10, 5\n\t"
	    "mcr p15, 0, %0, c7, c10, 4\n\t"
	    "mcr p15, 0, %0, c7, c0, 4" :: "r" (tmp));

	/* we should never get past here */

	panic("sleep resumed to originator?");
}

/* mapping of interrupts to parts of the wakeup mask */
static struct samsung_wakeup_mask wake_irqs[] = {
	{ .irq = IRQ_RTC_ALARM,	.bit = S3C64XX_PWRCFG_RTC_ALARM_DISABLE, },
	{ .irq = IRQ_RTC_TIC,	.bit = S3C64XX_PWRCFG_RTC_TICK_DISABLE, },
	{ .irq = IRQ_PENDN,	.bit = S3C64XX_PWRCFG_TS_DISABLE, },
	{ .irq = IRQ_HSMMC0,	.bit = S3C64XX_PWRCFG_MMC0_DISABLE, },
	{ .irq = IRQ_HSMMC1,	.bit = S3C64XX_PWRCFG_MMC1_DISABLE, },
	{ .irq = IRQ_HSMMC2,	.bit = S3C64XX_PWRCFG_MMC2_DISABLE, },
	{ .irq = NO_WAKEUP_IRQ,	.bit = S3C64XX_PWRCFG_BATF_DISABLE},
	{ .irq = NO_WAKEUP_IRQ,	.bit = S3C64XX_PWRCFG_MSM_DISABLE },
	{ .irq = NO_WAKEUP_IRQ,	.bit = S3C64XX_PWRCFG_HSI_DISABLE },
	{ .irq = NO_WAKEUP_IRQ,	.bit = S3C64XX_PWRCFG_MSM_DISABLE },
};

static void s3c64xx_pm_prepare(void)
{
	samsung_sync_wakemask(S3C64XX_PWR_CFG,
			      wake_irqs, ARRAY_SIZE(wake_irqs));

	/* store address of resume. */
	__raw_writel(virt_to_phys(s3c_cpu_resume), S3C64XX_INFORM0);

	/* ensure previous wakeup state is cleared before sleeping */
	__raw_writel(__raw_readl(S3C64XX_WAKEUP_STAT), S3C64XX_WAKEUP_STAT);
}

int __init s3c64xx_pm_init(void)
{
	int i;

	s3c_pm_init();

	for (i = 0; i < ARRAY_SIZE(s3c64xx_always_on_pm_domains); i++)
		pm_genpd_init(&s3c64xx_always_on_pm_domains[i]->pd,
			      &pm_domain_always_on_gov, false);

	for (i = 0; i < ARRAY_SIZE(s3c64xx_pm_domains); i++)
		pm_genpd_init(&s3c64xx_pm_domains[i]->pd, NULL, false);

	if (dev_get_platdata(&s3c_device_fb.dev))
		pm_genpd_add_device(&s3c64xx_pm_f.pd, &s3c_device_fb.dev);

	return 0;
}

static __init int s3c64xx_pm_initcall(void)
{
	pm_cpu_prep = s3c64xx_pm_prepare;
	pm_cpu_sleep = s3c64xx_cpu_suspend;
	pm_uart_udivslot = 1;

#ifdef CONFIG_S3C_PM_DEBUG_LED_SMDK
	gpio_request(S3C64XX_GPN(12), "DEBUG_LED0");
	gpio_request(S3C64XX_GPN(13), "DEBUG_LED1");
	gpio_request(S3C64XX_GPN(14), "DEBUG_LED2");
	gpio_request(S3C64XX_GPN(15), "DEBUG_LED3");
	gpio_direction_output(S3C64XX_GPN(12), 0);
	gpio_direction_output(S3C64XX_GPN(13), 0);
	gpio_direction_output(S3C64XX_GPN(14), 0);
	gpio_direction_output(S3C64XX_GPN(15), 0);
#endif

	return 0;
}
arch_initcall(s3c64xx_pm_initcall);

static __init int s3c64xx_pm_late_initcall(void)
{
	pm_genpd_poweroff_unused();

	return 0;
}
late_initcall(s3c64xx_pm_late_initcall);
