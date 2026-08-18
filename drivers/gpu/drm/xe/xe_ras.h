/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_RAS_H_
#define _XE_RAS_H_

struct xe_device;
struct xe_sysctrl_event_response;

void xe_ras_counter_threshold_crossed(struct xe_device *xe,
				      struct xe_sysctrl_event_response *response);

#endif
