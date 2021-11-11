// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on the x86 implementation.
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/perf_event.h>
#include <linux/kvm_host.h>

#include <asm/kvm_emulate.h>

DEFINE_STATIC_KEY_FALSE(kvm_arm_pmu_available);

void kvm_perf_init(void)
{
	kvm_register_perf_callbacks(NULL);
}

void kvm_perf_teardown(void)
{
	kvm_unregister_perf_callbacks();
}
