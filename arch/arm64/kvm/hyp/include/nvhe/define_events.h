/* SPDX-License-Identifier: GPL-2.0 */

#undef HYP_EVENT
#define HYP_EVENT(__name, __proto, __struct, __assign, __printk)	\
	struct hyp_event_id hyp_event_id_##__name			\
	__section(".hyp.event_ids."#__name) = {				\
		.enabled = ATOMIC_INIT(0),				\
	}

#define HYP_EVENT_MULTI_READ
#include <asm/kvm_hypevents.h>
#undef HYP_EVENT_MULTI_READ

#undef HYP_EVENT
