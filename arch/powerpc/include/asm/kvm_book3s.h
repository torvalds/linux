/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright SUSE Linux Products GmbH 2009
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#ifndef __ASM_KVM_BOOK3S_H__
#define __ASM_KVM_BOOK3S_H__

#include <linux/types.h>
#include <linux/kvm_host.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s_64_asm.h>

struct kvmppc_slb {
	u64 esid;
	u64 vsid;
	u64 orige;
	u64 origv;
	bool valid;
	bool Ks;
	bool Kp;
	bool nx;
	bool large;
	bool class;
};

struct kvmppc_sr {
	u32 raw;
	u32 vsid;
	bool Ks;
	bool Kp;
	bool nx;
};

struct kvmppc_bat {
	u64 raw;
	u32 bepi;
	u32 bepi_mask;
	bool vs;
	bool vp;
	u32 brpn;
	u8 wimg;
	u8 pp;
};

struct kvmppc_sid_map {
	u64 guest_vsid;
	u64 guest_esid;
	u64 host_vsid;
	bool valid;
};

#define SID_MAP_BITS    9
#define SID_MAP_NUM     (1 << SID_MAP_BITS)
#define SID_MAP_MASK    (SID_MAP_NUM - 1)

struct kvmppc_vcpu_book3s {
	struct kvm_vcpu vcpu;
	struct kvmppc_book3s_shadow_vcpu shadow_vcpu;
	struct kvmppc_sid_map sid_map[SID_MAP_NUM];
	struct kvmppc_slb slb[64];
	struct {
		u64 esid;
		u64 vsid;
	} slb_shadow[64];
	u8 slb_shadow_max;
	struct kvmppc_sr sr[16];
	struct kvmppc_bat ibat[8];
	struct kvmppc_bat dbat[8];
	u64 hid[6];
	int slb_nr;
	u64 sdr1;
	u64 dsisr;
	u64 hior;
	u64 msr_mask;
	u64 vsid_first;
	u64 vsid_next;
	u64 vsid_max;
	int context_id;
};

#define CONTEXT_HOST		0
#define CONTEXT_GUEST		1
#define CONTEXT_GUEST_END	2

#define VSID_REAL	0xfffffffffff00000
#define VSID_REAL_DR	0xffffffffffe00000
#define VSID_REAL_IR	0xffffffffffd00000
#define VSID_BAT	0xffffffffffc00000
#define VSID_PR		0x8000000000000000

extern void kvmppc_mmu_pte_flush(struct kvm_vcpu *vcpu, u64 ea, u64 ea_mask);
extern void kvmppc_mmu_pte_vflush(struct kvm_vcpu *vcpu, u64 vp, u64 vp_mask);
extern void kvmppc_mmu_pte_pflush(struct kvm_vcpu *vcpu, u64 pa_start, u64 pa_end);
extern void kvmppc_set_msr(struct kvm_vcpu *vcpu, u64 new_msr);
extern void kvmppc_mmu_book3s_64_init(struct kvm_vcpu *vcpu);
extern void kvmppc_mmu_book3s_32_init(struct kvm_vcpu *vcpu);
extern int kvmppc_mmu_map_page(struct kvm_vcpu *vcpu, struct kvmppc_pte *pte);
extern int kvmppc_mmu_map_segment(struct kvm_vcpu *vcpu, ulong eaddr);
extern void kvmppc_mmu_flush_segments(struct kvm_vcpu *vcpu);
extern struct kvmppc_pte *kvmppc_mmu_find_pte(struct kvm_vcpu *vcpu, u64 ea, bool data);
extern int kvmppc_ld(struct kvm_vcpu *vcpu, ulong eaddr, int size, void *ptr, bool data);
extern int kvmppc_st(struct kvm_vcpu *vcpu, ulong eaddr, int size, void *ptr);
extern void kvmppc_book3s_queue_irqprio(struct kvm_vcpu *vcpu, unsigned int vec);
extern void kvmppc_set_bat(struct kvm_vcpu *vcpu, struct kvmppc_bat *bat,
			   bool upper, u32 val);

extern u32 kvmppc_trampoline_lowmem;
extern u32 kvmppc_trampoline_enter;

static inline struct kvmppc_vcpu_book3s *to_book3s(struct kvm_vcpu *vcpu)
{
	return container_of(vcpu, struct kvmppc_vcpu_book3s, vcpu);
}

static inline ulong dsisr(void)
{
	ulong r;
	asm ( "mfdsisr %0 " : "=r" (r) );
	return r;
}

extern void kvm_return_point(void);

#define INS_DCBZ			0x7c0007ec

#endif /* __ASM_KVM_BOOK3S_H__ */
