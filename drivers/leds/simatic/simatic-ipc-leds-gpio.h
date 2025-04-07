/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Siemens SIMATIC IPC driver for GPIO based LEDs
 *
 * Copyright (c) Siemens AG, 2023
 *
 * Author:
 *  Henning Schild <henning.schild@siemens.com>
 */

#ifndef _SIMATIC_IPC_LEDS_GPIO_H
#define _SIMATIC_IPC_LEDS_GPIO_H

int simatic_ipc_leds_gpio_probe(struct platform_device *pdev,
				struct gpiod_lookup_table *table,
				struct gpiod_lookup_table *table_extra);

void simatic_ipc_leds_gpio_remove(struct platform_device *pdev,
				  struct gpiod_lookup_table *table,
				  struct gpiod_lookup_table *table_extra);

#endif /* _SIMATIC_IPC_LEDS_GPIO_H */
