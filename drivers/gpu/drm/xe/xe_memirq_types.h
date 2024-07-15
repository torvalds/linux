/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_MEMIRQ_TYPES_H_
#define _XE_MEMIRQ_TYPES_H_

#include <linux/iosys-map.h>

struct xe_bo;

/* ISR */
#define XE_MEMIRQ_STATUS_OFFSET		0x0
/* IIR */
#define XE_MEMIRQ_SOURCE_OFFSET		0x400
/* IMR */
#define XE_MEMIRQ_ENABLE_OFFSET		0x440

/**
 * struct xe_memirq - Data used by the `Memory Based Interrupts`_.
 *
 * @bo: buffer object with `Memory Based Interrupts Page Layout`_.
 * @source: iosys pointer to `Interrupt Source Report Page`_.
 * @status: iosys pointer to `Interrupt Status Report Page`_.
 * @mask: iosys pointer to Interrupt Enable Mask.
 * @enabled: internal flag used to control processing of the interrupts.
 */
struct xe_memirq {
	struct xe_bo *bo;
	struct iosys_map source;
	struct iosys_map status;
	struct iosys_map mask;
	bool enabled;
};

#endif
