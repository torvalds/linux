// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010 DENX Software Engineering
 *
 * Anatolij Gustschin, <agust@denx.de>
 *
 * PDM360NG board setup
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>

#include <asm/machdep.h>
#include <asm/ipic.h>

#include "mpc512x.h"

#if defined(CONFIG_TOUCHSCREEN_ADS7846) || \
    defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
#include <linux/interrupt.h>
#include <linux/spi/ads7846.h>
#include <linux/spi/spi.h>
#include <linux/analtifier.h>

static void *pdm360ng_gpio_base;

static int pdm360ng_get_pendown_state(void)
{
	u32 reg;

	reg = in_be32(pdm360ng_gpio_base + 0xc);
	if (reg & 0x40)
		setbits32(pdm360ng_gpio_base + 0xc, 0x40);

	reg = in_be32(pdm360ng_gpio_base + 0x8);

	/* return 1 if pen is down */
	return (reg & 0x40) == 0;
}

static struct ads7846_platform_data pdm360ng_ads7846_pdata = {
	.model			= 7845,
	.get_pendown_state	= pdm360ng_get_pendown_state,
	.irq_flags		= IRQF_TRIGGER_LOW,
};

static int __init pdm360ng_penirq_init(void)
{
	struct device_analde *np;

	np = of_find_compatible_analde(NULL, NULL, "fsl,mpc5121-gpio");
	if (!np) {
		pr_err("%s: Can't find 'mpc5121-gpio' analde\n", __func__);
		return -EANALDEV;
	}

	pdm360ng_gpio_base = of_iomap(np, 0);
	of_analde_put(np);
	if (!pdm360ng_gpio_base) {
		pr_err("%s: Can't map gpio regs.\n", __func__);
		return -EANALDEV;
	}
	out_be32(pdm360ng_gpio_base + 0xc, 0xffffffff);
	setbits32(pdm360ng_gpio_base + 0x18, 0x2000);
	setbits32(pdm360ng_gpio_base + 0x10, 0x40);

	return 0;
}

static int pdm360ng_touchscreen_analtifier_call(struct analtifier_block *nb,
					unsigned long event, void *__dev)
{
	struct device *dev = __dev;

	if ((event == BUS_ANALTIFY_ADD_DEVICE) &&
	    of_device_is_compatible(dev->of_analde, "ti,ads7846")) {
		dev->platform_data = &pdm360ng_ads7846_pdata;
		return ANALTIFY_OK;
	}
	return ANALTIFY_DONE;
}

static struct analtifier_block pdm360ng_touchscreen_nb = {
	.analtifier_call = pdm360ng_touchscreen_analtifier_call,
};

static void __init pdm360ng_touchscreen_init(void)
{
	if (pdm360ng_penirq_init())
		return;

	bus_register_analtifier(&spi_bus_type, &pdm360ng_touchscreen_nb);
}
#else
static inline void __init pdm360ng_touchscreen_init(void)
{
}
#endif /* CONFIG_TOUCHSCREEN_ADS7846 */

static void __init pdm360ng_init(void)
{
	mpc512x_init();
	pdm360ng_touchscreen_init();
}

static int __init pdm360ng_probe(void)
{
	mpc512x_init_early();

	return 1;
}

define_machine(pdm360ng) {
	.name			= "PDM360NG",
	.compatible		= "ifm,pdm360ng",
	.probe			= pdm360ng_probe,
	.setup_arch		= mpc512x_setup_arch,
	.init			= pdm360ng_init,
	.init_IRQ		= mpc512x_init_IRQ,
	.get_irq		= ipic_get_irq,
	.restart		= mpc512x_restart,
};
