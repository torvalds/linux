/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Camera Interface (CIF) Driver
 *
 * Abstraction for the INTERFACE and CROP parts of the different CIF variants.
 * They shall be represented as V4L2 subdevice with one sink pad and one
 * source pad. The sink pad is connected to a subdevice: either the subdevice
 * provided by the driver of the companion chip connected to the DVP, or the
 * subdevice provided by the MIPI CSI-2 receiver driver. The source pad is
 * to V4l2 device(s) provided by one or many instance(s) of the DMA
 * abstraction.
 *
 * Copyright (C) 2025 Michael Riesch <michael.riesch@wolfvision.net>
 * Copyright (C) 2025 Collabora, Ltd.
 */

#ifndef _RKCIF_INTERFACE_H
#define _RKCIF_INTERFACE_H

#include "rkcif-common.h"

int rkcif_interface_register(struct rkcif_device *rkcif,
			     struct rkcif_interface *interface);

void rkcif_interface_unregister(struct rkcif_interface *interface);

const struct rkcif_input_fmt *
rkcif_interface_find_input_fmt(struct rkcif_interface *interface, bool ret_def,
			       u32 mbus_code);

#endif
