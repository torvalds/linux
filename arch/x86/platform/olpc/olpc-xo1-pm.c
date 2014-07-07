/*
 * Support for power management features of the OLPC XO-1 laptop
 *
 * Copyright (C) 2010 Andres Salomon <dilinger@queued.net>
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
#include <linux/platform_device.h>
#include <linux/export.h>
#include <linux/pm.h>
#include <linux/mfd/core.h>
#include <linux/suspend.h>
#include <linux/olpc-ec.h>

#include <asm/io.h>
#include <asm/olpc.h>

#define DRV_NAME "olpc-xo1-pm"

static unsigned long acpi_base;
static unsigned long pms_base;

static u16 wakeup_mask = CS5536_PM_PWRBTN;

static struct {
	unsigned long address;
	unsigned short segment;
} ofw_bios_entry = { 0xF0000 + PAGE_OFFSET, __KERNEL_CS };

/* Set bits in the wakeup mask */
void olpc_xo1_pm_wakeup_set(u16 value)
{
	wakeup_mask |= value;
}
EXPORT_SYMBOL_GPL(olpc_xo1_pm_wakeup_set);

/* Clear bits in the wakeup mask */
void olpc_xo1_pm_wakeup_clear(u16 value)
{
	wakeup_mask &= ~value;
}
EXPORT_SYMBOL_GPL(olpc_xo1_pm_wakeup_clear);

static int xo1_power_state_enter(suspend_state_t pm_state)
{
	unsigned long saved_sci_mask;

	/* Only STR is supported */
	if (pm_state != PM_SUSPEND_MEM)
		return -EINVAL;

	/*
	 * Save SCI mask (this gets lost since PM1_EN is used as a mask for
	 * wakeup events, which is not necessarily the same event set)
	 */
	saved_sci_mask = inl(acpi_base + CS5536_PM1_STS);
	saved_sci_mask &= 0xffff0000;

	/* Save CPU state */
	do_olpc_suspend_lowlevel();

	/* Resume path starts here */

	/* Restore SCI mask (using dword access to CS5536_PM1_EN) */
	outl(saved_sci_mask, acpi_base + CS5536_PM1_STS);

	return 0;
}

asmlinkage __visible int xo1_do_sleep(u8 sleep_state)
{
	void *pgd_addr = __va(read_cr3());

	/* Program wakeup mask (using dword access to CS5536_PM1_EN) */
	outl(wakeup_mask << 16, acpi_base + CS5536_PM1_STS);

	__asm__("movl %0,%%eax" : : "r" (pgd_addr));
	__asm__("call *(%%edi); cld"
		: : "D" (&ofw_bios_entry));
	__asm__("movb $0x34, %al\n\t"
		"outb %al, $0x70\n\t"
		"movb $0x30, %al\n\t"
		"outb %al, $0x71\n\t");
	return 0;
}

static void xo1_power_off(void)
{
	printk(KERN_INFO "OLPC XO-1 power off sequence...\n");

	/* Enable all of these controls with 0 delay */
	outl(0x40000000, pms_base + CS5536_PM_SCLK);
	outl(0x40000000, pms_base + CS5536_PM_IN_SLPCTL);
	outl(0x40000000, pms_base + CS5536_PM_WKXD);
	outl(0x40000000, pms_base + CS5536_PM_WKD);

	/* Clear status bits (possibly unnecessary) */
	outl(0x0002ffff, pms_base  + CS5536_PM_SSC);
	outl(0xffffffff, acpi_base + CS5536_PM_GPE0_STS);

	/* Write SLP_EN bit to start the machinery */
	outl(0x00002000, acpi_base + CS5536_PM1_CNT);
}

static int xo1_power_state_valid(suspend_state_t pm_state)
{
	/* suspend-to-RAM only */
	return pm_state == PM_SUSPEND_MEM;
}

static const struct platform_suspend_ops xo1_suspend_ops = {
	.valid = xo1_power_state_valid,
	.enter = xo1_power_state_enter,
};

static int xo1_pm_probe(struct platform_device *pdev)
{
	struct resource *res;
	int err;

	/* don't run on non-XOs */
	if (!machine_is_olpc())
		return -ENODEV;

	err = mfd_cell_enable(pdev);
	if (err)
		return err;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res) {
		dev_err(&pdev->dev, "can't fetch device resource info\n");
		return -EIO;
	}
	if (strcmp(pdev->name, "cs5535-pms") == 0)
		pms_base = res->start;
	else if (strcmp(pdev->name, "olpc-xo1-pm-acpi") == 0)
		acpi_base = res->start;

	/* If we have both addresses, we can override the poweroff hook */
	if (pms_base && acpi_base) {
		suspend_set_ops(&xo1_suspend_ops);
		pm_power_off = xo1_power_off;
		printk(KERN_INFO "OLPC XO-1 support registered\n");
	}

	return 0;
}

static int xo1_pm_remove(struct platform_device *pdev)
{
	mfd_cell_disable(pdev);

	if (strcmp(pdev->name, "cs5535-pms") == 0)
		pms_base = 0;
	else if (strcmp(pdev->name, "olpc-xo1-pm-acpi") == 0)
		acpi_base = 0;

	pm_power_off = NULL;
	return 0;
}

static struct platform_driver cs5535_pms_driver = {
	.driver = {
		.name = "cs5535-pms",
		.owner = THIS_MODULE,
	},
	.probe = xo1_pm_probe,
	.remove = xo1_pm_remove,
};

static struct platform_driver cs5535_acpi_driver = {
	.driver = {
		.name = "olpc-xo1-pm-acpi",
		.owner = THIS_MODULE,
	},
	.probe = xo1_pm_probe,
	.remove = xo1_pm_remove,
};

static int __init xo1_pm_init(void)
{
	int r;

	r = platform_driver_register(&cs5535_pms_driver);
	if (r)
		return r;

	r = platform_driver_register(&cs5535_acpi_driver);
	if (r)
		platform_driver_unregister(&cs5535_pms_driver);

	return r;
}
arch_initcall(xo1_pm_init);
