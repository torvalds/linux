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
#ifdef CONFIG_PPC_BOOK3S
#include <asm/kvm_book3s.h>
#else
#include <asm/kvm_booke.h>
#endif
#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
#include <asm/paca.h>
#endif

enum emulation_result {
	EMULATE_DONE,         /* no further processing */
	EMULATE_DO_MMIO,      /* kvm_run filled with MMIO request */
	EMULATE_DO_DCR,       /* kvm_run filled with DCR request */
	EMULATE_FAIL,         /* can't emulate this instruction */
	EMULATE_AGAIN,        /* something went wrong. go again */
};

extern int kvmppc_vcpu_run(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu);
extern int __kvmppc_vcpu_run(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu);
extern char kvmppc_handlers_start[];
extern unsigned long kvmppc_handler_len;
extern void kvmppc_handler_highmem(void);

extern void kvmppc_dump_vcpu(struct kvm_vcpu *vcpu);
extern int kvmppc_handle_load(struct kvm_run *run, struct kvm_vcpu *vcpu,
                              unsigned int rt, unsigned int bytes,
                              int is_bigendian);
extern int kvmppc_handle_loads(struct kvm_run *run, struct kvm_vcpu *vcpu,
                               unsigned int rt, unsigned int bytes,
                               int is_bigendian);
extern int kvmppc_handle_store(struct kvm_run *run, struct kvm_vcpu *vcpu,
                               u64 val, unsigned int bytes, int is_bigendian);

extern int kvmppc_emulate_instruction(struct kvm_run *run,
                                      struct kvm_vcpu *vcpu);
extern int kvmppc_emulate_mmio(struct kvm_run *run, struct kvm_vcpu *vcpu);
extern void kvmppc_emulate_dec(struct kvm_vcpu *vcpu);
extern u32 kvmppc_get_dec(struct kvm_vcpu *vcpu, u64 tb);
extern void kvmppc_decrementer_func(unsigned long data);
extern int kvmppc_sanity_check(struct kvm_vcpu *vcpu);

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

extern struct kvm_vcpu *kvmppc_core_vcpu_create(struct kvm *kvm,
                                                unsigned int id);
extern void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu);
extern int kvmppc_core_vcpu_setup(struct kvm_vcpu *vcpu);
extern int kvmppc_core_check_processor_compat(void);
extern int kvmppc_core_vcpu_translate(struct kvm_vcpu *vcpu,
                                      struct kvm_translation *tr);

extern void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
extern void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu);

extern int kvmppc_core_prepare_to_enter(struct kvm_vcpu *vcpu);
extern int kvmppc_core_pending_dec(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_program(struct kvm_vcpu *vcpu, ulong flags);
extern void kvmppc_core_queue_dec(struct kvm_vcpu *vcpu);
extern void kvmppc_core_dequeue_dec(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_external(struct kvm_vcpu *vcpu,
                                       struct kvm_interrupt *irq);
extern void kvmppc_core_dequeue_external(struct kvm_vcpu *vcpu,
                                         struct kvm_interrupt *irq);

extern int kvmppc_core_emulate_op(struct kvm_run *run, struct kvm_vcpu *vcpu,
                                  unsigned int op, int *advance);
extern int kvmppc_core_emulate_mtspr(struct kvm_vcpu *vcpu, int sprn,
				     ulong val);
extern int kvmppc_core_emulate_mfspr(struct kvm_vcpu *vcpu, int sprn,
				     ulong *val);

extern int kvmppc_booke_init(void);
extern void kvmppc_booke_exit(void);

extern void kvmppc_core_destroy_mmu(struct kvm_vcpu *vcpu);
extern int kvmppc_kvm_pv(struct kvm_vcpu *vcpu);
extern void kvmppc_map_magic(struct kvm_vcpu *vcpu);

extern long kvmppc_alloc_hpt(struct kvm *kvm, u32 *htab_orderp);
extern long kvmppc_alloc_reset_hpt(struct kvm *kvm, u32 *htab_orderp);
extern void kvmppc_free_hpt(struct kvm *kvm);
extern long kvmppc_prepare_vrma(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem);
extern void kvmppc_map_vrma(struct kvm_vcpu *vcpu,
			struct kvm_memory_slot *memslot, unsigned long porder);
extern int kvmppc_pseries_do_hcall(struct kvm_vcpu *vcpu);
extern long kvm_vm_ioctl_create_spapr_tce(struct kvm *kvm,
				struct kvm_create_spapr_tce *args);
extern long kvmppc_h_put_tce(struct kvm_vcpu *vcpu, unsigned long liobn,
			     unsigned long ioba, unsigned long tce);
extern long kvm_vm_ioctl_allocate_rma(struct kvm *kvm,
				struct kvm_allocate_rma *rma);
extern struct kvmppc_linear_info *kvm_alloc_rma(void);
extern void kvm_release_rma(struct kvmppc_linear_info *ri);
extern struct kvmppc_linear_info *kvm_alloc_hpt(void);
extern void kvm_release_hpt(struct kvmppc_linear_info *li);
extern int kvmppc_core_init_vm(struct kvm *kvm);
extern void kvmppc_core_destroy_vm(struct kvm *kvm);
extern int kvmppc_core_prepare_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem);
extern void kvmppc_core_commit_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem);
extern int kvm_vm_ioctl_get_smmu_info(struct kvm *kvm,
				      struct kvm_ppc_smmu_info *info);

extern int kvmppc_bookehv_init(void);
extern void kvmppc_bookehv_exit(void);

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

void kvmppc_core_get_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);
int kvmppc_core_set_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);

void kvmppc_get_sregs_ivor(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);
int kvmppc_set_sregs_ivor(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);

int kvm_vcpu_ioctl_get_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg);
int kvm_vcpu_ioctl_set_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg);

void kvmppc_set_pid(struct kvm_vcpu *vcpu, u32 pid);

#ifdef CONFIG_KVM_BOOK3S_64_HV
static inline void kvmppc_set_xics_phys(int cpu, unsigned long addr)
{
	paca[cpu].kvm_hstate.xics_phys = addr;
}

extern void kvm_linear_init(void);

#else
static inline void kvmppc_set_xics_phys(int cpu, unsigned long addr)
{}

static inline void kvm_linear_init(void)
{}
#endif

int kvm_vcpu_ioctl_config_tlb(struct kvm_vcpu *vcpu,
			      struct kvm_config_tlb *cfg);
int kvm_vcpu_ioctl_dirty_tlb(struct kvm_vcpu *vcpu,
			     struct kvm_dirty_tlb *cfg);

long kvmppc_alloc_lpid(void);
void kvmppc_claim_lpid(long lpid);
void kvmppc_free_lpid(long lpid);
void kvmppc_init_lpid(unsigned long nr_lpids);

#endif /* __POWERPC_KVM_PPC_H__ */
