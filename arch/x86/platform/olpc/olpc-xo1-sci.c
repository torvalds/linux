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
#include <linux/pm_wakeup.h>
#include <linux/mfd/core.h>
#include <linux/power_supply.h>
#include <linux/suspend.h>
#include <linux/workqueue.h>
#include <linux/olpc-ec.h>

#include <asm/io.h>
#include <asm/msr.h>
#include <asm/olpc.h>

#define DRV_NAME	"olpc-xo1-sci"
#define PFX		DRV_NAME ": "

static unsigned long acpi_base;
static struct input_dev *power_button_idev;
static struct input_dev *ebook_switch_idev;
static struct input_dev *lid_switch_idev;

static int sci_irq;

static bool lid_open;
static bool lid_inverted;
static int lid_wake_mode;

enum lid_wake_modes {
	LID_WAKE_ALWAYS,
	LID_WAKE_OPEN,
	LID_WAKE_CLOSE,
};

static const char * const lid_wake_mode_names[] = {
	[LID_WAKE_ALWAYS] = "always",
	[LID_WAKE_OPEN] = "open",
	[LID_WAKE_CLOSE] = "close",
};

static void battery_status_changed(void)
{
	struct power_supply *psy = power_supply_get_by_name("olpc-battery");

	if (psy) {
		power_supply_changed(psy);
		power_supply_put(psy);
	}
}

static void ac_status_changed(void)
{
	struct power_supply *psy = power_supply_get_by_name("olpc-ac");

	if (psy) {
		power_supply_changed(psy);
		power_supply_put(psy);
	}
}

/* Report current ebook switch state through input layer */
static void send_ebook_state(void)
{
	unsigned char state;

	if (olpc_ec_cmd(EC_READ_EB_MODE, NULL, 0, &state, 1)) {
		pr_err(PFX "failed to get ebook state\n");
		return;
	}

	if (!!test_bit(SW_TABLET_MODE, ebook_switch_idev->sw) == state)
		return; /* Nothing new to report. */

	input_report_switch(ebook_switch_idev, SW_TABLET_MODE, state);
	input_sync(ebook_switch_idev);
	pm_wakeup_event(&ebook_switch_idev->dev, 0);
}

static void flip_lid_inverter(void)
{
	/* gpio is high; invert so we'll get l->h event interrupt */
	if (lid_inverted)
		cs5535_gpio_clear(OLPC_GPIO_LID, GPIO_INPUT_INVERT);
	else
		cs5535_gpio_set(OLPC_GPIO_LID, GPIO_INPUT_INVERT);
	lid_inverted = !lid_inverted;
}

static void detect_lid_state(void)
{
	/*
	 * the edge detector hookup on the gpio inputs on the geode is
	 * odd, to say the least.  See http://dev.laptop.org/ticket/5703
	 * for details, but in a nutshell:  we don't use the edge
	 * detectors.  instead, we make use of an anomoly:  with the both
	 * edge detectors turned off, we still get an edge event on a
	 * positive edge transition.  to take advantage of this, we use the
	 * front-end inverter to ensure that that's the edge we're always
	 * going to see next.
	 */

	int state;

	state = cs5535_gpio_isset(OLPC_GPIO_LID, GPIO_READ_BACK);
	lid_open = !state ^ !lid_inverted; /* x ^^ y */
	if (!state)
		return;

	flip_lid_inverter();
}

/* Report current lid switch state through input layer */
static void send_lid_state(void)
{
	if (!!test_bit(SW_LID, lid_switch_idev->sw) == !lid_open)
		return; /* Nothing new to report. */

	input_report_switch(lid_switch_idev, SW_LID, !lid_open);
	input_sync(lid_switch_idev);
	pm_wakeup_event(&lid_switch_idev->dev, 0);
}

static ssize_t lid_wake_mode_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	const char *mode = lid_wake_mode_names[lid_wake_mode];
	return sprintf(buf, "%s\n", mode);
}
static ssize_t lid_wake_mode_set(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(lid_wake_mode_names); i++) {
		const char *mode = lid_wake_mode_names[i];
		if (strlen(mode) != count || strncasecmp(mode, buf, count))
			continue;

		lid_wake_mode = i;
		return count;
	}
	return -EINVAL;
}
static DEVICE_ATTR(lid_wake_mode, S_IWUSR | S_IRUGO, lid_wake_mode_show,
		   lid_wake_mode_set);

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

		switch (data) {
		case EC_SCI_SRC_BATERR:
		case EC_SCI_SRC_BATSOC:
		case EC_SCI_SRC_BATTERY:
		case EC_SCI_SRC_BATCRIT:
			battery_status_changed();
			break;
		case EC_SCI_SRC_ACPWR:
			ac_status_changed();
			break;
		}

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

	if (sts & CS5536_PWRBTN_FLAG) {
		if (!(sts & CS5536_WAK_FLAG)) {
			/* Only report power button input when it was pressed
			 * during regular operation (as opposed to when it
			 * was used to wake the system). */
			input_report_key(power_button_idev, KEY_POWER, 1);
			input_sync(power_button_idev);
			input_report_key(power_button_idev, KEY_POWER, 0);
			input_sync(power_button_idev);
		}
		/* Report the wakeup event in all cases. */
		pm_wakeup_event(&power_button_idev->dev, 0);
	}

	if ((sts & (CS5536_RTC_FLAG | CS5536_WAK_FLAG)) ==
			(CS5536_RTC_FLAG | CS5536_WAK_FLAG)) {
		/* When the system is woken by the RTC alarm, report the
		 * event on the rtc device. */
		struct device *rtc = bus_find_device_by_name(
			&platform_bus_type, NULL, "rtc_cmos");
		if (rtc) {
			pm_wakeup_event(rtc, 0);
			put_device(rtc);
		}
	}

	if (gpe & CS5536_GPIOM7_PME_FLAG) { /* EC GPIO */
		cs5535_gpio_set(OLPC_GPIO_ECSCI, GPIO_NEGATIVE_EDGE_STS);
		schedule_work(&sci_work);
	}

	cs5535_gpio_set(OLPC_GPIO_LID, GPIO_NEGATIVE_EDGE_STS);
	cs5535_gpio_set(OLPC_GPIO_LID, GPIO_POSITIVE_EDGE_STS);
	detect_lid_state();
	send_lid_state();

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

	if (!device_may_wakeup(&lid_switch_idev->dev)) {
		cs5535_gpio_clear(OLPC_GPIO_LID, GPIO_EVENTS_ENABLE);
	} else if ((lid_open && lid_wake_mode == LID_WAKE_OPEN) ||
		   (!lid_open && lid_wake_mode == LID_WAKE_CLOSE)) {
		flip_lid_inverter();

		/* we may have just caused an event */
		cs5535_gpio_set(OLPC_GPIO_LID, GPIO_NEGATIVE_EDGE_STS);
		cs5535_gpio_set(OLPC_GPIO_LID, GPIO_POSITIVE_EDGE_STS);

		cs5535_gpio_set(OLPC_GPIO_LID, GPIO_EVENTS_ENABLE);
	}

	return 0;
}

static int xo1_sci_resume(struct platform_device *pdev)
{
	/*
	 * We don't know what may have happened while we were asleep.
	 * Reestablish our lid setup so we're sure to catch all transitions.
	 */
	detect_lid_state();
	send_lid_state();
	cs5535_gpio_set(OLPC_GPIO_LID, GPIO_EVENTS_ENABLE);

	/* Enable all EC events */
	olpc_ec_mask_write(EC_SCI_SRC_ALL);

	/* Power/battery status might have changed too */
	battery_status_changed();
	ac_status_changed();
	return 0;
}

static int setup_sci_interrupt(struct platform_device *pdev)
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

	/* Enable interesting SCI events, and clear pending interrupts */
	sts = inl(acpi_base + CS5536_PM1_STS);
	outl(((CS5536_PM_PWRBTN | CS5536_PM_RTC) << 16) | 0xffff,
	     acpi_base + CS5536_PM1_STS);

	r = request_irq(sci_irq, xo1_sci_intr, 0, DRV_NAME, pdev);
	if (r)
		dev_err(&pdev->dev, "can't request interrupt\n");

	return r;
}

static int setup_ec_sci(void)
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

static int setup_lid_events(void)
{
	int r;

	r = gpio_request(OLPC_GPIO_LID, "OLPC-LID");
	if (r)
		return r;

	gpio_direction_input(OLPC_GPIO_LID);

	cs5535_gpio_clear(OLPC_GPIO_LID, GPIO_INPUT_INVERT);
	lid_inverted = 0;

	/* Clear edge detection and event enable for now */
	cs5535_gpio_clear(OLPC_GPIO_LID, GPIO_EVENTS_ENABLE);
	cs5535_gpio_clear(OLPC_GPIO_LID, GPIO_NEGATIVE_EDGE_EN);
	cs5535_gpio_clear(OLPC_GPIO_LID, GPIO_POSITIVE_EDGE_EN);
	cs5535_gpio_set(OLPC_GPIO_LID, GPIO_NEGATIVE_EDGE_STS);
	cs5535_gpio_set(OLPC_GPIO_LID, GPIO_POSITIVE_EDGE_STS);

	/* Set the LID to cause an PME event on group 6 */
	cs5535_gpio_setup_event(OLPC_GPIO_LID, 6, 1);

	/* Set PME group 6 to fire the SCI interrupt */
	cs5535_gpio_set_irq(6, sci_irq);

	/* Enable the event */
	cs5535_gpio_set(OLPC_GPIO_LID, GPIO_EVENTS_ENABLE);

	return 0;
}

static void free_lid_events(void)
{
	gpio_free(OLPC_GPIO_LID);
}

static int setup_power_button(struct platform_device *pdev)
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
}

static int setup_ebook_switch(struct platform_device *pdev)
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
}

static int setup_lid_switch(struct platform_device *pdev)
{
	int r;

	lid_switch_idev = input_allocate_device();
	if (!lid_switch_idev)
		return -ENOMEM;

	lid_switch_idev->name = "Lid Switch";
	lid_switch_idev->phys = DRV_NAME "/input2";
	set_bit(EV_SW, lid_switch_idev->evbit);
	set_bit(SW_LID, lid_switch_idev->swbit);

	lid_switch_idev->dev.parent = &pdev->dev;
	device_set_wakeup_capable(&lid_switch_idev->dev, true);

	r = input_register_device(lid_switch_idev);
	if (r) {
		dev_err(&pdev->dev, "failed to register lid switch: %d\n", r);
		goto err_register;
	}

	r = device_create_file(&lid_switch_idev->dev, &dev_attr_lid_wake_mode);
	if (r) {
		dev_err(&pdev->dev, "failed to create wake mode attr: %d\n", r);
		goto err_create_attr;
	}

	return 0;

err_create_attr:
	input_unregister_device(lid_switch_idev);
	lid_switch_idev = NULL;
err_register:
	input_free_device(lid_switch_idev);
	return r;
}

static void free_lid_switch(void)
{
	device_remove_file(&lid_switch_idev->dev, &dev_attr_lid_wake_mode);
	input_unregister_device(lid_switch_idev);
}

static int xo1_sci_probe(struct platform_device *pdev)
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

	r = setup_lid_switch(pdev);
	if (r)
		goto err_lid;

	r = setup_lid_events();
	if (r)
		goto err_lidevt;

	r = setup_ec_sci();
	if (r)
		goto err_ecsci;

	/* Enable PME generation for EC-generated events */
	outl(CS5536_GPIOM6_PME_EN | CS5536_GPIOM7_PME_EN,
		acpi_base + CS5536_PM_GPE0_EN);

	/* Clear pending events */
	outl(0xffffffff, acpi_base + CS5536_PM_GPE0_STS);
	process_sci_queue(false);

	/* Initial sync */
	send_ebook_state();
	detect_lid_state();
	send_lid_state();

	r = setup_sci_interrupt(pdev);
	if (r)
		goto err_sci;

	/* Enable all EC events */
	olpc_ec_mask_write(EC_SCI_SRC_ALL);

	return r;

err_sci:
	free_ec_sci();
err_ecsci:
	free_lid_events();
err_lidevt:
	free_lid_switch();
err_lid:
	free_ebook_switch();
err_ebook:
	free_power_button();
	return r;
}

static int xo1_sci_remove(struct platform_device *pdev)
{
	mfd_cell_disable(pdev);
	free_irq(sci_irq, pdev);
	cancel_work_sync(&sci_work);
	free_ec_sci();
	free_lid_events();
	free_lid_switch();
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
	.remove = xo1_sci_remove,
	.suspend = xo1_sci_suspend,
	.resume = xo1_sci_resume,
};

static int __init xo1_sci_init(void)
{
	return platform_driver_register(&xo1_sci_driver);
}
arch_initcall(xo1_sci_init);
