/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022 Microchip Technology Inc. */

#ifndef _GPIO_PCI1XXXX_H
#define _GPIO_PCI1XXXX_H

#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include <linux/auxiliary_bus.h>

/* Perform operations like variable length write, read and write with read back for OTP / EEPROM
 * Perform bit mode write in OTP
 */

struct gp_aux_data_type {
	int irq_num;
	resource_size_t region_start;
	resource_size_t region_length;
};

struct auxiliary_device_wrapper {
	struct auxiliary_device aux_dev;
	struct gp_aux_data_type gp_aux_data;
};

#endif
