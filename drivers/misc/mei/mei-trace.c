// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2015, Intel Corporation.
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
