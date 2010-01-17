/* arch/arm/mach-msm/generic_gpio.c
 *
 * Copyright (C) 2007 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <asm/gpio.h>
#include "gpio_chip.h"

#define GPIO_NUM_TO_CHIP_INDEX(gpio) ((gpio)>>5)

struct gpio_state {
	unsigned long flags;
	int refcount;
};

static DEFINE_SPINLOCK(gpio_chips_lock);
static LIST_HEAD(gpio_chip_list);
static struct gpio_chip **gpio_chip_array;
static unsigned long gpio_chip_array_size;

int register_gpio_chip(struct gpio_chip *new_gpio_chip)
{
	int err = 0;
	struct gpio_chip *gpio_chip;
	int i;
	unsigned long irq_flags;
	unsigned int chip_array_start_index, chip_array_end_index;

	new_gpio_chip->state = kzalloc((new_gpio_chip->end + 1 - new_gpio_chip->start) * sizeof(new_gpio_chip->state[0]), GFP_KERNEL);
	if (new_gpio_chip->state == NULL) {
		printk(KERN_ERR "register_gpio_chip: failed to allocate state\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&gpio_chips_lock, irq_flags);
	chip_array_start_index = GPIO_NUM_TO_CHIP_INDEX(new_gpio_chip->start);
	chip_array_end_index = GPIO_NUM_TO_CHIP_INDEX(new_gpio_chip->end);
	if (chip_array_end_index >= gpio_chip_array_size) {
		struct gpio_chip **new_gpio_chip_array;
		unsigned long new_gpio_chip_array_size = chip_array_end_index + 1;

		new_gpio_chip_array = kmalloc(new_gpio_chip_array_size * sizeof(new_gpio_chip_array[0]), GFP_ATOMIC);
		if (new_gpio_chip_array == NULL) {
			printk(KERN_ERR "register_gpio_chip: failed to allocate array\n");
			err = -ENOMEM;
			goto failed;
		}
		for (i = 0; i < gpio_chip_array_size; i++)
			new_gpio_chip_array[i] = gpio_chip_array[i];
		for (i = gpio_chip_array_size; i < new_gpio_chip_array_size; i++)
			new_gpio_chip_array[i] = NULL;
		gpio_chip_array = new_gpio_chip_array;
		gpio_chip_array_size = new_gpio_chip_array_size;
	}
	list_for_each_entry(gpio_chip, &gpio_chip_list, list) {
		if (gpio_chip->start > new_gpio_chip->end) {
			list_add_tail(&new_gpio_chip->list, &gpio_chip->list);
			goto added;
		}
		if (gpio_chip->end >= new_gpio_chip->start) {
			printk(KERN_ERR "register_gpio_source %u-%u overlaps with %u-%u\n",
			       new_gpio_chip->start, new_gpio_chip->end,
			       gpio_chip->start, gpio_chip->end);
			err = -EBUSY;
			goto failed;
		}
	}
	list_add_tail(&new_gpio_chip->list, &gpio_chip_list);
added:
	for (i = chip_array_start_index; i <= chip_array_end_index; i++) {
		if (gpio_chip_array[i] == NULL || gpio_chip_array[i]->start > new_gpio_chip->start)
			gpio_chip_array[i] = new_gpio_chip;
	}
failed:
	spin_unlock_irqrestore(&gpio_chips_lock, irq_flags);
	if (err)
		kfree(new_gpio_chip->state);
	return err;
}

static struct gpio_chip *get_gpio_chip_locked(unsigned int gpio)
{
	unsigned long i;
	struct gpio_chip *chip;

	i = GPIO_NUM_TO_CHIP_INDEX(gpio);
	if (i >= gpio_chip_array_size)
		return NULL;
	chip = gpio_chip_array[i];
	if (chip == NULL)
		return NULL;
	list_for_each_entry_from(chip, &gpio_chip_list, list) {
		if (gpio < chip->start)
			return NULL;
		if (gpio <= chip->end)
			return chip;
	}
	return NULL;
}

static int request_gpio(unsigned int gpio, unsigned long flags)
{
	int err = 0;
	struct gpio_chip *chip;
	unsigned long irq_flags;
	unsigned long chip_index;

	spin_lock_irqsave(&gpio_chips_lock, irq_flags);
	chip = get_gpio_chip_locked(gpio);
	if (chip == NULL) {
		err = -EINVAL;
		goto err;
	}
	chip_index = gpio - chip->start;
	if (chip->state[chip_index].refcount == 0) {
		chip->configure(chip, gpio, flags);
		chip->state[chip_index].flags = flags;
		chip->state[chip_index].refcount++;
	} else if ((flags & IRQF_SHARED) && (chip->state[chip_index].flags & IRQF_SHARED))
		chip->state[chip_index].refcount++;
	else
		err = -EBUSY;
err:
	spin_unlock_irqrestore(&gpio_chips_lock, irq_flags);
	return err;
}

int gpio_request(unsigned gpio, const char *label)
{
	return request_gpio(gpio, 0);
}
EXPORT_SYMBOL(gpio_request);

void gpio_free(unsigned gpio)
{
	struct gpio_chip *chip;
	unsigned long irq_flags;
	unsigned long chip_index;

	spin_lock_irqsave(&gpio_chips_lock, irq_flags);
	chip = get_gpio_chip_locked(gpio);
	if (chip) {
		chip_index = gpio - chip->start;
		chip->state[chip_index].refcount--;
	}
	spin_unlock_irqrestore(&gpio_chips_lock, irq_flags);
}
EXPORT_SYMBOL(gpio_free);

static int gpio_get_irq_num(unsigned int gpio, unsigned int *irqp, unsigned long *irqnumflagsp)
{
	int ret = -ENOTSUPP;
	struct gpio_chip *chip;
	unsigned long irq_flags;

	spin_lock_irqsave(&gpio_chips_lock, irq_flags);
	chip = get_gpio_chip_locked(gpio);
	if (chip && chip->get_irq_num)
		ret = chip->get_irq_num(chip, gpio, irqp, irqnumflagsp);
	spin_unlock_irqrestore(&gpio_chips_lock, irq_flags);
	return ret;
}

int gpio_to_irq(unsigned gpio)
{
	int ret, irq;
	ret = gpio_get_irq_num(gpio, &irq, NULL);
	if (ret)
		return ret;
	return irq;
}
EXPORT_SYMBOL(gpio_to_irq);

int gpio_configure(unsigned int gpio, unsigned long flags)
{
	int ret = -ENOTSUPP;
	struct gpio_chip *chip;
	unsigned long irq_flags;

	spin_lock_irqsave(&gpio_chips_lock, irq_flags);
	chip = get_gpio_chip_locked(gpio);
	if (chip)
		ret = chip->configure(chip, gpio, flags);
	spin_unlock_irqrestore(&gpio_chips_lock, irq_flags);
	return ret;
}
EXPORT_SYMBOL(gpio_configure);

int gpio_direction_input(unsigned gpio)
{
	return gpio_configure(gpio, GPIOF_INPUT);
}
EXPORT_SYMBOL(gpio_direction_input);

int gpio_direction_output(unsigned gpio, int value)
{
	gpio_set_value(gpio, value);
	return gpio_configure(gpio, GPIOF_DRIVE_OUTPUT);
}
EXPORT_SYMBOL(gpio_direction_output);

int gpio_get_value(unsigned gpio)
{
	int ret = -ENOTSUPP;
	struct gpio_chip *chip;
	unsigned long irq_flags;

	spin_lock_irqsave(&gpio_chips_lock, irq_flags);
	chip = get_gpio_chip_locked(gpio);
	if (chip && chip->read)
		ret = chip->read(chip, gpio);
	spin_unlock_irqrestore(&gpio_chips_lock, irq_flags);
	return ret;
}
EXPORT_SYMBOL(gpio_get_value);

void gpio_set_value(unsigned gpio, int on)
{
	int ret = -ENOTSUPP;
	struct gpio_chip *chip;
	unsigned long irq_flags;

	spin_lock_irqsave(&gpio_chips_lock, irq_flags);
	chip = get_gpio_chip_locked(gpio);
	if (chip && chip->write)
		ret = chip->write(chip, gpio, on);
	spin_unlock_irqrestore(&gpio_chips_lock, irq_flags);
}
EXPORT_SYMBOL(gpio_set_value);

int gpio_read_detect_status(unsigned int gpio)
{
	int ret = -ENOTSUPP;
	struct gpio_chip *chip;
	unsigned long irq_flags;

	spin_lock_irqsave(&gpio_chips_lock, irq_flags);
	chip = get_gpio_chip_locked(gpio);
	if (chip && chip->read_detect_status)
		ret = chip->read_detect_status(chip, gpio);
	spin_unlock_irqrestore(&gpio_chips_lock, irq_flags);
	return ret;
}
EXPORT_SYMBOL(gpio_read_detect_status);

int gpio_clear_detect_status(unsigned int gpio)
{
	int ret = -ENOTSUPP;
	struct gpio_chip *chip;
	unsigned long irq_flags;

	spin_lock_irqsave(&gpio_chips_lock, irq_flags);
	chip = get_gpio_chip_locked(gpio);
	if (chip && chip->clear_detect_status)
		ret = chip->clear_detect_status(chip, gpio);
	spin_unlock_irqrestore(&gpio_chips_lock, irq_flags);
	return ret;
}
EXPORT_SYMBOL(gpio_clear_detect_status);
