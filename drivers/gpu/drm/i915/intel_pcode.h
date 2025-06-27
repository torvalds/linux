/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2013-2021 Intel Corporation
 */

#ifndef _INTEL_PCODE_H_
#define _INTEL_PCODE_H_

#include <linux/types.h>

struct drm_device;
struct intel_uncore;

int snb_pcode_read(struct intel_uncore *uncore, u32 mbox, u32 *val, u32 *val1);
int snb_pcode_write_timeout(struct intel_uncore *uncore, u32 mbox, u32 val, int timeout_ms);
#define snb_pcode_write(uncore, mbox, val) \
	snb_pcode_write_timeout((uncore), (mbox), (val), 1)

int skl_pcode_request(struct intel_uncore *uncore, u32 mbox, u32 request,
		      u32 reply_mask, u32 reply, int timeout_base_ms);

int intel_pcode_init(struct intel_uncore *uncore);

/*
 * Helpers for dGfx PCODE mailbox command formatting
 */
int snb_pcode_read_p(struct intel_uncore *uncore, u32 mbcmd, u32 p1, u32 p2, u32 *val);
int snb_pcode_write_p(struct intel_uncore *uncore, u32 mbcmd, u32 p1, u32 p2, u32 val);

/* Helpers with drm device */
int intel_pcode_read(struct drm_device *drm, u32 mbox, u32 *val, u32 *val1);
int intel_pcode_write_timeout(struct drm_device *drm, u32 mbox, u32 val, int timeout_ms);
#define intel_pcode_write(drm, mbox, val) \
	intel_pcode_write_timeout((drm), (mbox), (val), 1)

int intel_pcode_request(struct drm_device *drm, u32 mbox, u32 request,
			u32 reply_mask, u32 reply, int timeout_base_ms);

#endif /* _INTEL_PCODE_H */
