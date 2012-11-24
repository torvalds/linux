/*
 * Retu power button driver.
 *
 * Copyright (C) 2004-2010 Nokia Corporation
 *
 * Original code written by Ari Saastamoinen, Juha Yrjölä and Felipe Balbi.
 * Rewritten by Aaro Koskinen.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/irq.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/retu.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#define RETU_STATUS_PWRONX (1 << 5)

static irqreturn_t retu_pwrbutton_irq(int irq, void *_pwr)
{
	struct input_dev *idev = _pwr;
	struct retu_dev *rdev = input_get_drvdata(idev);
	bool state;

	state = !(retu_read(rdev, RETU_REG_STATUS) & RETU_STATUS_PWRONX);
	input_report_key(idev, KEY_POWER, state);
	input_sync(idev);

	return IRQ_HANDLED;
}

static int retu_pwrbutton_probe(struct platform_device *pdev)
{
	struct retu_dev *rdev = dev_get_drvdata(pdev->dev.parent);
	struct input_dev *idev;
	int irq;
	int error;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	idev = devm_input_allocate_device(&pdev->dev);
	if (!idev)
		return -ENOMEM;

	idev->name = "retu-pwrbutton";
	idev->dev.parent = &pdev->dev;

	input_set_capability(idev, EV_KEY, KEY_POWER);
	input_set_drvdata(idev, rdev);

	error = devm_request_threaded_irq(&pdev->dev, irq,
					  NULL, retu_pwrbutton_irq, 0,
					  "retu-pwrbutton", idev);
	if (error)
		return error;

	error = input_register_device(idev);
	if (error)
		return error;

	return 0;
}

static int retu_pwrbutton_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver retu_pwrbutton_driver = {
	.probe		= retu_pwrbutton_probe,
	.remove		= retu_pwrbutton_remove,
	.driver		= {
		.name	= "retu-pwrbutton",
		.owner	= THIS_MODULE,
	},
};
module_platform_driver(retu_pwrbutton_driver);

MODULE_ALIAS("platform:retu-pwrbutton");
MODULE_DESCRIPTION("Retu Power Button");
MODULE_AUTHOR("Ari Saastamoinen");
MODULE_AUTHOR("Felipe Balbi");
MODULE_AUTHOR("Aaro Koskinen <aaro.koskinen@iki.fi>");
MODULE_LICENSE("GPL");
