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

static unsigned int kvm_guest_state(void)
{
	struct kvm_vcpu *vcpu = kvm_get_running_vcpu();
	unsigned int state;

	if (!vcpu)
		return 0;

	state = PERF_GUEST_ACTIVE;
	if (!vcpu_mode_priv(vcpu))
		state |= PERF_GUEST_USER;

	return state;
}

static unsigned long kvm_get_guest_ip(void)
{
	struct kvm_vcpu *vcpu = kvm_get_running_vcpu();

	if (WARN_ON_ONCE(!vcpu))
		return 0;

	return *vcpu_pc(vcpu);
}

static struct perf_guest_info_callbacks kvm_guest_cbs = {
	.state		= kvm_guest_state,
	.get_ip		= kvm_get_guest_ip,
};

void kvm_perf_init(void)
{
	perf_register_guest_info_callbacks(&kvm_guest_cbs);
}

void kvm_perf_teardown(void)
{
	perf_unregister_guest_info_callbacks(&kvm_guest_cbs);
}
