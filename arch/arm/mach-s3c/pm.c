// SPDX-License-Identifier: GPL-2.0
//
// Copyright 2008 Openmoko, Inc.
// Copyright 2004-2008 Simtec Electronics
//	Ben Dooks <ben@simtec.co.uk>
//	http://armlinux.simtec.co.uk/
//
// S3C common power management (suspend to ram) support.

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/serial_s3c.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/suspend.h>

#include "map.h"
#include "regs-clock.h"
#include "regs-irq.h"
#include "irqs.h"

#include <asm/irq.h>

#include "cpu.h"
#include "pm.h"
#include "pm-core.h"

/* for external use */

unsigned long s3c_pm_flags;

/* The IRQ ext-int code goes here, it is too small to currently bother
 * with its own file. */

unsigned long s3c_irqwake_intmask	= 0xffffffffL;
unsigned long s3c_irqwake_eintmask	= 0xffffffffL;

int s3c_irqext_wake(struct irq_data *data, unsigned int state)
{
	unsigned long bit = 1L << IRQ_EINT_BIT(data->irq);

	if (!(s3c_irqwake_eintallow & bit))
		return -ENOENT;

	printk(KERN_INFO "wake %s for irq %d\n",
	       state ? "enabled" : "disabled", data->irq);

	if (!state)
		s3c_irqwake_eintmask |= bit;
	else
		s3c_irqwake_eintmask &= ~bit;

	return 0;
}

void (*pm_cpu_prep)(void);
int (*pm_cpu_sleep)(unsigned long);

#define any_allowed(mask, allow) (((mask) & (allow)) != (allow))

/* s3c_pm_enter
 *
 * central control for sleep/resume process
*/

static int s3c_pm_enter(suspend_state_t state)
{
	int ret;
	/* ensure the debug is initialised (if enabled) */
	s3c_pm_debug_init_uart();

	S3C_PMDBG("%s(%d)\n", __func__, state);

	if (pm_cpu_prep == NULL || pm_cpu_sleep == NULL) {
		printk(KERN_ERR "%s: error: no cpu sleep function\n", __func__);
		return -EINVAL;
	}

	/* check if we have anything to wake-up with... bad things seem
	 * to happen if you suspend with no wakeup (system will often
	 * require a full power-cycle)
	*/

	if (!of_have_populated_dt() &&
	    !any_allowed(s3c_irqwake_intmask, s3c_irqwake_intallow) &&
	    !any_allowed(s3c_irqwake_eintmask, s3c_irqwake_eintallow)) {
		printk(KERN_ERR "%s: No wake-up sources!\n", __func__);
		printk(KERN_ERR "%s: Aborting sleep\n", __func__);
		return -EINVAL;
	}

	/* save all necessary core registers not covered by the drivers */

	if (!of_have_populated_dt()) {
		samsung_pm_save_gpios();
		samsung_pm_saved_gpios();
	}

	s3c_pm_save_uarts(soc_is_s3c2410());
	s3c_pm_save_core();

	/* set the irq configuration for wake */

	s3c_pm_configure_extint();

	S3C_PMDBG("sleep: irq wakeup masks: %08lx,%08lx\n",
	    s3c_irqwake_intmask, s3c_irqwake_eintmask);

	s3c_pm_arch_prepare_irqs();

	/* call cpu specific preparation */

	pm_cpu_prep();

	/* flush cache back to ram */

	flush_cache_all();

	s3c_pm_check_store();

	/* send the cpu to sleep... */

	s3c_pm_arch_stop_clocks();

	/* this will also act as our return point from when
	 * we resume as it saves its own register state and restores it
	 * during the resume.  */

	ret = cpu_suspend(0, pm_cpu_sleep);
	if (ret)
		return ret;

	/* restore the system state */

	s3c_pm_restore_core();
	s3c_pm_restore_uarts(soc_is_s3c2410());

	if (!of_have_populated_dt()) {
		samsung_pm_restore_gpios();
		s3c_pm_restored_gpios();
	}

	s3c_pm_debug_init_uart();

	/* check what irq (if any) restored the system */

	s3c_pm_arch_show_resume_irqs();

	S3C_PMDBG("%s: post sleep, preparing to return\n", __func__);

	/* LEDs should now be 1110 */
	s3c_pm_debug_smdkled(1 << 1, 0);

	s3c_pm_check_restore();

	/* ok, let's return from sleep */

	S3C_PMDBG("S3C PM Resume (post-restore)\n");
	return 0;
}

static int s3c_pm_prepare(void)
{
	/* prepare check area if configured */

	s3c_pm_check_prepare();
	return 0;
}

static void s3c_pm_finish(void)
{
	s3c_pm_check_cleanup();
}

static const struct platform_suspend_ops s3c_pm_ops = {
	.enter		= s3c_pm_enter,
	.prepare	= s3c_pm_prepare,
	.finish		= s3c_pm_finish,
	.valid		= suspend_valid_only_mem,
};

/* s3c_pm_init
 *
 * Attach the power management functions. This should be called
 * from the board specific initialisation if the board supports
 * it.
*/

int __init s3c_pm_init(void)
{
	printk("S3C Power Management, Copyright 2004 Simtec Electronics\n");

	suspend_set_ops(&s3c_pm_ops);
	return 0;
}
