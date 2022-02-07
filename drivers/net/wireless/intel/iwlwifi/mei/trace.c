// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Intel Corporation
 */

#include <linux/module.h>

/* sparse doesn't like tracepoint macros */
#ifndef __CHECKER__

#define CREATE_TRACE_POINTS
#include "trace.h"
#include "trace-data.h"

#endif /* __CHECKER__ */
