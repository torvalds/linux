/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHIQ_CONNECTED_H
#define VCHIQ_CONNECTED_H

void vchiq_add_connected_callback(void (*callback)(void));
void vchiq_call_connected_callbacks(void);

#endif /* VCHIQ_CONNECTED_H */
