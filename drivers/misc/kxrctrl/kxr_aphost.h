/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPI controller driver for the nordic52832 SoCs
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "kxr_spi_xfer.h"
#include "kxr_spi_xchg.h"
#include "kxr_spi_uart.h"
#include "kxr_aphost_v1.h"

#pragma once

enum kxr_spi_power_mode {
	KXR_SPI_POWER_MODE_OFF,
	KXR_SPI_POWER_MODE_ON,
	KXR_SPI_POWER_MODE_DFU,
};

struct kxr_aphost {
	struct kxr_spi_xfer xfer;
	struct kxr_spi_xchg xchg;
	struct kxr_spi_uart uart;
	struct js_spi_client js;

#ifndef CONFIG_KXR_SIMULATION_TEST
	struct pinctrl *pinctrl;
	struct pinctrl_state *active;
	struct pinctrl_state *suspend;

	struct gpio_desc *gpio_dfu;
	struct gpio_desc *gpio_vcc;
	struct gpio_desc *gpio_irq;
	struct gpio_desc *gpio_ledl;
	struct gpio_desc *gpio_ledr;

	struct regulator *pwr_v1p8;
#endif
	struct mutex power_mutex;
	bool pwr_enabled;
};

void kxr_aphost_println(const char *fmt, ...);
bool kxr_aphost_power_mode_set(struct kxr_aphost *aphost, enum kxr_spi_power_mode mode);
enum kxr_spi_power_mode kxr_aphost_power_mode_get(void);

int kxr_spi_xfer_setup(struct spi_device *spi);
void kxr_spi_xchg_write_command(struct kxr_aphost *aphost, u32 command);
int kxr_spi_xchg_show_command(struct kxr_aphost *aphost, u32 command, char *buff, int size);
int kxr_spi_xfer_probe(struct kxr_aphost *aphost);
int kxr_spi_xfer_start(struct kxr_aphost *aphost);
void kxr_spi_xfer_remove(struct kxr_aphost *aphost);

bool kxr_spi_xchg_sync(struct kxr_aphost *aphost);
int kxr_spi_xchg_probe(struct kxr_aphost *aphost);
void kxr_spi_xchg_remove(struct kxr_aphost *aphost);

int kxr_spi_uart_probe(struct kxr_aphost *aphost);
void kxr_spi_uart_remove(struct kxr_aphost *aphost);
bool kxr_spi_uart_sync(struct kxr_aphost *aphost);

int kxr_spi_dfu_probe(struct kxr_aphost *aphost);
void kxr_spi_dfu_remove(struct kxr_aphost *aphost);

bool js_thread(struct kxr_aphost *aphost);
int js_spi_driver_probe(struct kxr_aphost *aphost);
int js_spi_driver_remove(struct kxr_aphost *aphost);

static inline struct kxr_aphost *kxr_aphost_get_drv_data(const struct device *dev)
{
	return (struct kxr_aphost *) dev_get_drvdata(dev);
}

static inline struct kxr_aphost *kxr_aphost_get_uart_data(struct uart_port *port)
{
	return (struct kxr_aphost *) port->private_data;
}

static inline struct kxr_aphost *kxr_spi_xfer_get_aphost(struct kxr_spi_xfer *xfer)
{
	return container_of(xfer, struct kxr_aphost, xfer);
}

static inline struct kxr_aphost *kxr_spi_xchg_get_aphost(struct kxr_spi_xchg *xchg)
{
	return container_of(xchg, struct kxr_aphost, xchg);
}

static inline struct kxr_aphost *kxr_spi_uart_get_aphost(struct kxr_spi_uart *uart)
{
	return container_of(uart, struct kxr_aphost, uart);
}

static inline bool kxr_aphost_power_enabled(void)
{
	return kxr_aphost_power_mode_get() != KXR_SPI_POWER_MODE_OFF;
}

static inline bool kxr_aphost_power_disabled(void)
{
	return kxr_aphost_power_mode_get() == KXR_SPI_POWER_MODE_OFF;
}

