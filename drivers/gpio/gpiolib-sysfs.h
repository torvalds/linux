/* SPDX-License-Identifier: GPL-2.0 */

#ifndef GPIOLIB_SYSFS_H
#define GPIOLIB_SYSFS_H

struct gpio_device;

#ifdef CONFIG_GPIO_SYSFS

int gpiochip_sysfs_register(struct gpio_device *gdev);
int gpiochip_sysfs_register_all(void);
void gpiochip_sysfs_unregister(struct gpio_device *gdev);

#else

static inline int gpiochip_sysfs_register(struct gpio_device *gdev)
{
	return 0;
}

static inline int gpiochip_sysfs_register_all(void)
{
	return 0;
}

static inline void gpiochip_sysfs_unregister(struct gpio_device *gdev)
{
}

#endif /* CONFIG_GPIO_SYSFS */

#endif /* GPIOLIB_SYSFS_H */
