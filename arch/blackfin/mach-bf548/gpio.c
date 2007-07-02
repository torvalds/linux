/*
 * File:         arch/blackfin/mach-bf548/gpio.c
 * Based on:
 * Author:       Michael Hennerich (hennerich@blackfin.uclinux.org)
 *
 * Created:
 * Description:  GPIO Abstraction Layer
 *
 * Modified:
 *               Copyright 2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/err.h>
#include <asm/blackfin.h>
#include <asm/gpio.h>
#include <linux/irq.h>

static struct gpio_port_t *gpio_array[gpio_bank(MAX_BLACKFIN_GPIOS)] = {
	(struct gpio_port_t *) PORTA_FER,
	(struct gpio_port_t *) PORTB_FER,
	(struct gpio_port_t *) PORTC_FER,
	(struct gpio_port_t *) PORTD_FER,
	(struct gpio_port_t *) PORTE_FER,
	(struct gpio_port_t *) PORTF_FER,
	(struct gpio_port_t *) PORTG_FER,
	(struct gpio_port_t *) PORTH_FER,
	(struct gpio_port_t *) PORTI_FER,
	(struct gpio_port_t *) PORTJ_FER,
};

static unsigned short reserved_map[gpio_bank(MAX_BLACKFIN_GPIOS)];

inline int check_gpio(unsigned short gpio)
{
	if (gpio == GPIO_PB15 || gpio == GPIO_PC14 || gpio == GPIO_PC15 \
			|| gpio == GPIO_PH14 || gpio == GPIO_PH15 \
			|| gpio == GPIO_PJ14 || gpio == GPIO_PJ15 \
			|| gpio > MAX_BLACKFIN_GPIOS)
		return -EINVAL;
	return 0;
}

static void port_setup(unsigned short gpio, unsigned short usage)
{
	if (usage == GPIO_USAGE) {
		if (gpio_array[gpio_bank(gpio)]->port_fer & gpio_bit(gpio))
			printk(KERN_WARNING "bfin-gpio: Possible Conflict with Peripheral "
			       "usage and GPIO %d detected!\n", gpio);
		gpio_array[gpio_bank(gpio)]->port_fer &= ~gpio_bit(gpio);
	} else
		gpio_array[gpio_bank(gpio)]->port_fer |= gpio_bit(gpio);
	SSYNC();
}

static int __init bfin_gpio_init(void)
{
	int i;

	printk(KERN_INFO "Blackfin GPIO Controller\n");

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE)
		reserved_map[gpio_bank(i)] = 0;

	return 0;
}

arch_initcall(bfin_gpio_init);


/***********************************************************
*
* FUNCTIONS: Blackfin GPIO Driver
*
* INPUTS/OUTPUTS:
* gpio - GPIO Number between 0 and MAX_BLACKFIN_GPIOS
*
*
* DESCRIPTION: Blackfin GPIO Driver API
*
* CAUTION:
*************************************************************
* MODIFICATION HISTORY :
**************************************************************/

int gpio_request(unsigned short gpio, const char *label)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return -EINVAL;

	local_irq_save(flags);

	if (unlikely(reserved_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		printk(KERN_ERR "bfin-gpio: GPIO %d is already reserved!\n", gpio);
		dump_stack();
		local_irq_restore(flags);
		return -EBUSY;
	}
	reserved_map[gpio_bank(gpio)] |= gpio_bit(gpio);

	local_irq_restore(flags);

	port_setup(gpio, GPIO_USAGE);

	return 0;
}
EXPORT_SYMBOL(gpio_request);


void gpio_free(unsigned short gpio)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return;

	local_irq_save(flags);

	if (unlikely(!(reserved_map[gpio_bank(gpio)] & gpio_bit(gpio)))) {
		printk(KERN_ERR "bfin-gpio: GPIO %d wasn't reserved!\n", gpio);
		dump_stack();
		local_irq_restore(flags);
		return;
	}

	reserved_map[gpio_bank(gpio)] &= ~gpio_bit(gpio);

	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_free);


void gpio_direction_input(unsigned short gpio)
{
	unsigned long flags;

	BUG_ON(!(reserved_map[gpio_bank(gpio)] & gpio_bit(gpio)));

	local_irq_save(flags);
	gpio_array[gpio_bank(gpio)]->port_dir_clear = gpio_bit(gpio);
	gpio_array[gpio_bank(gpio)]->port_inen |= gpio_bit(gpio);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_direction_input);

void gpio_direction_output(unsigned short gpio)
{
	unsigned long flags;

	BUG_ON(!(reserved_map[gpio_bank(gpio)] & gpio_bit(gpio)));

	local_irq_save(flags);
	gpio_array[gpio_bank(gpio)]->port_inen &= ~gpio_bit(gpio);
	gpio_array[gpio_bank(gpio)]->port_dir_set = gpio_bit(gpio);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_direction_output);

void gpio_set_value(unsigned short gpio, unsigned short arg)
{
	if (arg)
		gpio_array[gpio_bank(gpio)]->port_set = gpio_bit(gpio);
	else
		gpio_array[gpio_bank(gpio)]->port_clear = gpio_bit(gpio);

}
EXPORT_SYMBOL(gpio_set_value);

unsigned short gpio_get_value(unsigned short gpio)
{
	return (1 & (gpio_array[gpio_bank(gpio)]->port_data >> gpio_sub_n(gpio)));
}
EXPORT_SYMBOL(gpio_get_value);
