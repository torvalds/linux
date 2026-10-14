/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025, 2026 Arm Ltd.
 */

#ifndef __KVM_ARM_VGICV5_TABLES_H__
#define __KVM_ARM_VGICV5_TABLES_H__

#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/irqchip/arm-gic-v5.h>
#include <linux/list.h>

/* Level 1 Virtual Machine Table Entry */
typedef __le64 vmtl1_entry;

/* Level 2 Virtual Machine Table Entry */
struct vmtl2_entry {
	__le64 val[4];
};

/* Virtual PE Table Entry */
typedef __le64 vpe_entry;

struct vgic_v5_vm_info {
	void			*vmd_base;
	vpe_entry		*vpet_base;
	void			*vped_base;
	u8			vpe_id_bits;

	/*
	 * Both the LPI and SPI ISTs are allocated by the hypervisor. While it
	 * would be possible to track and access them by iterating over the ISTs
	 * themselves, it makes more sense to store pointers to the ISTs.
	 *
	 * The LPI IST can either be two-level or linear. Hence, we keep track
	 * of the structure. If it is two-level, we retain pointers to the L1
	 * IST and to each L2 IST array. If it is linear, we just store the base
	 * address of the IST array.
	 *
	 * The SPI IST is linear, and therefore we just store the base address
	 * of the SPI IST array.
	 */
	bool			h_lpi_ist_structure;
	__le64			*h_lpi_ist;
	__le64			**h_lpi_l2_ists;
	__le64			*h_spi_ist;

	/* Tracking of pending interrupts as part of IST restore */
	struct list_head	pending_irqs;
};

struct vgic_v5_vmt {
	union {
		struct {
			struct vmtl2_entry *vmt_base;
			unsigned int num_ents;
		} linear;
		struct {
			vmtl1_entry *vmt_base;
			struct vmtl2_entry **l2ptrs;
			unsigned int num_l1_ents;
		} l2;
	};
	bool		two_level;
	unsigned int	num_entries;
	unsigned int	max_vpes;
	size_t		vmd_size;
	size_t		vped_size;
	struct ida	vm_id_ida;
};

static inline u32 vgic_v5_vm_id(struct kvm *kvm)
{
	return kvm->arch.vgic.gicv5_vm.vm_id;
}

/*
 * For vGICv5, we need to consolidate two views:
 *   - The vcpu ID assigned by userspace, which it can assign however it
 *     sees fit.
 *   - The index into the VPET, which is also presented to the guest as
 *     the IAFFID for the VPE in question.
 *
 * Ideally, we don't want to intercept the guest setting an interrupt's affinity
 * - this would somewhat defeat the point of allowing the hardware to manage the
 * interrupt directly. Therefore, we need to use vcpu->vcpu_id as our VPE ID,
 * which ensures that both userspace and the GICv5 hardware have a consistent
 * view.
 *
 * This means two things:
 *   - Our VPET might be sparse, depending on the IDs picked by userspace.
 *   - We need to check the vcpu ID provided by userspace, and reject anything
 *     we cannot handle.
 *
 * In order to at least ensure consistent usage in the places that we need to
 * use the VPE ID, we provide this helper.
 */
static inline u16 vgic_v5_vpe_id(struct kvm_vcpu *vcpu)
{
	return vcpu->vcpu_id;
}

static inline int vgic_v5_vpe_db(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.vgic_cpu.vgic_v5.gicv5_vpe.db;
}

extern raw_spinlock_t vgic_v5_irs_lock;

int vgic_v5_vmt_allocate(unsigned int max_vpes);
int vgic_v5_vmt_free(void);
phys_addr_t vgic_v5_get_vmt_base(void);

int vgic_v5_allocate_vm_id(struct kvm *kvm);
void vgic_v5_release_vm_id(struct kvm *kvm);

int vgic_v5_vmte_init(struct kvm *kvm);
int vgic_v5_vmte_release(struct kvm *kvm);
u8 vgic_v5_vmte_vpe_id_bits(struct kvm_vcpu *vcpu);
int vgic_v5_vmte_alloc_vpe(struct kvm_vcpu *vcpu);
int vgic_v5_vmte_free_vpe(struct kvm_vcpu *vcpu);

int vgic_v5_spi_ist_alloc(struct kvm *kvm, unsigned int id_bits);
int vgic_v5_lpi_ist_exists(struct kvm *kvm);
int vgic_v5_lpi_ist_alloc(struct kvm *kvm, unsigned int id_bits);
int vgic_v5_lpi_ist_free(struct kvm *kvm);

int vgic_v5_save_spi_ist(struct kvm *kvm,
			 struct kvm_vgic_v5_ist *ist_attr);
int vgic_v5_save_lpi_ist(struct kvm *kvm,
			 struct kvm_vgic_v5_ist *ist_attr);
int vgic_v5_restore_spi_ist(struct kvm *kvm,
			    struct kvm_vgic_v5_ist *ist_attr);
int vgic_v5_restore_lpi_ist(struct kvm *kvm,
			    struct kvm_vgic_v5_ist *ist_attr);
int vgic_v5_restore_pending_irqs(struct kvm *kvm);
void vgic_v5_discard_pending_irqs(struct kvm *kvm);

#endif
