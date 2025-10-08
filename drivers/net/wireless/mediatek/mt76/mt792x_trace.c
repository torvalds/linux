// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2023 Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include <linux/module.h>

#ifndef __CHECKER__
#define CREATE_TRACE_POINTS
#include "mt792x_trace.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(lp_event);

#endif
