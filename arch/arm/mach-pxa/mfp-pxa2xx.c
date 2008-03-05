/*
 *  linux/arch/arm/mach-pxa/mfp-pxa2xx.c
 *
 *  PXA2xx pin mux configuration support
 *
 *  The GPIOs on PXA2xx can be configured as one of many alternate
 *  functions, this is by concept samilar to the MFP configuration
 *  on PXA3xx,  what's more important, the low power pin state and
 *  wakeup detection are also supported by the same framework.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sysdev.h>

#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/mfp-pxa2xx.h>

#include "generic.h"

#define PGSR(x)		__REG2(0x40F00020, ((x) & 0x60) >> 3)

#define PWER_WE35	(1 << 24)

static struct {
	unsigned	valid		: 1;
	unsigned	can_wakeup	: 1;
	unsigned	keypad_gpio	: 1;
	unsigned int	mask; /* bit mask in PWER or PKWR */
	unsigned long	config;
} gpio_desc[MFP_PIN_GPIO127 + 1];

static inline int __mfp_config_gpio(unsigned gpio, unsigned long c)
{
	unsigned long gafr, mask = GPIO_bit(gpio);
	int fn;

	fn = MFP_AF(c);
	if (fn > 3)
		return -EINVAL;

	/* alternate function and direction */
	gafr = GAFR(gpio) & ~(0x3 << ((gpio & 0xf) * 2));
	GAFR(gpio) = gafr |  (fn  << ((gpio & 0xf) * 2));

	if (c & MFP_DIR_OUT)
		GPDR(gpio) |= mask;
	else
		GPDR(gpio) &= ~mask;

	/* low power state */
	switch (c & MFP_LPM_STATE_MASK) {
	case MFP_LPM_DRIVE_HIGH:
		PGSR(gpio) |= mask;
		break;
	case MFP_LPM_DRIVE_LOW:
		PGSR(gpio) &= ~mask;
		break;
	case MFP_LPM_INPUT:
		break;
	default:
		pr_warning("%s: invalid low power state for GPIO%d\n",
				__func__, gpio);
		return -EINVAL;
	}

	/* wakeup enabling */
	if ((c & MFP_LPM_WAKEUP_ENABLE) == 0)
		return 0;

	if (!gpio_desc[gpio].can_wakeup || c & MFP_DIR_OUT) {
		pr_warning("%s: GPIO%d unable to wakeup\n",
				__func__, gpio);
		return -EINVAL;
	}

	if (gpio_desc[gpio].keypad_gpio)
		PKWR |= gpio_desc[gpio].mask;
	else {
		PWER |= gpio_desc[gpio].mask;

		if (c & MFP_LPM_EDGE_RISE)
			PRER |= gpio_desc[gpio].mask;

		if (c & MFP_LPM_EDGE_FALL)
			PFER |= gpio_desc[gpio].mask;
	}

	return 0;
}

void pxa2xx_mfp_config(unsigned long *mfp_cfgs, int num)
{
	unsigned long flags;
	unsigned long *c;
	int i, gpio;

	for (i = 0, c = mfp_cfgs; i < num; i++, c++) {

		gpio = mfp_to_gpio(MFP_PIN(*c));

		if (!gpio_desc[gpio].valid) {
			pr_warning("%s: GPIO%d is invalid pin\n",
				__func__, gpio);
			continue;
		}

		local_irq_save(flags);

		gpio_desc[gpio].config = *c;
		__mfp_config_gpio(gpio, *c);

		local_irq_restore(flags);
	}
}

#ifdef CONFIG_PXA25x
static int __init pxa25x_mfp_init(void)
{
	int i;

	if (cpu_is_pxa25x()) {
		for (i = 0; i <= 84; i++)
			gpio_desc[i].valid = 1;

		for (i = 0; i <= 15; i++) {
			gpio_desc[i].can_wakeup = 1;
			gpio_desc[i].mask = GPIO_bit(i);
		}
	}

	return 0;
}
postcore_initcall(pxa25x_mfp_init);
#endif /* CONFIG_PXA25x */

#ifdef CONFIG_PXA27x
static int pxa27x_pkwr_gpio[] __initdata = {
	13, 16, 17, 34, 36, 37, 38, 39, 90, 91, 93, 94,
	95, 96, 97, 98, 99, 100, 101, 102
};

static int __init pxa27x_mfp_init(void)
{
	int i, gpio;

	if (cpu_is_pxa27x()) {
		for (i = 0; i <= 120; i++) {
			/* skip GPIO2, 5, 6, 7, 8, they are not
			 * valid pins allow configuration
			 */
			if (i == 2 || i == 5 || i == 6 ||
			    i == 7 || i == 8)
				continue;

			gpio_desc[i].valid = 1;
		}

		/* Keypad GPIOs */
		for (i = 0; i < ARRAY_SIZE(pxa27x_pkwr_gpio); i++) {
			gpio = pxa27x_pkwr_gpio[i];
			gpio_desc[gpio].can_wakeup = 1;
			gpio_desc[gpio].keypad_gpio = 1;
			gpio_desc[gpio].mask = 1 << i;
		}

		/* Overwrite GPIO13 as a PWER wakeup source */
		for (i = 0; i <= 15; i++) {
			/* skip GPIO2, 5, 6, 7, 8 */
			if (GPIO_bit(i) & 0x1e4)
				continue;

			gpio_desc[i].can_wakeup = 1;
			gpio_desc[i].mask = GPIO_bit(i);
		}

		gpio_desc[35].can_wakeup = 1;
		gpio_desc[35].mask = PWER_WE35;
	}

	return 0;
}
postcore_initcall(pxa27x_mfp_init);
#endif /* CONFIG_PXA27x */
