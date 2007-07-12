/* power.c: Power management driver.
 *
 * Copyright (C) 1999, 2007 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/syscalls.h>

#include <asm/system.h>
#include <asm/auxio.h>
#include <asm/prom.h>
#include <asm/of_device.h>
#include <asm/io.h>
#include <asm/power.h>
#include <asm/sstate.h>

#include <linux/unistd.h>

/*
 * sysctl - toggle power-off restriction for serial console 
 * systems in machine_power_off()
 */
int scons_pwroff = 1; 

static void __iomem *power_reg;

static DECLARE_WAIT_QUEUE_HEAD(powerd_wait);
static int button_pressed;

void wake_up_powerd(void)
{
	if (button_pressed == 0) {
		button_pressed = 1;
		wake_up(&powerd_wait);
	}
}

static irqreturn_t power_handler(int irq, void *dev_id)
{
	wake_up_powerd();

	/* FIXME: Check registers for status... */
	return IRQ_HANDLED;
}

extern void machine_halt(void);
extern void machine_alt_power_off(void);
static void (*poweroff_method)(void) = machine_alt_power_off;

void machine_power_off(void)
{
	sstate_poweroff();
	if (!serial_console || scons_pwroff) {
		if (power_reg) {
			/* Both register bits seem to have the
			 * same effect, so until I figure out
			 * what the difference is...
			 */
			writel(AUXIO_PCIO_CPWR_OFF | AUXIO_PCIO_SPWR_OFF, power_reg);
		} else {
			if (poweroff_method != NULL) {
				poweroff_method();
				/* not reached */
			}
		}
	}
	machine_halt();
}

void (*pm_power_off)(void) = machine_power_off;
EXPORT_SYMBOL(pm_power_off);

static int powerd(void *__unused)
{
	static char *envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
	char *argv[] = { "/sbin/shutdown", "-h", "now", NULL };
	DECLARE_WAITQUEUE(wait, current);

	daemonize("powerd");

	add_wait_queue(&powerd_wait, &wait);

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
	if (kernel_execve("/sbin/shutdown", argv, envp) < 0) {
		printk(KERN_ERR "powerd: shutdown execution failed\n");
		machine_power_off();
	}
	return 0;
}

int start_powerd(void)
{
	int err;

	err = kernel_thread(powerd, NULL, CLONE_FS);
	if (err < 0)
		printk(KERN_ERR "power: Failed to start power daemon.\n");
	else
		printk(KERN_INFO "power: powerd running.\n");

	return err;
}

static int __init has_button_interrupt(unsigned int irq, struct device_node *dp)
{
	if (irq == 0xffffffff)
		return 0;
	if (!of_find_property(dp, "button", NULL))
		return 0;

	return 1;
}

static int __devinit power_probe(struct of_device *op, const struct of_device_id *match)
{
	struct resource *res = &op->resource[0];
	unsigned int irq= op->irqs[0];

	power_reg = of_ioremap(res, 0, 0x4, "power");

	printk("%s: Control reg at %lx ... ",
	       op->node->name, res->start);

	poweroff_method = machine_halt;  /* able to use the standard halt */

	if (has_button_interrupt(irq, op->node)) {
		if (start_powerd() < 0)
			return 0;

		if (request_irq(irq,
				power_handler, 0, "power", NULL) < 0)
			printk(KERN_ERR "power: Cannot setup IRQ handler.\n");
	} else {
		printk(KERN_INFO "power: Not using powerd.\n");
	}

	return 0;
}

static struct of_device_id power_match[] = {
	{
		.name = "power",
	},
	{},
};

static struct of_platform_driver power_driver = {
	.name		= "power",
	.match_table	= power_match,
	.probe		= power_probe,
};

void __init power_init(void)
{
	of_register_driver(&power_driver, &of_bus_type);
	return;
}
