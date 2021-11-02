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
#include <asm/kvm_vcpu_fp.h>
#include <asm/kvm_vcpu_timer.h>

#ifdef CONFIG_64BIT
#define KVM_MAX_VCPUS			(1U << 16)
#else
#define KVM_MAX_VCPUS			(1U << 9)
#endif

#define KVM_HALT_POLL_NS_DEFAULT	500000

#define KVM_VCPU_MAX_FEATURES		0

#define KVM_REQ_SLEEP \
	KVM_ARCH_REQ_FLAGS(0, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_VCPU_RESET		KVM_ARCH_REQ(1)
#define KVM_REQ_UPDATE_HGATP		KVM_ARCH_REQ(2)

struct kvm_vm_stat {
	struct kvm_vm_stat_generic generic;
};

struct kvm_vcpu_stat {
	struct kvm_vcpu_stat_generic generic;
	u64 ecall_exit_stat;
	u64 wfi_exit_stat;
	u64 mmio_exit_user;
	u64 mmio_exit_kernel;
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
	/* stage2 vmid */
	struct kvm_vmid vmid;

	/* stage2 page table */
	pgd_t *pgd;
	phys_addr_t pgd_phys;

	/* Guest Timer */
	struct kvm_guest_timer timer;
};

struct kvm_mmio_decode {
	unsigned long insn;
	int insn_len;
	int len;
	int shift;
	int return_handled;
};

struct kvm_sbi_context {
	int return_handled;
};

#define KVM_MMU_PAGE_CACHE_NR_OBJS	32

struct kvm_mmu_page_cache {
	int nobjs;
	void *objects[KVM_MMU_PAGE_CACHE_NR_OBJS];
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
};

struct kvm_vcpu_arch {
	/* VCPU ran at least once */
	bool ran_atleast_once;

	/* ISA feature bits (similar to MISA) */
	unsigned long isa;

	/* SSCRATCH, STVEC, and SCOUNTEREN of Host */
	unsigned long host_sscratch;
	unsigned long host_stvec;
	unsigned long host_scounteren;

	/* CPU context of Host */
	struct kvm_cpu_context host_context;

	/* CPU context of Guest VCPU */
	struct kvm_cpu_context guest_context;

	/* CPU CSR context of Guest VCPU */
	struct kvm_vcpu_csr guest_csr;

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
	unsigned long irqs_pending;
	unsigned long irqs_pending_mask;

	/* VCPU Timer */
	struct kvm_vcpu_timer timer;

	/* MMIO instruction details */
	struct kvm_mmio_decode mmio_decode;

	/* SBI context */
	struct kvm_sbi_context sbi_context;

	/* Cache pages needed to program page tables with spinlock held */
	struct kvm_mmu_page_cache mmu_page_cache;

	/* VCPU power-off state */
	bool power_off;

	/* Don't run the VCPU (blocked) */
	bool pause;

	/* SRCU lock index for in-kernel run loop */
	int srcu_idx;
};

static inline void kvm_arch_hardware_unsetup(void) {}
static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu) {}
static inline void kvm_arch_vcpu_block_finish(struct kvm_vcpu *vcpu) {}

#define KVM_ARCH_WANT_MMU_NOTIFIER

void __kvm_riscv_hfence_gvma_vmid_gpa(unsigned long gpa_divby_4,
				      unsigned long vmid);
void __kvm_riscv_hfence_gvma_vmid(unsigned long vmid);
void __kvm_riscv_hfence_gvma_gpa(unsigned long gpa_divby_4);
void __kvm_riscv_hfence_gvma_all(void);

int kvm_riscv_stage2_map(struct kvm_vcpu *vcpu,
			 struct kvm_memory_slot *memslot,
			 gpa_t gpa, unsigned long hva, bool is_write);
void kvm_riscv_stage2_flush_cache(struct kvm_vcpu *vcpu);
int kvm_riscv_stage2_alloc_pgd(struct kvm *kvm);
void kvm_riscv_stage2_free_pgd(struct kvm *kvm);
void kvm_riscv_stage2_update_hgatp(struct kvm_vcpu *vcpu);
void kvm_riscv_stage2_mode_detect(void);
unsigned long kvm_riscv_stage2_mode(void);

void kvm_riscv_stage2_vmid_detect(void);
unsigned long kvm_riscv_stage2_vmid_bits(void);
int kvm_riscv_stage2_vmid_init(struct kvm *kvm);
bool kvm_riscv_stage2_vmid_ver_changed(struct kvm_vmid *vmid);
void kvm_riscv_stage2_vmid_update(struct kvm_vcpu *vcpu);

void __kvm_riscv_unpriv_trap(void);

unsigned long kvm_riscv_vcpu_unpriv_read(struct kvm_vcpu *vcpu,
					 bool read_insn,
					 unsigned long guest_addr,
					 struct kvm_cpu_trap *trap);
void kvm_riscv_vcpu_trap_redirect(struct kvm_vcpu *vcpu,
				  struct kvm_cpu_trap *trap);
int kvm_riscv_vcpu_mmio_return(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_riscv_vcpu_exit(struct kvm_vcpu *vcpu, struct kvm_run *run,
			struct kvm_cpu_trap *trap);

void __kvm_riscv_switch_to(struct kvm_vcpu_arch *vcpu_arch);

int kvm_riscv_vcpu_set_interrupt(struct kvm_vcpu *vcpu, unsigned int irq);
int kvm_riscv_vcpu_unset_interrupt(struct kvm_vcpu *vcpu, unsigned int irq);
void kvm_riscv_vcpu_flush_interrupts(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_sync_interrupts(struct kvm_vcpu *vcpu);
bool kvm_riscv_vcpu_has_interrupts(struct kvm_vcpu *vcpu, unsigned long mask);
void kvm_riscv_vcpu_power_off(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_power_on(struct kvm_vcpu *vcpu);

int kvm_riscv_vcpu_sbi_return(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_riscv_vcpu_sbi_ecall(struct kvm_vcpu *vcpu, struct kvm_run *run);

#endif /* __RISCV_KVM_HOST_H__ */
