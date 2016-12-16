/*
 * SDK7786 FPGA USRGPIR Support.
 *
 * Copyright (C) 2010  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/gpio/driver.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <mach/fpga.h>

#define NR_FPGA_GPIOS	8

static const char *usrgpir_gpio_names[NR_FPGA_GPIOS] = {
	"in0", "in1", "in2", "in3", "in4", "in5", "in6", "in7",
};

static int usrgpir_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	/* always in */
	return 0;
}

static int usrgpir_gpio_get(struct gpio_chip *chip, unsigned gpio)
{
	return !!(fpga_read_reg(USRGPIR) & (1 << gpio));
}

static struct gpio_chip usrgpir_gpio_chip = {
	.label			= "sdk7786-fpga",
	.names			= usrgpir_gpio_names,
	.direction_input	= usrgpir_gpio_direction_input,
	.get			= usrgpir_gpio_get,
	.base			= -1, /* don't care */
	.ngpio			= NR_FPGA_GPIOS,
};

static int __init usrgpir_gpio_setup(void)
{
	return gpiochip_add_data(&usrgpir_gpio_chip, NULL);
}
device_initcall(usrgpir_gpio_setup);
