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
#include <asm/kvm_book3s_asm.h>

struct kvmppc_slb {
	u64 esid;
	u64 vsid;
	u64 orige;
	u64 origv;
	bool valid	: 1;
	bool Ks		: 1;
	bool Kp		: 1;
	bool nx		: 1;
	bool large	: 1;	/* PTEs are 16MB */
	bool tb		: 1;	/* 1TB segment */
	bool class	: 1;
};

struct kvmppc_bat {
	u64 raw;
	u32 bepi;
	u32 bepi_mask;
	u32 brpn;
	u8 wimg;
	u8 pp;
	bool vs		: 1;
	bool vp		: 1;
};

struct kvmppc_sid_map {
	u64 guest_vsid;
	u64 guest_esid;
	u64 host_vsid;
	bool valid	: 1;
};

#define SID_MAP_BITS    9
#define SID_MAP_NUM     (1 << SID_MAP_BITS)
#define SID_MAP_MASK    (SID_MAP_NUM - 1)

#ifdef CONFIG_PPC_BOOK3S_64
#define SID_CONTEXTS	1
#else
#define SID_CONTEXTS	128
#define VSID_POOL_SIZE	(SID_CONTEXTS * 16)
#endif

struct kvmppc_vcpu_book3s {
	struct kvm_vcpu vcpu;
	struct kvmppc_book3s_shadow_vcpu *shadow_vcpu;
	struct kvmppc_sid_map sid_map[SID_MAP_NUM];
	struct kvmppc_slb slb[64];
	struct {
		u64 esid;
		u64 vsid;
	} slb_shadow[64];
	u8 slb_shadow_max;
	struct kvmppc_bat ibat[8];
	struct kvmppc_bat dbat[8];
	u64 hid[6];
	u64 gqr[8];
	int slb_nr;
	u64 sdr1;
	u64 hior;
	u64 msr_mask;
	u64 vsid_next;
#ifdef CONFIG_PPC_BOOK3S_32
	u32 vsid_pool[VSID_POOL_SIZE];
#else
	u64 vsid_first;
	u64 vsid_max;
#endif
	int context_id[SID_CONTEXTS];
	ulong prog_flags; /* flags to inject when giving a 700 trap */
};

#define CONTEXT_HOST		0
#define CONTEXT_GUEST		1
#define CONTEXT_GUEST_END	2

#define VSID_REAL	0x1fffffffffc00000ULL
#define VSID_BAT	0x1fffffffffb00000ULL
#define VSID_REAL_DR	0x2000000000000000ULL
#define VSID_REAL_IR	0x4000000000000000ULL
#define VSID_PR		0x8000000000000000ULL

extern void kvmppc_mmu_pte_flush(struct kvm_vcpu *vcpu, ulong ea, ulong ea_mask);
extern void kvmppc_mmu_pte_vflush(struct kvm_vcpu *vcpu, u64 vp, u64 vp_mask);
extern void kvmppc_mmu_pte_pflush(struct kvm_vcpu *vcpu, ulong pa_start, ulong pa_end);
extern void kvmppc_set_msr(struct kvm_vcpu *vcpu, u64 new_msr);
extern void kvmppc_mmu_book3s_64_init(struct kvm_vcpu *vcpu);
extern void kvmppc_mmu_book3s_32_init(struct kvm_vcpu *vcpu);
extern int kvmppc_mmu_map_page(struct kvm_vcpu *vcpu, struct kvmppc_pte *pte);
extern int kvmppc_mmu_map_segment(struct kvm_vcpu *vcpu, ulong eaddr);
extern void kvmppc_mmu_flush_segments(struct kvm_vcpu *vcpu);

extern void kvmppc_mmu_hpte_cache_map(struct kvm_vcpu *vcpu, struct hpte_cache *pte);
extern struct hpte_cache *kvmppc_mmu_hpte_cache_next(struct kvm_vcpu *vcpu);
extern void kvmppc_mmu_hpte_destroy(struct kvm_vcpu *vcpu);
extern int kvmppc_mmu_hpte_init(struct kvm_vcpu *vcpu);
extern void kvmppc_mmu_invalidate_pte(struct kvm_vcpu *vcpu, struct hpte_cache *pte);
extern int kvmppc_mmu_hpte_sysinit(void);
extern void kvmppc_mmu_hpte_sysexit(void);

extern int kvmppc_ld(struct kvm_vcpu *vcpu, ulong *eaddr, int size, void *ptr, bool data);
extern int kvmppc_st(struct kvm_vcpu *vcpu, ulong *eaddr, int size, void *ptr, bool data);
extern void kvmppc_book3s_queue_irqprio(struct kvm_vcpu *vcpu, unsigned int vec);
extern void kvmppc_set_bat(struct kvm_vcpu *vcpu, struct kvmppc_bat *bat,
			   bool upper, u32 val);
extern void kvmppc_giveup_ext(struct kvm_vcpu *vcpu, ulong msr);
extern int kvmppc_emulate_paired_single(struct kvm_run *run, struct kvm_vcpu *vcpu);
extern pfn_t kvmppc_gfn_to_pfn(struct kvm_vcpu *vcpu, gfn_t gfn);

extern ulong kvmppc_trampoline_lowmem;
extern ulong kvmppc_trampoline_enter;
extern void kvmppc_rmcall(ulong srr0, ulong srr1);
extern void kvmppc_load_up_fpu(void);
extern void kvmppc_load_up_altivec(void);
extern void kvmppc_load_up_vsx(void);
extern u32 kvmppc_alignment_dsisr(struct kvm_vcpu *vcpu, unsigned int inst);
extern ulong kvmppc_alignment_dar(struct kvm_vcpu *vcpu, unsigned int inst);

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
static inline struct kvmppc_book3s_shadow_vcpu *to_svcpu(struct kvm_vcpu *vcpu);

static inline void kvmppc_set_gpr(struct kvm_vcpu *vcpu, int num, ulong val)
{
	if ( num < 14 ) {
		to_svcpu(vcpu)->gpr[num] = val;
		to_book3s(vcpu)->shadow_vcpu->gpr[num] = val;
	} else
		vcpu->arch.gpr[num] = val;
}

static inline ulong kvmppc_get_gpr(struct kvm_vcpu *vcpu, int num)
{
	if ( num < 14 )
		return to_svcpu(vcpu)->gpr[num];
	else
		return vcpu->arch.gpr[num];
}

static inline void kvmppc_set_cr(struct kvm_vcpu *vcpu, u32 val)
{
	to_svcpu(vcpu)->cr = val;
	to_book3s(vcpu)->shadow_vcpu->cr = val;
}

static inline u32 kvmppc_get_cr(struct kvm_vcpu *vcpu)
{
	return to_svcpu(vcpu)->cr;
}

static inline void kvmppc_set_xer(struct kvm_vcpu *vcpu, u32 val)
{
	to_svcpu(vcpu)->xer = val;
	to_book3s(vcpu)->shadow_vcpu->xer = val;
}

static inline u32 kvmppc_get_xer(struct kvm_vcpu *vcpu)
{
	return to_svcpu(vcpu)->xer;
}

static inline void kvmppc_set_ctr(struct kvm_vcpu *vcpu, ulong val)
{
	to_svcpu(vcpu)->ctr = val;
}

static inline ulong kvmppc_get_ctr(struct kvm_vcpu *vcpu)
{
	return to_svcpu(vcpu)->ctr;
}

static inline void kvmppc_set_lr(struct kvm_vcpu *vcpu, ulong val)
{
	to_svcpu(vcpu)->lr = val;
}

static inline ulong kvmppc_get_lr(struct kvm_vcpu *vcpu)
{
	return to_svcpu(vcpu)->lr;
}

static inline void kvmppc_set_pc(struct kvm_vcpu *vcpu, ulong val)
{
	to_svcpu(vcpu)->pc = val;
}

static inline ulong kvmppc_get_pc(struct kvm_vcpu *vcpu)
{
	return to_svcpu(vcpu)->pc;
}

static inline u32 kvmppc_get_last_inst(struct kvm_vcpu *vcpu)
{
	ulong pc = kvmppc_get_pc(vcpu);
	struct kvmppc_book3s_shadow_vcpu *svcpu = to_svcpu(vcpu);

	/* Load the instruction manually if it failed to do so in the
	 * exit path */
	if (svcpu->last_inst == KVM_INST_FETCH_FAILED)
		kvmppc_ld(vcpu, &pc, sizeof(u32), &svcpu->last_inst, false);

	return svcpu->last_inst;
}

static inline ulong kvmppc_get_fault_dar(struct kvm_vcpu *vcpu)
{
	return to_svcpu(vcpu)->fault_dar;
}

/* Magic register values loaded into r3 and r4 before the 'sc' assembly
 * instruction for the OSI hypercalls */
#define OSI_SC_MAGIC_R3			0x113724FA
#define OSI_SC_MAGIC_R4			0x77810F9B

#define INS_DCBZ			0x7c0007ec

/* Also add subarch specific defines */

#ifdef CONFIG_PPC_BOOK3S_32
#include <asm/kvm_book3s_32.h>
#else
#include <asm/kvm_book3s_64.h>
#endif

#endif /* __ASM_KVM_BOOK3S_H__ */
