/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2018-2021 Intel Corporation */

#ifndef __PECI_INTERNAL_H
#define __PECI_INTERNAL_H

#include <linux/device.h>
#include <linux/types.h>

struct peci_controller;

extern struct bus_type peci_bus_type;

extern struct device_type peci_controller_type;

#endif /* __PECI_INTERNAL_H */
