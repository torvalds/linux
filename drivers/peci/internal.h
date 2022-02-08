/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2018-2021 Intel Corporation */

#ifndef __PECI_INTERNAL_H
#define __PECI_INTERNAL_H

#include <linux/device.h>
#include <linux/types.h>

struct peci_controller;
struct attribute_group;
struct peci_device;
struct peci_request;

/* PECI CPU address range 0x30-0x37 */
#define PECI_BASE_ADDR		0x30
#define PECI_DEVICE_NUM_MAX	8

struct peci_request *peci_request_alloc(struct peci_device *device, u8 tx_len, u8 rx_len);
void peci_request_free(struct peci_request *req);

extern struct device_type peci_device_type;
extern const struct attribute_group *peci_device_groups[];

int peci_device_create(struct peci_controller *controller, u8 addr);
void peci_device_destroy(struct peci_device *device);

extern struct bus_type peci_bus_type;
extern const struct attribute_group *peci_bus_groups[];

extern struct device_type peci_controller_type;

int peci_controller_scan_devices(struct peci_controller *controller);

#endif /* __PECI_INTERNAL_H */
