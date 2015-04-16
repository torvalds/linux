/*
 * Copyright (C) 2013 TangoTec Ltd.
 * Author: Baruch Siach <baruch@tkos.co.il>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver for the Xtensa LX4 GPIO32 Option
 *
 * Documentation: Xtensa LX4 Microprocessor Data Book, Section 2.22
 *
 * GPIO32 is a standard optional extension to the Xtensa architecture core that
 * provides preconfigured output and input ports for intra SoC signaling. The
 * GPIO32 option is implemented as 32bit Tensilica Instruction Extension (TIE)
 * output state called EXPSTATE, and 32bit input wire called IMPWIRE. This
 * driver treats input and output states as two distinct devices.
 *
 * Access to GPIO32 specific instructions is controlled by the CPENABLE
 * (Coprocessor Enable Bits) register. By default Xtensa Linux startup code
 * disables access to all coprocessors. This driver sets the CPENABLE bit
 * corresponding to GPIO32 before any GPIO32 specific instruction, and restores
 * CPENABLE state after that.
 *
 * This driver is currently incompatible with SMP. The GPIO32 extension is not
 * guaranteed to be available in all cores. Moreover, each core controls a
 * different set of IO wires. A theoretical SMP aware version of this driver
 * would need to have a per core workqueue to do the actual GPIO manipulation.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>

#include <asm/coprocessor.h> /* CPENABLE read/write macros */

#ifndef XCHAL_CP_ID_XTIOP
#error GPIO32 option is not enabled for your xtensa core variant
#endif

#if XCHAL_HAVE_CP

static inline unsigned long enable_cp(unsigned long *cpenable)
{
	unsigned long flags;

	local_irq_save(flags);
	RSR_CPENABLE(*cpenable);
	WSR_CPENABLE(*cpenable | BIT(XCHAL_CP_ID_XTIOP));

	return flags;
}

static inline void disable_cp(unsigned long flags, unsigned long cpenable)
{
	WSR_CPENABLE(cpenable);
	local_irq_restore(flags);
}

#else

static inline unsigned long enable_cp(unsigned long *cpenable)
{
	*cpenable = 0; /* avoid uninitialized value warning */
	return 0;
}

static inline void disable_cp(unsigned long flags, unsigned long cpenable)
{
}

#endif /* XCHAL_HAVE_CP */

static int xtensa_impwire_get_direction(struct gpio_chip *gc, unsigned offset)
{
	return 1; /* input only */
}

static int xtensa_impwire_get_value(struct gpio_chip *gc, unsigned offset)
{
	unsigned long flags, saved_cpenable;
	u32 impwire;

	flags = enable_cp(&saved_cpenable);
	__asm__ __volatile__("read_impwire %0" : "=a" (impwire));
	disable_cp(flags, saved_cpenable);

	return !!(impwire & BIT(offset));
}

static void xtensa_impwire_set_value(struct gpio_chip *gc, unsigned offset,
				    int value)
{
	BUG(); /* output only; should never be called */
}

static int xtensa_expstate_get_direction(struct gpio_chip *gc, unsigned offset)
{
	return 0; /* output only */
}

static int xtensa_expstate_get_value(struct gpio_chip *gc, unsigned offset)
{
	unsigned long flags, saved_cpenable;
	u32 expstate;

	flags = enable_cp(&saved_cpenable);
	__asm__ __volatile__("rur.expstate %0" : "=a" (expstate));
	disable_cp(flags, saved_cpenable);

	return !!(expstate & BIT(offset));
}

static void xtensa_expstate_set_value(struct gpio_chip *gc, unsigned offset,
				     int value)
{
	unsigned long flags, saved_cpenable;
	u32 mask = BIT(offset);
	u32 val = value ? BIT(offset) : 0;

	flags = enable_cp(&saved_cpenable);
	__asm__ __volatile__("wrmsk_expstate %0, %1"
			     :: "a" (val), "a" (mask));
	disable_cp(flags, saved_cpenable);
}

static struct gpio_chip impwire_chip = {
	.label		= "impwire",
	.base		= -1,
	.ngpio		= 32,
	.get_direction	= xtensa_impwire_get_direction,
	.get		= xtensa_impwire_get_value,
	.set		= xtensa_impwire_set_value,
};

static struct gpio_chip expstate_chip = {
	.label		= "expstate",
	.base		= -1,
	.ngpio		= 32,
	.get_direction	= xtensa_expstate_get_direction,
	.get		= xtensa_expstate_get_value,
	.set		= xtensa_expstate_set_value,
};

static int xtensa_gpio_probe(struct platform_device *pdev)
{
	int ret;

	ret = gpiochip_add(&impwire_chip);
	if (ret)
		return ret;
	return gpiochip_add(&expstate_chip);
}

static struct platform_driver xtensa_gpio_driver = {
	.driver		= {
		.name		= "xtensa-gpio",
	},
	.probe		= xtensa_gpio_probe,
};

static int __init xtensa_gpio_init(void)
{
	struct platform_device *pdev;

	pdev = platform_device_register_simple("xtensa-gpio", 0, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	return platform_driver_register(&xtensa_gpio_driver);
}
device_initcall(xtensa_gpio_init);

MODULE_AUTHOR("Baruch Siach <baruch@tkos.co.il>");
MODULE_DESCRIPTION("Xtensa LX4 GPIO32 driver");
MODULE_LICENSE("GPL");
