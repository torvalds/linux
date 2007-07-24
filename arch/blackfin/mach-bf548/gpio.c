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
#include <asm/portmux.h>
#include <linux/irq.h>

static struct gpio_port_t *gpio_array[gpio_bank(MAX_BLACKFIN_GPIOS)] = {
	(struct gpio_port_t *)PORTA_FER,
	(struct gpio_port_t *)PORTB_FER,
	(struct gpio_port_t *)PORTC_FER,
	(struct gpio_port_t *)PORTD_FER,
	(struct gpio_port_t *)PORTE_FER,
	(struct gpio_port_t *)PORTF_FER,
	(struct gpio_port_t *)PORTG_FER,
	(struct gpio_port_t *)PORTH_FER,
	(struct gpio_port_t *)PORTI_FER,
	(struct gpio_port_t *)PORTJ_FER,
};

static unsigned short reserved_gpio_map[gpio_bank(MAX_BLACKFIN_GPIOS)];
static unsigned short reserved_peri_map[gpio_bank(MAX_BLACKFIN_GPIOS)];
char *str_ident = NULL;

#define RESOURCE_LABEL_SIZE 16

inline int check_gpio(unsigned short gpio)
{
	if (gpio == GPIO_PB15 || gpio == GPIO_PC14 || gpio == GPIO_PC15
	    || gpio == GPIO_PH14 || gpio == GPIO_PH15
	    || gpio == GPIO_PJ14 || gpio == GPIO_PJ15
	    || gpio > MAX_BLACKFIN_GPIOS)
		return -EINVAL;
	return 0;
}

inline void portmux_setup(unsigned short portno, unsigned short function)
{
	u32 pmux;

	pmux = gpio_array[gpio_bank(portno)]->port_mux;

	pmux &= ~(0x3 << (2 * gpio_sub_n(portno)));
	pmux |= (function & 0x3) << (2 * gpio_sub_n(portno));

	gpio_array[gpio_bank(portno)]->port_mux = pmux;
}

inline u16 get_portmux(unsigned short portno)
{
	u32 pmux;

	pmux = gpio_array[gpio_bank(portno)]->port_mux;

	return (pmux >> (2 * gpio_sub_n(portno)) & 0x3);
}

static void port_setup(unsigned short gpio, unsigned short usage)
{
	if (usage == GPIO_USAGE) {
		gpio_array[gpio_bank(gpio)]->port_fer &= ~gpio_bit(gpio);
	} else
		gpio_array[gpio_bank(gpio)]->port_fer |= gpio_bit(gpio);
	SSYNC();
}

static int __init bfin_gpio_init(void)
{

	str_ident = kzalloc(RESOURCE_LABEL_SIZE * 256, GFP_KERNEL);
	if (!str_ident)
		return -ENOMEM;

	printk(KERN_INFO "Blackfin GPIO Controller\n");

	return 0;
}

arch_initcall(bfin_gpio_init);

static void set_label(unsigned short ident, const char *label)
{

	if (label && str_ident) {
		strncpy(str_ident + ident * RESOURCE_LABEL_SIZE, label,
			 RESOURCE_LABEL_SIZE);
		str_ident[ident * RESOURCE_LABEL_SIZE +
			 RESOURCE_LABEL_SIZE - 1] = 0;
	}
}

static char *get_label(unsigned short ident)
{
	if (!str_ident)
		return "UNKNOWN";

	return (str_ident[ident * RESOURCE_LABEL_SIZE] ?
		(str_ident + ident * RESOURCE_LABEL_SIZE) : "UNKNOWN");
}

static int cmp_label(unsigned short ident, const char *label)
{
	if (label && str_ident)
		return strncmp(str_ident + ident * RESOURCE_LABEL_SIZE,
				 label, strlen(label));
	else
		return -EINVAL;
}

int peripheral_request(unsigned short per, const char *label)
{
	unsigned long flags;
	unsigned short ident = P_IDENT(per);

	/*
	 * Don't cares are pins with only one dedicated function
	 */

	if (per & P_DONTCARE)
		return 0;

	if (!(per & P_DEFINED))
		return -ENODEV;

	if (check_gpio(ident) < 0)
		return -EINVAL;

	local_irq_save(flags);

	if (unlikely(reserved_gpio_map[gpio_bank(ident)] & gpio_bit(ident))) {
		printk(KERN_ERR
		       "%s: Peripheral %d is already reserved as GPIO by %s !\n",
		       __FUNCTION__, ident, get_label(ident));
		dump_stack();
		local_irq_restore(flags);
		return -EBUSY;
	}

	if (unlikely(reserved_peri_map[gpio_bank(ident)] & gpio_bit(ident))) {

		u16 funct = get_portmux(ident);

	/*
	 * Pin functions like AMC address strobes my
	 * be requested and used by several drivers
	 */

		if (!((per & P_MAYSHARE) && (funct == P_FUNCT2MUX(per)))) {

		/*
		 * Allow that the identical pin function can
		 * be requested from the same driver twice
		 */

		if (cmp_label(ident, label) == 0)
			goto anyway;

			printk(KERN_ERR
			       "%s: Peripheral %d function %d is already reserved by %s !\n",
			       __FUNCTION__, ident, P_FUNCT2MUX(per), get_label(ident));
			dump_stack();
			local_irq_restore(flags);
			return -EBUSY;
		}
	}

anyway:
	reserved_peri_map[gpio_bank(ident)] |= gpio_bit(ident);

	portmux_setup(ident, P_FUNCT2MUX(per));
	port_setup(ident, PERIPHERAL_USAGE);

	local_irq_restore(flags);
	set_label(ident, label);

	return 0;
}
EXPORT_SYMBOL(peripheral_request);

int peripheral_request_list(unsigned short per[], const char *label)
{
	u16 cnt;
	int ret;

	for (cnt = 0; per[cnt] != 0; cnt++) {

		ret = peripheral_request(per[cnt], label);

		if (ret < 0) {
			for ( ; cnt > 0; cnt--) {
				peripheral_free(per[cnt - 1]);
			}
		return ret;
		}
	}


	return 0;
}
EXPORT_SYMBOL(peripheral_request_list);

void peripheral_free(unsigned short per)
{
	unsigned long flags;
	unsigned short ident = P_IDENT(per);

	if (per & P_DONTCARE)
		return;

	if (!(per & P_DEFINED))
		return;

	if (check_gpio(ident) < 0)
		return;

	local_irq_save(flags);

	if (unlikely(!(reserved_peri_map[gpio_bank(ident)] & gpio_bit(ident)))) {
		local_irq_restore(flags);
		return;
	}

	if (!(per & P_MAYSHARE)) {
		port_setup(ident, GPIO_USAGE);
	}

	reserved_peri_map[gpio_bank(ident)] &= ~gpio_bit(ident);

	local_irq_restore(flags);
}
EXPORT_SYMBOL(peripheral_free);

void peripheral_free_list(unsigned short per[])
{
	u16 cnt;

	for (cnt = 0; per[cnt] != 0; cnt++) {
		peripheral_free(per[cnt]);
	}

}
EXPORT_SYMBOL(peripheral_free_list);

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

	if (unlikely(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		printk(KERN_ERR "bfin-gpio: GPIO %d is already reserved by %s !\n",
			 gpio, get_label(gpio));
		dump_stack();
		local_irq_restore(flags);
		return -EBUSY;
	}

	if (unlikely(reserved_peri_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		printk(KERN_ERR
		       "bfin-gpio: GPIO %d is already reserved as Peripheral by %s !\n",
		       gpio, get_label(gpio));
		dump_stack();
		local_irq_restore(flags);
		return -EBUSY;
	}

	reserved_gpio_map[gpio_bank(gpio)] |= gpio_bit(gpio);

	local_irq_restore(flags);

	port_setup(gpio, GPIO_USAGE);
	set_label(gpio, label);

	return 0;
}
EXPORT_SYMBOL(gpio_request);

void gpio_free(unsigned short gpio)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return;

	local_irq_save(flags);

	if (unlikely(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio)))) {
		printk(KERN_ERR "bfin-gpio: GPIO %d wasn't reserved!\n", gpio);
		dump_stack();
		local_irq_restore(flags);
		return;
	}

	reserved_gpio_map[gpio_bank(gpio)] &= ~gpio_bit(gpio);

	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_free);

void gpio_direction_input(unsigned short gpio)
{
	unsigned long flags;

	BUG_ON(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio)));

	local_irq_save(flags);
	gpio_array[gpio_bank(gpio)]->port_dir_clear = gpio_bit(gpio);
	gpio_array[gpio_bank(gpio)]->port_inen |= gpio_bit(gpio);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_direction_input);

void gpio_direction_output(unsigned short gpio)
{
	unsigned long flags;

	BUG_ON(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio)));

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
