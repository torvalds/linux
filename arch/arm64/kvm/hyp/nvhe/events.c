// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC
 * Author: Vincent Donnefort <vdonnefort@google.com>
 */

#include <nvhe/mm.h>
#include <nvhe/trace.h>

#include <nvhe/define_events.h>

int __tracing_enable_event(unsigned short id, bool enable)
{
	struct hyp_event_id *event_id = &__hyp_event_ids_start[id];
	atomic_t *enabled;

	if (event_id >= __hyp_event_ids_end)
		return -EINVAL;

	enabled = hyp_fixmap_map(__hyp_pa(&event_id->enabled));
	atomic_set(enabled, enable);
	hyp_fixmap_unmap();

	return 0;
}
