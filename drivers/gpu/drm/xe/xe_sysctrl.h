/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_SYSCTRL_H_
#define _XE_SYSCTRL_H_

#include <linux/container_of.h>

#include "xe_device_types.h"
#include "xe_sysctrl_types.h"

static inline struct xe_device *sc_to_xe(struct xe_sysctrl *sc)
{
	return container_of(sc, struct xe_device, sc);
}

void xe_sysctrl_event(struct xe_sysctrl *sc);
int xe_sysctrl_init(struct xe_device *xe);
void xe_sysctrl_irq_handler(struct xe_device *xe, u32 master_ctl);
void xe_sysctrl_pm_resume(struct xe_device *xe);

#endif
