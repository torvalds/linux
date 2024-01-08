/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#ifndef __RISCV_KVM_HOST_H__
#define __RISCV_KVM_HOST_H__

#include <linux/types.h>
#include <linux/kvm.h>
#include <linux/kvm_types.h>
#include <linux/spinlock.h>
#include <asm/hwcap.h>
#include <asm/kvm_aia.h>
#include <asm/ptrace.h>
#include <asm/kvm_vcpu_fp.h>
#include <asm/kvm_vcpu_insn.h>
#include <asm/kvm_vcpu_sbi.h>
#include <asm/kvm_vcpu_timer.h>
#include <asm/kvm_vcpu_pmu.h>

#define KVM_MAX_VCPUS			1024

#define KVM_HALT_POLL_NS_DEFAULT	500000

#define KVM_VCPU_MAX_FEATURES		0

#define KVM_IRQCHIP_NUM_PINS		1024

#define KVM_REQ_SLEEP \
	KVM_ARCH_REQ_FLAGS(0, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_VCPU_RESET		KVM_ARCH_REQ(1)
#define KVM_REQ_UPDATE_HGATP		KVM_ARCH_REQ(2)
#define KVM_REQ_FENCE_I			\
	KVM_ARCH_REQ_FLAGS(3, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_HFENCE_GVMA_VMID_ALL	KVM_REQ_TLB_FLUSH
#define KVM_REQ_HFENCE_VVMA_ALL		\
	KVM_ARCH_REQ_FLAGS(4, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_HFENCE			\
	KVM_ARCH_REQ_FLAGS(5, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)

enum kvm_riscv_hfence_type {
	KVM_RISCV_HFENCE_UNKNOWN = 0,
	KVM_RISCV_HFENCE_GVMA_VMID_GPA,
	KVM_RISCV_HFENCE_VVMA_ASID_GVA,
	KVM_RISCV_HFENCE_VVMA_ASID_ALL,
	KVM_RISCV_HFENCE_VVMA_GVA,
};

struct kvm_riscv_hfence {
	enum kvm_riscv_hfence_type type;
	unsigned long asid;
	unsigned long order;
	gpa_t addr;
	gpa_t size;
};

#define KVM_RISCV_VCPU_MAX_HFENCE	64

struct kvm_vm_stat {
	struct kvm_vm_stat_generic generic;
};

struct kvm_vcpu_stat {
	struct kvm_vcpu_stat_generic generic;
	u64 ecall_exit_stat;
	u64 wfi_exit_stat;
	u64 mmio_exit_user;
	u64 mmio_exit_kernel;
	u64 csr_exit_user;
	u64 csr_exit_kernel;
	u64 signal_exits;
	u64 exits;
};

struct kvm_arch_memory_slot {
};

struct kvm_vmid {
	/*
	 * Writes to vmid_version and vmid happen with vmid_lock held
	 * whereas reads happen without any lock held.
	 */
	unsigned long vmid_version;
	unsigned long vmid;
};

struct kvm_arch {
	/* G-stage vmid */
	struct kvm_vmid vmid;

	/* G-stage page table */
	pgd_t *pgd;
	phys_addr_t pgd_phys;

	/* Guest Timer */
	struct kvm_guest_timer timer;

	/* AIA Guest/VM context */
	struct kvm_aia aia;
};

struct kvm_cpu_trap {
	unsigned long sepc;
	unsigned long scause;
	unsigned long stval;
	unsigned long htval;
	unsigned long htinst;
};

struct kvm_cpu_context {
	unsigned long zero;
	unsigned long ra;
	unsigned long sp;
	unsigned long gp;
	unsigned long tp;
	unsigned long t0;
	unsigned long t1;
	unsigned long t2;
	unsigned long s0;
	unsigned long s1;
	unsigned long a0;
	unsigned long a1;
	unsigned long a2;
	unsigned long a3;
	unsigned long a4;
	unsigned long a5;
	unsigned long a6;
	unsigned long a7;
	unsigned long s2;
	unsigned long s3;
	unsigned long s4;
	unsigned long s5;
	unsigned long s6;
	unsigned long s7;
	unsigned long s8;
	unsigned long s9;
	unsigned long s10;
	unsigned long s11;
	unsigned long t3;
	unsigned long t4;
	unsigned long t5;
	unsigned long t6;
	unsigned long sepc;
	unsigned long sstatus;
	unsigned long hstatus;
	union __riscv_fp_state fp;
	struct __riscv_v_ext_state vector;
};

struct kvm_vcpu_csr {
	unsigned long vsstatus;
	unsigned long vsie;
	unsigned long vstvec;
	unsigned long vsscratch;
	unsigned long vsepc;
	unsigned long vscause;
	unsigned long vstval;
	unsigned long hvip;
	unsigned long vsatp;
	unsigned long scounteren;
	unsigned long senvcfg;
};

struct kvm_vcpu_config {
	u64 henvcfg;
	u64 hstateen0;
};

struct kvm_vcpu_smstateen_csr {
	unsigned long sstateen0;
};

struct kvm_vcpu_arch {
	/* VCPU ran at least once */
	bool ran_atleast_once;

	/* Last Host CPU on which Guest VCPU exited */
	int last_exit_cpu;

	/* ISA feature bits (similar to MISA) */
	DECLARE_BITMAP(isa, RISCV_ISA_EXT_MAX);

	/* Vendor, Arch, and Implementation details */
	unsigned long mvendorid;
	unsigned long marchid;
	unsigned long mimpid;

	/* SSCRATCH, STVEC, and SCOUNTEREN of Host */
	unsigned long host_sscratch;
	unsigned long host_stvec;
	unsigned long host_scounteren;
	unsigned long host_senvcfg;
	unsigned long host_sstateen0;

	/* CPU context of Host */
	struct kvm_cpu_context host_context;

	/* CPU context of Guest VCPU */
	struct kvm_cpu_context guest_context;

	/* CPU CSR context of Guest VCPU */
	struct kvm_vcpu_csr guest_csr;

	/* CPU Smstateen CSR context of Guest VCPU */
	struct kvm_vcpu_smstateen_csr smstateen_csr;

	/* CPU context upon Guest VCPU reset */
	struct kvm_cpu_context guest_reset_context;

	/* CPU CSR context upon Guest VCPU reset */
	struct kvm_vcpu_csr guest_reset_csr;

	/*
	 * VCPU interrupts
	 *
	 * We have a lockless approach for tracking pending VCPU interrupts
	 * implemented using atomic bitops. The irqs_pending bitmap represent
	 * pending interrupts whereas irqs_pending_mask represent bits changed
	 * in irqs_pending. Our approach is modeled around multiple producer
	 * and single consumer problem where the consumer is the VCPU itself.
	 */
#define KVM_RISCV_VCPU_NR_IRQS	64
	DECLARE_BITMAP(irqs_pending, KVM_RISCV_VCPU_NR_IRQS);
	DECLARE_BITMAP(irqs_pending_mask, KVM_RISCV_VCPU_NR_IRQS);

	/* VCPU Timer */
	struct kvm_vcpu_timer timer;

	/* HFENCE request queue */
	spinlock_t hfence_lock;
	unsigned long hfence_head;
	unsigned long hfence_tail;
	struct kvm_riscv_hfence hfence_queue[KVM_RISCV_VCPU_MAX_HFENCE];

	/* MMIO instruction details */
	struct kvm_mmio_decode mmio_decode;

	/* CSR instruction details */
	struct kvm_csr_decode csr_decode;

	/* SBI context */
	struct kvm_vcpu_sbi_context sbi_context;

	/* AIA VCPU context */
	struct kvm_vcpu_aia aia_context;

	/* Cache pages needed to program page tables with spinlock held */
	struct kvm_mmu_memory_cache mmu_page_cache;

	/* VCPU power-off state */
	bool power_off;

	/* Don't run the VCPU (blocked) */
	bool pause;

	/* Performance monitoring context */
	struct kvm_pmu pmu_context;

	/* 'static' configurations which are set only once */
	struct kvm_vcpu_config cfg;
};

static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu) {}

#define KVM_ARCH_WANT_MMU_NOTIFIER

#define KVM_RISCV_GSTAGE_TLB_MIN_ORDER		12

void kvm_riscv_local_hfence_gvma_vmid_gpa(unsigned long vmid,
					  gpa_t gpa, gpa_t gpsz,
					  unsigned long order);
void kvm_riscv_local_hfence_gvma_vmid_all(unsigned long vmid);
void kvm_riscv_local_hfence_gvma_gpa(gpa_t gpa, gpa_t gpsz,
				     unsigned long order);
void kvm_riscv_local_hfence_gvma_all(void);
void kvm_riscv_local_hfence_vvma_asid_gva(unsigned long vmid,
					  unsigned long asid,
					  unsigned long gva,
					  unsigned long gvsz,
					  unsigned long order);
void kvm_riscv_local_hfence_vvma_asid_all(unsigned long vmid,
					  unsigned long asid);
void kvm_riscv_local_hfence_vvma_gva(unsigned long vmid,
				     unsigned long gva, unsigned long gvsz,
				     unsigned long order);
void kvm_riscv_local_hfence_vvma_all(unsigned long vmid);

void kvm_riscv_local_tlb_sanitize(struct kvm_vcpu *vcpu);

void kvm_riscv_fence_i_process(struct kvm_vcpu *vcpu);
void kvm_riscv_hfence_gvma_vmid_all_process(struct kvm_vcpu *vcpu);
void kvm_riscv_hfence_vvma_all_process(struct kvm_vcpu *vcpu);
void kvm_riscv_hfence_process(struct kvm_vcpu *vcpu);

void kvm_riscv_fence_i(struct kvm *kvm,
		       unsigned long hbase, unsigned long hmask);
void kvm_riscv_hfence_gvma_vmid_gpa(struct kvm *kvm,
				    unsigned long hbase, unsigned long hmask,
				    gpa_t gpa, gpa_t gpsz,
				    unsigned long order);
void kvm_riscv_hfence_gvma_vmid_all(struct kvm *kvm,
				    unsigned long hbase, unsigned long hmask);
void kvm_riscv_hfence_vvma_asid_gva(struct kvm *kvm,
				    unsigned long hbase, unsigned long hmask,
				    unsigned long gva, unsigned long gvsz,
				    unsigned long order, unsigned long asid);
void kvm_riscv_hfence_vvma_asid_all(struct kvm *kvm,
				    unsigned long hbase, unsigned long hmask,
				    unsigned long asid);
void kvm_riscv_hfence_vvma_gva(struct kvm *kvm,
			       unsigned long hbase, unsigned long hmask,
			       unsigned long gva, unsigned long gvsz,
			       unsigned long order);
void kvm_riscv_hfence_vvma_all(struct kvm *kvm,
			       unsigned long hbase, unsigned long hmask);

int kvm_riscv_gstage_ioremap(struct kvm *kvm, gpa_t gpa,
			     phys_addr_t hpa, unsigned long size,
			     bool writable, bool in_atomic);
void kvm_riscv_gstage_iounmap(struct kvm *kvm, gpa_t gpa,
			      unsigned long size);
int kvm_riscv_gstage_map(struct kvm_vcpu *vcpu,
			 struct kvm_memory_slot *memslot,
			 gpa_t gpa, unsigned long hva, bool is_write);
int kvm_riscv_gstage_alloc_pgd(struct kvm *kvm);
void kvm_riscv_gstage_free_pgd(struct kvm *kvm);
void kvm_riscv_gstage_update_hgatp(struct kvm_vcpu *vcpu);
void __init kvm_riscv_gstage_mode_detect(void);
unsigned long __init kvm_riscv_gstage_mode(void);
int kvm_riscv_gstage_gpa_bits(void);

void __init kvm_riscv_gstage_vmid_detect(void);
unsigned long kvm_riscv_gstage_vmid_bits(void);
int kvm_riscv_gstage_vmid_init(struct kvm *kvm);
bool kvm_riscv_gstage_vmid_ver_changed(struct kvm_vmid *vmid);
void kvm_riscv_gstage_vmid_update(struct kvm_vcpu *vcpu);

int kvm_riscv_setup_default_irq_routing(struct kvm *kvm, u32 lines);

void __kvm_riscv_unpriv_trap(void);

unsigned long kvm_riscv_vcpu_unpriv_read(struct kvm_vcpu *vcpu,
					 bool read_insn,
					 unsigned long guest_addr,
					 struct kvm_cpu_trap *trap);
void kvm_riscv_vcpu_trap_redirect(struct kvm_vcpu *vcpu,
				  struct kvm_cpu_trap *trap);
int kvm_riscv_vcpu_exit(struct kvm_vcpu *vcpu, struct kvm_run *run,
			struct kvm_cpu_trap *trap);

void __kvm_riscv_switch_to(struct kvm_vcpu_arch *vcpu_arch);

void kvm_riscv_vcpu_setup_isa(struct kvm_vcpu *vcpu);
unsigned long kvm_riscv_vcpu_num_regs(struct kvm_vcpu *vcpu);
int kvm_riscv_vcpu_copy_reg_indices(struct kvm_vcpu *vcpu,
				    u64 __user *uindices);
int kvm_riscv_vcpu_get_reg(struct kvm_vcpu *vcpu,
			   const struct kvm_one_reg *reg);
int kvm_riscv_vcpu_set_reg(struct kvm_vcpu *vcpu,
			   const struct kvm_one_reg *reg);

int kvm_riscv_vcpu_set_interrupt(struct kvm_vcpu *vcpu, unsigned int irq);
int kvm_riscv_vcpu_unset_interrupt(struct kvm_vcpu *vcpu, unsigned int irq);
void kvm_riscv_vcpu_flush_interrupts(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_sync_interrupts(struct kvm_vcpu *vcpu);
bool kvm_riscv_vcpu_has_interrupts(struct kvm_vcpu *vcpu, u64 mask);
void kvm_riscv_vcpu_power_off(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_power_on(struct kvm_vcpu *vcpu);

#endif /* __RISCV_KVM_HOST_H__ */
