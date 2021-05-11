/* SPDX-License-Identifier: GPL-2.0-only */
/*
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

/*
 * Struct for a virtual core.
 * Note: entry_exit_map combines a bitmap of threads that have entered
 * in the bottom 8 bits and a bitmap of threads that have exited in the
 * next 8 bits.  This is so that we can atomically set the entry bit
 * iff the exit map is 0 without taking a lock.
 */
struct kvmppc_vcore {
	int n_runnable;
	int num_threads;
	int entry_exit_map;
	int napping_threads;
	int first_vcpuid;
	u16 pcpu;
	u16 last_cpu;
	u8 vcore_state;
	u8 in_guest;
	struct kvm_vcpu *runnable_threads[MAX_SMT_THREADS];
	struct list_head preempt_list;
	spinlock_t lock;
	struct rcuwait wait;
	spinlock_t stoltb_lock;	/* protects stolen_tb and preempt_tb */
	u64 stolen_tb;
	u64 preempt_tb;
	struct kvm_vcpu *runner;
	struct kvm *kvm;
	u64 tb_offset;		/* guest timebase - host timebase */
	u64 tb_offset_applied;	/* timebase offset currently in force */
	ulong lpcr;
	u32 arch_compat;
	ulong pcr;
	ulong dpdes;		/* doorbell state (POWER8) */
	ulong vtb;		/* virtual timebase */
	ulong conferring_threads;
	unsigned int halt_poll_ns;
	atomic_t online_count;
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
	u64 vtb;
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
extern int kvmppc_book3s_hv_page_fault(struct kvm_vcpu *vcpu,
			unsigned long addr, unsigned long status);
extern long kvmppc_hv_find_lock_hpte(struct kvm *kvm, gva_t eaddr,
			unsigned long slb_v, unsigned long valid);
extern int kvmppc_hv_emulate_mmio(struct kvm_vcpu *vcpu,
			unsigned long gpa, gva_t ea, int is_store);

extern void kvmppc_mmu_hpte_cache_map(struct kvm_vcpu *vcpu, struct hpte_cache *pte);
extern struct hpte_cache *kvmppc_mmu_hpte_cache_next(struct kvm_vcpu *vcpu);
extern void kvmppc_mmu_hpte_cache_free(struct hpte_cache *pte);
extern void kvmppc_mmu_hpte_destroy(struct kvm_vcpu *vcpu);
extern int kvmppc_mmu_hpte_init(struct kvm_vcpu *vcpu);
extern void kvmppc_mmu_invalidate_pte(struct kvm_vcpu *vcpu, struct hpte_cache *pte);
extern int kvmppc_mmu_hpte_sysinit(void);
extern void kvmppc_mmu_hpte_sysexit(void);
extern int kvmppc_mmu_hv_init(void);
extern int kvmppc_book3s_hcall_implemented(struct kvm *kvm, unsigned long hc);

extern int kvmppc_book3s_radix_page_fault(struct kvm_vcpu *vcpu,
			unsigned long ea, unsigned long dsisr);
extern unsigned long __kvmhv_copy_tofrom_guest_radix(int lpid, int pid,
					gva_t eaddr, void *to, void *from,
					unsigned long n);
extern long kvmhv_copy_from_guest_radix(struct kvm_vcpu *vcpu, gva_t eaddr,
					void *to, unsigned long n);
extern long kvmhv_copy_to_guest_radix(struct kvm_vcpu *vcpu, gva_t eaddr,
				      void *from, unsigned long n);
extern int kvmppc_mmu_walk_radix_tree(struct kvm_vcpu *vcpu, gva_t eaddr,
				      struct kvmppc_pte *gpte, u64 root,
				      u64 *pte_ret_p);
extern int kvmppc_mmu_radix_translate_table(struct kvm_vcpu *vcpu, gva_t eaddr,
			struct kvmppc_pte *gpte, u64 table,
			int table_index, u64 *pte_ret_p);
extern int kvmppc_mmu_radix_xlate(struct kvm_vcpu *vcpu, gva_t eaddr,
			struct kvmppc_pte *gpte, bool data, bool iswrite);
extern void kvmppc_radix_tlbie_page(struct kvm *kvm, unsigned long addr,
				    unsigned int pshift, unsigned int lpid);
extern void kvmppc_unmap_pte(struct kvm *kvm, pte_t *pte, unsigned long gpa,
			unsigned int shift,
			const struct kvm_memory_slot *memslot,
			unsigned int lpid);
extern bool kvmppc_hv_handle_set_rc(struct kvm *kvm, bool nested,
				    bool writing, unsigned long gpa,
				    unsigned int lpid);
extern int kvmppc_book3s_instantiate_page(struct kvm_vcpu *vcpu,
				unsigned long gpa,
				struct kvm_memory_slot *memslot,
				bool writing, bool kvm_ro,
				pte_t *inserted_pte, unsigned int *levelp);
extern int kvmppc_init_vm_radix(struct kvm *kvm);
extern void kvmppc_free_radix(struct kvm *kvm);
extern void kvmppc_free_pgtable_radix(struct kvm *kvm, pgd_t *pgd,
				      unsigned int lpid);
extern int kvmppc_radix_init(void);
extern void kvmppc_radix_exit(void);
extern void kvm_unmap_radix(struct kvm *kvm, struct kvm_memory_slot *memslot,
			    unsigned long gfn);
extern bool kvm_age_radix(struct kvm *kvm, struct kvm_memory_slot *memslot,
			  unsigned long gfn);
extern bool kvm_test_age_radix(struct kvm *kvm, struct kvm_memory_slot *memslot,
			       unsigned long gfn);
extern long kvmppc_hv_get_dirty_log_radix(struct kvm *kvm,
			struct kvm_memory_slot *memslot, unsigned long *map);
extern void kvmppc_radix_flush_memslot(struct kvm *kvm,
			const struct kvm_memory_slot *memslot);
extern int kvmhv_get_rmmu_info(struct kvm *kvm, struct kvm_ppc_rmmu_info *info);

/* XXX remove this export when load_last_inst() is generic */
extern int kvmppc_ld(struct kvm_vcpu *vcpu, ulong *eaddr, int size, void *ptr, bool data);
extern void kvmppc_book3s_queue_irqprio(struct kvm_vcpu *vcpu, unsigned int vec);
extern void kvmppc_book3s_dequeue_irqprio(struct kvm_vcpu *vcpu,
					  unsigned int vec);
extern void kvmppc_inject_interrupt(struct kvm_vcpu *vcpu, int vec, u64 flags);
extern void kvmppc_trigger_fac_interrupt(struct kvm_vcpu *vcpu, ulong fac);
extern void kvmppc_set_bat(struct kvm_vcpu *vcpu, struct kvmppc_bat *bat,
			   bool upper, u32 val);
extern void kvmppc_giveup_ext(struct kvm_vcpu *vcpu, ulong msr);
extern int kvmppc_emulate_paired_single(struct kvm_vcpu *vcpu);
extern kvm_pfn_t kvmppc_gpa_to_pfn(struct kvm_vcpu *vcpu, gpa_t gpa,
			bool writing, bool *writable);
extern void kvmppc_add_revmap_chain(struct kvm *kvm, struct revmap_entry *rev,
			unsigned long *rmap, long pte_index, int realmode);
extern void kvmppc_update_dirty_map(const struct kvm_memory_slot *memslot,
			unsigned long gfn, unsigned long psize);
extern void kvmppc_invalidate_hpte(struct kvm *kvm, __be64 *hptep,
			unsigned long pte_index);
void kvmppc_clear_ref_hpte(struct kvm *kvm, __be64 *hptep,
			unsigned long pte_index);
extern void *kvmppc_pin_guest_page(struct kvm *kvm, unsigned long addr,
			unsigned long *nb_ret);
extern void kvmppc_unpin_guest_page(struct kvm *kvm, void *addr,
			unsigned long gpa, bool dirty);
extern long kvmppc_do_h_enter(struct kvm *kvm, unsigned long flags,
			long pte_index, unsigned long pteh, unsigned long ptel,
			pgd_t *pgdir, bool realmode, unsigned long *idx_ret);
extern long kvmppc_do_h_remove(struct kvm *kvm, unsigned long flags,
			unsigned long pte_index, unsigned long avpn,
			unsigned long *hpret);
extern long kvmppc_hv_get_dirty_log_hpt(struct kvm *kvm,
			struct kvm_memory_slot *memslot, unsigned long *map);
extern void kvmppc_harvest_vpa_dirty(struct kvmppc_vpa *vpa,
			struct kvm_memory_slot *memslot,
			unsigned long *map);
extern unsigned long kvmppc_filter_lpcr_hv(struct kvm *kvm,
			unsigned long lpcr);
extern void kvmppc_update_lpcr(struct kvm *kvm, unsigned long lpcr,
			unsigned long mask);
extern void kvmppc_set_fscr(struct kvm_vcpu *vcpu, u64 fscr);

extern int kvmhv_p9_tm_emulation_early(struct kvm_vcpu *vcpu);
extern int kvmhv_p9_tm_emulation(struct kvm_vcpu *vcpu);
extern void kvmhv_emulate_tm_rollback(struct kvm_vcpu *vcpu);

extern void kvmppc_entry_trampoline(void);
extern void kvmppc_hv_entry_trampoline(void);
extern u32 kvmppc_alignment_dsisr(struct kvm_vcpu *vcpu, unsigned int inst);
extern ulong kvmppc_alignment_dar(struct kvm_vcpu *vcpu, unsigned int inst);
extern int kvmppc_h_pr(struct kvm_vcpu *vcpu, unsigned long cmd);
extern void kvmppc_pr_init_default_hcalls(struct kvm *kvm);
extern int kvmppc_hcall_impl_pr(unsigned long cmd);
extern int kvmppc_hcall_impl_hv_realmode(unsigned long cmd);
extern void kvmppc_copy_to_svcpu(struct kvm_vcpu *vcpu);
extern void kvmppc_copy_from_svcpu(struct kvm_vcpu *vcpu);

long kvmppc_read_intr(void);
void kvmppc_bad_interrupt(struct pt_regs *regs);
void kvmhv_p9_set_lpcr(struct kvm_split_mode *sip);
void kvmhv_p9_restore_lpcr(struct kvm_split_mode *sip);
void kvmppc_set_msr_hv(struct kvm_vcpu *vcpu, u64 msr);
void kvmppc_inject_interrupt_hv(struct kvm_vcpu *vcpu, int vec, u64 srr1_flags);

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
void kvmppc_save_tm_pr(struct kvm_vcpu *vcpu);
void kvmppc_restore_tm_pr(struct kvm_vcpu *vcpu);
void kvmppc_save_tm_sprs(struct kvm_vcpu *vcpu);
void kvmppc_restore_tm_sprs(struct kvm_vcpu *vcpu);
#else
static inline void kvmppc_save_tm_pr(struct kvm_vcpu *vcpu) {}
static inline void kvmppc_restore_tm_pr(struct kvm_vcpu *vcpu) {}
static inline void kvmppc_save_tm_sprs(struct kvm_vcpu *vcpu) {}
static inline void kvmppc_restore_tm_sprs(struct kvm_vcpu *vcpu) {}
#endif

long kvmhv_nested_init(void);
void kvmhv_nested_exit(void);
void kvmhv_vm_nested_init(struct kvm *kvm);
long kvmhv_set_partition_table(struct kvm_vcpu *vcpu);
long kvmhv_copy_tofrom_guest_nested(struct kvm_vcpu *vcpu);
void kvmhv_set_ptbl_entry(unsigned int lpid, u64 dw0, u64 dw1);
void kvmhv_release_all_nested(struct kvm *kvm);
long kvmhv_enter_nested_guest(struct kvm_vcpu *vcpu);
long kvmhv_do_nested_tlbie(struct kvm_vcpu *vcpu);
int kvmhv_run_single_vcpu(struct kvm_vcpu *vcpu,
			  u64 time_limit, unsigned long lpcr);
void kvmhv_save_hv_regs(struct kvm_vcpu *vcpu, struct hv_guest_state *hr);
void kvmhv_restore_hv_return_state(struct kvm_vcpu *vcpu,
				   struct hv_guest_state *hr);
long int kvmhv_nested_page_fault(struct kvm_vcpu *vcpu);

void kvmppc_giveup_fac(struct kvm_vcpu *vcpu, ulong fac);

extern int kvm_irq_bypass;

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
	vcpu->arch.regs.gpr[num] = val;
}

static inline ulong kvmppc_get_gpr(struct kvm_vcpu *vcpu, int num)
{
	return vcpu->arch.regs.gpr[num];
}

static inline void kvmppc_set_cr(struct kvm_vcpu *vcpu, u32 val)
{
	vcpu->arch.regs.ccr = val;
}

static inline u32 kvmppc_get_cr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.regs.ccr;
}

static inline void kvmppc_set_xer(struct kvm_vcpu *vcpu, ulong val)
{
	vcpu->arch.regs.xer = val;
}

static inline ulong kvmppc_get_xer(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.regs.xer;
}

static inline void kvmppc_set_ctr(struct kvm_vcpu *vcpu, ulong val)
{
	vcpu->arch.regs.ctr = val;
}

static inline ulong kvmppc_get_ctr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.regs.ctr;
}

static inline void kvmppc_set_lr(struct kvm_vcpu *vcpu, ulong val)
{
	vcpu->arch.regs.link = val;
}

static inline ulong kvmppc_get_lr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.regs.link;
}

static inline void kvmppc_set_pc(struct kvm_vcpu *vcpu, ulong val)
{
	vcpu->arch.regs.nip = val;
}

static inline ulong kvmppc_get_pc(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.regs.nip;
}

static inline u64 kvmppc_get_msr(struct kvm_vcpu *vcpu);
static inline bool kvmppc_need_byteswap(struct kvm_vcpu *vcpu)
{
	return (kvmppc_get_msr(vcpu) & MSR_LE) != (MSR_KERNEL & MSR_LE);
}

static inline ulong kvmppc_get_fault_dar(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault_dar;
}

static inline bool is_kvmppc_resume_guest(int r)
{
	return (r == RESUME_GUEST || r == RESUME_GUEST_NV);
}

static inline bool is_kvmppc_hv_enabled(struct kvm *kvm);
static inline bool kvmppc_supports_magic_page(struct kvm_vcpu *vcpu)
{
	/* Only PR KVM supports the magic page */
	return !is_kvmppc_hv_enabled(vcpu->kvm);
}

extern int kvmppc_h_logical_ci_load(struct kvm_vcpu *vcpu);
extern int kvmppc_h_logical_ci_store(struct kvm_vcpu *vcpu);

/* Magic register values loaded into r3 and r4 before the 'sc' assembly
 * instruction for the OSI hypercalls */
#define OSI_SC_MAGIC_R3			0x113724FA
#define OSI_SC_MAGIC_R4			0x77810F9B

#define INS_DCBZ			0x7c0007ec
/* TO = 31 for unconditional trap */
#define INS_TW				0x7fe00008

#define SPLIT_HACK_MASK			0xff000000
#define SPLIT_HACK_OFFS			0xfb000000

/*
 * This packs a VCPU ID from the [0..KVM_MAX_VCPU_ID) space down to the
 * [0..KVM_MAX_VCPUS) space, using knowledge of the guest's core stride
 * (but not its actual threading mode, which is not available) to avoid
 * collisions.
 *
 * The implementation leaves VCPU IDs from the range [0..KVM_MAX_VCPUS) (block
 * 0) unchanged: if the guest is filling each VCORE completely then it will be
 * using consecutive IDs and it will fill the space without any packing.
 *
 * For higher VCPU IDs, the packed ID is based on the VCPU ID modulo
 * KVM_MAX_VCPUS (effectively masking off the top bits) and then an offset is
 * added to avoid collisions.
 *
 * VCPU IDs in the range [KVM_MAX_VCPUS..(KVM_MAX_VCPUS*2)) (block 1) are only
 * possible if the guest is leaving at least 1/2 of each VCORE empty, so IDs
 * can be safely packed into the second half of each VCORE by adding an offset
 * of (stride / 2).
 *
 * Similarly, if VCPU IDs in the range [(KVM_MAX_VCPUS*2)..(KVM_MAX_VCPUS*4))
 * (blocks 2 and 3) are seen, the guest must be leaving at least 3/4 of each
 * VCORE empty so packed IDs can be offset by (stride / 4) and (stride * 3 / 4).
 *
 * Finally, VCPU IDs from blocks 5..7 will only be seen if the guest is using a
 * stride of 8 and 1 thread per core so the remaining offsets of 1, 5, 3 and 7
 * must be free to use.
 *
 * (The offsets for each block are stored in block_offsets[], indexed by the
 * block number if the stride is 8. For cases where the guest's stride is less
 * than 8, we can re-use the block_offsets array by multiplying the block
 * number by (MAX_SMT_THREADS / stride) to reach the correct entry.)
 */
static inline u32 kvmppc_pack_vcpu_id(struct kvm *kvm, u32 id)
{
	const int block_offsets[MAX_SMT_THREADS] = {0, 4, 2, 6, 1, 5, 3, 7};
	int stride = kvm->arch.emul_smt_mode;
	int block = (id / KVM_MAX_VCPUS) * (MAX_SMT_THREADS / stride);
	u32 packed_id;

	if (WARN_ONCE(block >= MAX_SMT_THREADS, "VCPU ID too large to pack"))
		return 0;
	packed_id = (id % KVM_MAX_VCPUS) + block_offsets[block];
	if (WARN_ONCE(packed_id >= KVM_MAX_VCPUS, "VCPU ID packing failed"))
		return 0;
	return packed_id;
}

#endif /* __ASM_KVM_BOOK3S_H__ */
