/*
 * arch/arm/mach-u300/regulator.c
 *
 * Copyright (C) 2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * Handle board-bound regulators and board power not related
 * to any devices.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */
#include <linux/device.h>
#include <linux/signal.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
/* Those are just for writing in syscon */
#include <linux/io.h>
#include <mach/hardware.h>
#include <mach/syscon.h>

/*
 * Regulators that power the board and chip and which are
 * not copuled to specific drivers are hogged in these
 * instances.
 */
static struct regulator *main_power_15;

/*
 * This function is used from pm.h to shut down the system by
 * resetting all regulators in turn and then disable regulator
 * LDO D (main power).
 */
void u300_pm_poweroff(void)
{
	sigset_t old, all;

	sigfillset(&all);
	if (!sigprocmask(SIG_BLOCK, &all, &old)) {
		/* Disable LDO D to shut down the system */
		if (main_power_15)
			regulator_disable(main_power_15);
		else
			pr_err("regulator not available to shut down system\n");
		(void) sigprocmask(SIG_SETMASK, &old, NULL);
	}
	return;
}

/*
 * Hog the regulators needed to power up the board.
 */
static int __init u300_init_boardpower(void)
{
	int err;
	u32 val;

	pr_info("U300: setting up board power\n");
	main_power_15 = regulator_get(NULL, "vana15");
	if (IS_ERR(main_power_15)) {
		pr_err("could not get vana15");
		return PTR_ERR(main_power_15);
	}
	err = regulator_enable(main_power_15);
	if (err) {
		pr_err("could not enable vana15\n");
		return err;
	}

	/*
	 * On U300 a special system controller register pulls up the DC
	 * until the vana15 (LDO D) regulator comes up. At this point, all
	 * regulators are set and we do not need power control via
	 * DC ON anymore. This function will likely be moved whenever
	 * the rest of the U300 power management is implemented.
	 */
	pr_info("U300: disable system controller pull-up\n");
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_PMCR);
	val &= ~U300_SYSCON_PMCR_DCON_ENABLE;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_PMCR);

	/* Register globally exported PM poweroff hook */
	pm_power_off = u300_pm_poweroff;

	return 0;
}

/*
 * So at module init time we hog the regulator!
 */
module_init(u300_init_boardpower);
