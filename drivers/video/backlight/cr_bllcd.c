/*
 * Copyright (c) Intel Corp. 2007.
 * All Rights Reserved.
 *
 * Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 * develop this driver.
 *
 * This file is part of the Carillo Ranch video subsystem driver.
 * The Carillo Ranch video subsystem driver is free software;
 * you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Carillo Ranch video subsystem driver is distributed
 * in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *   Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 *   Alan Hourihane <alanh-at-tungstengraphics-dot-com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/lcd.h>
#include <linux/pci.h>

/* The LVDS- and panel power controls sits on the
 * GPIO port of the ISA bridge.
 */

#define CRVML_DEVICE_LPC    0x27B8
#define CRVML_REG_GPIOBAR   0x48
#define CRVML_REG_GPIOEN    0x4C
#define CRVML_GPIOEN_BIT    (1 << 4)
#define CRVML_PANEL_PORT    0x38
#define CRVML_LVDS_ON       0x00000001
#define CRVML_PANEL_ON      0x00000002
#define CRVML_BACKLIGHT_OFF 0x00000004

/* The PLL Clock register sits on Host bridge */
#define CRVML_DEVICE_MCH   0x5001
#define CRVML_REG_MCHBAR   0x44
#define CRVML_REG_MCHEN    0x54
#define CRVML_MCHEN_BIT    (1 << 28)
#define CRVML_MCHMAP_SIZE  4096
#define CRVML_REG_CLOCK    0xc3c
#define CRVML_CLOCK_SHIFT  8
#define CRVML_CLOCK_MASK   0x00000f00

static struct pci_dev *lpc_dev;
static u32 gpio_bar;

struct cr_panel {
	struct backlight_device *cr_backlight_device;
	struct lcd_device *cr_lcd_device;
};

static int cr_backlight_set_intensity(struct backlight_device *bd)
{
	int intensity = bd->props.brightness;
	u32 addr = gpio_bar + CRVML_PANEL_PORT;
	u32 cur = inl(addr);

	if (bd->props.power == FB_BLANK_UNBLANK)
		intensity = FB_BLANK_UNBLANK;
	if (bd->props.fb_blank == FB_BLANK_UNBLANK)
		intensity = FB_BLANK_UNBLANK;
	if (bd->props.power == FB_BLANK_POWERDOWN)
		intensity = FB_BLANK_POWERDOWN;
	if (bd->props.fb_blank == FB_BLANK_POWERDOWN)
		intensity = FB_BLANK_POWERDOWN;

	if (intensity == FB_BLANK_UNBLANK) { /* FULL ON */
		cur &= ~CRVML_BACKLIGHT_OFF;
		outl(cur, addr);
	} else if (intensity == FB_BLANK_POWERDOWN) { /* OFF */
		cur |= CRVML_BACKLIGHT_OFF;
		outl(cur, addr);
	} /* anything else, don't bother */

	return 0;
}

static int cr_backlight_get_intensity(struct backlight_device *bd)
{
	u32 addr = gpio_bar + CRVML_PANEL_PORT;
	u32 cur = inl(addr);
	u8 intensity;

	if (cur & CRVML_BACKLIGHT_OFF)
		intensity = FB_BLANK_POWERDOWN;
	else
		intensity = FB_BLANK_UNBLANK;

	return intensity;
}

static const struct backlight_ops cr_backlight_ops = {
	.get_brightness = cr_backlight_get_intensity,
	.update_status = cr_backlight_set_intensity,
};

static void cr_panel_on(void)
{
	u32 addr = gpio_bar + CRVML_PANEL_PORT;
	u32 cur = inl(addr);

	if (!(cur & CRVML_PANEL_ON)) {
		/* Make sure LVDS controller is down. */
		if (cur & 0x00000001) {
			cur &= ~CRVML_LVDS_ON;
			outl(cur, addr);
		}
		/* Power up Panel */
		schedule_timeout(HZ / 10);
		cur |= CRVML_PANEL_ON;
		outl(cur, addr);
	}

	/* Power up LVDS controller */

	if (!(cur & CRVML_LVDS_ON)) {
		schedule_timeout(HZ / 10);
		outl(cur | CRVML_LVDS_ON, addr);
	}
}

static void cr_panel_off(void)
{
	u32 addr = gpio_bar + CRVML_PANEL_PORT;
	u32 cur = inl(addr);

	/* Power down LVDS controller first to avoid high currents */
	if (cur & CRVML_LVDS_ON) {
		cur &= ~CRVML_LVDS_ON;
		outl(cur, addr);
	}
	if (cur & CRVML_PANEL_ON) {
		schedule_timeout(HZ / 10);
		outl(cur & ~CRVML_PANEL_ON, addr);
	}
}

static int cr_lcd_set_power(struct lcd_device *ld, int power)
{
	if (power == FB_BLANK_UNBLANK)
		cr_panel_on();
	if (power == FB_BLANK_POWERDOWN)
		cr_panel_off();

	return 0;
}

static struct lcd_ops cr_lcd_ops = {
	.set_power = cr_lcd_set_power,
};

static int cr_backlight_probe(struct platform_device *pdev)
{
	struct backlight_device *bdp;
	struct lcd_device *ldp;
	struct cr_panel *crp;
	u8 dev_en;

	lpc_dev = pci_get_device(PCI_VENDOR_ID_INTEL,
					CRVML_DEVICE_LPC, NULL);
	if (!lpc_dev) {
		printk("INTEL CARILLO RANCH LPC not found.\n");
		return -ENODEV;
	}

	pci_read_config_byte(lpc_dev, CRVML_REG_GPIOEN, &dev_en);
	if (!(dev_en & CRVML_GPIOEN_BIT)) {
		printk(KERN_ERR
		       "Carillo Ranch GPIO device was not enabled.\n");
		pci_dev_put(lpc_dev);
		return -ENODEV;
	}

	bdp = backlight_device_register("cr-backlight",
					&pdev->dev, NULL, &cr_backlight_ops);
	if (IS_ERR(bdp)) {
		pci_dev_put(lpc_dev);
		return PTR_ERR(bdp);
	}

	ldp = lcd_device_register("cr-lcd", &pdev->dev, NULL, &cr_lcd_ops);
	if (IS_ERR(ldp)) {
		backlight_device_unregister(bdp);
		pci_dev_put(lpc_dev);
		return PTR_ERR(ldp);
	}

	pci_read_config_dword(lpc_dev, CRVML_REG_GPIOBAR,
			      &gpio_bar);
	gpio_bar &= ~0x3F;

	crp = kzalloc(sizeof(*crp), GFP_KERNEL);
	if (!crp) {
		lcd_device_unregister(ldp);
		backlight_device_unregister(bdp);
		pci_dev_put(lpc_dev);
		return -ENOMEM;
	}

	crp->cr_backlight_device = bdp;
	crp->cr_lcd_device = ldp;
	crp->cr_backlight_device->props.power = FB_BLANK_UNBLANK;
	crp->cr_backlight_device->props.brightness = 0;
	crp->cr_backlight_device->props.max_brightness = 0;
	cr_backlight_set_intensity(crp->cr_backlight_device);

	cr_lcd_set_power(crp->cr_lcd_device, FB_BLANK_UNBLANK);

	platform_set_drvdata(pdev, crp);

	return 0;
}

static int cr_backlight_remove(struct platform_device *pdev)
{
	struct cr_panel *crp = platform_get_drvdata(pdev);
	crp->cr_backlight_device->props.power = FB_BLANK_POWERDOWN;
	crp->cr_backlight_device->props.brightness = 0;
	crp->cr_backlight_device->props.max_brightness = 0;
	cr_backlight_set_intensity(crp->cr_backlight_device);
	cr_lcd_set_power(crp->cr_lcd_device, FB_BLANK_POWERDOWN);
	backlight_device_unregister(crp->cr_backlight_device);
	lcd_device_unregister(crp->cr_lcd_device);
	pci_dev_put(lpc_dev);

	return 0;
}

static struct platform_driver cr_backlight_driver = {
	.probe = cr_backlight_probe,
	.remove = cr_backlight_remove,
	.driver = {
		   .name = "cr_backlight",
		   },
};

static struct platform_device *crp;

static int __init cr_backlight_init(void)
{
	int ret = platform_driver_register(&cr_backlight_driver);

	if (ret)
		return ret;

	crp = platform_device_register_simple("cr_backlight", -1, NULL, 0);
	if (IS_ERR(crp)) {
		platform_driver_unregister(&cr_backlight_driver);
		return PTR_ERR(crp);
	}

	printk("Carillo Ranch Backlight Driver Initialized.\n");

	return 0;
}

static void __exit cr_backlight_exit(void)
{
	platform_device_unregister(crp);
	platform_driver_unregister(&cr_backlight_driver);
}

module_init(cr_backlight_init);
module_exit(cr_backlight_exit);

MODULE_AUTHOR("Tungsten Graphics Inc.");
MODULE_DESCRIPTION("Carillo Ranch Backlight Driver");
MODULE_LICENSE("GPL");
