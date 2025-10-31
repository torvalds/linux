// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_pagefault.h"
#include "xe_pagefault_types.h"

/**
 * DOC: Xe page faults
 *
 * Xe page faults are handled in two layers. The producer layer interacts with
 * hardware or firmware to receive and parse faults into struct xe_pagefault,
 * then forwards them to the consumer. The consumer layer services the faults
 * (e.g., memory migration, page table updates) and acknowledges the result back
 * to the producer, which then forwards the results to the hardware or firmware.
 * The consumer uses a page fault queue sized to absorb all potential faults and
 * a multi-threaded worker to process them. Multiple producers are supported,
 * with a single shared consumer.
 *
 * xe_pagefault.c implements the consumer layer.
 */

/**
 * xe_pagefault_init() - Page fault init
 * @xe: xe device instance
 *
 * Initialize Xe page fault state. Must be done after reading fuses.
 *
 * Return: 0 on Success, errno on failure
 */
int xe_pagefault_init(struct xe_device *xe)
{
	/* TODO - implement */
	return 0;
}

/**
 * xe_pagefault_reset() - Page fault reset for a GT
 * @xe: xe device instance
 * @gt: GT being reset
 *
 * Reset the Xe page fault state for a GT; that is, squash any pending faults on
 * the GT.
 */
void xe_pagefault_reset(struct xe_device *xe, struct xe_gt *gt)
{
	/* TODO - implement */
}

/**
 * xe_pagefault_handler() - Page fault handler
 * @xe: xe device instance
 * @pf: Page fault
 *
 * Sink the page fault to a queue (i.e., a memory buffer) and queue a worker to
 * service it. Safe to be called from IRQ or process context. Reclaim safe.
 *
 * Return: 0 on success, errno on failure
 */
int xe_pagefault_handler(struct xe_device *xe, struct xe_pagefault *pf)
{
	/* TODO - implement */
	return 0;
}
