/*
 *  Backlight Driver for HP Jornada 680
 *
 *  Copyright (c) 2005 Andriy Skulysh
 *
 *  Based on Sharp's Corgi Backlight Driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/fb.h>
#include <linux/backlight.h>

#include <cpu/dac.h>
#include <mach/hp6xx.h>
#include <asm/hd64461.h>

#define HP680_MAX_INTENSITY 255
#define HP680_DEFAULT_INTENSITY 10

static int hp680bl_suspended;
static int current_intensity;
static DEFINE_SPINLOCK(bl_lock);

static void hp680bl_send_intensity(struct backlight_device *bd)
{
	unsigned long flags;
	u16 v;
	int intensity = bd->props.brightness;

	if (bd->props.power != FB_BLANK_UNBLANK)
		intensity = 0;
	if (bd->props.fb_blank != FB_BLANK_UNBLANK)
		intensity = 0;
	if (hp680bl_suspended)
		intensity = 0;

	spin_lock_irqsave(&bl_lock, flags);
	if (intensity && current_intensity == 0) {
		sh_dac_enable(DAC_LCD_BRIGHTNESS);
		v = inw(HD64461_GPBDR);
		v &= ~HD64461_GPBDR_LCDOFF;
		outw(v, HD64461_GPBDR);
		sh_dac_output(255-(u8)intensity, DAC_LCD_BRIGHTNESS);
	} else if (intensity == 0 && current_intensity != 0) {
		sh_dac_output(255-(u8)intensity, DAC_LCD_BRIGHTNESS);
		sh_dac_disable(DAC_LCD_BRIGHTNESS);
		v = inw(HD64461_GPBDR);
		v |= HD64461_GPBDR_LCDOFF;
		outw(v, HD64461_GPBDR);
	} else if (intensity) {
		sh_dac_output(255-(u8)intensity, DAC_LCD_BRIGHTNESS);
	}
	spin_unlock_irqrestore(&bl_lock, flags);

	current_intensity = intensity;
}


#ifdef CONFIG_PM_SLEEP
static int hp680bl_suspend(struct device *dev)
{
	struct backlight_device *bd = dev_get_drvdata(dev);

	hp680bl_suspended = 1;
	hp680bl_send_intensity(bd);
	return 0;
}

static int hp680bl_resume(struct device *dev)
{
	struct backlight_device *bd = dev_get_drvdata(dev);

	hp680bl_suspended = 0;
	hp680bl_send_intensity(bd);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(hp680bl_pm_ops, hp680bl_suspend, hp680bl_resume);

static int hp680bl_set_intensity(struct backlight_device *bd)
{
	hp680bl_send_intensity(bd);
	return 0;
}

static int hp680bl_get_intensity(struct backlight_device *bd)
{
	return current_intensity;
}

static const struct backlight_ops hp680bl_ops = {
	.get_brightness = hp680bl_get_intensity,
	.update_status  = hp680bl_set_intensity,
};

static int hp680bl_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct backlight_device *bd;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = HP680_MAX_INTENSITY;
	bd = backlight_device_register("hp680-bl", &pdev->dev, NULL,
				       &hp680bl_ops, &props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	platform_set_drvdata(pdev, bd);

	bd->props.brightness = HP680_DEFAULT_INTENSITY;
	hp680bl_send_intensity(bd);

	return 0;
}

static int hp680bl_remove(struct platform_device *pdev)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);

	bd->props.brightness = 0;
	bd->props.power = 0;
	hp680bl_send_intensity(bd);

	backlight_device_unregister(bd);

	return 0;
}

static struct platform_driver hp680bl_driver = {
	.probe		= hp680bl_probe,
	.remove		= hp680bl_remove,
	.driver		= {
		.name	= "hp680-bl",
		.pm	= &hp680bl_pm_ops,
	},
};

static struct platform_device *hp680bl_device;

static int __init hp680bl_init(void)
{
	int ret;

	ret = platform_driver_register(&hp680bl_driver);
	if (ret)
		return ret;
	hp680bl_device = platform_device_register_simple("hp680-bl", -1,
							NULL, 0);
	if (IS_ERR(hp680bl_device)) {
		platform_driver_unregister(&hp680bl_driver);
		return PTR_ERR(hp680bl_device);
	}
	return 0;
}

static void __exit hp680bl_exit(void)
{
	platform_device_unregister(hp680bl_device);
	platform_driver_unregister(&hp680bl_driver);
}

module_init(hp680bl_init);
module_exit(hp680bl_exit);

MODULE_AUTHOR("Andriy Skulysh <askulysh@gmail.com>");
MODULE_DESCRIPTION("HP Jornada 680 Backlight Driver");
MODULE_LICENSE("GPL");
