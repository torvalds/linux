/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *	Atish Patra <atish.patra@wdc.com>
 */

#ifndef __KVM_VCPU_RISCV_TIMER_H
#define __KVM_VCPU_RISCV_TIMER_H

#include <linux/hrtimer.h>

struct kvm_guest_timer {
	/* Mult & Shift values to get nanoseconds from cycles */
	u32 nsec_mult;
	u32 nsec_shift;
	/* Time delta value */
	u64 time_delta;
};

struct kvm_vcpu_timer {
	/* Flag for whether init is done */
	bool init_done;
	/* Flag for whether timer event is configured */
	bool next_set;
	/* Next timer event cycles */
	u64 next_cycles;
	/* Underlying hrtimer instance */
	struct hrtimer hrt;
};

int kvm_riscv_vcpu_timer_next_event(struct kvm_vcpu *vcpu, u64 ncycles);
int kvm_riscv_vcpu_get_reg_timer(struct kvm_vcpu *vcpu,
				 const struct kvm_one_reg *reg);
int kvm_riscv_vcpu_set_reg_timer(struct kvm_vcpu *vcpu,
				 const struct kvm_one_reg *reg);
int kvm_riscv_vcpu_timer_init(struct kvm_vcpu *vcpu);
int kvm_riscv_vcpu_timer_deinit(struct kvm_vcpu *vcpu);
int kvm_riscv_vcpu_timer_reset(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_timer_restore(struct kvm_vcpu *vcpu);
int kvm_riscv_guest_timer_init(struct kvm *kvm);

#endif
