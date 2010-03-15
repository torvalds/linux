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

#include <mach/map.h>

#include <plat/pm.h>
#include <mach/regs-sys.h>
#include <mach/regs-gpio.h>
#include <mach/regs-clock.h>
#include <mach/regs-syscon-power.h>
#include <mach/regs-gpio-memport.h>

#ifdef CONFIG_S3C_PM_DEBUG_LED_SMDK
#include <mach/gpio-bank-n.h>

void s3c_pm_debug_smdkled(u32 set, u32 clear)
{
	unsigned long flags;
	u32 reg;

	local_irq_save(flags);
	reg = __raw_readl(S3C64XX_GPNCON);
	reg &= ~(S3C64XX_GPN_CONMASK(12) | S3C64XX_GPN_CONMASK(13) |
		 S3C64XX_GPN_CONMASK(14) | S3C64XX_GPN_CONMASK(15));
	reg |= S3C64XX_GPN_OUTPUT(12) | S3C64XX_GPN_OUTPUT(13) |
	       S3C64XX_GPN_OUTPUT(14) | S3C64XX_GPN_OUTPUT(15);
	__raw_writel(reg, S3C64XX_GPNCON);

	reg = __raw_readl(S3C64XX_GPNDAT);
	reg &= ~(clear << 12);
	reg |= set << 12;
	__raw_writel(reg, S3C64XX_GPNDAT);

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

static void s3c64xx_cpu_suspend(void)
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

static void s3c64xx_pm_prepare(void)
{
	/* store address of resume. */
	__raw_writel(virt_to_phys(s3c_cpu_resume), S3C64XX_INFORM0);

	/* ensure previous wakeup state is cleared before sleeping */
	__raw_writel(__raw_readl(S3C64XX_WAKEUP_STAT), S3C64XX_WAKEUP_STAT);
}

static int s3c64xx_pm_init(void)
{
	pm_cpu_prep = s3c64xx_pm_prepare;
	pm_cpu_sleep = s3c64xx_cpu_suspend;
	pm_uart_udivslot = 1;
	return 0;
}

arch_initcall(s3c64xx_pm_init);
