/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ARM64_KVM_HYPEVENTS_H_
#define __ARM64_KVM_HYPEVENTS_H_

#ifdef __KVM_NVHE_HYPERVISOR__
#include <nvhe/trace.h>
#endif

/*
 * Hypervisor events definitions.
 */

HYP_EVENT(hyp_enter,
	HE_PROTO(void),
	HE_STRUCT(
	),
	HE_ASSIGN(
	),
	HE_PRINTK(" ")
);

HYP_EVENT(hyp_exit,
	HE_PROTO(void),
	HE_STRUCT(
	),
	HE_ASSIGN(
	),
	HE_PRINTK(" ")
);
#endif
