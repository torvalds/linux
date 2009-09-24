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
 * Copyright IBM Corp. 2007
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __POWERPC_KVM_HOST_H__
#define __POWERPC_KVM_HOST_H__

#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/kvm_types.h>
#include <asm/kvm_asm.h>

#define KVM_MAX_VCPUS 1
#define KVM_MEMORY_SLOTS 32
/* memory slots that does not exposed to userspace */
#define KVM_PRIVATE_MEM_SLOTS 4

#define KVM_COALESCED_MMIO_PAGE_OFFSET 1

/* We don't currently support large pages. */
#define KVM_NR_PAGE_SIZES	1
#define KVM_PAGES_PER_HPAGE(x)	(1UL<<31)

struct kvm;
struct kvm_run;
struct kvm_vcpu;

struct kvm_vm_stat {
	u32 remote_tlb_flush;
};

struct kvm_vcpu_stat {
	u32 sum_exits;
	u32 mmio_exits;
	u32 dcr_exits;
	u32 signal_exits;
	u32 light_exits;
	/* Account for special types of light exits: */
	u32 itlb_real_miss_exits;
	u32 itlb_virt_miss_exits;
	u32 dtlb_real_miss_exits;
	u32 dtlb_virt_miss_exits;
	u32 syscall_exits;
	u32 isi_exits;
	u32 dsi_exits;
	u32 emulated_inst_exits;
	u32 dec_exits;
	u32 ext_intr_exits;
	u32 halt_wakeup;
};

enum kvm_exit_types {
	MMIO_EXITS,
	DCR_EXITS,
	SIGNAL_EXITS,
	ITLB_REAL_MISS_EXITS,
	ITLB_VIRT_MISS_EXITS,
	DTLB_REAL_MISS_EXITS,
	DTLB_VIRT_MISS_EXITS,
	SYSCALL_EXITS,
	ISI_EXITS,
	DSI_EXITS,
	EMULATED_INST_EXITS,
	EMULATED_MTMSRWE_EXITS,
	EMULATED_WRTEE_EXITS,
	EMULATED_MTSPR_EXITS,
	EMULATED_MFSPR_EXITS,
	EMULATED_MTMSR_EXITS,
	EMULATED_MFMSR_EXITS,
	EMULATED_TLBSX_EXITS,
	EMULATED_TLBWE_EXITS,
	EMULATED_RFI_EXITS,
	DEC_EXITS,
	EXT_INTR_EXITS,
	HALT_WAKEUP,
	USR_PR_INST,
	FP_UNAVAIL,
	DEBUG_EXITS,
	TIMEINGUEST,
	__NUMBER_OF_KVM_EXIT_TYPES
};

/* allow access to big endian 32bit upper/lower parts and 64bit var */
struct kvmppc_exit_timing {
	union {
		u64 tv64;
		struct {
			u32 tbu, tbl;
		} tv32;
	};
};

struct kvm_arch {
};

struct kvm_vcpu_arch {
	u32 host_stack;
	u32 host_pid;

	u64 fpr[32];
	ulong gpr[32];

	ulong pc;
	u32 cr;
	ulong ctr;
	ulong lr;
	ulong xer;

	ulong msr;
	u32 mmucr;
	ulong sprg0;
	ulong sprg1;
	ulong sprg2;
	ulong sprg3;
	ulong sprg4;
	ulong sprg5;
	ulong sprg6;
	ulong sprg7;
	ulong srr0;
	ulong srr1;
	ulong csrr0;
	ulong csrr1;
	ulong dsrr0;
	ulong dsrr1;
	ulong dear;
	ulong esr;
	u32 dec;
	u32 decar;
	u32 tbl;
	u32 tbu;
	u32 tcr;
	u32 tsr;
	u32 ivor[64];
	ulong ivpr;
	u32 pir;

	u32 shadow_pid;
	u32 pid;
	u32 swap_pid;

	u32 ccr0;
	u32 ccr1;
	u32 dbcr0;
	u32 dbcr1;
	u32 dbsr;

#ifdef CONFIG_KVM_EXIT_TIMING
	struct kvmppc_exit_timing timing_exit;
	struct kvmppc_exit_timing timing_last_enter;
	u32 last_exit_type;
	u32 timing_count_type[__NUMBER_OF_KVM_EXIT_TYPES];
	u64 timing_sum_duration[__NUMBER_OF_KVM_EXIT_TYPES];
	u64 timing_sum_quad_duration[__NUMBER_OF_KVM_EXIT_TYPES];
	u64 timing_min_duration[__NUMBER_OF_KVM_EXIT_TYPES];
	u64 timing_max_duration[__NUMBER_OF_KVM_EXIT_TYPES];
	u64 timing_last_exit;
	struct dentry *debugfs_exit_timing;
#endif

	u32 last_inst;
	ulong fault_dear;
	ulong fault_esr;
	gpa_t paddr_accessed;

	u8 io_gpr; /* GPR used as IO source/target */
	u8 mmio_is_bigendian;
	u8 dcr_needed;
	u8 dcr_is_write;

	u32 cpr0_cfgaddr; /* holds the last set cpr0_cfgaddr */

	struct timer_list dec_timer;
	unsigned long pending_exceptions;
};

#endif /* __POWERPC_KVM_HOST_H__ */
