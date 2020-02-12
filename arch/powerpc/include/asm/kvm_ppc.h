/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __POWERPC_KVM_PPC_H__
#define __POWERPC_KVM_PPC_H__

/* This file exists just so we can dereference kvm_vcpu, avoiding nested header
 * dependencies. */

#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/kvm_types.h>
#include <linux/kvm_host.h>
#include <linux/bug.h>
#ifdef CONFIG_PPC_BOOK3S
#include <asm/kvm_book3s.h>
#else
#include <asm/kvm_booke.h>
#endif
#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
#include <asm/paca.h>
#include <asm/xive.h>
#include <asm/cpu_has_feature.h>
#endif

/*
 * KVMPPC_INST_SW_BREAKPOINT is debug Instruction
 * for supporting software breakpoint.
 */
#define KVMPPC_INST_SW_BREAKPOINT	0x00dddd00

enum emulation_result {
	EMULATE_DONE,         /* no further processing */
	EMULATE_DO_MMIO,      /* kvm_run filled with MMIO request */
	EMULATE_FAIL,         /* can't emulate this instruction */
	EMULATE_AGAIN,        /* something went wrong. go again */
	EMULATE_EXIT_USER,    /* emulation requires exit to user-space */
};

enum instruction_fetch_type {
	INST_GENERIC,
	INST_SC,		/* system call */
};

enum xlate_instdata {
	XLATE_INST,		/* translate instruction address */
	XLATE_DATA		/* translate data address */
};

enum xlate_readwrite {
	XLATE_READ,		/* check for read permissions */
	XLATE_WRITE		/* check for write permissions */
};

extern int kvmppc_vcpu_run(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu);
extern int __kvmppc_vcpu_run(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu);
extern void kvmppc_handler_highmem(void);

extern void kvmppc_dump_vcpu(struct kvm_vcpu *vcpu);
extern int kvmppc_handle_load(struct kvm_run *run, struct kvm_vcpu *vcpu,
                              unsigned int rt, unsigned int bytes,
			      int is_default_endian);
extern int kvmppc_handle_loads(struct kvm_run *run, struct kvm_vcpu *vcpu,
                               unsigned int rt, unsigned int bytes,
			       int is_default_endian);
extern int kvmppc_handle_vsx_load(struct kvm_run *run, struct kvm_vcpu *vcpu,
				unsigned int rt, unsigned int bytes,
			int is_default_endian, int mmio_sign_extend);
extern int kvmppc_handle_vmx_load(struct kvm_run *run, struct kvm_vcpu *vcpu,
		unsigned int rt, unsigned int bytes, int is_default_endian);
extern int kvmppc_handle_vmx_store(struct kvm_run *run, struct kvm_vcpu *vcpu,
		unsigned int rs, unsigned int bytes, int is_default_endian);
extern int kvmppc_handle_store(struct kvm_run *run, struct kvm_vcpu *vcpu,
			       u64 val, unsigned int bytes,
			       int is_default_endian);
extern int kvmppc_handle_vsx_store(struct kvm_run *run, struct kvm_vcpu *vcpu,
				int rs, unsigned int bytes,
				int is_default_endian);

extern int kvmppc_load_last_inst(struct kvm_vcpu *vcpu,
				 enum instruction_fetch_type type, u32 *inst);

extern int kvmppc_ld(struct kvm_vcpu *vcpu, ulong *eaddr, int size, void *ptr,
		     bool data);
extern int kvmppc_st(struct kvm_vcpu *vcpu, ulong *eaddr, int size, void *ptr,
		     bool data);
extern int kvmppc_emulate_instruction(struct kvm_run *run,
                                      struct kvm_vcpu *vcpu);
extern int kvmppc_emulate_loadstore(struct kvm_vcpu *vcpu);
extern int kvmppc_emulate_mmio(struct kvm_run *run, struct kvm_vcpu *vcpu);
extern void kvmppc_emulate_dec(struct kvm_vcpu *vcpu);
extern u32 kvmppc_get_dec(struct kvm_vcpu *vcpu, u64 tb);
extern void kvmppc_decrementer_func(struct kvm_vcpu *vcpu);
extern int kvmppc_sanity_check(struct kvm_vcpu *vcpu);
extern int kvmppc_subarch_vcpu_init(struct kvm_vcpu *vcpu);
extern void kvmppc_subarch_vcpu_uninit(struct kvm_vcpu *vcpu);

/* Core-specific hooks */

extern void kvmppc_mmu_map(struct kvm_vcpu *vcpu, u64 gvaddr, gpa_t gpaddr,
                           unsigned int gtlb_idx);
extern void kvmppc_mmu_priv_switch(struct kvm_vcpu *vcpu, int usermode);
extern void kvmppc_mmu_switch_pid(struct kvm_vcpu *vcpu, u32 pid);
extern void kvmppc_mmu_destroy(struct kvm_vcpu *vcpu);
extern int kvmppc_mmu_init(struct kvm_vcpu *vcpu);
extern int kvmppc_mmu_dtlb_index(struct kvm_vcpu *vcpu, gva_t eaddr);
extern int kvmppc_mmu_itlb_index(struct kvm_vcpu *vcpu, gva_t eaddr);
extern gpa_t kvmppc_mmu_xlate(struct kvm_vcpu *vcpu, unsigned int gtlb_index,
                              gva_t eaddr);
extern void kvmppc_mmu_dtlb_miss(struct kvm_vcpu *vcpu);
extern void kvmppc_mmu_itlb_miss(struct kvm_vcpu *vcpu);
extern int kvmppc_xlate(struct kvm_vcpu *vcpu, ulong eaddr,
			enum xlate_instdata xlid, enum xlate_readwrite xlrw,
			struct kvmppc_pte *pte);

extern int kvmppc_core_vcpu_create(struct kvm_vcpu *vcpu);
extern void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu);
extern int kvmppc_core_vcpu_setup(struct kvm_vcpu *vcpu);
extern int kvmppc_core_check_processor_compat(void);
extern int kvmppc_core_vcpu_translate(struct kvm_vcpu *vcpu,
                                      struct kvm_translation *tr);

extern void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
extern void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu);

extern int kvmppc_core_prepare_to_enter(struct kvm_vcpu *vcpu);
extern int kvmppc_core_pending_dec(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_machine_check(struct kvm_vcpu *vcpu, ulong flags);
extern void kvmppc_core_queue_program(struct kvm_vcpu *vcpu, ulong flags);
extern void kvmppc_core_queue_fpunavail(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_vec_unavail(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_vsx_unavail(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_dec(struct kvm_vcpu *vcpu);
extern void kvmppc_core_dequeue_dec(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_external(struct kvm_vcpu *vcpu,
                                       struct kvm_interrupt *irq);
extern void kvmppc_core_dequeue_external(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_dtlb_miss(struct kvm_vcpu *vcpu, ulong dear_flags,
					ulong esr_flags);
extern void kvmppc_core_queue_data_storage(struct kvm_vcpu *vcpu,
					   ulong dear_flags,
					   ulong esr_flags);
extern void kvmppc_core_queue_itlb_miss(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_inst_storage(struct kvm_vcpu *vcpu,
					   ulong esr_flags);
extern void kvmppc_core_flush_tlb(struct kvm_vcpu *vcpu);
extern int kvmppc_core_check_requests(struct kvm_vcpu *vcpu);

extern int kvmppc_booke_init(void);
extern void kvmppc_booke_exit(void);

extern void kvmppc_core_destroy_mmu(struct kvm_vcpu *vcpu);
extern int kvmppc_kvm_pv(struct kvm_vcpu *vcpu);
extern void kvmppc_map_magic(struct kvm_vcpu *vcpu);

extern int kvmppc_allocate_hpt(struct kvm_hpt_info *info, u32 order);
extern void kvmppc_set_hpt(struct kvm *kvm, struct kvm_hpt_info *info);
extern long kvmppc_alloc_reset_hpt(struct kvm *kvm, int order);
extern void kvmppc_free_hpt(struct kvm_hpt_info *info);
extern void kvmppc_rmap_reset(struct kvm *kvm);
extern long kvmppc_prepare_vrma(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem);
extern void kvmppc_map_vrma(struct kvm_vcpu *vcpu,
			struct kvm_memory_slot *memslot, unsigned long porder);
extern int kvmppc_pseries_do_hcall(struct kvm_vcpu *vcpu);
extern long kvm_spapr_tce_attach_iommu_group(struct kvm *kvm, int tablefd,
		struct iommu_group *grp);
extern void kvm_spapr_tce_release_iommu_group(struct kvm *kvm,
		struct iommu_group *grp);
extern int kvmppc_switch_mmu_to_hpt(struct kvm *kvm);
extern int kvmppc_switch_mmu_to_radix(struct kvm *kvm);
extern void kvmppc_setup_partition_table(struct kvm *kvm);

extern long kvm_vm_ioctl_create_spapr_tce(struct kvm *kvm,
				struct kvm_create_spapr_tce_64 *args);
extern struct kvmppc_spapr_tce_table *kvmppc_find_table(
		struct kvm *kvm, unsigned long liobn);
#define kvmppc_ioba_validate(stt, ioba, npages)                         \
		(iommu_tce_check_ioba((stt)->page_shift, (stt)->offset, \
				(stt)->size, (ioba), (npages)) ?        \
				H_PARAMETER : H_SUCCESS)
extern long kvmppc_h_put_tce(struct kvm_vcpu *vcpu, unsigned long liobn,
			     unsigned long ioba, unsigned long tce);
extern long kvmppc_h_put_tce_indirect(struct kvm_vcpu *vcpu,
		unsigned long liobn, unsigned long ioba,
		unsigned long tce_list, unsigned long npages);
extern long kvmppc_h_stuff_tce(struct kvm_vcpu *vcpu,
		unsigned long liobn, unsigned long ioba,
		unsigned long tce_value, unsigned long npages);
extern long kvmppc_h_get_tce(struct kvm_vcpu *vcpu, unsigned long liobn,
			     unsigned long ioba);
extern struct page *kvm_alloc_hpt_cma(unsigned long nr_pages);
extern void kvm_free_hpt_cma(struct page *page, unsigned long nr_pages);
extern int kvmppc_core_init_vm(struct kvm *kvm);
extern void kvmppc_core_destroy_vm(struct kvm *kvm);
extern void kvmppc_core_free_memslot(struct kvm *kvm,
				     struct kvm_memory_slot *free,
				     struct kvm_memory_slot *dont);
extern int kvmppc_core_create_memslot(struct kvm *kvm,
				      struct kvm_memory_slot *slot,
				      unsigned long npages);
extern int kvmppc_core_prepare_memory_region(struct kvm *kvm,
				struct kvm_memory_slot *memslot,
				const struct kvm_userspace_memory_region *mem);
extern void kvmppc_core_commit_memory_region(struct kvm *kvm,
				const struct kvm_userspace_memory_region *mem,
				const struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change);
extern int kvm_vm_ioctl_get_smmu_info(struct kvm *kvm,
				      struct kvm_ppc_smmu_info *info);
extern void kvmppc_core_flush_memslot(struct kvm *kvm,
				      struct kvm_memory_slot *memslot);

extern int kvmppc_bookehv_init(void);
extern void kvmppc_bookehv_exit(void);

extern int kvmppc_prepare_to_enter(struct kvm_vcpu *vcpu);

extern int kvm_vm_ioctl_get_htab_fd(struct kvm *kvm, struct kvm_get_htab_fd *);
extern long kvm_vm_ioctl_resize_hpt_prepare(struct kvm *kvm,
					    struct kvm_ppc_resize_hpt *rhpt);
extern long kvm_vm_ioctl_resize_hpt_commit(struct kvm *kvm,
					   struct kvm_ppc_resize_hpt *rhpt);

int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu, struct kvm_interrupt *irq);

extern int kvm_vm_ioctl_rtas_define_token(struct kvm *kvm, void __user *argp);
extern int kvmppc_rtas_hcall(struct kvm_vcpu *vcpu);
extern void kvmppc_rtas_tokens_free(struct kvm *kvm);

extern int kvmppc_xics_set_xive(struct kvm *kvm, u32 irq, u32 server,
				u32 priority);
extern int kvmppc_xics_get_xive(struct kvm *kvm, u32 irq, u32 *server,
				u32 *priority);
extern int kvmppc_xics_int_on(struct kvm *kvm, u32 irq);
extern int kvmppc_xics_int_off(struct kvm *kvm, u32 irq);

void kvmppc_core_dequeue_debug(struct kvm_vcpu *vcpu);
void kvmppc_core_queue_debug(struct kvm_vcpu *vcpu);

union kvmppc_one_reg {
	u32	wval;
	u64	dval;
	vector128 vval;
	u64	vsxval[2];
	u32	vsx32val[4];
	u16	vsx16val[8];
	u8	vsx8val[16];
	struct {
		u64	addr;
		u64	length;
	}	vpaval;
	u64	xive_timaval[2];
};

struct kvmppc_ops {
	struct module *owner;
	int (*get_sregs)(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);
	int (*set_sregs)(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);
	int (*get_one_reg)(struct kvm_vcpu *vcpu, u64 id,
			   union kvmppc_one_reg *val);
	int (*set_one_reg)(struct kvm_vcpu *vcpu, u64 id,
			   union kvmppc_one_reg *val);
	void (*vcpu_load)(struct kvm_vcpu *vcpu, int cpu);
	void (*vcpu_put)(struct kvm_vcpu *vcpu);
	void (*inject_interrupt)(struct kvm_vcpu *vcpu, int vec, u64 srr1_flags);
	void (*set_msr)(struct kvm_vcpu *vcpu, u64 msr);
	int (*vcpu_run)(struct kvm_run *run, struct kvm_vcpu *vcpu);
	int (*vcpu_create)(struct kvm_vcpu *vcpu);
	void (*vcpu_free)(struct kvm_vcpu *vcpu);
	int (*check_requests)(struct kvm_vcpu *vcpu);
	int (*get_dirty_log)(struct kvm *kvm, struct kvm_dirty_log *log);
	void (*flush_memslot)(struct kvm *kvm, struct kvm_memory_slot *memslot);
	int (*prepare_memory_region)(struct kvm *kvm,
				     struct kvm_memory_slot *memslot,
				     const struct kvm_userspace_memory_region *mem);
	void (*commit_memory_region)(struct kvm *kvm,
				     const struct kvm_userspace_memory_region *mem,
				     const struct kvm_memory_slot *old,
				     const struct kvm_memory_slot *new,
				     enum kvm_mr_change change);
	int (*unmap_hva_range)(struct kvm *kvm, unsigned long start,
			   unsigned long end);
	int (*age_hva)(struct kvm *kvm, unsigned long start, unsigned long end);
	int (*test_age_hva)(struct kvm *kvm, unsigned long hva);
	void (*set_spte_hva)(struct kvm *kvm, unsigned long hva, pte_t pte);
	void (*mmu_destroy)(struct kvm_vcpu *vcpu);
	void (*free_memslot)(struct kvm_memory_slot *free,
			     struct kvm_memory_slot *dont);
	int (*create_memslot)(struct kvm_memory_slot *slot,
			      unsigned long npages);
	int (*init_vm)(struct kvm *kvm);
	void (*destroy_vm)(struct kvm *kvm);
	int (*get_smmu_info)(struct kvm *kvm, struct kvm_ppc_smmu_info *info);
	int (*emulate_op)(struct kvm_run *run, struct kvm_vcpu *vcpu,
			  unsigned int inst, int *advance);
	int (*emulate_mtspr)(struct kvm_vcpu *vcpu, int sprn, ulong spr_val);
	int (*emulate_mfspr)(struct kvm_vcpu *vcpu, int sprn, ulong *spr_val);
	void (*fast_vcpu_kick)(struct kvm_vcpu *vcpu);
	long (*arch_vm_ioctl)(struct file *filp, unsigned int ioctl,
			      unsigned long arg);
	int (*hcall_implemented)(unsigned long hcall);
	int (*irq_bypass_add_producer)(struct irq_bypass_consumer *,
				       struct irq_bypass_producer *);
	void (*irq_bypass_del_producer)(struct irq_bypass_consumer *,
					struct irq_bypass_producer *);
	int (*configure_mmu)(struct kvm *kvm, struct kvm_ppc_mmuv3_cfg *cfg);
	int (*get_rmmu_info)(struct kvm *kvm, struct kvm_ppc_rmmu_info *info);
	int (*set_smt_mode)(struct kvm *kvm, unsigned long mode,
			    unsigned long flags);
	void (*giveup_ext)(struct kvm_vcpu *vcpu, ulong msr);
	int (*enable_nested)(struct kvm *kvm);
	int (*load_from_eaddr)(struct kvm_vcpu *vcpu, ulong *eaddr, void *ptr,
			       int size);
	int (*store_to_eaddr)(struct kvm_vcpu *vcpu, ulong *eaddr, void *ptr,
			      int size);
	int (*svm_off)(struct kvm *kvm);
};

extern struct kvmppc_ops *kvmppc_hv_ops;
extern struct kvmppc_ops *kvmppc_pr_ops;

static inline int kvmppc_get_last_inst(struct kvm_vcpu *vcpu,
				enum instruction_fetch_type type, u32 *inst)
{
	int ret = EMULATE_DONE;
	u32 fetched_inst;

	/* Load the instruction manually if it failed to do so in the
	 * exit path */
	if (vcpu->arch.last_inst == KVM_INST_FETCH_FAILED)
		ret = kvmppc_load_last_inst(vcpu, type, &vcpu->arch.last_inst);

	/*  Write fetch_failed unswapped if the fetch failed */
	if (ret == EMULATE_DONE)
		fetched_inst = kvmppc_need_byteswap(vcpu) ?
				swab32(vcpu->arch.last_inst) :
				vcpu->arch.last_inst;
	else
		fetched_inst = vcpu->arch.last_inst;

	*inst = fetched_inst;
	return ret;
}

static inline bool is_kvmppc_hv_enabled(struct kvm *kvm)
{
	return kvm->arch.kvm_ops == kvmppc_hv_ops;
}

extern int kvmppc_hwrng_present(void);

/*
 * Cuts out inst bits with ordering according to spec.
 * That means the leftmost bit is zero. All given bits are included.
 */
static inline u32 kvmppc_get_field(u64 inst, int msb, int lsb)
{
	u32 r;
	u32 mask;

	BUG_ON(msb > lsb);

	mask = (1 << (lsb - msb + 1)) - 1;
	r = (inst >> (63 - lsb)) & mask;

	return r;
}

/*
 * Replaces inst bits with ordering according to spec.
 */
static inline u32 kvmppc_set_field(u64 inst, int msb, int lsb, int value)
{
	u32 r;
	u32 mask;

	BUG_ON(msb > lsb);

	mask = ((1 << (lsb - msb + 1)) - 1) << (63 - lsb);
	r = (inst & ~mask) | ((value << (63 - lsb)) & mask);

	return r;
}

#define one_reg_size(id)	\
	(1ul << (((id) & KVM_REG_SIZE_MASK) >> KVM_REG_SIZE_SHIFT))

#define get_reg_val(id, reg)	({		\
	union kvmppc_one_reg __u;		\
	switch (one_reg_size(id)) {		\
	case 4: __u.wval = (reg); break;	\
	case 8: __u.dval = (reg); break;	\
	default: BUG();				\
	}					\
	__u;					\
})


#define set_reg_val(id, val)	({		\
	u64 __v;				\
	switch (one_reg_size(id)) {		\
	case 4: __v = (val).wval; break;	\
	case 8: __v = (val).dval; break;	\
	default: BUG();				\
	}					\
	__v;					\
})

int kvmppc_core_get_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);
int kvmppc_core_set_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);

int kvmppc_get_sregs_ivor(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);
int kvmppc_set_sregs_ivor(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);

int kvm_vcpu_ioctl_get_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg);
int kvm_vcpu_ioctl_set_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg);
int kvmppc_get_one_reg(struct kvm_vcpu *vcpu, u64 id, union kvmppc_one_reg *);
int kvmppc_set_one_reg(struct kvm_vcpu *vcpu, u64 id, union kvmppc_one_reg *);

void kvmppc_set_pid(struct kvm_vcpu *vcpu, u32 pid);

struct openpic;

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
extern void kvm_cma_reserve(void) __init;
static inline void kvmppc_set_xics_phys(int cpu, unsigned long addr)
{
	paca_ptrs[cpu]->kvm_hstate.xics_phys = (void __iomem *)addr;
}

static inline void kvmppc_set_xive_tima(int cpu,
					unsigned long phys_addr,
					void __iomem *virt_addr)
{
	paca_ptrs[cpu]->kvm_hstate.xive_tima_phys = (void __iomem *)phys_addr;
	paca_ptrs[cpu]->kvm_hstate.xive_tima_virt = virt_addr;
}

static inline u32 kvmppc_get_xics_latch(void)
{
	u32 xirr;

	xirr = get_paca()->kvm_hstate.saved_xirr;
	get_paca()->kvm_hstate.saved_xirr = 0;
	return xirr;
}

/*
 * To avoid the need to unnecessarily exit fully to the host kernel, an IPI to
 * a CPU thread that's running/napping inside of a guest is by default regarded
 * as a request to wake the CPU (if needed) and continue execution within the
 * guest, potentially to process new state like externally-generated
 * interrupts or IPIs sent from within the guest itself (e.g. H_PROD/H_IPI).
 *
 * To force an exit to the host kernel, kvmppc_set_host_ipi() must be called
 * prior to issuing the IPI to set the corresponding 'host_ipi' flag in the
 * target CPU's PACA. To avoid unnecessary exits to the host, this flag should
 * be immediately cleared via kvmppc_clear_host_ipi() by the IPI handler on
 * the receiving side prior to processing the IPI work.
 *
 * NOTE:
 *
 * We currently issue an smp_mb() at the beginning of kvmppc_set_host_ipi().
 * This is to guard against sequences such as the following:
 *
 *      CPU
 *        X: smp_muxed_ipi_set_message():
 *        X:   smp_mb()
 *        X:   message[RESCHEDULE] = 1
 *        X: doorbell_global_ipi(42):
 *        X:   kvmppc_set_host_ipi(42)
 *        X:   ppc_msgsnd_sync()/smp_mb()
 *        X:   ppc_msgsnd() -> 42
 *       42: doorbell_exception(): // from CPU X
 *       42:   ppc_msgsync()
 *      105: smp_muxed_ipi_set_message():
 *      105:   smb_mb()
 *           // STORE DEFERRED DUE TO RE-ORDERING
 *    --105:   message[CALL_FUNCTION] = 1
 *    | 105: doorbell_global_ipi(42):
 *    | 105:   kvmppc_set_host_ipi(42)
 *    |  42:   kvmppc_clear_host_ipi(42)
 *    |  42: smp_ipi_demux_relaxed()
 *    |  42: // returns to executing guest
 *    |      // RE-ORDERED STORE COMPLETES
 *    ->105:   message[CALL_FUNCTION] = 1
 *      105:   ppc_msgsnd_sync()/smp_mb()
 *      105:   ppc_msgsnd() -> 42
 *       42: local_paca->kvm_hstate.host_ipi == 0 // IPI ignored
 *      105: // hangs waiting on 42 to process messages/call_single_queue
 *
 * We also issue an smp_mb() at the end of kvmppc_clear_host_ipi(). This is
 * to guard against sequences such as the following (as well as to create
 * a read-side pairing with the barrier in kvmppc_set_host_ipi()):
 *
 *      CPU
 *        X: smp_muxed_ipi_set_message():
 *        X:   smp_mb()
 *        X:   message[RESCHEDULE] = 1
 *        X: doorbell_global_ipi(42):
 *        X:   kvmppc_set_host_ipi(42)
 *        X:   ppc_msgsnd_sync()/smp_mb()
 *        X:   ppc_msgsnd() -> 42
 *       42: doorbell_exception(): // from CPU X
 *       42:   ppc_msgsync()
 *           // STORE DEFERRED DUE TO RE-ORDERING
 *    -- 42:   kvmppc_clear_host_ipi(42)
 *    |  42: smp_ipi_demux_relaxed()
 *    | 105: smp_muxed_ipi_set_message():
 *    | 105:   smb_mb()
 *    | 105:   message[CALL_FUNCTION] = 1
 *    | 105: doorbell_global_ipi(42):
 *    | 105:   kvmppc_set_host_ipi(42)
 *    |      // RE-ORDERED STORE COMPLETES
 *    -> 42:   kvmppc_clear_host_ipi(42)
 *       42: // returns to executing guest
 *      105:   ppc_msgsnd_sync()/smp_mb()
 *      105:   ppc_msgsnd() -> 42
 *       42: local_paca->kvm_hstate.host_ipi == 0 // IPI ignored
 *      105: // hangs waiting on 42 to process messages/call_single_queue
 */
static inline void kvmppc_set_host_ipi(int cpu)
{
	/*
	 * order stores of IPI messages vs. setting of host_ipi flag
	 *
	 * pairs with the barrier in kvmppc_clear_host_ipi()
	 */
	smp_mb();
	paca_ptrs[cpu]->kvm_hstate.host_ipi = 1;
}

static inline void kvmppc_clear_host_ipi(int cpu)
{
	paca_ptrs[cpu]->kvm_hstate.host_ipi = 0;
	/*
	 * order clearing of host_ipi flag vs. processing of IPI messages
	 *
	 * pairs with the barrier in kvmppc_set_host_ipi()
	 */
	smp_mb();
}

static inline void kvmppc_fast_vcpu_kick(struct kvm_vcpu *vcpu)
{
	vcpu->kvm->arch.kvm_ops->fast_vcpu_kick(vcpu);
}

extern void kvm_hv_vm_activated(void);
extern void kvm_hv_vm_deactivated(void);
extern bool kvm_hv_mode_active(void);

extern void kvmppc_check_need_tlb_flush(struct kvm *kvm, int pcpu,
					struct kvm_nested_guest *nested);

#else
static inline void __init kvm_cma_reserve(void)
{}

static inline void kvmppc_set_xics_phys(int cpu, unsigned long addr)
{}

static inline void kvmppc_set_xive_tima(int cpu,
					unsigned long phys_addr,
					void __iomem *virt_addr)
{}

static inline u32 kvmppc_get_xics_latch(void)
{
	return 0;
}

static inline void kvmppc_set_host_ipi(int cpu)
{}

static inline void kvmppc_clear_host_ipi(int cpu)
{}

static inline void kvmppc_fast_vcpu_kick(struct kvm_vcpu *vcpu)
{
	kvm_vcpu_kick(vcpu);
}

static inline bool kvm_hv_mode_active(void)		{ return false; }

#endif

#ifdef CONFIG_KVM_XICS
static inline int kvmppc_xics_enabled(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.irq_type == KVMPPC_IRQ_XICS;
}

static inline struct kvmppc_passthru_irqmap *kvmppc_get_passthru_irqmap(
				struct kvm *kvm)
{
	if (kvm && kvm_irq_bypass)
		return kvm->arch.pimap;
	return NULL;
}

extern void kvmppc_alloc_host_rm_ops(void);
extern void kvmppc_free_host_rm_ops(void);
extern void kvmppc_free_pimap(struct kvm *kvm);
extern int kvmppc_xics_rm_complete(struct kvm_vcpu *vcpu, u32 hcall);
extern void kvmppc_xics_free_icp(struct kvm_vcpu *vcpu);
extern int kvmppc_xics_hcall(struct kvm_vcpu *vcpu, u32 cmd);
extern u64 kvmppc_xics_get_icp(struct kvm_vcpu *vcpu);
extern int kvmppc_xics_set_icp(struct kvm_vcpu *vcpu, u64 icpval);
extern int kvmppc_xics_connect_vcpu(struct kvm_device *dev,
			struct kvm_vcpu *vcpu, u32 cpu);
extern void kvmppc_xics_ipi_action(void);
extern void kvmppc_xics_set_mapped(struct kvm *kvm, unsigned long guest_irq,
				   unsigned long host_irq);
extern void kvmppc_xics_clr_mapped(struct kvm *kvm, unsigned long guest_irq,
				   unsigned long host_irq);
extern long kvmppc_deliver_irq_passthru(struct kvm_vcpu *vcpu, __be32 xirr,
					struct kvmppc_irq_map *irq_map,
					struct kvmppc_passthru_irqmap *pimap,
					bool *again);

extern int kvmppc_xics_set_irq(struct kvm *kvm, int irq_source_id, u32 irq,
			       int level, bool line_status);

extern int h_ipi_redirect;
#else
static inline struct kvmppc_passthru_irqmap *kvmppc_get_passthru_irqmap(
				struct kvm *kvm)
	{ return NULL; }
static inline void kvmppc_alloc_host_rm_ops(void) {};
static inline void kvmppc_free_host_rm_ops(void) {};
static inline void kvmppc_free_pimap(struct kvm *kvm) {};
static inline int kvmppc_xics_rm_complete(struct kvm_vcpu *vcpu, u32 hcall)
	{ return 0; }
static inline int kvmppc_xics_enabled(struct kvm_vcpu *vcpu)
	{ return 0; }
static inline void kvmppc_xics_free_icp(struct kvm_vcpu *vcpu) { }
static inline int kvmppc_xics_hcall(struct kvm_vcpu *vcpu, u32 cmd)
	{ return 0; }
#endif

#ifdef CONFIG_KVM_XIVE
/*
 * Below the first "xive" is the "eXternal Interrupt Virtualization Engine"
 * ie. P9 new interrupt controller, while the second "xive" is the legacy
 * "eXternal Interrupt Vector Entry" which is the configuration of an
 * interrupt on the "xics" interrupt controller on P8 and earlier. Those
 * two function consume or produce a legacy "XIVE" state from the
 * new "XIVE" interrupt controller.
 */
extern int kvmppc_xive_set_xive(struct kvm *kvm, u32 irq, u32 server,
				u32 priority);
extern int kvmppc_xive_get_xive(struct kvm *kvm, u32 irq, u32 *server,
				u32 *priority);
extern int kvmppc_xive_int_on(struct kvm *kvm, u32 irq);
extern int kvmppc_xive_int_off(struct kvm *kvm, u32 irq);
extern void kvmppc_xive_init_module(void);
extern void kvmppc_xive_exit_module(void);

extern int kvmppc_xive_connect_vcpu(struct kvm_device *dev,
				    struct kvm_vcpu *vcpu, u32 cpu);
extern void kvmppc_xive_cleanup_vcpu(struct kvm_vcpu *vcpu);
extern int kvmppc_xive_set_mapped(struct kvm *kvm, unsigned long guest_irq,
				  struct irq_desc *host_desc);
extern int kvmppc_xive_clr_mapped(struct kvm *kvm, unsigned long guest_irq,
				  struct irq_desc *host_desc);
extern u64 kvmppc_xive_get_icp(struct kvm_vcpu *vcpu);
extern int kvmppc_xive_set_icp(struct kvm_vcpu *vcpu, u64 icpval);

extern int kvmppc_xive_set_irq(struct kvm *kvm, int irq_source_id, u32 irq,
			       int level, bool line_status);
extern void kvmppc_xive_push_vcpu(struct kvm_vcpu *vcpu);

static inline int kvmppc_xive_enabled(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.irq_type == KVMPPC_IRQ_XIVE;
}

extern int kvmppc_xive_native_connect_vcpu(struct kvm_device *dev,
					   struct kvm_vcpu *vcpu, u32 cpu);
extern void kvmppc_xive_native_cleanup_vcpu(struct kvm_vcpu *vcpu);
extern void kvmppc_xive_native_init_module(void);
extern void kvmppc_xive_native_exit_module(void);
extern int kvmppc_xive_native_get_vp(struct kvm_vcpu *vcpu,
				     union kvmppc_one_reg *val);
extern int kvmppc_xive_native_set_vp(struct kvm_vcpu *vcpu,
				     union kvmppc_one_reg *val);
extern bool kvmppc_xive_native_supported(void);

#else
static inline int kvmppc_xive_set_xive(struct kvm *kvm, u32 irq, u32 server,
				       u32 priority) { return -1; }
static inline int kvmppc_xive_get_xive(struct kvm *kvm, u32 irq, u32 *server,
				       u32 *priority) { return -1; }
static inline int kvmppc_xive_int_on(struct kvm *kvm, u32 irq) { return -1; }
static inline int kvmppc_xive_int_off(struct kvm *kvm, u32 irq) { return -1; }
static inline void kvmppc_xive_init_module(void) { }
static inline void kvmppc_xive_exit_module(void) { }

static inline int kvmppc_xive_connect_vcpu(struct kvm_device *dev,
					   struct kvm_vcpu *vcpu, u32 cpu) { return -EBUSY; }
static inline void kvmppc_xive_cleanup_vcpu(struct kvm_vcpu *vcpu) { }
static inline int kvmppc_xive_set_mapped(struct kvm *kvm, unsigned long guest_irq,
					 struct irq_desc *host_desc) { return -ENODEV; }
static inline int kvmppc_xive_clr_mapped(struct kvm *kvm, unsigned long guest_irq,
					 struct irq_desc *host_desc) { return -ENODEV; }
static inline u64 kvmppc_xive_get_icp(struct kvm_vcpu *vcpu) { return 0; }
static inline int kvmppc_xive_set_icp(struct kvm_vcpu *vcpu, u64 icpval) { return -ENOENT; }

static inline int kvmppc_xive_set_irq(struct kvm *kvm, int irq_source_id, u32 irq,
				      int level, bool line_status) { return -ENODEV; }
static inline void kvmppc_xive_push_vcpu(struct kvm_vcpu *vcpu) { }

static inline int kvmppc_xive_enabled(struct kvm_vcpu *vcpu)
	{ return 0; }
static inline int kvmppc_xive_native_connect_vcpu(struct kvm_device *dev,
			  struct kvm_vcpu *vcpu, u32 cpu) { return -EBUSY; }
static inline void kvmppc_xive_native_cleanup_vcpu(struct kvm_vcpu *vcpu) { }
static inline void kvmppc_xive_native_init_module(void) { }
static inline void kvmppc_xive_native_exit_module(void) { }
static inline int kvmppc_xive_native_get_vp(struct kvm_vcpu *vcpu,
					    union kvmppc_one_reg *val)
{ return 0; }
static inline int kvmppc_xive_native_set_vp(struct kvm_vcpu *vcpu,
					    union kvmppc_one_reg *val)
{ return -ENOENT; }

#endif /* CONFIG_KVM_XIVE */

#if defined(CONFIG_PPC_POWERNV) && defined(CONFIG_KVM_BOOK3S_64_HANDLER)
static inline bool xics_on_xive(void)
{
	return xive_enabled() && cpu_has_feature(CPU_FTR_HVMODE);
}
#else
static inline bool xics_on_xive(void)
{
	return false;
}
#endif

/*
 * Prototypes for functions called only from assembler code.
 * Having prototypes reduces sparse errors.
 */
long kvmppc_rm_h_put_tce(struct kvm_vcpu *vcpu, unsigned long liobn,
			 unsigned long ioba, unsigned long tce);
long kvmppc_rm_h_put_tce_indirect(struct kvm_vcpu *vcpu,
				  unsigned long liobn, unsigned long ioba,
				  unsigned long tce_list, unsigned long npages);
long kvmppc_rm_h_stuff_tce(struct kvm_vcpu *vcpu,
			   unsigned long liobn, unsigned long ioba,
			   unsigned long tce_value, unsigned long npages);
long int kvmppc_rm_h_confer(struct kvm_vcpu *vcpu, int target,
                            unsigned int yield_count);
long kvmppc_h_random(struct kvm_vcpu *vcpu);
void kvmhv_commence_exit(int trap);
void kvmppc_realmode_machine_check(struct kvm_vcpu *vcpu);
void kvmppc_subcore_enter_guest(void);
void kvmppc_subcore_exit_guest(void);
long kvmppc_realmode_hmi_handler(void);
long kvmppc_h_enter(struct kvm_vcpu *vcpu, unsigned long flags,
                    long pte_index, unsigned long pteh, unsigned long ptel);
long kvmppc_h_remove(struct kvm_vcpu *vcpu, unsigned long flags,
                     unsigned long pte_index, unsigned long avpn);
long kvmppc_h_bulk_remove(struct kvm_vcpu *vcpu);
long kvmppc_h_protect(struct kvm_vcpu *vcpu, unsigned long flags,
                      unsigned long pte_index, unsigned long avpn,
                      unsigned long va);
long kvmppc_h_read(struct kvm_vcpu *vcpu, unsigned long flags,
                   unsigned long pte_index);
long kvmppc_h_clear_ref(struct kvm_vcpu *vcpu, unsigned long flags,
                        unsigned long pte_index);
long kvmppc_h_clear_mod(struct kvm_vcpu *vcpu, unsigned long flags,
                        unsigned long pte_index);
long kvmppc_rm_h_page_init(struct kvm_vcpu *vcpu, unsigned long flags,
			   unsigned long dest, unsigned long src);
long kvmppc_hpte_hv_fault(struct kvm_vcpu *vcpu, unsigned long addr,
                          unsigned long slb_v, unsigned int status, bool data);
unsigned long kvmppc_rm_h_xirr(struct kvm_vcpu *vcpu);
unsigned long kvmppc_rm_h_xirr_x(struct kvm_vcpu *vcpu);
unsigned long kvmppc_rm_h_ipoll(struct kvm_vcpu *vcpu, unsigned long server);
int kvmppc_rm_h_ipi(struct kvm_vcpu *vcpu, unsigned long server,
                    unsigned long mfrr);
int kvmppc_rm_h_cppr(struct kvm_vcpu *vcpu, unsigned long cppr);
int kvmppc_rm_h_eoi(struct kvm_vcpu *vcpu, unsigned long xirr);
void kvmppc_guest_entry_inject_int(struct kvm_vcpu *vcpu);

/*
 * Host-side operations we want to set up while running in real
 * mode in the guest operating on the xics.
 * Currently only VCPU wakeup is supported.
 */

union kvmppc_rm_state {
	unsigned long raw;
	struct {
		u32 in_host;
		u32 rm_action;
	};
};

struct kvmppc_host_rm_core {
	union kvmppc_rm_state rm_state;
	void *rm_data;
	char pad[112];
};

struct kvmppc_host_rm_ops {
	struct kvmppc_host_rm_core	*rm_core;
	void		(*vcpu_kick)(struct kvm_vcpu *vcpu);
};

extern struct kvmppc_host_rm_ops *kvmppc_host_rm_ops_hv;

static inline unsigned long kvmppc_get_epr(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_KVM_BOOKE_HV
	return mfspr(SPRN_GEPR);
#elif defined(CONFIG_BOOKE)
	return vcpu->arch.epr;
#else
	return 0;
#endif
}

static inline void kvmppc_set_epr(struct kvm_vcpu *vcpu, u32 epr)
{
#ifdef CONFIG_KVM_BOOKE_HV
	mtspr(SPRN_GEPR, epr);
#elif defined(CONFIG_BOOKE)
	vcpu->arch.epr = epr;
#endif
}

#ifdef CONFIG_KVM_MPIC

void kvmppc_mpic_set_epr(struct kvm_vcpu *vcpu);
int kvmppc_mpic_connect_vcpu(struct kvm_device *dev, struct kvm_vcpu *vcpu,
			     u32 cpu);
void kvmppc_mpic_disconnect_vcpu(struct openpic *opp, struct kvm_vcpu *vcpu);

#else

static inline void kvmppc_mpic_set_epr(struct kvm_vcpu *vcpu)
{
}

static inline int kvmppc_mpic_connect_vcpu(struct kvm_device *dev,
		struct kvm_vcpu *vcpu, u32 cpu)
{
	return -EINVAL;
}

static inline void kvmppc_mpic_disconnect_vcpu(struct openpic *opp,
		struct kvm_vcpu *vcpu)
{
}

#endif /* CONFIG_KVM_MPIC */

int kvm_vcpu_ioctl_config_tlb(struct kvm_vcpu *vcpu,
			      struct kvm_config_tlb *cfg);
int kvm_vcpu_ioctl_dirty_tlb(struct kvm_vcpu *vcpu,
			     struct kvm_dirty_tlb *cfg);

long kvmppc_alloc_lpid(void);
void kvmppc_claim_lpid(long lpid);
void kvmppc_free_lpid(long lpid);
void kvmppc_init_lpid(unsigned long nr_lpids);

static inline void kvmppc_mmu_flush_icache(kvm_pfn_t pfn)
{
	struct page *page;
	/*
	 * We can only access pages that the kernel maps
	 * as memory. Bail out for unmapped ones.
	 */
	if (!pfn_valid(pfn))
		return;

	/* Clear i-cache for new pages */
	page = pfn_to_page(pfn);
	if (!test_bit(PG_arch_1, &page->flags)) {
		flush_dcache_icache_page(page);
		set_bit(PG_arch_1, &page->flags);
	}
}

/*
 * Shared struct helpers. The shared struct can be little or big endian,
 * depending on the guest endianness. So expose helpers to all of them.
 */
static inline bool kvmppc_shared_big_endian(struct kvm_vcpu *vcpu)
{
#if defined(CONFIG_PPC_BOOK3S_64) && defined(CONFIG_KVM_BOOK3S_PR_POSSIBLE)
	/* Only Book3S_64 PR supports bi-endian for now */
	return vcpu->arch.shared_big_endian;
#elif defined(CONFIG_PPC_BOOK3S_64) && defined(__LITTLE_ENDIAN__)
	/* Book3s_64 HV on little endian is always little endian */
	return false;
#else
	return true;
#endif
}

#define SPRNG_WRAPPER_GET(reg, bookehv_spr)				\
static inline ulong kvmppc_get_##reg(struct kvm_vcpu *vcpu)		\
{									\
	return mfspr(bookehv_spr);					\
}									\

#define SPRNG_WRAPPER_SET(reg, bookehv_spr)				\
static inline void kvmppc_set_##reg(struct kvm_vcpu *vcpu, ulong val)	\
{									\
	mtspr(bookehv_spr, val);						\
}									\

#define SHARED_WRAPPER_GET(reg, size)					\
static inline u##size kvmppc_get_##reg(struct kvm_vcpu *vcpu)		\
{									\
	if (kvmppc_shared_big_endian(vcpu))				\
	       return be##size##_to_cpu(vcpu->arch.shared->reg);	\
	else								\
	       return le##size##_to_cpu(vcpu->arch.shared->reg);	\
}									\

#define SHARED_WRAPPER_SET(reg, size)					\
static inline void kvmppc_set_##reg(struct kvm_vcpu *vcpu, u##size val)	\
{									\
	if (kvmppc_shared_big_endian(vcpu))				\
	       vcpu->arch.shared->reg = cpu_to_be##size(val);		\
	else								\
	       vcpu->arch.shared->reg = cpu_to_le##size(val);		\
}									\

#define SHARED_WRAPPER(reg, size)					\
	SHARED_WRAPPER_GET(reg, size)					\
	SHARED_WRAPPER_SET(reg, size)					\

#define SPRNG_WRAPPER(reg, bookehv_spr)					\
	SPRNG_WRAPPER_GET(reg, bookehv_spr)				\
	SPRNG_WRAPPER_SET(reg, bookehv_spr)				\

#ifdef CONFIG_KVM_BOOKE_HV

#define SHARED_SPRNG_WRAPPER(reg, size, bookehv_spr)			\
	SPRNG_WRAPPER(reg, bookehv_spr)					\

#else

#define SHARED_SPRNG_WRAPPER(reg, size, bookehv_spr)			\
	SHARED_WRAPPER(reg, size)					\

#endif

SHARED_WRAPPER(critical, 64)
SHARED_SPRNG_WRAPPER(sprg0, 64, SPRN_GSPRG0)
SHARED_SPRNG_WRAPPER(sprg1, 64, SPRN_GSPRG1)
SHARED_SPRNG_WRAPPER(sprg2, 64, SPRN_GSPRG2)
SHARED_SPRNG_WRAPPER(sprg3, 64, SPRN_GSPRG3)
SHARED_SPRNG_WRAPPER(srr0, 64, SPRN_GSRR0)
SHARED_SPRNG_WRAPPER(srr1, 64, SPRN_GSRR1)
SHARED_SPRNG_WRAPPER(dar, 64, SPRN_GDEAR)
SHARED_SPRNG_WRAPPER(esr, 64, SPRN_GESR)
SHARED_WRAPPER_GET(msr, 64)
static inline void kvmppc_set_msr_fast(struct kvm_vcpu *vcpu, u64 val)
{
	if (kvmppc_shared_big_endian(vcpu))
	       vcpu->arch.shared->msr = cpu_to_be64(val);
	else
	       vcpu->arch.shared->msr = cpu_to_le64(val);
}
SHARED_WRAPPER(dsisr, 32)
SHARED_WRAPPER(int_pending, 32)
SHARED_WRAPPER(sprg4, 64)
SHARED_WRAPPER(sprg5, 64)
SHARED_WRAPPER(sprg6, 64)
SHARED_WRAPPER(sprg7, 64)

static inline u32 kvmppc_get_sr(struct kvm_vcpu *vcpu, int nr)
{
	if (kvmppc_shared_big_endian(vcpu))
	       return be32_to_cpu(vcpu->arch.shared->sr[nr]);
	else
	       return le32_to_cpu(vcpu->arch.shared->sr[nr]);
}

static inline void kvmppc_set_sr(struct kvm_vcpu *vcpu, int nr, u32 val)
{
	if (kvmppc_shared_big_endian(vcpu))
	       vcpu->arch.shared->sr[nr] = cpu_to_be32(val);
	else
	       vcpu->arch.shared->sr[nr] = cpu_to_le32(val);
}

/*
 * Please call after prepare_to_enter. This function puts the lazy ee and irq
 * disabled tracking state back to normal mode, without actually enabling
 * interrupts.
 */
static inline void kvmppc_fix_ee_before_entry(void)
{
	trace_hardirqs_on();

#ifdef CONFIG_PPC64
	/*
	 * To avoid races, the caller must have gone directly from having
	 * interrupts fully-enabled to hard-disabled.
	 */
	WARN_ON(local_paca->irq_happened != PACA_IRQ_HARD_DIS);

	/* Only need to enable IRQs by hard enabling them after this */
	local_paca->irq_happened = 0;
	irq_soft_mask_set(IRQS_ENABLED);
#endif
}

static inline ulong kvmppc_get_ea_indexed(struct kvm_vcpu *vcpu, int ra, int rb)
{
	ulong ea;
	ulong msr_64bit = 0;

	ea = kvmppc_get_gpr(vcpu, rb);
	if (ra)
		ea += kvmppc_get_gpr(vcpu, ra);

#if defined(CONFIG_PPC_BOOK3E_64)
	msr_64bit = MSR_CM;
#elif defined(CONFIG_PPC_BOOK3S_64)
	msr_64bit = MSR_SF;
#endif

	if (!(kvmppc_get_msr(vcpu) & msr_64bit))
		ea = (uint32_t)ea;

	return ea;
}

extern void xics_wake_cpu(int cpu);

#endif /* __POWERPC_KVM_PPC_H__ */
