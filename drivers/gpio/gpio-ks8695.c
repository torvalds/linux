/*
 * arch/arm/mach-ks8695/gpio.c
 *
 * Copyright (C) 2006 Andrew Victor
 * Updated to GPIOLIB, Copyright 2008 Simtec Electronics
 *                     Daniel Silverstone <dsilvers@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/mach/irq.h>

#include <mach/regs-gpio.h>
#include <mach/gpio-ks8695.h>

/*
 * Configure a GPIO line for either GPIO function, or its internal
 * function (Interrupt, Timer, etc).
 */
static void ks8695_gpio_mode(unsigned int pin, short gpio)
{
	unsigned int enable[] = { IOPC_IOEINT0EN, IOPC_IOEINT1EN, IOPC_IOEINT2EN, IOPC_IOEINT3EN, IOPC_IOTIM0EN, IOPC_IOTIM1EN };
	unsigned long x, flags;

	if (pin > KS8695_GPIO_5)	/* only GPIO 0..5 have internal functions */
		return;

	local_irq_save(flags);

	x = __raw_readl(KS8695_GPIO_VA + KS8695_IOPC);
	if (gpio)			/* GPIO: set bit to 0 */
		x &= ~enable[pin];
	else				/* Internal function: set bit to 1 */
		x |= enable[pin];
	__raw_writel(x, KS8695_GPIO_VA + KS8695_IOPC);

	local_irq_restore(flags);
}


static unsigned short gpio_irq[] = { KS8695_IRQ_EXTERN0, KS8695_IRQ_EXTERN1, KS8695_IRQ_EXTERN2, KS8695_IRQ_EXTERN3 };

/*
 * Configure GPIO pin as external interrupt source.
 */
int ks8695_gpio_interrupt(unsigned int pin, unsigned int type)
{
	unsigned long x, flags;

	if (pin > KS8695_GPIO_3)	/* only GPIO 0..3 can generate IRQ */
		return -EINVAL;

	local_irq_save(flags);

	/* set pin as input */
	x = __raw_readl(KS8695_GPIO_VA + KS8695_IOPM);
	x &= ~IOPM(pin);
	__raw_writel(x, KS8695_GPIO_VA + KS8695_IOPM);

	local_irq_restore(flags);

	/* Set IRQ triggering type */
	irq_set_irq_type(gpio_irq[pin], type);

	/* enable interrupt mode */
	ks8695_gpio_mode(pin, 0);

	return 0;
}
EXPORT_SYMBOL(ks8695_gpio_interrupt);



/* .... Generic GPIO interface .............................................. */

/*
 * Configure the GPIO line as an input.
 */
static int ks8695_gpio_direction_input(struct gpio_chip *gc, unsigned int pin)
{
	unsigned long x, flags;

	if (pin > KS8695_GPIO_15)
		return -EINVAL;

	/* set pin to GPIO mode */
	ks8695_gpio_mode(pin, 1);

	local_irq_save(flags);

	/* set pin as input */
	x = __raw_readl(KS8695_GPIO_VA + KS8695_IOPM);
	x &= ~IOPM(pin);
	__raw_writel(x, KS8695_GPIO_VA + KS8695_IOPM);

	local_irq_restore(flags);

	return 0;
}


/*
 * Configure the GPIO line as an output, with default state.
 */
static int ks8695_gpio_direction_output(struct gpio_chip *gc,
					unsigned int pin, int state)
{
	unsigned long x, flags;

	if (pin > KS8695_GPIO_15)
		return -EINVAL;

	/* set pin to GPIO mode */
	ks8695_gpio_mode(pin, 1);

	local_irq_save(flags);

	/* set line state */
	x = __raw_readl(KS8695_GPIO_VA + KS8695_IOPD);
	if (state)
		x |= IOPD(pin);
	else
		x &= ~IOPD(pin);
	__raw_writel(x, KS8695_GPIO_VA + KS8695_IOPD);

	/* set pin as output */
	x = __raw_readl(KS8695_GPIO_VA + KS8695_IOPM);
	x |= IOPM(pin);
	__raw_writel(x, KS8695_GPIO_VA + KS8695_IOPM);

	local_irq_restore(flags);

	return 0;
}


/*
 * Set the state of an output GPIO line.
 */
static void ks8695_gpio_set_value(struct gpio_chip *gc,
				  unsigned int pin, int state)
{
	unsigned long x, flags;

	if (pin > KS8695_GPIO_15)
		return;

	local_irq_save(flags);

	/* set output line state */
	x = __raw_readl(KS8695_GPIO_VA + KS8695_IOPD);
	if (state)
		x |= IOPD(pin);
	else
		x &= ~IOPD(pin);
	__raw_writel(x, KS8695_GPIO_VA + KS8695_IOPD);

	local_irq_restore(flags);
}


/*
 * Read the state of a GPIO line.
 */
static int ks8695_gpio_get_value(struct gpio_chip *gc, unsigned int pin)
{
	unsigned long x;

	if (pin > KS8695_GPIO_15)
		return -EINVAL;

	x = __raw_readl(KS8695_GPIO_VA + KS8695_IOPD);
	return (x & IOPD(pin)) != 0;
}


/*
 * Map GPIO line to IRQ number.
 */
static int ks8695_gpio_to_irq(struct gpio_chip *gc, unsigned int pin)
{
	if (pin > KS8695_GPIO_3)	/* only GPIO 0..3 can generate IRQ */
		return -EINVAL;

	return gpio_irq[pin];
}

/*
 * Map IRQ number to GPIO line.
 */
int irq_to_gpio(unsigned int irq)
{
	if ((irq < KS8695_IRQ_EXTERN0) || (irq > KS8695_IRQ_EXTERN3))
		return -EINVAL;

	return (irq - KS8695_IRQ_EXTERN0);
}
EXPORT_SYMBOL(irq_to_gpio);

/* GPIOLIB interface */

static struct gpio_chip ks8695_gpio_chip = {
	.label			= "KS8695",
	.direction_input	= ks8695_gpio_direction_input,
	.direction_output	= ks8695_gpio_direction_output,
	.get			= ks8695_gpio_get_value,
	.set			= ks8695_gpio_set_value,
	.to_irq			= ks8695_gpio_to_irq,
	.base			= 0,
	.ngpio			= 16,
	.can_sleep		= false,
};

/* Register the GPIOs */
void ks8695_register_gpios(void)
{
	if (gpiochip_add_data(&ks8695_gpio_chip, NULL))
		printk(KERN_ERR "Unable to register core GPIOs\n");
}

/* .... Debug interface ..................................................... */

#ifdef CONFIG_DEBUG_FS

static int ks8695_gpio_show(struct seq_file *s, void *unused)
{
	unsigned int enable[] = { IOPC_IOEINT0EN, IOPC_IOEINT1EN, IOPC_IOEINT2EN, IOPC_IOEINT3EN, IOPC_IOTIM0EN, IOPC_IOTIM1EN };
	unsigned int intmask[] = { IOPC_IOEINT0TM, IOPC_IOEINT1TM, IOPC_IOEINT2TM, IOPC_IOEINT3TM };
	unsigned long mode, ctrl, data;
	int i;

	mode = __raw_readl(KS8695_GPIO_VA + KS8695_IOPM);
	ctrl = __raw_readl(KS8695_GPIO_VA + KS8695_IOPC);
	data = __raw_readl(KS8695_GPIO_VA + KS8695_IOPD);

	seq_printf(s, "Pin\tI/O\tFunction\tState\n\n");

	for (i = KS8695_GPIO_0; i <= KS8695_GPIO_15 ; i++) {
		seq_printf(s, "%i:\t", i);

		seq_printf(s, "%s\t", (mode & IOPM(i)) ? "Output" : "Input");

		if (i <= KS8695_GPIO_3) {
			if (ctrl & enable[i]) {
				seq_printf(s, "EXT%i ", i);

				switch ((ctrl & intmask[i]) >> (4 * i)) {
				case IOPC_TM_LOW:
					seq_printf(s, "(Low)");		break;
				case IOPC_TM_HIGH:
					seq_printf(s, "(High)");	break;
				case IOPC_TM_RISING:
					seq_printf(s, "(Rising)");	break;
				case IOPC_TM_FALLING:
					seq_printf(s, "(Falling)");	break;
				case IOPC_TM_EDGE:
					seq_printf(s, "(Edges)");	break;
				}
			} else
				seq_printf(s, "GPIO\t");
		} else if (i <= KS8695_GPIO_5) {
			if (ctrl & enable[i])
				seq_printf(s, "TOUT%i\t", i - KS8695_GPIO_4);
			else
				seq_printf(s, "GPIO\t");
		} else {
			seq_printf(s, "GPIO\t");
		}

		seq_printf(s, "\t");

		seq_printf(s, "%i\n", (data & IOPD(i)) ? 1 : 0);
	}
	return 0;
}

static int ks8695_gpio_open(struct inode *inode, struct file *file)
{
	return single_open(file, ks8695_gpio_show, NULL);
}

static const struct file_operations ks8695_gpio_operations = {
	.open		= ks8695_gpio_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init ks8695_gpio_debugfs_init(void)
{
	/* /sys/kernel/debug/ks8695_gpio */
	(void) debugfs_create_file("ks8695_gpio", S_IFREG | S_IRUGO, NULL, NULL, &ks8695_gpio_operations);
	return 0;
}
postcore_initcall(ks8695_gpio_debugfs_init);

#endif
