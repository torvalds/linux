// SPDX-License-Identifier: GPL-2.0-only
/*
 * MMP2 Power Management Routines
 *
 * (C) Copyright 2012 Marvell International Ltd.
 * All Rights Reserved
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/suspend.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <asm/mach-types.h>

#include <linux/soc/mmp/cputype.h>
#include "addr-map.h"
#include "pm-mmp2.h"
#include "regs-icu.h"
#include "irqs.h"

int mmp2_set_wake(struct irq_data *d, unsigned int on)
{
	unsigned long data = 0;
	int irq = d->irq;

	/* enable wakeup sources */
	switch (irq) {
	case IRQ_MMP2_RTC:
	case IRQ_MMP2_RTC_ALARM:
		data = MPMU_WUCRM_PJ_WAKEUP(4) | MPMU_WUCRM_PJ_RTC_ALARM;
		break;
	case IRQ_MMP2_PMIC:
		data = MPMU_WUCRM_PJ_WAKEUP(7);
		break;
	case IRQ_MMP2_MMC2:
		/* mmc use WAKEUP2, same as GPIO wakeup source */
		data = MPMU_WUCRM_PJ_WAKEUP(2);
		break;
	}
	if (on) {
		if (data) {
			data |= __raw_readl(MPMU_WUCRM_PJ);
			__raw_writel(data, MPMU_WUCRM_PJ);
		}
	} else {
		if (data) {
			data = ~data & __raw_readl(MPMU_WUCRM_PJ);
			__raw_writel(data, MPMU_WUCRM_PJ);
		}
	}
	return 0;
}

static void pm_scu_clk_disable(void)
{
	unsigned int val;

	/* close AXI fabric clock gate */
	__raw_writel(0x0, CIU_REG(0x64));
	__raw_writel(0x0, CIU_REG(0x68));

	/* close MCB master clock gate */
	val = __raw_readl(CIU_REG(0x1c));
	val |= 0xf0;
	__raw_writel(val, CIU_REG(0x1c));

	return ;
}

static void pm_scu_clk_enable(void)
{
	unsigned int val;

	/* open AXI fabric clock gate */
	__raw_writel(0x03003003, CIU_REG(0x64));
	__raw_writel(0x00303030, CIU_REG(0x68));

	/* open MCB master clock gate */
	val = __raw_readl(CIU_REG(0x1c));
	val &= ~(0xf0);
	__raw_writel(val, CIU_REG(0x1c));

	return ;
}

static void pm_mpmu_clk_disable(void)
{
	/*
	 * disable clocks in MPMU_CGR_PJ register
	 * except clock for APMU_PLL1, APMU_PLL1_2 and AP_26M
	 */
	__raw_writel(0x0000a010, MPMU_CGR_PJ);
}

static void pm_mpmu_clk_enable(void)
{
	unsigned int val;

	__raw_writel(0xdffefffe, MPMU_CGR_PJ);
	val = __raw_readl(MPMU_PLL2_CTRL1);
	val |= (1 << 29);
	__raw_writel(val, MPMU_PLL2_CTRL1);

	return ;
}

void mmp2_pm_enter_lowpower_mode(int state)
{
	uint32_t idle_cfg, apcr;

	idle_cfg = __raw_readl(APMU_PJ_IDLE_CFG);
	apcr = __raw_readl(MPMU_PCR_PJ);
	apcr &= ~(MPMU_PCR_PJ_SLPEN | MPMU_PCR_PJ_DDRCORSD | MPMU_PCR_PJ_APBSD
		 | MPMU_PCR_PJ_AXISD | MPMU_PCR_PJ_VCTCXOSD | (1 << 13));
	idle_cfg &= ~APMU_PJ_IDLE_CFG_PJ_IDLE;

	switch (state) {
	case POWER_MODE_SYS_SLEEP:
		apcr |= MPMU_PCR_PJ_SLPEN;		/* set the SLPEN bit */
		apcr |= MPMU_PCR_PJ_VCTCXOSD;		/* set VCTCXOSD */
		/* fall through */
	case POWER_MODE_CHIP_SLEEP:
		apcr |= MPMU_PCR_PJ_SLPEN;
		/* fall through */
	case POWER_MODE_APPS_SLEEP:
		apcr |= MPMU_PCR_PJ_APBSD;		/* set APBSD */
		/* fall through */
	case POWER_MODE_APPS_IDLE:
		apcr |= MPMU_PCR_PJ_AXISD;		/* set AXISDD bit */
		apcr |= MPMU_PCR_PJ_DDRCORSD;		/* set DDRCORSD bit */
		idle_cfg |= APMU_PJ_IDLE_CFG_PJ_PWRDWN;	/* PJ power down */
		apcr |= MPMU_PCR_PJ_SPSD;
		/* fall through */
	case POWER_MODE_CORE_EXTIDLE:
		idle_cfg |= APMU_PJ_IDLE_CFG_PJ_IDLE;	/* set the IDLE bit */
		idle_cfg &= ~APMU_PJ_IDLE_CFG_ISO_MODE_CNTRL_MASK;
		idle_cfg |= APMU_PJ_IDLE_CFG_PWR_SW(3)
			| APMU_PJ_IDLE_CFG_L2_PWR_SW;
		break;
	case POWER_MODE_CORE_INTIDLE:
		apcr &= ~MPMU_PCR_PJ_SPSD;
		break;
	}

	/* set reserve bits */
	apcr |= (1 << 30) | (1 << 25);

	/* finally write the registers back */
	__raw_writel(idle_cfg, APMU_PJ_IDLE_CFG);
	__raw_writel(apcr, MPMU_PCR_PJ);	/* 0xfe086000 */
}

static int mmp2_pm_enter(suspend_state_t state)
{
	int temp;

	temp = __raw_readl(MMP2_ICU_INT4_MASK);
	if (temp & (1 << 1)) {
		printk(KERN_ERR "%s: PMIC interrupt is handling\n", __func__);
		return -EAGAIN;
	}

	temp = __raw_readl(APMU_SRAM_PWR_DWN);
	temp |= ((1 << 19) | (1 << 18));
	__raw_writel(temp, APMU_SRAM_PWR_DWN);
	pm_mpmu_clk_disable();
	pm_scu_clk_disable();

	printk(KERN_INFO "%s: before suspend\n", __func__);
	cpu_do_idle();
	printk(KERN_INFO "%s: after suspend\n", __func__);

	pm_mpmu_clk_enable();		/* enable clocks in MPMU */
	pm_scu_clk_enable();		/* enable clocks in SCU */

	return 0;
}

/*
 * Called after processes are frozen, but before we shut down devices.
 */
static int mmp2_pm_prepare(void)
{
	mmp2_pm_enter_lowpower_mode(POWER_MODE_SYS_SLEEP);

	return 0;
}

/*
 * Called after devices are re-setup, but before processes are thawed.
 */
static void mmp2_pm_finish(void)
{
	mmp2_pm_enter_lowpower_mode(POWER_MODE_CORE_INTIDLE);
}

static int mmp2_pm_valid(suspend_state_t state)
{
	return ((state == PM_SUSPEND_STANDBY) || (state == PM_SUSPEND_MEM));
}

/*
 * Set to PM_DISK_FIRMWARE so we can quickly veto suspend-to-disk.
 */
static const struct platform_suspend_ops mmp2_pm_ops = {
	.valid		= mmp2_pm_valid,
	.prepare	= mmp2_pm_prepare,
	.enter		= mmp2_pm_enter,
	.finish		= mmp2_pm_finish,
};

static int __init mmp2_pm_init(void)
{
	uint32_t apcr;

	if (!cpu_is_mmp2())
		return -EIO;

	suspend_set_ops(&mmp2_pm_ops);

	/*
	 * Set bit 0, Slow clock Select 32K clock input instead of VCXO
	 * VCXO is chosen by default, which would be disabled in suspend
	 */
	__raw_writel(0x5, MPMU_SCCR);

	/*
	 * Clear bit 23 of CIU_CPU_CONF
	 * direct PJ4 to DDR access through Memory Controller slow queue
	 * fast queue has issue and cause lcd will flick
	 */
	__raw_writel(__raw_readl(CIU_REG(0x8)) & ~(0x1 << 23), CIU_REG(0x8));

	/* Clear default low power control bit */
	apcr = __raw_readl(MPMU_PCR_PJ);
	apcr &= ~(MPMU_PCR_PJ_SLPEN | MPMU_PCR_PJ_DDRCORSD
			| MPMU_PCR_PJ_APBSD | MPMU_PCR_PJ_AXISD | 1 << 13);
	__raw_writel(apcr, MPMU_PCR_PJ);

	return 0;
}

late_initcall(mmp2_pm_init);
