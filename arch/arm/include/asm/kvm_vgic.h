/*
 * Copyright (C) 2012 ARM Ltd.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __ASM_ARM_KVM_VGIC_H
#define __ASM_ARM_KVM_VGIC_H

#include <linux/irqchip/arm-gic.h>

struct vgic_dist {
	/* Distributor and vcpu interface mapping in the guest */
	phys_addr_t		vgic_dist_base;
	phys_addr_t		vgic_cpu_base;
};

struct vgic_cpu {
};

struct kvm;
struct kvm_vcpu;
struct kvm_run;
struct kvm_exit_mmio;

#ifdef CONFIG_KVM_ARM_VGIC
int kvm_vgic_set_addr(struct kvm *kvm, unsigned long type, u64 addr);
bool vgic_handle_mmio(struct kvm_vcpu *vcpu, struct kvm_run *run,
		      struct kvm_exit_mmio *mmio);

#else
static inline int kvm_vgic_hyp_init(void)
{
	return 0;
}

static inline int kvm_vgic_set_addr(struct kvm *kvm, unsigned long type, u64 addr)
{
	return 0;
}

static inline int kvm_vgic_init(struct kvm *kvm)
{
	return 0;
}

static inline int kvm_vgic_create(struct kvm *kvm)
{
	return 0;
}

static inline int kvm_vgic_vcpu_init(struct kvm_vcpu *vcpu)
{
	return 0;
}

static inline void kvm_vgic_flush_hwstate(struct kvm_vcpu *vcpu) {}
static inline void kvm_vgic_sync_hwstate(struct kvm_vcpu *vcpu) {}

static inline int kvm_vgic_vcpu_pending_irq(struct kvm_vcpu *vcpu)
{
	return 0;
}

static inline bool vgic_handle_mmio(struct kvm_vcpu *vcpu, struct kvm_run *run,
				    struct kvm_exit_mmio *mmio)
{
	return false;
}

static inline int irqchip_in_kernel(struct kvm *kvm)
{
	return 0;
}
#endif

#endif
