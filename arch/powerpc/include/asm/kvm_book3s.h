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

struct hpte_cache {
	struct hlist_node list_pte;
	struct hlist_node list_pte_long;
	struct hlist_node list_vpte;
	struct hlist_node list_vpte_long;
#ifdef CONFIG_PPC_BOOK3S_64
	struct hlist_node list_vpte_64k;
#endif
	struct rcu_head rcu_head;
	u64 host_vpn;
	u64 pfn;
	ulong slot;
	struct kvmppc_pte pte;
	int pagesize;
};

struct kvmppc_vcpu_book3s {
	struct kvmppc_sid_map sid_map[SID_MAP_NUM];
	struct {
		u64 esid;
		u64 vsid;
	} slb_shadow[64];
	u8 slb_shadow_max;
	struct kvmppc_bat ibat[8];
	struct kvmppc_bat dbat[8];
	u64 hid[6];
	u64 gqr[8];
	u64 sdr1;
	u64 hior;
	u64 msr_mask;
	u64 purr_offset;
	u64 spurr_offset;
#ifdef CONFIG_PPC_BOOK3S_32
	u32 vsid_pool[VSID_POOL_SIZE];
	u32 vsid_next;
#else
	u64 proto_vsid_first;
	u64 proto_vsid_max;
	u64 proto_vsid_next;
#endif
	int context_id[SID_CONTEXTS];

	bool hior_explicit;		/* HIOR is set by ioctl, not PVR */

	struct hlist_head hpte_hash_pte[HPTEG_HASH_NUM_PTE];
	struct hlist_head hpte_hash_pte_long[HPTEG_HASH_NUM_PTE_LONG];
	struct hlist_head hpte_hash_vpte[HPTEG_HASH_NUM_VPTE];
	struct hlist_head hpte_hash_vpte_long[HPTEG_HASH_NUM_VPTE_LONG];
#ifdef CONFIG_PPC_BOOK3S_64
	struct hlist_head hpte_hash_vpte_64k[HPTEG_HASH_NUM_VPTE_64K];
#endif
	int hpte_cache_count;
	spinlock_t mmu_lock;
};

#define CONTEXT_HOST		0
#define CONTEXT_GUEST		1
#define CONTEXT_GUEST_END	2

#define VSID_REAL	0x07ffffffffc00000ULL
#define VSID_BAT	0x07ffffffffb00000ULL
#define VSID_64K	0x0800000000000000ULL
#define VSID_1T		0x1000000000000000ULL
#define VSID_REAL_DR	0x2000000000000000ULL
#define VSID_REAL_IR	0x4000000000000000ULL
#define VSID_PR		0x8000000000000000ULL

extern void kvmppc_mmu_pte_flush(struct kvm_vcpu *vcpu, ulong ea, ulong ea_mask);
extern void kvmppc_mmu_pte_vflush(struct kvm_vcpu *vcpu, u64 vp, u64 vp_mask);
extern void kvmppc_mmu_pte_pflush(struct kvm_vcpu *vcpu, ulong pa_start, ulong pa_end);
extern void kvmppc_set_msr(struct kvm_vcpu *vcpu, u64 new_msr);
extern void kvmppc_mmu_book3s_64_init(struct kvm_vcpu *vcpu);
extern void kvmppc_mmu_book3s_32_init(struct kvm_vcpu *vcpu);
extern void kvmppc_mmu_book3s_hv_init(struct kvm_vcpu *vcpu);
extern int kvmppc_mmu_map_page(struct kvm_vcpu *vcpu, struct kvmppc_pte *pte,
			       bool iswrite);
extern void kvmppc_mmu_unmap_page(struct kvm_vcpu *vcpu, struct kvmppc_pte *pte);
extern int kvmppc_mmu_map_segment(struct kvm_vcpu *vcpu, ulong eaddr);
extern void kvmppc_mmu_flush_segment(struct kvm_vcpu *vcpu, ulong eaddr, ulong seg_size);
extern void kvmppc_mmu_flush_segments(struct kvm_vcpu *vcpu);
extern int kvmppc_book3s_hv_page_fault(struct kvm_run *run,
			struct kvm_vcpu *vcpu, unsigned long addr,
			unsigned long status);
extern long kvmppc_hv_find_lock_hpte(struct kvm *kvm, gva_t eaddr,
			unsigned long slb_v, unsigned long valid);

extern void kvmppc_mmu_hpte_cache_map(struct kvm_vcpu *vcpu, struct hpte_cache *pte);
extern struct hpte_cache *kvmppc_mmu_hpte_cache_next(struct kvm_vcpu *vcpu);
extern void kvmppc_mmu_hpte_cache_free(struct hpte_cache *pte);
extern void kvmppc_mmu_hpte_destroy(struct kvm_vcpu *vcpu);
extern int kvmppc_mmu_hpte_init(struct kvm_vcpu *vcpu);
extern void kvmppc_mmu_invalidate_pte(struct kvm_vcpu *vcpu, struct hpte_cache *pte);
extern int kvmppc_mmu_hpte_sysinit(void);
extern void kvmppc_mmu_hpte_sysexit(void);
extern int kvmppc_mmu_hv_init(void);

extern int kvmppc_ld(struct kvm_vcpu *vcpu, ulong *eaddr, int size, void *ptr, bool data);
extern int kvmppc_st(struct kvm_vcpu *vcpu, ulong *eaddr, int size, void *ptr, bool data);
extern void kvmppc_book3s_queue_irqprio(struct kvm_vcpu *vcpu, unsigned int vec);
extern void kvmppc_book3s_dequeue_irqprio(struct kvm_vcpu *vcpu,
					  unsigned int vec);
extern void kvmppc_inject_interrupt(struct kvm_vcpu *vcpu, int vec, u64 flags);
extern void kvmppc_set_bat(struct kvm_vcpu *vcpu, struct kvmppc_bat *bat,
			   bool upper, u32 val);
extern void kvmppc_giveup_ext(struct kvm_vcpu *vcpu, ulong msr);
extern int kvmppc_emulate_paired_single(struct kvm_run *run, struct kvm_vcpu *vcpu);
extern pfn_t kvmppc_gfn_to_pfn(struct kvm_vcpu *vcpu, gfn_t gfn, bool writing,
			bool *writable);
extern void kvmppc_add_revmap_chain(struct kvm *kvm, struct revmap_entry *rev,
			unsigned long *rmap, long pte_index, int realmode);
extern void kvmppc_invalidate_hpte(struct kvm *kvm, unsigned long *hptep,
			unsigned long pte_index);
void kvmppc_clear_ref_hpte(struct kvm *kvm, unsigned long *hptep,
			unsigned long pte_index);
extern void *kvmppc_pin_guest_page(struct kvm *kvm, unsigned long addr,
			unsigned long *nb_ret);
extern void kvmppc_unpin_guest_page(struct kvm *kvm, void *addr,
			unsigned long gpa, bool dirty);
extern long kvmppc_virtmode_h_enter(struct kvm_vcpu *vcpu, unsigned long flags,
			long pte_index, unsigned long pteh, unsigned long ptel);
extern long kvmppc_do_h_enter(struct kvm *kvm, unsigned long flags,
			long pte_index, unsigned long pteh, unsigned long ptel,
			pgd_t *pgdir, bool realmode, unsigned long *idx_ret);
extern long kvmppc_do_h_remove(struct kvm *kvm, unsigned long flags,
			unsigned long pte_index, unsigned long avpn,
			unsigned long *hpret);
extern long kvmppc_hv_get_dirty_log(struct kvm *kvm,
			struct kvm_memory_slot *memslot, unsigned long *map);
extern void kvmppc_update_lpcr(struct kvm *kvm, unsigned long lpcr,
			unsigned long mask);

extern void kvmppc_entry_trampoline(void);
extern void kvmppc_hv_entry_trampoline(void);
extern u32 kvmppc_alignment_dsisr(struct kvm_vcpu *vcpu, unsigned int inst);
extern ulong kvmppc_alignment_dar(struct kvm_vcpu *vcpu, unsigned int inst);
extern int kvmppc_h_pr(struct kvm_vcpu *vcpu, unsigned long cmd);
extern void kvmppc_copy_to_svcpu(struct kvmppc_book3s_shadow_vcpu *svcpu,
				 struct kvm_vcpu *vcpu);
extern void kvmppc_copy_from_svcpu(struct kvm_vcpu *vcpu,
				   struct kvmppc_book3s_shadow_vcpu *svcpu);

static inline struct kvmppc_vcpu_book3s *to_book3s(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.book3s;
}

/* Also add subarch specific defines */

#ifdef CONFIG_KVM_BOOK3S_32_HANDLER
#include <asm/kvm_book3s_32.h>
#endif
#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
#include <asm/kvm_book3s_64.h>
#endif

static inline void kvmppc_set_gpr(struct kvm_vcpu *vcpu, int num, ulong val)
{
	vcpu->arch.gpr[num] = val;
}

static inline ulong kvmppc_get_gpr(struct kvm_vcpu *vcpu, int num)
{
	return vcpu->arch.gpr[num];
}

static inline void kvmppc_set_cr(struct kvm_vcpu *vcpu, u32 val)
{
	vcpu->arch.cr = val;
}

static inline u32 kvmppc_get_cr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.cr;
}

static inline void kvmppc_set_xer(struct kvm_vcpu *vcpu, u32 val)
{
	vcpu->arch.xer = val;
}

static inline u32 kvmppc_get_xer(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.xer;
}

static inline void kvmppc_set_ctr(struct kvm_vcpu *vcpu, ulong val)
{
	vcpu->arch.ctr = val;
}

static inline ulong kvmppc_get_ctr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.ctr;
}

static inline void kvmppc_set_lr(struct kvm_vcpu *vcpu, ulong val)
{
	vcpu->arch.lr = val;
}

static inline ulong kvmppc_get_lr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.lr;
}

static inline void kvmppc_set_pc(struct kvm_vcpu *vcpu, ulong val)
{
	vcpu->arch.pc = val;
}

static inline ulong kvmppc_get_pc(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.pc;
}

static inline bool kvmppc_need_byteswap(struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.shared->msr & MSR_LE) != (MSR_KERNEL & MSR_LE);
}

static inline u32 kvmppc_get_last_inst_internal(struct kvm_vcpu *vcpu, ulong pc)
{
	/* Load the instruction manually if it failed to do so in the
	 * exit path */
	if (vcpu->arch.last_inst == KVM_INST_FETCH_FAILED)
		kvmppc_ld(vcpu, &pc, sizeof(u32), &vcpu->arch.last_inst, false);

	return kvmppc_need_byteswap(vcpu) ? swab32(vcpu->arch.last_inst) :
		vcpu->arch.last_inst;
}

static inline u32 kvmppc_get_last_inst(struct kvm_vcpu *vcpu)
{
	return kvmppc_get_last_inst_internal(vcpu, kvmppc_get_pc(vcpu));
}

/*
 * Like kvmppc_get_last_inst(), but for fetching a sc instruction.
 * Because the sc instruction sets SRR0 to point to the following
 * instruction, we have to fetch from pc - 4.
 */
static inline u32 kvmppc_get_last_sc(struct kvm_vcpu *vcpu)
{
	return kvmppc_get_last_inst_internal(vcpu, kvmppc_get_pc(vcpu) - 4);
}

static inline ulong kvmppc_get_fault_dar(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault_dar;
}

static inline bool is_kvmppc_resume_guest(int r)
{
	return (r == RESUME_GUEST || r == RESUME_GUEST_NV);
}

/* Magic register values loaded into r3 and r4 before the 'sc' assembly
 * instruction for the OSI hypercalls */
#define OSI_SC_MAGIC_R3			0x113724FA
#define OSI_SC_MAGIC_R4			0x77810F9B

#define INS_DCBZ			0x7c0007ec
/* TO = 31 for unconditional trap */
#define INS_TW				0x7fe00008

/* LPIDs we support with this build -- runtime limit may be lower */
#define KVMPPC_NR_LPIDS			(LPID_RSVD + 1)

#endif /* __ASM_KVM_BOOK3S_H__ */
