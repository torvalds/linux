/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ARM64_KVM_HYP_H__
#define __ARM64_KVM_HYP_H__

#include <linux/compiler.h>
#include <linux/kvm_host.h>
#include <asm/kvm_mmu.h>
#include <asm/sysreg.h>

#define __hyp_text __section(.hyp.text) notrace

#define kern_hyp_va(v) (typeof(v))((unsigned long)(v) & HYP_PAGE_OFFSET_MASK)
#define hyp_kern_va(v) (typeof(v))((unsigned long)(v) - HYP_PAGE_OFFSET \
						      + PAGE_OFFSET)

void __vgic_v2_save_state(struct kvm_vcpu *vcpu);
void __vgic_v2_restore_state(struct kvm_vcpu *vcpu);

void __vgic_v3_save_state(struct kvm_vcpu *vcpu);
void __vgic_v3_restore_state(struct kvm_vcpu *vcpu);

void __timer_save_state(struct kvm_vcpu *vcpu);
void __timer_restore_state(struct kvm_vcpu *vcpu);

void __sysreg_save_state(struct kvm_cpu_context *ctxt);
void __sysreg_restore_state(struct kvm_cpu_context *ctxt);
void __sysreg32_save_state(struct kvm_vcpu *vcpu);
void __sysreg32_restore_state(struct kvm_vcpu *vcpu);

void __debug_save_state(struct kvm_vcpu *vcpu,
			struct kvm_guest_debug_arch *dbg,
			struct kvm_cpu_context *ctxt);
void __debug_restore_state(struct kvm_vcpu *vcpu,
			   struct kvm_guest_debug_arch *dbg,
			   struct kvm_cpu_context *ctxt);
void __debug_cond_save_host_state(struct kvm_vcpu *vcpu);
void __debug_cond_restore_host_state(struct kvm_vcpu *vcpu);

u64 __guest_enter(struct kvm_vcpu *vcpu, struct kvm_cpu_context *host_ctxt);

#endif /* __ARM64_KVM_HYP_H__ */

