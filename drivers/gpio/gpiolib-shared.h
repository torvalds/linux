/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_GPIO_SHARED_H
#define __LINUX_GPIO_SHARED_H

#include <linux/mutex.h>

struct gpio_device;
struct gpio_desc;
struct device;
struct fwnode_handle;

#if IS_ENABLED(CONFIG_GPIO_SHARED)

int gpiochip_setup_shared(struct gpio_chip *gc);
void gpio_device_teardown_shared(struct gpio_device *gdev);
int gpio_shared_add_proxy_lookup(struct device *consumer,
				 struct fwnode_handle *fwnode,
				 const char *con_id, unsigned long lflags);

#else

static inline int gpiochip_setup_shared(struct gpio_chip *gc)
{
	return 0;
}

static inline void gpio_device_teardown_shared(struct gpio_device *gdev) { }

static inline int gpio_shared_add_proxy_lookup(struct device *consumer,
					       struct fwnode_handle *fwnode,
					       const char *con_id,
					       unsigned long lflags)
{
	return 0;
}

#endif /* CONFIG_GPIO_SHARED */

struct gpio_shared_desc {
	struct gpio_desc *desc;
	unsigned long cfg;
	unsigned int usecnt;
	unsigned int highcnt;
	struct mutex mutex; /* serializes all proxy operations on this descriptor */
};

struct gpio_shared_desc *devm_gpiod_shared_get(struct device *dev);

#endif /* __LINUX_GPIO_SHARED_H */
