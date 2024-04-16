/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later */
/*
 * Copyright 2008 - 2015 Freescale Semiconductor Inc.
 */

#ifndef __DTSEC_H
#define __DTSEC_H

#include "fman_mac.h"

struct mac_device;

int dtsec_initialization(struct mac_device *mac_dev,
			 struct device_node *mac_node,
			 struct fman_mac_params *params);

#endif /* __DTSEC_H */
