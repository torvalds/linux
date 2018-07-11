/* SPDX-License-Identifier: GPL-2.0 */
/* Common logging utilities for the Gasket driver framework.
 *
 * Copyright (C) 2018 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/pci.h>
#include <linux/printk.h>

#ifndef _GASKET_LOGGING_H_
#define _GASKET_LOGGING_H_

/* Base macro; other logging can/should be built on top of this. */
#define gasket_dev_log(level, device, pci_dev, format, arg...)                 \
	if (false) {                                                           \
		if (pci_dev) {                                                 \
			dev_##level(&(pci_dev)->dev, "%s: " format "\n",       \
				__func__, ##arg);                              \
		} else {                                                       \
			gasket_nodev_log(level, format, ##arg);                \
		}                                                              \
	}

/* "No-device" logging macros. */
#define gasket_nodev_log(level, format, arg...)                                \
	if (false) pr_##level("gasket: %s: " format "\n", __func__, ##arg)

#define gasket_nodev_debug(format, arg...)                                     \
	if (false) gasket_nodev_log(debug, format, ##arg)

#define gasket_nodev_info(format, arg...)                                      \
	if (false) gasket_nodev_log(info, format, ##arg)

#define gasket_nodev_warn(format, arg...)                                      \
	if (false) gasket_nodev_log(warn, format, ##arg)

#define gasket_nodev_error(format, arg...)                                     \
	if (false) gasket_nodev_log(err, format, ##arg)

/* gasket_dev logging macros */
#define gasket_log_info(gasket_dev, format, arg...)                            \
	if (false) gasket_dev_log(info, (gasket_dev)->dev_info.device,         \
		(gasket_dev)->pci_dev, format, ##arg)

#define gasket_log_warn(gasket_dev, format, arg...)                            \
	if (false) gasket_dev_log(warn, (gasket_dev)->dev_info.device,         \
		(gasket_dev)->pci_dev, format, ##arg)

#define gasket_log_error(gasket_dev, format, arg...)                           \
	if (false) gasket_dev_log(err, (gasket_dev)->dev_info.device,          \
		(gasket_dev)->pci_dev, format, ##arg)

#define gasket_log_debug(gasket_dev, format, arg...)                           \
	if (false){                                                            \
		if ((gasket_dev)->pci_dev) {                                   \
			dev_dbg(&((gasket_dev)->pci_dev)->dev, "%s: " format   \
			"\n", __func__, ##arg);                                \
		} else {                                                       \
			gasket_nodev_log(debug, format, ##arg);                \
		}                                                              \
	}

#endif
