// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Intel Corporation */

#ifndef _INTEL_THC_WOT_H_
#define _INTEL_THC_WOT_H_

#include <linux/types.h>

#include <linux/gpio/consumer.h>

/**
 * struct thc_wot - THC Wake-on-Touch data structure
 * @gpio_irq : GPIO interrupt IRQ number for wake-on-touch
 * @gpio_irq_wakeable : Indicate GPIO IRQ workable or not
 */
struct thc_wot {
	int gpio_irq;
	bool gpio_irq_wakeable;
};

struct thc_device;

void thc_wot_config(struct thc_device *thc_dev, const struct acpi_gpio_mapping *gpio_map);
void thc_wot_unconfig(struct thc_device *thc_dev);

#endif /* _INTEL_THC_WOT_H_ */
