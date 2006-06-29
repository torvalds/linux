/* $Id: power.c,v 1.10 2001/12/11 01:57:16 davem Exp $
 * power.c: Power management driver.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#define __KERNEL_SYSCALLS__

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pm.h>

#include <asm/system.h>
#include <asm/ebus.h>
#include <asm/isa.h>
#include <asm/auxio.h>

#include <linux/unistd.h>

/*
 * sysctl - toggle power-off restriction for serial console 
 * systems in machine_power_off()
 */
int scons_pwroff = 1; 

#ifdef CONFIG_PCI
static void __iomem *power_reg;

static DECLARE_WAIT_QUEUE_HEAD(powerd_wait);
static int button_pressed;

static irqreturn_t power_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	if (button_pressed == 0) {
		button_pressed = 1;
		wake_up(&powerd_wait);
	}

	/* FIXME: Check registers for status... */
	return IRQ_HANDLED;
}
#endif /* CONFIG_PCI */

extern void machine_halt(void);
extern void machine_alt_power_off(void);
static void (*poweroff_method)(void) = machine_alt_power_off;

void machine_power_off(void)
{
	if (!serial_console || scons_pwroff) {
#ifdef CONFIG_PCI
		if (power_reg) {
			/* Both register bits seem to have the
			 * same effect, so until I figure out
			 * what the difference is...
			 */
			writel(AUXIO_PCIO_CPWR_OFF | AUXIO_PCIO_SPWR_OFF, power_reg);
		} else
#endif /* CONFIG_PCI */
			if (poweroff_method != NULL) {
				poweroff_method();
				/* not reached */
			}
	}
	machine_halt();
}

void (*pm_power_off)(void) = machine_power_off;
EXPORT_SYMBOL(pm_power_off);

#ifdef CONFIG_PCI
static int powerd(void *__unused)
{
	static char *envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
	char *argv[] = { "/sbin/shutdown", "-h", "now", NULL };
	DECLARE_WAITQUEUE(wait, current);

	daemonize("powerd");

	add_wait_queue(&powerd_wait, &wait);
again:
	for (;;) {
		set_task_state(current, TASK_INTERRUPTIBLE);
		if (button_pressed)
			break;
		flush_signals(current);
		schedule();
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&powerd_wait, &wait);

	/* Ok, down we go... */
	button_pressed = 0;
	if (execve("/sbin/shutdown", argv, envp) < 0) {
		printk("powerd: shutdown execution failed\n");
		add_wait_queue(&powerd_wait, &wait);
		goto again;
	}
	return 0;
}

static int __init has_button_interrupt(unsigned int irq, struct device_node *dp)
{
	if (irq == PCI_IRQ_NONE)
		return 0;
	if (!of_find_property(dp, "button", NULL))
		return 0;

	return 1;
}

static void __devinit power_probe_common(struct of_device *dev, struct resource *res, unsigned int irq)
{
	power_reg = ioremap(res->start, 0x4);

	printk("power: Control reg at %p ... ", power_reg);

	poweroff_method = machine_halt;  /* able to use the standard halt */

	if (has_button_interrupt(irq, dev->node)) {
		if (kernel_thread(powerd, NULL, CLONE_FS) < 0) {
			printk("Failed to start power daemon.\n");
			return;
		}
		printk("powerd running.\n");

		if (request_irq(irq,
				power_handler, SA_SHIRQ, "power", NULL) < 0)
			printk("power: Error, cannot register IRQ handler.\n");
	} else {
		printk("not using powerd.\n");
	}
}

static struct of_device_id power_match[] = {
	{
		.name = "power",
	},
	{},
};

static int __devinit ebus_power_probe(struct of_device *dev, const struct of_device_id *match)
{
	struct linux_ebus_device *edev = to_ebus_device(&dev->dev);
	struct resource *res = &edev->resource[0];
	unsigned int irq = edev->irqs[0];

	power_probe_common(dev, res,irq);

	return 0;
}

static struct of_platform_driver ebus_power_driver = {
	.name		= "power",
	.match_table	= power_match,
	.probe		= ebus_power_probe,
};

static int __devinit isa_power_probe(struct of_device *dev, const struct of_device_id *match)
{
	struct sparc_isa_device *idev = to_isa_device(&dev->dev);
	struct resource *res = &idev->resource;
	unsigned int irq = idev->irq;

	power_probe_common(dev, res,irq);

	return 0;
}

static struct of_platform_driver isa_power_driver = {
	.name		= "power",
	.match_table	= power_match,
	.probe		= isa_power_probe,
};

void __init power_init(void)
{
	of_register_driver(&ebus_power_driver, &ebus_bus_type);
	of_register_driver(&isa_power_driver, &isa_bus_type);
	return;
}
#endif /* CONFIG_PCI */
