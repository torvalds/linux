/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_MEMIRQ_TYPES_H_
#define _XE_MEMIRQ_TYPES_H_

#include <linux/iosys-map.h>

struct xe_bo;

/**
 * struct xe_memirq - Data used by the `Memory Based Interrupts`_.
 *
 * @bo: buffer object with `Memory Based Interrupts Page Layout`_.
 * @num_pages: number of per-instance source/status pages.
 * @source: iosys pointer to `Interrupt Source Report Page`_.
 * @enabled: internal flag used to control processing of the interrupts.
 */
struct xe_memirq {
	struct xe_bo *bo;
	unsigned int num_pages;
	struct iosys_map source;
	bool enabled;
};

#endif
