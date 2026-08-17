/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_SYSCTRL_TYPES_H_
#define _XE_SYSCTRL_TYPES_H_

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue_types.h>

struct xe_mmio;

/**
 * struct xe_sysctrl - System Controller driver context
 *
 * This structure maintains the runtime state for System Controller
 * communication. All fields are initialized during xe_sysctrl_init()
 * and protected appropriately for concurrent access.
 */
struct xe_sysctrl {
	/** @mmio: MMIO region for system control registers */
	struct xe_mmio *mmio;

	/** @cmd_lock: Mutex protecting mailbox command operations */
	struct mutex cmd_lock;

	/** @phase_bit: Message boundary phase toggle bit (0 or 1) */
	bool phase_bit;

	/** @work: Pending events worker */
	struct work_struct work;

	/** @event_lock: Mutex protecting pending events */
	struct mutex event_lock;
};

#endif
