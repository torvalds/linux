// SPDX-License-Identifier: GPL-2.0
//
// Copyright 2008 Openmoko, Inc.
// Copyright 2008 Simtec Electronics
//	Ben Dooks <ben@simtec.co.uk>
//	http://armlinux.simtec.co.uk/
//
// S3C64XX CPU PM support.

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/serial_core.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/pm_domain.h>

#include "map.h"
#include <mach/irqs.h>

#include "cpu.h"
#include "devs.h"
#include "pm.h"
#include "wakeup-mask.h"

#include "regs-gpio.h"
#include "regs-clock.h"
#include "gpio-samsung.h"

#include "regs-gpio-memport-s3c64xx.h"
#include "regs-modem-s3c64xx.h"
#include "regs-sys-s3c64xx.h"
#include "regs-syscon-power-s3c64xx.h"

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

#ifdef CONFIG_PM_SLEEP
static struct sleep_save core_save[] = {
	SAVE_ITEM(S3C64XX_MEM0DRVCON),
	SAVE_ITEM(S3C64XX_MEM1DRVCON),
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
#endif

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

	pr_info("Failed to suspend the system\n");
	return 1; /* Aborting suspend */
}

/* mapping of interrupts to parts of the wakeup mask */
static const struct samsung_wakeup_mask wake_irqs[] = {
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
	__raw_writel(__pa_symbol(s3c_cpu_resume), S3C64XX_INFORM0);

	/* ensure previous wakeup state is cleared before sleeping */
	__raw_writel(__raw_readl(S3C64XX_WAKEUP_STAT), S3C64XX_WAKEUP_STAT);
}

#ifdef CONFIG_SAMSUNG_PM_DEBUG
void s3c_pm_arch_update_uart(void __iomem *regs, struct pm_uart_save *save)
{
	u32 ucon;
	u32 ucon_clk
	u32 save_clk;
	u32 new_ucon;
	u32 delta;

	if (!soc_is_s3c64xx())
		return;

	ucon = __raw_readl(regs + S3C2410_UCON);
	ucon_clk = ucon & S3C6400_UCON_CLKMASK;
	sav_clk = save->ucon & S3C6400_UCON_CLKMASK;

	/* S3C64XX UART blocks only support level interrupts, so ensure that
	 * when we restore unused UART blocks we force the level interrupt
	 * settings. */
	save->ucon |= S3C2410_UCON_TXILEVEL | S3C2410_UCON_RXILEVEL;

	/* We have a constraint on changing the clock type of the UART
	 * between UCLKx and PCLK, so ensure that when we restore UCON
	 * that the CLK field is correctly modified if the bootloader
	 * has changed anything.
	 */
	if (ucon_clk != save_clk) {
		new_ucon = save->ucon;
		delta = ucon_clk ^ save_clk;

		/* change from UCLKx => wrong PCLK,
		 * either UCLK can be tested for by a bit-test
		 * with UCLK0 */
		if (ucon_clk & S3C6400_UCON_UCLK0 &&
		    !(save_clk & S3C6400_UCON_UCLK0) &&
		    delta & S3C6400_UCON_PCLK2) {
			new_ucon &= ~S3C6400_UCON_UCLK0;
		} else if (delta == S3C6400_UCON_PCLK2) {
			/* as an precaution, don't change from
			 * PCLK2 => PCLK or vice-versa */
			new_ucon ^= S3C6400_UCON_PCLK2;
		}

		S3C_PMDBG("ucon change %04x => %04x (save=%04x)\n",
			  ucon, new_ucon, save->ucon);
		save->ucon = new_ucon;
	}
}
#endif

int __init s3c64xx_pm_init(void)
{
	int i;

	s3c_pm_init();

	for (i = 0; i < ARRAY_SIZE(s3c64xx_always_on_pm_domains); i++)
		pm_genpd_init(&s3c64xx_always_on_pm_domains[i]->pd,
			      &pm_domain_always_on_gov, false);

	for (i = 0; i < ARRAY_SIZE(s3c64xx_pm_domains); i++)
		pm_genpd_init(&s3c64xx_pm_domains[i]->pd, NULL, false);

#ifdef CONFIG_S3C_DEV_FB
	if (dev_get_platdata(&s3c_device_fb.dev))
		pm_genpd_add_device(&s3c64xx_pm_f.pd, &s3c_device_fb.dev);
#endif

	return 0;
}

static __init int s3c64xx_pm_initcall(void)
{
	if (!soc_is_s3c64xx())
		return 0;

	pm_cpu_prep = s3c64xx_pm_prepare;
	pm_cpu_sleep = s3c64xx_cpu_suspend;

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
