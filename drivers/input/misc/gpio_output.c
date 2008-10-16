/* drivers/input/misc/gpio_output.c
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
#include <linux/gpio.h>
#include <linux/gpio_event.h>

int gpio_event_output_event(
	struct input_dev *input_dev, struct gpio_event_info *info, void **data,
	unsigned int type, unsigned int code, int value)
{
	int i;
	struct gpio_event_output_info *oi;
	oi = container_of(info, struct gpio_event_output_info, info);
	if (type != oi->type)
		return 0;
	if (!(oi->flags & GPIOEDF_ACTIVE_HIGH))
		value = !value;
	for (i = 0; i < oi->keymap_size; i++)
		if (code == oi->keymap[i].code)
			gpio_set_value(oi->keymap[i].gpio, value);
	return 0;
}

int gpio_event_output_func(
	struct input_dev *input_dev, struct gpio_event_info *info, void **data,
	int func)
{
	int ret;
	int i;
	struct gpio_event_output_info *oi;
	oi = container_of(info, struct gpio_event_output_info, info);

	if (func == GPIO_EVENT_FUNC_SUSPEND || func == GPIO_EVENT_FUNC_RESUME)
		return 0;

	if (func == GPIO_EVENT_FUNC_INIT) {
		int output_level = !(oi->flags & GPIOEDF_ACTIVE_HIGH);
		for (i = 0; i < oi->keymap_size; i++)
			input_set_capability(input_dev, oi->type,
					     oi->keymap[i].code);

		for (i = 0; i < oi->keymap_size; i++) {
			ret = gpio_request(oi->keymap[i].gpio,
					   "gpio_event_output");
			if (ret) {
				pr_err("gpio_event_output_func: gpio_request "
					"failed for %d\n", oi->keymap[i].gpio);
				goto err_gpio_request_failed;
			}
			ret = gpio_direction_output(oi->keymap[i].gpio,
						    output_level);
			if (ret) {
				pr_err("gpio_event_output_func: "
					"gpio_direction_output failed for %d\n",
					oi->keymap[i].gpio);
				goto err_gpio_direction_output_failed;
			}
		}
		return 0;
	}

	ret = 0;
	for (i = oi->keymap_size - 1; i >= 0; i--) {
err_gpio_direction_output_failed:
		gpio_free(oi->keymap[i].gpio);
err_gpio_request_failed:
		;
	}
	return ret;
}

