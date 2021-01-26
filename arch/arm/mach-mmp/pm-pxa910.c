// SPDX-License-Identifier: GPL-2.0-only
/*
 * PXA910 Power Management Routines
 *
 * (C) Copyright 2009 Marvell International Ltd.
 * All Rights Reserved
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/suspend.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <asm/mach-types.h>
#include <asm/outercache.h>

#include <linux/soc/mmp/cputype.h>
#include "addr-map.h"
#include "pm-pxa910.h"
#include "regs-icu.h"
#include "irqs.h"

int pxa910_set_wake(struct irq_data *data, unsigned int on)
{
	uint32_t awucrm = 0, apcr = 0;
	int irq = data->irq;

	/* setting wakeup sources */
	switch (irq) {
	/* wakeup line 2 */
	case IRQ_PXA910_AP_GPIO:
		awucrm = MPMU_AWUCRM_WAKEUP(2);
		apcr |= MPMU_APCR_SLPWP2;
		break;
	/* wakeup line 3 */
	case IRQ_PXA910_KEYPAD:
		awucrm = MPMU_AWUCRM_WAKEUP(3) | MPMU_AWUCRM_KEYPRESS;
		apcr |= MPMU_APCR_SLPWP3;
		break;
	case IRQ_PXA910_ROTARY:
		awucrm = MPMU_AWUCRM_WAKEUP(3) | MPMU_AWUCRM_NEWROTARY;
		apcr |= MPMU_APCR_SLPWP3;
		break;
	case IRQ_PXA910_TRACKBALL:
		awucrm = MPMU_AWUCRM_WAKEUP(3) | MPMU_AWUCRM_TRACKBALL;
		apcr |= MPMU_APCR_SLPWP3;
		break;
	/* wakeup line 4 */
	case IRQ_PXA910_AP1_TIMER1:
		awucrm = MPMU_AWUCRM_WAKEUP(4) | MPMU_AWUCRM_AP1_TIMER_1;
		apcr |= MPMU_APCR_SLPWP4;
		break;
	case IRQ_PXA910_AP1_TIMER2:
		awucrm = MPMU_AWUCRM_WAKEUP(4) | MPMU_AWUCRM_AP1_TIMER_2;
		apcr |= MPMU_APCR_SLPWP4;
		break;
	case IRQ_PXA910_AP1_TIMER3:
		awucrm = MPMU_AWUCRM_WAKEUP(4) | MPMU_AWUCRM_AP1_TIMER_3;
		apcr |= MPMU_APCR_SLPWP4;
		break;
	case IRQ_PXA910_AP2_TIMER1:
		awucrm = MPMU_AWUCRM_WAKEUP(4) | MPMU_AWUCRM_AP2_TIMER_1;
		apcr |= MPMU_APCR_SLPWP4;
		break;
	case IRQ_PXA910_AP2_TIMER2:
		awucrm = MPMU_AWUCRM_WAKEUP(4) | MPMU_AWUCRM_AP2_TIMER_2;
		apcr |= MPMU_APCR_SLPWP4;
		break;
	case IRQ_PXA910_AP2_TIMER3:
		awucrm = MPMU_AWUCRM_WAKEUP(4) | MPMU_AWUCRM_AP2_TIMER_3;
		apcr |= MPMU_APCR_SLPWP4;
		break;
	case IRQ_PXA910_RTC_ALARM:
		awucrm = MPMU_AWUCRM_WAKEUP(4) | MPMU_AWUCRM_RTC_ALARM;
		apcr |= MPMU_APCR_SLPWP4;
		break;
	/* wakeup line 5 */
	case IRQ_PXA910_USB1:
	case IRQ_PXA910_USB2:
		awucrm = MPMU_AWUCRM_WAKEUP(5);
		apcr |= MPMU_APCR_SLPWP5;
		break;
	/* wakeup line 6 */
	case IRQ_PXA910_MMC:
		awucrm = MPMU_AWUCRM_WAKEUP(6)
			| MPMU_AWUCRM_SDH1
			| MPMU_AWUCRM_SDH2;
		apcr |= MPMU_APCR_SLPWP6;
		break;
	/* wakeup line 7 */
	case IRQ_PXA910_PMIC_INT:
		awucrm = MPMU_AWUCRM_WAKEUP(7);
		apcr |= MPMU_APCR_SLPWP7;
		break;
	default:
		if (irq >= IRQ_GPIO_START && irq < IRQ_BOARD_START) {
			awucrm = MPMU_AWUCRM_WAKEUP(2);
			apcr |= MPMU_APCR_SLPWP2;
		} else {
			/* FIXME: This should return a proper error code ! */
			printk(KERN_ERR "Error: no defined wake up source irq: %d\n",
				irq);
		}
	}

	if (on) {
		if (awucrm) {
			awucrm |= __raw_readl(MPMU_AWUCRM);
			__raw_writel(awucrm, MPMU_AWUCRM);
		}
		if (apcr) {
			apcr = ~apcr & __raw_readl(MPMU_APCR);
			__raw_writel(apcr, MPMU_APCR);
		}
	} else {
		if (awucrm) {
			awucrm = ~awucrm & __raw_readl(MPMU_AWUCRM);
			__raw_writel(awucrm, MPMU_AWUCRM);
		}
		if (apcr) {
			apcr |= __raw_readl(MPMU_APCR);
			__raw_writel(apcr, MPMU_APCR);
		}
	}
	return 0;
}

void pxa910_pm_enter_lowpower_mode(int state)
{
	uint32_t idle_cfg, apcr;

	idle_cfg = __raw_readl(APMU_MOH_IDLE_CFG);
	apcr = __raw_readl(MPMU_APCR);

	apcr &= ~(MPMU_APCR_DDRCORSD | MPMU_APCR_APBSD | MPMU_APCR_AXISD
		| MPMU_APCR_VCTCXOSD | MPMU_APCR_STBYEN);
	idle_cfg &= ~(APMU_MOH_IDLE_CFG_MOH_IDLE
		| APMU_MOH_IDLE_CFG_MOH_PWRDWN);

	switch (state) {
	case POWER_MODE_UDR:
		/* only shutdown APB in UDR */
		apcr |= MPMU_APCR_STBYEN | MPMU_APCR_APBSD;
		fallthrough;
	case POWER_MODE_SYS_SLEEP:
		apcr |= MPMU_APCR_SLPEN;		/* set the SLPEN bit */
		apcr |= MPMU_APCR_VCTCXOSD;		/* set VCTCXOSD */
		fallthrough;
	case POWER_MODE_APPS_SLEEP:
		apcr |= MPMU_APCR_DDRCORSD;		/* set DDRCORSD */
		fallthrough;
	case POWER_MODE_APPS_IDLE:
		apcr |= MPMU_APCR_AXISD;		/* set AXISDD bit */
		fallthrough;
	case POWER_MODE_CORE_EXTIDLE:
		idle_cfg |= APMU_MOH_IDLE_CFG_MOH_IDLE;
		idle_cfg |= APMU_MOH_IDLE_CFG_MOH_PWRDWN;
		idle_cfg |= APMU_MOH_IDLE_CFG_MOH_PWR_SW(3)
			| APMU_MOH_IDLE_CFG_MOH_L2_PWR_SW(3);
		fallthrough;
	case POWER_MODE_CORE_INTIDLE:
		break;
	}

	/* program the memory controller hardware sleep type and auto wakeup */
	idle_cfg |= APMU_MOH_IDLE_CFG_MOH_DIS_MC_SW_REQ;
	idle_cfg |= APMU_MOH_IDLE_CFG_MOH_MC_WAKE_EN;
	__raw_writel(0x0, APMU_MC_HW_SLP_TYPE);		/* auto refresh */

	/* set DSPSD, DTCMSD, BBSD, MSASLPEN */
	apcr |= MPMU_APCR_DSPSD | MPMU_APCR_DTCMSD | MPMU_APCR_BBSD
		| MPMU_APCR_MSASLPEN;

	/*always set SLEPEN bit mainly for MSA*/
	apcr |= MPMU_APCR_SLPEN;

	/* finally write the registers back */
	__raw_writel(idle_cfg, APMU_MOH_IDLE_CFG);
	__raw_writel(apcr, MPMU_APCR);

}

static int pxa910_pm_enter(suspend_state_t state)
{
	unsigned int idle_cfg, reg = 0;

	/*pmic thread not completed,exit;otherwise system can't be waked up*/
	reg = __raw_readl(ICU_INT_CONF(IRQ_PXA910_PMIC_INT));
	if ((reg & 0x3) == 0)
		return -EAGAIN;

	idle_cfg = __raw_readl(APMU_MOH_IDLE_CFG);
	idle_cfg |= APMU_MOH_IDLE_CFG_MOH_PWRDWN
		| APMU_MOH_IDLE_CFG_MOH_SRAM_PWRDWN;
	__raw_writel(idle_cfg, APMU_MOH_IDLE_CFG);

	/* disable L2 */
	outer_disable();
	/* wait for l2 idle */
	while (!(readl(CIU_REG(0x8)) & (1 << 16)))
		udelay(1);

	cpu_do_idle();

	/* enable L2 */
	outer_resume();
	/* wait for l2 idle */
	while (!(readl(CIU_REG(0x8)) & (1 << 16)))
		udelay(1);

	idle_cfg = __raw_readl(APMU_MOH_IDLE_CFG);
	idle_cfg &= ~(APMU_MOH_IDLE_CFG_MOH_PWRDWN
		| APMU_MOH_IDLE_CFG_MOH_SRAM_PWRDWN);
	__raw_writel(idle_cfg, APMU_MOH_IDLE_CFG);

	return 0;
}

/*
 * Called after processes are frozen, but before we shut down devices.
 */
static int pxa910_pm_prepare(void)
{
	pxa910_pm_enter_lowpower_mode(POWER_MODE_UDR);
	return 0;
}

/*
 * Called after devices are re-setup, but before processes are thawed.
 */
static void pxa910_pm_finish(void)
{
	pxa910_pm_enter_lowpower_mode(POWER_MODE_CORE_INTIDLE);
}

static int pxa910_pm_valid(suspend_state_t state)
{
	return ((state == PM_SUSPEND_STANDBY) || (state == PM_SUSPEND_MEM));
}

static const struct platform_suspend_ops pxa910_pm_ops = {
	.valid		= pxa910_pm_valid,
	.prepare	= pxa910_pm_prepare,
	.enter		= pxa910_pm_enter,
	.finish		= pxa910_pm_finish,
};

static int __init pxa910_pm_init(void)
{
	uint32_t awucrm = 0;

	if (!cpu_is_pxa910())
		return -EIO;

	suspend_set_ops(&pxa910_pm_ops);

	/* Set the following bits for MMP3 playback with VCTXO on */
	__raw_writel(__raw_readl(APMU_SQU_CLK_GATE_CTRL) | (1 << 30),
		APMU_SQU_CLK_GATE_CTRL);
	__raw_writel(__raw_readl(MPMU_FCCR) | (1 << 28), MPMU_FCCR);

	awucrm |= MPMU_AWUCRM_AP_ASYNC_INT | MPMU_AWUCRM_AP_FULL_IDLE;
	__raw_writel(awucrm, MPMU_AWUCRM);

	return 0;
}

late_initcall(pxa910_pm_init);
