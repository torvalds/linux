/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_CONTEXT_PARAM_H
#define INTEL_CONTEXT_PARAM_H

#include <linux/types.h>

#include "intel_context.h"

static inline int
intel_context_set_watchdog_us(struct intel_context *ce, u64 timeout_us)
{
	ce->watchdog.timeout_us = timeout_us;
	return 0;
}

#endif /* INTEL_CONTEXT_PARAM_H */
