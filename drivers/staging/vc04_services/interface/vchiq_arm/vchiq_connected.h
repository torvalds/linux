/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#include "vchiq_bus.h"

#ifndef VCHIQ_CONNECTED_H
#define VCHIQ_CONNECTED_H

struct vchiq_drv_mgmt;

void vchiq_add_connected_callback(struct vchiq_device *device, void (*callback)(void));
void vchiq_call_connected_callbacks(struct vchiq_drv_mgmt *mgmt);

#endif /* VCHIQ_CONNECTED_H */
