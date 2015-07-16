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
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

/* Power Management Control 16bit (R/W) */
#define U300_SYSCON_PMCR					(0x50)
#define U300_SYSCON_PMCR_DCON_ENABLE				(0x0002)
#define U300_SYSCON_PMCR_PWR_MGNT_ENABLE			(0x0001)

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
static int __init __u300_init_boardpower(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *syscon_np;
	struct regmap *regmap;
	int err;

	pr_info("U300: setting up board power\n");

	syscon_np = of_parse_phandle(np, "syscon", 0);
	if (!syscon_np) {
		pr_crit("U300: no syscon node\n");
		return -ENODEV;
	}
	regmap = syscon_node_to_regmap(syscon_np);
	if (IS_ERR(regmap)) {
		pr_crit("U300: could not locate syscon regmap\n");
		return PTR_ERR(regmap);
	}

	main_power_15 = regulator_get(&pdev->dev, "vana15");

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
	regmap_update_bits(regmap, U300_SYSCON_PMCR,
			   U300_SYSCON_PMCR_DCON_ENABLE, 0);

	/* Register globally exported PM poweroff hook */
	pm_power_off = u300_pm_poweroff;

	return 0;
}

static int __init s365_board_probe(struct platform_device *pdev)
{
	return __u300_init_boardpower(pdev);
}

static const struct of_device_id s365_board_match[] = {
	{ .compatible = "stericsson,s365" },
	{},
};

static struct platform_driver s365_board_driver = {
	.driver		= {
		.name   = "s365-board",
		.of_match_table = s365_board_match,
	},
};

/*
 * So at module init time we hog the regulator!
 */
static int __init u300_init_boardpower(void)
{
	return platform_driver_probe(&s365_board_driver,
				     s365_board_probe);
}

device_initcall(u300_init_boardpower);
