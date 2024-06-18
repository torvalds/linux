/* SPDX-License-Identifier: GPL-2.0 */

#ifndef GPIOLIB_SWNODE_H
#define GPIOLIB_SWNODE_H

struct fwnode_handle;
struct gpio_desc;

struct gpio_desc *swnode_find_gpio(struct fwnode_handle *fwnode,
				   const char *con_id, unsigned int idx,
				   unsigned long *flags);
int swnode_gpio_count(const struct fwnode_handle *fwnode, const char *con_id);

#endif /* GPIOLIB_SWNODE_H */
