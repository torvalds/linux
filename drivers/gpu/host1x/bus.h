/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012-2013, NVIDIA Corporation
 */

#ifndef HOST1X_BUS_H
#define HOST1X_BUS_H

struct bus_type;
struct host1x;

extern const struct bus_type host1x_bus_type;

int host1x_register(struct host1x *host1x);
int host1x_unregister(struct host1x *host1x);

#endif
