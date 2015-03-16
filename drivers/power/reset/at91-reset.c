/*
 * Atmel AT91 SAM9 SoCs reset code
 *
 * Copyright (C) 2007 Atmel Corporation.
 * Copyright (C) BitBox Ltd 2010
 * Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcosoft.com>
 * Copyright (C) 2014 Free Electrons
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>

#include <soc/at91/at91sam9_ddrsdr.h>
#include <soc/at91/at91sam9_sdramc.h>

#define AT91_RSTC_CR	0x00		/* Reset Controller Control Register */
#define AT91_RSTC_PROCRST	BIT(0)		/* Processor Reset */
#define AT91_RSTC_PERRST	BIT(2)		/* Peripheral Reset */
#define AT91_RSTC_EXTRST	BIT(3)		/* External Reset */
#define AT91_RSTC_KEY		(0xa5 << 24)	/* KEY Password */

#define AT91_RSTC_SR	0x04		/* Reset Controller Status Register */
#define AT91_RSTC_URSTS		BIT(0)		/* User Reset Status */
#define AT91_RSTC_RSTTYP	GENMASK(10, 8)	/* Reset Type */
#define AT91_RSTC_NRSTL		BIT(16)		/* NRST Pin Level */
#define AT91_RSTC_SRCMP		BIT(17)		/* Software Reset Command in Progress */

#define AT91_RSTC_MR	0x08		/* Reset Controller Mode Register */
#define AT91_RSTC_URSTEN	BIT(0)		/* User Reset Enable */
#define AT91_RSTC_URSTIEN	BIT(4)		/* User Reset Interrupt Enable */
#define AT91_RSTC_ERSTL		GENMASK(11, 8)	/* External Reset Length */

enum reset_type {
	RESET_TYPE_GENERAL	= 0,
	RESET_TYPE_WAKEUP	= 1,
	RESET_TYPE_WATCHDOG	= 2,
	RESET_TYPE_SOFTWARE	= 3,
	RESET_TYPE_USER		= 4,
};

static void __iomem *at91_ramc_base[2], *at91_rstc_base;

/*
* unless the SDRAM is cleanly shutdown before we hit the
* reset register it can be left driving the data bus and
* killing the chance of a subsequent boot from NAND
*/
static int at91sam9260_restart(struct notifier_block *this, unsigned long mode,
			       void *cmd)
{
	asm volatile(
		/* Align to cache lines */
		".balign 32\n\t"

		/* Disable SDRAM accesses */
		"str	%2, [%0, #" __stringify(AT91_SDRAMC_TR) "]\n\t"

		/* Power down SDRAM */
		"str	%3, [%0, #" __stringify(AT91_SDRAMC_LPR) "]\n\t"

		/* Reset CPU */
		"str	%4, [%1, #" __stringify(AT91_RSTC_CR) "]\n\t"

		"b	.\n\t"
		:
		: "r" (at91_ramc_base[0]),
		  "r" (at91_rstc_base),
		  "r" (1),
		  "r" (AT91_SDRAMC_LPCB_POWER_DOWN),
		  "r" (AT91_RSTC_KEY | AT91_RSTC_PERRST | AT91_RSTC_PROCRST));

	return NOTIFY_DONE;
}

static int at91sam9g45_restart(struct notifier_block *this, unsigned long mode,
			       void *cmd)
{
	asm volatile(
		/*
		 * Test wether we have a second RAM controller to care
		 * about.
		 *
		 * First, test that we can dereference the virtual address.
		 */
		"cmp	%1, #0\n\t"
		"beq	1f\n\t"

		/* Then, test that the RAM controller is enabled */
		"ldr	r0, [%1]\n\t"
		"cmp	r0, #0\n\t"

		/* Align to cache lines */
		".balign 32\n\t"

		/* Disable SDRAM0 accesses */
		"1:	str	%3, [%0, #" __stringify(AT91_DDRSDRC_RTR) "]\n\t"
		/* Power down SDRAM0 */
		"	str	%4, [%0, #" __stringify(AT91_DDRSDRC_LPR) "]\n\t"
		/* Disable SDRAM1 accesses */
		"	strne	%3, [%1, #" __stringify(AT91_DDRSDRC_RTR) "]\n\t"
		/* Power down SDRAM1 */
		"	strne	%4, [%1, #" __stringify(AT91_DDRSDRC_LPR) "]\n\t"
		/* Reset CPU */
		"	str	%5, [%2, #" __stringify(AT91_RSTC_CR) "]\n\t"

		"	b	.\n\t"
		:
		: "r" (at91_ramc_base[0]),
		  "r" (at91_ramc_base[1]),
		  "r" (at91_rstc_base),
		  "r" (1),
		  "r" (AT91_DDRSDRC_LPCB_POWER_DOWN),
		  "r" (AT91_RSTC_KEY | AT91_RSTC_PERRST | AT91_RSTC_PROCRST)
		: "r0");

	return NOTIFY_DONE;
}

static void __init at91_reset_status(struct platform_device *pdev)
{
	u32 reg = readl(at91_rstc_base + AT91_RSTC_SR);
	char *reason;

	switch ((reg & AT91_RSTC_RSTTYP) >> 8) {
	case RESET_TYPE_GENERAL:
		reason = "general reset";
		break;
	case RESET_TYPE_WAKEUP:
		reason = "wakeup";
		break;
	case RESET_TYPE_WATCHDOG:
		reason = "watchdog reset";
		break;
	case RESET_TYPE_SOFTWARE:
		reason = "software reset";
		break;
	case RESET_TYPE_USER:
		reason = "user reset";
		break;
	default:
		reason = "unknown reset";
		break;
	}

	pr_info("AT91: Starting after %s\n", reason);
}

static const struct of_device_id at91_ramc_of_match[] = {
	{ .compatible = "atmel,at91sam9260-sdramc", },
	{ .compatible = "atmel,at91sam9g45-ddramc", },
	{ .compatible = "atmel,sama5d3-ddramc", },
	{ /* sentinel */ }
};

static const struct of_device_id at91_reset_of_match[] = {
	{ .compatible = "atmel,at91sam9260-rstc", .data = at91sam9260_restart },
	{ .compatible = "atmel,at91sam9g45-rstc", .data = at91sam9g45_restart },
	{ /* sentinel */ }
};

static struct notifier_block at91_restart_nb = {
	.priority = 192,
};

static int at91_reset_of_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *np;
	int idx = 0;

	at91_rstc_base = of_iomap(pdev->dev.of_node, 0);
	if (!at91_rstc_base) {
		dev_err(&pdev->dev, "Could not map reset controller address\n");
		return -ENODEV;
	}

	for_each_matching_node(np, at91_ramc_of_match) {
		at91_ramc_base[idx] = of_iomap(np, 0);
		if (!at91_ramc_base[idx]) {
			dev_err(&pdev->dev, "Could not map ram controller address\n");
			return -ENODEV;
		}
		idx++;
	}

	match = of_match_node(at91_reset_of_match, pdev->dev.of_node);
	at91_restart_nb.notifier_call = match->data;
	return register_restart_handler(&at91_restart_nb);
}

static int at91_reset_platform_probe(struct platform_device *pdev)
{
	const struct platform_device_id *match;
	struct resource *res;
	int idx = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	at91_rstc_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(at91_rstc_base)) {
		dev_err(&pdev->dev, "Could not map reset controller address\n");
		return PTR_ERR(at91_rstc_base);
	}

	for (idx = 0; idx < 2; idx++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, idx + 1 );
		at91_ramc_base[idx] = devm_ioremap(&pdev->dev, res->start,
						   resource_size(res));
		if (IS_ERR(at91_ramc_base[idx])) {
			dev_err(&pdev->dev, "Could not map ram controller address\n");
			return PTR_ERR(at91_ramc_base[idx]);
		}
	}

	match = platform_get_device_id(pdev);
	at91_restart_nb.notifier_call =
		(int (*)(struct notifier_block *,
			 unsigned long, void *)) match->driver_data;

	return register_restart_handler(&at91_restart_nb);
}

static int at91_reset_probe(struct platform_device *pdev)
{
	int ret;

	if (pdev->dev.of_node)
		ret = at91_reset_of_probe(pdev);
	else
		ret = at91_reset_platform_probe(pdev);

	if (ret)
		return ret;

	at91_reset_status(pdev);

	return 0;
}

static struct platform_device_id at91_reset_plat_match[] = {
	{ "at91-sam9260-reset", (unsigned long)at91sam9260_restart },
	{ "at91-sam9g45-reset", (unsigned long)at91sam9g45_restart },
	{ /* sentinel */ }
};

static struct platform_driver at91_reset_driver = {
	.probe = at91_reset_probe,
	.driver = {
		.name = "at91-reset",
		.of_match_table = at91_reset_of_match,
	},
	.id_table = at91_reset_plat_match,
};
module_platform_driver(at91_reset_driver);
