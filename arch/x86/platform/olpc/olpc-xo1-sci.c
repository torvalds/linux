/*
 * Support for OLPC XO-1 System Control Interrupts (SCI)
 *
 * Copyright (C) 2010 One Laptop per Child
 * Copyright (C) 2006 Red Hat, Inc.
 * Copyright (C) 2006 Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/cs5535.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/mfd/core.h>
#include <linux/suspend.h>

#include <asm/io.h>
#include <asm/msr.h>
#include <asm/olpc.h>

#define DRV_NAME	"olpc-xo1-sci"
#define PFX		DRV_NAME ": "

static unsigned long acpi_base;
static struct input_dev *power_button_idev;
static int sci_irq;

static irqreturn_t xo1_sci_intr(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	u32 sts;
	u32 gpe;

	sts = inl(acpi_base + CS5536_PM1_STS);
	outl(sts | 0xffff, acpi_base + CS5536_PM1_STS);

	gpe = inl(acpi_base + CS5536_PM_GPE0_STS);
	outl(0xffffffff, acpi_base + CS5536_PM_GPE0_STS);

	dev_dbg(&pdev->dev, "sts %x gpe %x\n", sts, gpe);

	if (sts & CS5536_PWRBTN_FLAG && !(sts & CS5536_WAK_FLAG)) {
		input_report_key(power_button_idev, KEY_POWER, 1);
		input_sync(power_button_idev);
		input_report_key(power_button_idev, KEY_POWER, 0);
		input_sync(power_button_idev);
	}

	return IRQ_HANDLED;
}

static int xo1_sci_suspend(struct platform_device *pdev, pm_message_t state)
{
	if (device_may_wakeup(&power_button_idev->dev))
		olpc_xo1_pm_wakeup_set(CS5536_PM_PWRBTN);
	else
		olpc_xo1_pm_wakeup_clear(CS5536_PM_PWRBTN);
	return 0;
}

static int __devinit setup_sci_interrupt(struct platform_device *pdev)
{
	u32 lo, hi;
	u32 sts;
	int r;

	rdmsr(0x51400020, lo, hi);
	sci_irq = (lo >> 20) & 15;

	if (sci_irq) {
		dev_info(&pdev->dev, "SCI is mapped to IRQ %d\n", sci_irq);
	} else {
		/* Zero means masked */
		dev_info(&pdev->dev, "SCI unmapped. Mapping to IRQ 3\n");
		sci_irq = 3;
		lo |= 0x00300000;
		wrmsrl(0x51400020, lo);
	}

	/* Select level triggered in PIC */
	if (sci_irq < 8) {
		lo = inb(CS5536_PIC_INT_SEL1);
		lo |= 1 << sci_irq;
		outb(lo, CS5536_PIC_INT_SEL1);
	} else {
		lo = inb(CS5536_PIC_INT_SEL2);
		lo |= 1 << (sci_irq - 8);
		outb(lo, CS5536_PIC_INT_SEL2);
	}

	/* Enable SCI from power button, and clear pending interrupts */
	sts = inl(acpi_base + CS5536_PM1_STS);
	outl((CS5536_PM_PWRBTN << 16) | 0xffff, acpi_base + CS5536_PM1_STS);

	r = request_irq(sci_irq, xo1_sci_intr, 0, DRV_NAME, pdev);
	if (r)
		dev_err(&pdev->dev, "can't request interrupt\n");

	return r;
}

static int __devinit setup_power_button(struct platform_device *pdev)
{
	int r;

	power_button_idev = input_allocate_device();
	if (!power_button_idev)
		return -ENOMEM;

	power_button_idev->name = "Power Button";
	power_button_idev->phys = DRV_NAME "/input0";
	set_bit(EV_KEY, power_button_idev->evbit);
	set_bit(KEY_POWER, power_button_idev->keybit);

	power_button_idev->dev.parent = &pdev->dev;
	device_init_wakeup(&power_button_idev->dev, 1);

	r = input_register_device(power_button_idev);
	if (r) {
		dev_err(&pdev->dev, "failed to register power button: %d\n", r);
		input_free_device(power_button_idev);
	}

	return r;
}

static void free_power_button(void)
{
	input_unregister_device(power_button_idev);
	input_free_device(power_button_idev);
}

static int __devinit xo1_sci_probe(struct platform_device *pdev)
{
	struct resource *res;
	int r;

	/* don't run on non-XOs */
	if (!machine_is_olpc())
		return -ENODEV;

	r = mfd_cell_enable(pdev);
	if (r)
		return r;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res) {
		dev_err(&pdev->dev, "can't fetch device resource info\n");
		return -EIO;
	}
	acpi_base = res->start;

	r = setup_power_button(pdev);
	if (r)
		return r;

	r = setup_sci_interrupt(pdev);
	if (r)
		free_power_button();

	return r;
}

static int __devexit xo1_sci_remove(struct platform_device *pdev)
{
	mfd_cell_disable(pdev);
	free_irq(sci_irq, pdev);
	free_power_button();
	acpi_base = 0;
	return 0;
}

static struct platform_driver xo1_sci_driver = {
	.driver = {
		.name = "olpc-xo1-sci-acpi",
	},
	.probe = xo1_sci_probe,
	.remove = __devexit_p(xo1_sci_remove),
	.suspend = xo1_sci_suspend,
};

static int __init xo1_sci_init(void)
{
	return platform_driver_register(&xo1_sci_driver);
}
arch_initcall(xo1_sci_init);
