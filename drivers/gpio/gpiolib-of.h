/* SPDX-License-Identifier: GPL-2.0 */

#ifndef GPIOLIB_OF_H
#define GPIOLIB_OF_H

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/types.h>

#include <linux/notifier.h>

struct device;

struct gpio_chip;
struct gpio_desc;
struct gpio_device;

#ifdef CONFIG_OF_GPIO
struct gpio_desc *of_find_gpio(struct device_node *np,
			       const char *con_id,
			       unsigned int idx,
			       unsigned long *lookupflags);
int of_gpiochip_add(struct gpio_chip *gc);
void of_gpiochip_remove(struct gpio_chip *gc);
int of_gpio_get_count(struct device *dev, const char *con_id);
#else
static inline struct gpio_desc *of_find_gpio(struct device_node *np,
					     const char *con_id,
					     unsigned int idx,
					     unsigned long *lookupflags)
{
	return ERR_PTR(-ENOENT);
}
static inline int of_gpiochip_add(struct gpio_chip *gc) { return 0; }
static inline void of_gpiochip_remove(struct gpio_chip *gc) { }
static inline int of_gpio_get_count(struct device *dev, const char *con_id)
{
	return 0;
}
#endif /* CONFIG_OF_GPIO */

extern struct notifier_block gpio_of_notifier;

#endif /* GPIOLIB_OF_H */
