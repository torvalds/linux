// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2012 John Crispin <john@phrozen.org>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/legacy-of-mm-gpiochip.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <lantiq_soc.h>

/*
 * By attaching hardware latches to the EBU it is possible to create output
 * only gpios. This driver configures a special memory address, which when
 * written to outputs 16 bit to the latches.
 */

#define LTQ_EBU_BUSCON	0x1e7ff		/* 16 bit access, slowest timing */
#define LTQ_EBU_WP	0x80000000	/* write protect bit */

struct ltq_mm {
	struct of_mm_gpio_chip mmchip;
	u16 shadow;	/* shadow the latches state */
};

/**
 * ltq_mm_apply() - write the shadow value to the ebu address.
 * @chip:     Pointer to our private data structure.
 *
 * Write the shadow value to the EBU to set the gpios. We need to set the
 * global EBU lock to make sure that PCI/MTD don't break.
 */
static void ltq_mm_apply(struct ltq_mm *chip)
{
	unsigned long flags;

	spin_lock_irqsave(&ebu_lock, flags);
	ltq_ebu_w32(LTQ_EBU_BUSCON, LTQ_EBU_BUSCON1);
	__raw_writew(chip->shadow, chip->mmchip.regs);
	ltq_ebu_w32(LTQ_EBU_BUSCON | LTQ_EBU_WP, LTQ_EBU_BUSCON1);
	spin_unlock_irqrestore(&ebu_lock, flags);
}

/**
 * ltq_mm_set() - gpio_chip->set - set gpios.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 * @val:    Value to be written to specified signal.
 *
 * Set the shadow value and call ltq_mm_apply.
 */
static void ltq_mm_set(struct gpio_chip *gc, unsigned offset, int value)
{
	struct ltq_mm *chip = gpiochip_get_data(gc);

	if (value)
		chip->shadow |= (1 << offset);
	else
		chip->shadow &= ~(1 << offset);
	ltq_mm_apply(chip);
}

/**
 * ltq_mm_dir_out() - gpio_chip->dir_out - set gpio direction.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 * @val:    Value to be written to specified signal.
 *
 * Same as ltq_mm_set, always returns 0.
 */
static int ltq_mm_dir_out(struct gpio_chip *gc, unsigned offset, int value)
{
	ltq_mm_set(gc, offset, value);

	return 0;
}

/**
 * ltq_mm_save_regs() - Set initial values of GPIO pins
 * @mm_gc: pointer to memory mapped GPIO chip structure
 */
static void ltq_mm_save_regs(struct of_mm_gpio_chip *mm_gc)
{
	struct ltq_mm *chip =
		container_of(mm_gc, struct ltq_mm, mmchip);

	/* tell the ebu controller which memory address we will be using */
	ltq_ebu_w32(CPHYSADDR(chip->mmchip.regs) | 0x1, LTQ_EBU_ADDRSEL1);

	ltq_mm_apply(chip);
}

static int ltq_mm_probe(struct platform_device *pdev)
{
	struct ltq_mm *chip;
	u32 shadow;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	platform_set_drvdata(pdev, chip);

	chip->mmchip.gc.ngpio = 16;
	chip->mmchip.gc.direction_output = ltq_mm_dir_out;
	chip->mmchip.gc.set = ltq_mm_set;
	chip->mmchip.save_regs = ltq_mm_save_regs;

	/* store the shadow value if one was passed by the devicetree */
	if (!of_property_read_u32(pdev->dev.of_node, "lantiq,shadow", &shadow))
		chip->shadow = shadow;

	return of_mm_gpiochip_add_data(pdev->dev.of_node, &chip->mmchip, chip);
}

static void ltq_mm_remove(struct platform_device *pdev)
{
	struct ltq_mm *chip = platform_get_drvdata(pdev);

	of_mm_gpiochip_remove(&chip->mmchip);
}

static const struct of_device_id ltq_mm_match[] = {
	{ .compatible = "lantiq,gpio-mm" },
	{},
};
MODULE_DEVICE_TABLE(of, ltq_mm_match);

static struct platform_driver ltq_mm_driver = {
	.probe = ltq_mm_probe,
	.remove_new = ltq_mm_remove,
	.driver = {
		.name = "gpio-mm-ltq",
		.of_match_table = ltq_mm_match,
	},
};

static int __init ltq_mm_init(void)
{
	return platform_driver_register(&ltq_mm_driver);
}

subsys_initcall(ltq_mm_init);

static void __exit ltq_mm_exit(void)
{
	platform_driver_unregister(&ltq_mm_driver);
}
module_exit(ltq_mm_exit);
