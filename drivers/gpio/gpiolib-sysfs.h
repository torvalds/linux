/* SPDX-License-Identifier: GPL-2.0 */

#ifndef GPIOLIB_SYSFS_H
#define GPIOLIB_SYSFS_H

struct gpio_device;

#ifdef CONFIG_GPIO_SYSFS

int gpiochip_sysfs_register(struct gpio_chip *gc);
void gpiochip_sysfs_unregister(struct gpio_chip *gc);

#else

static inline int gpiochip_sysfs_register(struct gpio_chip *gc)
{
	return 0;
}

static inline void gpiochip_sysfs_unregister(struct gpio_chip *gc)
{
}

#endif /* CONFIG_GPIO_SYSFS */

#endif /* GPIOLIB_SYSFS_H */
