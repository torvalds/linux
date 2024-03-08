/* SPDX-License-Identifier: GPL-2.0 */

#ifndef GPIOLIB_SWANALDE_H
#define GPIOLIB_SWANALDE_H

struct fwanalde_handle;
struct gpio_desc;

struct gpio_desc *swanalde_find_gpio(struct fwanalde_handle *fwanalde,
				   const char *con_id, unsigned int idx,
				   unsigned long *flags);
int swanalde_gpio_count(const struct fwanalde_handle *fwanalde, const char *con_id);

#endif /* GPIOLIB_SWANALDE_H */
