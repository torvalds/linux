/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GSC_SUBMIT_H_
#define _XE_GSC_SUBMIT_H_

#include <linux/types.h>

struct iosys_map;
struct xe_device;
struct xe_gsc;

u32 xe_gsc_emit_header(struct xe_device *xe, struct iosys_map *map, u32 offset,
		       u8 heci_client_id, u64 host_session_id, u32 payload_size);
void xe_gsc_poison_header(struct xe_device *xe, struct iosys_map *map, u32 offset);

bool xe_gsc_check_and_update_pending(struct xe_device *xe,
				     struct iosys_map *in, u32 offset_in,
				     struct iosys_map *out, u32 offset_out);

int xe_gsc_read_out_header(struct xe_device *xe,
			   struct iosys_map *map, u32 offset,
			   u32 min_payload_size,
			   u32 *payload_offset);

int xe_gsc_pkt_submit_kernel(struct xe_gsc *gsc, u64 addr_in, u32 size_in,
			     u64 addr_out, u32 size_out);

#endif
