/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#include <linux/module.h>

/* sparse doesn't like tracepoint macros */
#ifndef __CHECKER__
#define CREATE_TRACE_POINTS
#include "mei-trace.h"

EXPORT_TRACEPOINT_SYMBOL(mei_reg_read);
EXPORT_TRACEPOINT_SYMBOL(mei_reg_write);
EXPORT_TRACEPOINT_SYMBOL(mei_pci_cfg_read);
#endif /* __CHECKER__ */
