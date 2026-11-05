/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_PAGEFAULT_H_
#define _XE_PAGEFAULT_H_

#include <linux/bitfield.h>

#include "xe_pagefault_types.h"

struct drm_printer;
struct xe_device;
struct xe_gt;
struct xe_pagefault;

int xe_pagefault_init(struct xe_device *xe);

void xe_pagefault_reset(struct xe_device *xe, struct xe_gt *gt);

int xe_pagefault_handler(struct xe_device *xe, struct xe_pagefault *pf);

void xe_pagefault_print_info(struct xe_device *xe, struct drm_printer *p);

/*
 * consumer.page_addr is always page (4K) aligned, so the low bits are
 * reserved and unused by the real address. Steal a byte of those bits to
 * record an &enum xe_pagefault_error describing the high-level point at
 * which servicing of the fault failed, so it can be reported by
 * xe_pagefault_print(). All real address consumers of page_addr must go
 * through xe_pagefault_addr() to mask off these reserved bits.
 */
#define XE_PAGEFAULT_ERROR_MASK		GENMASK_ULL(7, 0)

/**
 * xe_pagefault_set_error() - record the failure reason for a pagefault
 * @pf: Pagefault entry
 * @error: Failure reason
 *
 * Encodes @error into the reserved low bits of consumer.page_addr. Should be
 * called at the high-level point a pagefault fails to service so
 * xe_pagefault_print() can later report a mnemonic failure reason.
 */
static inline void
xe_pagefault_set_error(struct xe_pagefault *pf, enum xe_pagefault_error error)
{
	pf->consumer.page_addr &= ~XE_PAGEFAULT_ERROR_MASK;
	pf->consumer.page_addr |= FIELD_PREP(XE_PAGEFAULT_ERROR_MASK, error);
}

/**
 * xe_pagefault_get_error() - read the failure reason for a pagefault
 * @pf: Pagefault entry
 *
 * Return: The &enum xe_pagefault_error previously recorded via
 * xe_pagefault_set_error(), or %XE_PAGEFAULT_ERROR_NONE if none was recorded.
 */
static inline enum xe_pagefault_error
xe_pagefault_get_error(struct xe_pagefault *pf)
{
	return FIELD_GET(XE_PAGEFAULT_ERROR_MASK, pf->consumer.page_addr);
}

/**
 * xe_pagefault_addr() - read the real faulted address for a pagefault
 * @pf: Pagefault entry
 *
 * consumer.page_addr may have failure reason bits encoded into its reserved
 * low bits by xe_pagefault_set_error(). This masks those bits off, returning
 * the real page address. All accesses to the faulted address must go through
 * this helper rather than reading consumer.page_addr directly.
 *
 * Return: The real (page aligned) faulted address.
 */
static inline u64 xe_pagefault_addr(struct xe_pagefault *pf)
{
	return pf->consumer.page_addr & ~XE_PAGEFAULT_ERROR_MASK;
}

#define XE_PAGEFAULT_END_ADDR_MASK	(~0xfffull)

/**
 * xe_pagefault_set_end_addr() - store serviced range end for a pagefault
 * @pf: Pagefault entry
 * @end_addr: Inclusive end address of the serviced fault range
 *
 * The pagefault consumer stores the resolved fault range so subsequent faults
 * hitting the same range can be immediately acknowledged without re-running
 * the full fault handling path.
 *
 * The end address shares storage with other consumer metadata and therefore
 * must be masked with %XE_PAGEFAULT_END_ADDR_MASK before storing. Bits outside
 * the mask are reserved for internal state tracking and must be preserved.
 */
static inline void
xe_pagefault_set_end_addr(struct xe_pagefault *pf, u64 end_addr)
{
	pf->consumer.end_addr &= ~XE_PAGEFAULT_END_ADDR_MASK;
	pf->consumer.end_addr |= end_addr;
}

/**
 * xe_pagefault_end_addr() - read serviced range end for a pagefault
 * @pf: Pagefault entry
 *
 * Returns the inclusive end address of the range previously recorded by
 * xe_pagefault_set_end_addr(). Only the bits covered by
 * %XE_PAGEFAULT_END_ADDR_MASK are returned; other bits in the storage are
 * reserved for internal state.
 *
 * Return: End address of the serviced fault range.
 */
static inline u64 xe_pagefault_end_addr(struct xe_pagefault *pf)
{
	return pf->consumer.end_addr & XE_PAGEFAULT_END_ADDR_MASK;
}

#undef XE_PAGEFAULT_END_ADDR_MASK

/**
 * xe_pagefault_set_start_addr() - store serviced range start for a pagefault
 * @pf: Pagefault entry
 * @start_addr: Start address of the serviced fault range
 *
 * The pagefault consumer stores the resolved fault range so subsequent faults
 * hitting the same range can be immediately acknowledged without re-running
 * the full fault handling path.
 *
 * The start address shares storage with the failure reason recorded by
 * xe_pagefault_set_error() and therefore must be masked with
 * %XE_PAGEFAULT_ERROR_MASK before storing so any previously recorded error is
 * preserved.
 */
static inline void
xe_pagefault_set_start_addr(struct xe_pagefault *pf, u64 start_addr)
{
	pf->consumer.page_addr &= XE_PAGEFAULT_ERROR_MASK;
	pf->consumer.page_addr |= (start_addr & ~XE_PAGEFAULT_ERROR_MASK);
}

/**
 * xe_pagefault_start_addr() - read serviced range start for a pagefault
 * @pf: Pagefault entry
 *
 * Returns the inclusive start address of the range previously recorded by
 * xe_pagefault_set_start_addr().
 *
 * Return: Start address of the serviced fault range.
 */
static inline u64 xe_pagefault_start_addr(struct xe_pagefault *pf)
{
	return xe_pagefault_addr(pf);
}

#endif
