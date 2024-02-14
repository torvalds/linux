// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC
 */

#include <nvhe/trace.h>
#include <nvhe/mm.h>

extern struct hyp_event_id __hyp_event_ids_start[];
extern struct hyp_event_id __hyp_event_ids_end[];

#undef HYP_EVENT
#define HYP_EVENT(__name, __proto, __struct, __assign, __printk)	\
	atomic_t __ro_after_init __name##_enabled = ATOMIC_INIT(0);	\
	struct hyp_event_id hyp_event_id_##__name __section(".hyp.event_ids") = {	\
		.data = (void *)&__name##_enabled,			\
	}

#include <asm/kvm_hypevents.h>

int __pkvm_enable_event(unsigned short id, bool enable)
{
	struct hyp_event_id *event_id = __hyp_event_ids_start;
	atomic_t *enable_key;

	for (; (unsigned long)event_id < (unsigned long)__hyp_event_ids_end;
	     event_id++) {
		if (event_id->id != id)
			continue;

		enable_key = (atomic_t *)event_id->data;
		enable_key = hyp_fixmap_map(__hyp_pa(enable_key));

		atomic_set(enable_key, enable);

		hyp_fixmap_unmap();

		return 0;
	}

	return -EINVAL;
}
