/* power.c: Power management driver.
 *
 * Copyright (C) 1999, 2007, 2008 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/reboot.h>
#include <linux/of_device.h>

#include <asm/auxio.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/sstate.h>
#include <asm/reboot.h>

/*
 * sysctl - toggle power-off restriction for serial console 
 * systems in machine_power_off()
 */
int scons_pwroff = 1; 

static void __iomem *power_reg;

static irqreturn_t power_handler(int irq, void *dev_id)
{
	orderly_poweroff(true);

	/* FIXME: Check registers for status... */
	return IRQ_HANDLED;
}

static void (*poweroff_method)(void) = machine_alt_power_off;

void machine_power_off(void)
{
	sstate_poweroff();
	if (strcmp(of_console_device->type, "serial") || scons_pwroff) {
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

	printk(KERN_INFO "%s: Control reg at %lx\n",
	       op->node->name, res->start);

	poweroff_method = machine_halt;  /* able to use the standard halt */

	if (has_button_interrupt(irq, op->node)) {
		if (request_irq(irq,
				power_handler, 0, "power", NULL) < 0)
			printk(KERN_ERR "power: Cannot setup IRQ handler.\n");
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
	.match_table	= power_match,
	.probe		= power_probe,
	.driver		= {
		.name	= "power",
	},
};

static int __init power_init(void)
{
	return of_register_driver(&power_driver, &of_platform_bus_type);
}

device_initcall(power_init);
