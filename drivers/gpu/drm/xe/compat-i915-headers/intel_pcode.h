/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_PCODE_H__
#define __INTEL_PCODE_H__

#include "intel_uncore.h"
#include "xe_pcode.h"

static inline int
snb_pcode_write_timeout(struct intel_uncore *uncore, u32 mbox, u32 val,
			int fast_timeout_us, int slow_timeout_ms)
{
	return xe_pcode_write_timeout(__compat_uncore_to_tile(uncore), mbox, val,
				      slow_timeout_ms ?: 1);
}

static inline int
snb_pcode_write(struct intel_uncore *uncore, u32 mbox, u32 val)
{

	return xe_pcode_write(__compat_uncore_to_tile(uncore), mbox, val);
}

static inline int
snb_pcode_read(struct intel_uncore *uncore, u32 mbox, u32 *val, u32 *val1)
{
	return xe_pcode_read(__compat_uncore_to_tile(uncore), mbox, val, val1);
}

static inline int
skl_pcode_request(struct intel_uncore *uncore, u32 mbox,
		  u32 request, u32 reply_mask, u32 reply,
		  int timeout_base_ms)
{
	return xe_pcode_request(__compat_uncore_to_tile(uncore), mbox, request, reply_mask, reply,
				timeout_base_ms);
}

#endif /* __INTEL_PCODE_H__ */
