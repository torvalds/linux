/*
 * Backlight control code for Sharp Zaurus SL-5500
 *
 * Copyright 2005 John Lenz <lenz@cs.wisc.edu>
 * Maintainer: Pavel Machek <pavel@suse.cz> (unless John wants to :-)
 * GPL v2
 *
 * This driver assumes single CPU. That's okay, because collie is
 * slightly old hardware, and noone is going to retrofit second CPU to
 * old PDA.
 */

/* LCD power functions */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>

#include <asm/hardware/locomo.h>
#include <asm/irq.h>

#ifdef CONFIG_SA1100_COLLIE
#include <asm/arch/collie.h>
#else
#include <asm/arch/poodle.h>
#endif

extern void (*sa1100fb_lcd_power)(int on);

static struct locomo_dev *locomolcd_dev;

static void locomolcd_on(int comadj)
{
	locomo_gpio_set_dir(locomolcd_dev, LOCOMO_GPIO_LCD_VSHA_ON, 0);
	locomo_gpio_write(locomolcd_dev, LOCOMO_GPIO_LCD_VSHA_ON, 1);
	mdelay(2);

	locomo_gpio_set_dir(locomolcd_dev, LOCOMO_GPIO_LCD_VSHD_ON, 0);
	locomo_gpio_write(locomolcd_dev, LOCOMO_GPIO_LCD_VSHD_ON, 1);
	mdelay(2);

	locomo_m62332_senddata(locomolcd_dev, comadj, 0);
	mdelay(5);

	locomo_gpio_set_dir(locomolcd_dev, LOCOMO_GPIO_LCD_VEE_ON, 0);
	locomo_gpio_write(locomolcd_dev, LOCOMO_GPIO_LCD_VEE_ON, 1);
	mdelay(10);

	/* TFTCRST | CPSOUT=0 | CPSEN */
	locomo_writel(0x01, locomolcd_dev->mapbase + LOCOMO_TC);

	/* Set CPSD */
	locomo_writel(6, locomolcd_dev->mapbase + LOCOMO_CPSD);

	/* TFTCRST | CPSOUT=0 | CPSEN */
	locomo_writel((0x04 | 0x01), locomolcd_dev->mapbase + LOCOMO_TC);
	mdelay(10);

	locomo_gpio_set_dir(locomolcd_dev, LOCOMO_GPIO_LCD_MOD, 0);
	locomo_gpio_write(locomolcd_dev, LOCOMO_GPIO_LCD_MOD, 1);
}

static void locomolcd_off(int comadj)
{
	/* TFTCRST=1 | CPSOUT=1 | CPSEN = 0 */
	locomo_writel(0x06, locomolcd_dev->mapbase + LOCOMO_TC);
	mdelay(1);

	locomo_gpio_write(locomolcd_dev, LOCOMO_GPIO_LCD_VSHA_ON, 0);
	mdelay(110);

	locomo_gpio_write(locomolcd_dev, LOCOMO_GPIO_LCD_VEE_ON, 0);
	mdelay(700);

	/* TFTCRST=0 | CPSOUT=0 | CPSEN = 0 */
	locomo_writel(0, locomolcd_dev->mapbase + LOCOMO_TC);
	locomo_gpio_write(locomolcd_dev, LOCOMO_GPIO_LCD_MOD, 0);
	locomo_gpio_write(locomolcd_dev, LOCOMO_GPIO_LCD_VSHD_ON, 0);
}

void locomolcd_power(int on)
{
	int comadj = 118;
	unsigned long flags;

	local_irq_save(flags);

	if (!locomolcd_dev) {
		local_irq_restore(flags);
		return;
	}

	/* read comadj */
#ifdef CONFIG_MACH_POODLE
	comadj = 118;
#else
	comadj = 128;
#endif

	if (on)
		locomolcd_on(comadj);
	else
		locomolcd_off(comadj);

	local_irq_restore(flags);
}
EXPORT_SYMBOL(locomolcd_power);

static int poodle_lcd_probe(struct locomo_dev *dev)
{
	unsigned long flags;

	local_irq_save(flags);
	locomolcd_dev = dev;

	/* the poodle_lcd_power function is called for the first time
	 * from fs_initcall, which is before locomo is activated.
	 * We need to recall poodle_lcd_power here*/
#ifdef CONFIG_MACH_POODLE
	locomolcd_power(1);
#endif
	local_irq_restore(flags);
	return 0;
}

static int poodle_lcd_remove(struct locomo_dev *dev)
{
	unsigned long flags;
	local_irq_save(flags);
	locomolcd_dev = NULL;
	local_irq_restore(flags);
	return 0;
}

static struct locomo_driver poodle_lcd_driver = {
	.drv = {
		.name = "locomo-backlight",
	},
	.devid	= LOCOMO_DEVID_BACKLIGHT,
	.probe	= poodle_lcd_probe,
	.remove	= poodle_lcd_remove,
};

static int __init poodle_lcd_init(void)
{
	int ret = locomo_driver_register(&poodle_lcd_driver);
	if (ret) return ret;

#ifdef CONFIG_SA1100_COLLIE
	sa1100fb_lcd_power = locomolcd_power;
#endif
	return 0;
}
device_initcall(poodle_lcd_init);

