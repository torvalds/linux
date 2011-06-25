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
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/mfd/core.h>
#include <linux/suspend.h>
#include <linux/workqueue.h>

#include <asm/io.h>
#include <asm/msr.h>
#include <asm/olpc.h>

#define DRV_NAME	"olpc-xo1-sci"
#define PFX		DRV_NAME ": "

static unsigned long acpi_base;
static struct input_dev *power_button_idev;
static struct input_dev *ebook_switch_idev;

static int sci_irq;

/* Report current ebook switch state through input layer */
static void send_ebook_state(void)
{
	unsigned char state;

	if (olpc_ec_cmd(EC_READ_EB_MODE, NULL, 0, &state, 1)) {
		pr_err(PFX "failed to get ebook state\n");
		return;
	}

	input_report_switch(ebook_switch_idev, SW_TABLET_MODE, state);
	input_sync(ebook_switch_idev);
}

/*
 * Process all items in the EC's SCI queue.
 *
 * This is handled in a workqueue because olpc_ec_cmd can be slow (and
 * can even timeout).
 *
 * If propagate_events is false, the queue is drained without events being
 * generated for the interrupts.
 */
static void process_sci_queue(bool propagate_events)
{
	int r;
	u16 data;

	do {
		r = olpc_ec_sci_query(&data);
		if (r || !data)
			break;

		pr_debug(PFX "SCI 0x%x received\n", data);

		if (data == EC_SCI_SRC_EBOOK && propagate_events)
			send_ebook_state();
	} while (data);

	if (r)
		pr_err(PFX "Failed to clear SCI queue");
}

static void process_sci_queue_work(struct work_struct *work)
{
	process_sci_queue(true);
}

static DECLARE_WORK(sci_work, process_sci_queue_work);

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

	if (gpe & CS5536_GPIOM7_PME_FLAG) { /* EC GPIO */
		cs5535_gpio_set(OLPC_GPIO_ECSCI, GPIO_NEGATIVE_EDGE_STS);
		schedule_work(&sci_work);
	}

	return IRQ_HANDLED;
}

static int xo1_sci_suspend(struct platform_device *pdev, pm_message_t state)
{
	if (device_may_wakeup(&power_button_idev->dev))
		olpc_xo1_pm_wakeup_set(CS5536_PM_PWRBTN);
	else
		olpc_xo1_pm_wakeup_clear(CS5536_PM_PWRBTN);

	if (device_may_wakeup(&ebook_switch_idev->dev))
		olpc_ec_wakeup_set(EC_SCI_SRC_EBOOK);
	else
		olpc_ec_wakeup_clear(EC_SCI_SRC_EBOOK);

	return 0;
}

static int xo1_sci_resume(struct platform_device *pdev)
{
	/* Enable all EC events */
	olpc_ec_mask_write(EC_SCI_SRC_ALL);
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

static int __devinit setup_ec_sci(void)
{
	int r;

	r = gpio_request(OLPC_GPIO_ECSCI, "OLPC-ECSCI");
	if (r)
		return r;

	gpio_direction_input(OLPC_GPIO_ECSCI);

	/* Clear pending EC SCI events */
	cs5535_gpio_set(OLPC_GPIO_ECSCI, GPIO_NEGATIVE_EDGE_STS);
	cs5535_gpio_set(OLPC_GPIO_ECSCI, GPIO_POSITIVE_EDGE_STS);

	/*
	 * Enable EC SCI events, and map them to both a PME and the SCI
	 * interrupt.
	 *
	 * Ordinarily, in addition to functioning as GPIOs, Geode GPIOs can
	 * be mapped to regular interrupts *or* Geode-specific Power
	 * Management Events (PMEs) - events that bring the system out of
	 * suspend. In this case, we want both of those things - the system
	 * wakeup, *and* the ability to get an interrupt when an event occurs.
	 *
	 * To achieve this, we map the GPIO to a PME, and then we use one
	 * of the many generic knobs on the CS5535 PIC to additionally map the
	 * PME to the regular SCI interrupt line.
	 */
	cs5535_gpio_set(OLPC_GPIO_ECSCI, GPIO_EVENTS_ENABLE);

	/* Set the SCI to cause a PME event on group 7 */
	cs5535_gpio_setup_event(OLPC_GPIO_ECSCI, 7, 1);

	/* And have group 7 also fire the SCI interrupt */
	cs5535_pic_unreqz_select_high(7, sci_irq);

	return 0;
}

static void free_ec_sci(void)
{
	gpio_free(OLPC_GPIO_ECSCI);
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

static int __devinit setup_ebook_switch(struct platform_device *pdev)
{
	int r;

	ebook_switch_idev = input_allocate_device();
	if (!ebook_switch_idev)
		return -ENOMEM;

	ebook_switch_idev->name = "EBook Switch";
	ebook_switch_idev->phys = DRV_NAME "/input1";
	set_bit(EV_SW, ebook_switch_idev->evbit);
	set_bit(SW_TABLET_MODE, ebook_switch_idev->swbit);

	ebook_switch_idev->dev.parent = &pdev->dev;
	device_set_wakeup_capable(&ebook_switch_idev->dev, true);

	r = input_register_device(ebook_switch_idev);
	if (r) {
		dev_err(&pdev->dev, "failed to register ebook switch: %d\n", r);
		input_free_device(ebook_switch_idev);
	}

	return r;
}

static void free_ebook_switch(void)
{
	input_unregister_device(ebook_switch_idev);
	input_free_device(ebook_switch_idev);
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

	r = setup_ebook_switch(pdev);
	if (r)
		goto err_ebook;

	r = setup_ec_sci();
	if (r)
		goto err_ecsci;

	/* Enable PME generation for EC-generated events */
	outl(CS5536_GPIOM7_PME_EN, acpi_base + CS5536_PM_GPE0_EN);

	/* Clear pending events */
	outl(0xffffffff, acpi_base + CS5536_PM_GPE0_STS);
	process_sci_queue(false);

	/* Initial sync */
	send_ebook_state();

	r = setup_sci_interrupt(pdev);
	if (r)
		goto err_sci;

	/* Enable all EC events */
	olpc_ec_mask_write(EC_SCI_SRC_ALL);

	return r;

err_sci:
	free_ec_sci();
err_ecsci:
	free_ebook_switch();
err_ebook:
	free_power_button();
	return r;
}

static int __devexit xo1_sci_remove(struct platform_device *pdev)
{
	mfd_cell_disable(pdev);
	free_irq(sci_irq, pdev);
	cancel_work_sync(&sci_work);
	free_ec_sci();
	free_ebook_switch();
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
	.resume = xo1_sci_resume,
};

static int __init xo1_sci_init(void)
{
	return platform_driver_register(&xo1_sci_driver);
}
arch_initcall(xo1_sci_init);
