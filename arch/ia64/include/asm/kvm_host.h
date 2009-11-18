/*
 * kvm_host.h: used for kvm module, and hold ia64-specific sections.
 *
 * Copyright (C) 2007, Intel Corporation.
 *
 * Xiantao Zhang <xiantao.zhang@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#ifndef __ASM_KVM_HOST_H
#define __ASM_KVM_HOST_H

#define KVM_MEMORY_SLOTS 32
/* memory slots that does not exposed to userspace */
#define KVM_PRIVATE_MEM_SLOTS 4

#define KVM_COALESCED_MMIO_PAGE_OFFSET 1

/* define exit reasons from vmm to kvm*/
#define EXIT_REASON_VM_PANIC		0
#define EXIT_REASON_MMIO_INSTRUCTION	1
#define EXIT_REASON_PAL_CALL		2
#define EXIT_REASON_SAL_CALL		3
#define EXIT_REASON_SWITCH_RR6		4
#define EXIT_REASON_VM_DESTROY		5
#define EXIT_REASON_EXTERNAL_INTERRUPT	6
#define EXIT_REASON_IPI			7
#define EXIT_REASON_PTC_G		8
#define EXIT_REASON_DEBUG		20

/*Define vmm address space and vm data space.*/
#define KVM_VMM_SIZE (__IA64_UL_CONST(16)<<20)
#define KVM_VMM_SHIFT 24
#define KVM_VMM_BASE 0xD000000000000000
#define VMM_SIZE (__IA64_UL_CONST(8)<<20)

/*
 * Define vm_buffer, used by PAL Services, base address.
 * Note: vm_buffer is in the VMM-BLOCK, the size must be < 8M
 */
#define KVM_VM_BUFFER_BASE (KVM_VMM_BASE + VMM_SIZE)
#define KVM_VM_BUFFER_SIZE (__IA64_UL_CONST(8)<<20)

/*
 * kvm guest's data area looks as follow:
 *
 *            +----------------------+	-------	KVM_VM_DATA_SIZE
 *	      |	    vcpu[n]'s data   |	 |     ___________________KVM_STK_OFFSET
 *     	      |			     |	 |    /			  |
 *     	      |	       ..........    |	 |   /vcpu's struct&stack |
 *     	      |	       ..........    |	 |  /---------------------|---- 0
 *	      |	    vcpu[5]'s data   |	 | /	   vpd		  |
 *	      |	    vcpu[4]'s data   |	 |/-----------------------|
 *	      |	    vcpu[3]'s data   |	 /	   vtlb		  |
 *	      |	    vcpu[2]'s data   |	/|------------------------|
 *	      |	    vcpu[1]'s data   |/  |	   vhpt		  |
 *	      |	    vcpu[0]'s data   |____________________________|
 *            +----------------------+	 |
 *	      |	   memory dirty log  |	 |
 *            +----------------------+	 |
 *	      |	   vm's data struct  |	 |
 *            +----------------------+	 |
 *	      |			     |	 |
 *	      |			     |	 |
 *	      |			     |	 |
 *	      |			     |	 |
 *	      |			     |	 |
 *	      |			     |	 |
 *	      |			     |	 |
 *	      |	  vm's p2m table  |	 |
 *	      |			     |	 |
 *            |			     |	 |
 *	      |			     |	 |  |
 * vm's data->|			     |   |  |
 *	      +----------------------+ ------- 0
 * To support large memory, needs to increase the size of p2m.
 * To support more vcpus, needs to ensure it has enough space to
 * hold vcpus' data.
 */

#define KVM_VM_DATA_SHIFT	26
#define KVM_VM_DATA_SIZE	(__IA64_UL_CONST(1) << KVM_VM_DATA_SHIFT)
#define KVM_VM_DATA_BASE	(KVM_VMM_BASE + KVM_VM_DATA_SIZE)

#define KVM_P2M_BASE		KVM_VM_DATA_BASE
#define KVM_P2M_SIZE		(__IA64_UL_CONST(24) << 20)

#define VHPT_SHIFT		16
#define VHPT_SIZE		(__IA64_UL_CONST(1) << VHPT_SHIFT)
#define VHPT_NUM_ENTRIES	(__IA64_UL_CONST(1) << (VHPT_SHIFT-5))

#define VTLB_SHIFT		16
#define VTLB_SIZE		(__IA64_UL_CONST(1) << VTLB_SHIFT)
#define VTLB_NUM_ENTRIES	(1UL << (VHPT_SHIFT-5))

#define VPD_SHIFT		16
#define VPD_SIZE		(__IA64_UL_CONST(1) << VPD_SHIFT)

#define VCPU_STRUCT_SHIFT	16
#define VCPU_STRUCT_SIZE	(__IA64_UL_CONST(1) << VCPU_STRUCT_SHIFT)

/*
 * This must match KVM_IA64_VCPU_STACK_{SHIFT,SIZE} arch/ia64/include/asm/kvm.h
 */
#define KVM_STK_SHIFT		16
#define KVM_STK_OFFSET		(__IA64_UL_CONST(1)<< KVM_STK_SHIFT)

#define KVM_VM_STRUCT_SHIFT	19
#define KVM_VM_STRUCT_SIZE	(__IA64_UL_CONST(1) << KVM_VM_STRUCT_SHIFT)

#define KVM_MEM_DIRY_LOG_SHIFT	19
#define KVM_MEM_DIRTY_LOG_SIZE (__IA64_UL_CONST(1) << KVM_MEM_DIRY_LOG_SHIFT)

#ifndef __ASSEMBLY__

/*Define the max vcpus and memory for Guests.*/
#define KVM_MAX_VCPUS	(KVM_VM_DATA_SIZE - KVM_P2M_SIZE - KVM_VM_STRUCT_SIZE -\
			KVM_MEM_DIRTY_LOG_SIZE) / sizeof(struct kvm_vcpu_data)
#define KVM_MAX_MEM_SIZE (KVM_P2M_SIZE >> 3 << PAGE_SHIFT)

#define VMM_LOG_LEN 256

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/kvm.h>
#include <linux/kvm_para.h>
#include <linux/kvm_types.h>

#include <asm/pal.h>
#include <asm/sal.h>
#include <asm/page.h>

struct kvm_vcpu_data {
	char vcpu_vhpt[VHPT_SIZE];
	char vcpu_vtlb[VTLB_SIZE];
	char vcpu_vpd[VPD_SIZE];
	char vcpu_struct[VCPU_STRUCT_SIZE];
};

struct kvm_vm_data {
	char kvm_p2m[KVM_P2M_SIZE];
	char kvm_vm_struct[KVM_VM_STRUCT_SIZE];
	char kvm_mem_dirty_log[KVM_MEM_DIRTY_LOG_SIZE];
	struct kvm_vcpu_data vcpu_data[KVM_MAX_VCPUS];
};

#define VCPU_BASE(n)	(KVM_VM_DATA_BASE + \
				offsetof(struct kvm_vm_data, vcpu_data[n]))
#define KVM_VM_BASE	(KVM_VM_DATA_BASE + \
				offsetof(struct kvm_vm_data, kvm_vm_struct))
#define KVM_MEM_DIRTY_LOG_BASE	KVM_VM_DATA_BASE + \
				offsetof(struct kvm_vm_data, kvm_mem_dirty_log)

#define VHPT_BASE(n) (VCPU_BASE(n) + offsetof(struct kvm_vcpu_data, vcpu_vhpt))
#define VTLB_BASE(n) (VCPU_BASE(n) + offsetof(struct kvm_vcpu_data, vcpu_vtlb))
#define VPD_BASE(n)  (VCPU_BASE(n) + offsetof(struct kvm_vcpu_data, vcpu_vpd))
#define VCPU_STRUCT_BASE(n)	(VCPU_BASE(n) + \
				offsetof(struct kvm_vcpu_data, vcpu_struct))

/*IO section definitions*/
#define IOREQ_READ      1
#define IOREQ_WRITE     0

#define STATE_IOREQ_NONE        0
#define STATE_IOREQ_READY       1
#define STATE_IOREQ_INPROCESS   2
#define STATE_IORESP_READY      3

/*Guest Physical address layout.*/
#define GPFN_MEM        (0UL << 60) /* Guest pfn is normal mem */
#define GPFN_FRAME_BUFFER   (1UL << 60) /* VGA framebuffer */
#define GPFN_LOW_MMIO       (2UL << 60) /* Low MMIO range */
#define GPFN_PIB        (3UL << 60) /* PIB base */
#define GPFN_IOSAPIC        (4UL << 60) /* IOSAPIC base */
#define GPFN_LEGACY_IO      (5UL << 60) /* Legacy I/O base */
#define GPFN_GFW        (6UL << 60) /* Guest Firmware */
#define GPFN_PHYS_MMIO      (7UL << 60) /* Directed MMIO Range */

#define GPFN_IO_MASK        (7UL << 60) /* Guest pfn is I/O type */
#define GPFN_INV_MASK       (1UL << 63) /* Guest pfn is invalid */
#define INVALID_MFN       (~0UL)
#define MEM_G   (1UL << 30)
#define MEM_M   (1UL << 20)
#define MMIO_START       (3 * MEM_G)
#define MMIO_SIZE        (512 * MEM_M)
#define VGA_IO_START     0xA0000UL
#define VGA_IO_SIZE      0x20000
#define LEGACY_IO_START  (MMIO_START + MMIO_SIZE)
#define LEGACY_IO_SIZE   (64 * MEM_M)
#define IO_SAPIC_START   0xfec00000UL
#define IO_SAPIC_SIZE    0x100000
#define PIB_START 0xfee00000UL
#define PIB_SIZE 0x200000
#define GFW_START        (4 * MEM_G - 16 * MEM_M)
#define GFW_SIZE         (16 * MEM_M)

/*Deliver mode, defined for ioapic.c*/
#define dest_Fixed IOSAPIC_FIXED
#define dest_LowestPrio IOSAPIC_LOWEST_PRIORITY

#define NMI_VECTOR      		2
#define ExtINT_VECTOR       		0
#define NULL_VECTOR     		(-1)
#define IA64_SPURIOUS_INT_VECTOR    	0x0f

#define VCPU_LID(v) (((u64)(v)->vcpu_id) << 24)

/*
 *Delivery mode
 */
#define SAPIC_DELIV_SHIFT      8
#define SAPIC_FIXED            0x0
#define SAPIC_LOWEST_PRIORITY  0x1
#define SAPIC_PMI              0x2
#define SAPIC_NMI              0x4
#define SAPIC_INIT             0x5
#define SAPIC_EXTINT           0x7

/*
 * vcpu->requests bit members for arch
 */
#define KVM_REQ_PTC_G		32
#define KVM_REQ_RESUME		33

#define KVM_NR_PAGE_SIZES	1
#define KVM_PAGES_PER_HPAGE(x)	1

struct kvm;
struct kvm_vcpu;

struct kvm_mmio_req {
	uint64_t addr;          /*  physical address		*/
	uint64_t size;          /*  size in bytes		*/
	uint64_t data;          /*  data (or paddr of data)     */
	uint8_t state:4;
	uint8_t dir:1;          /*  1=read, 0=write             */
};

/*Pal data struct */
struct kvm_pal_call{
	/*In area*/
	uint64_t gr28;
	uint64_t gr29;
	uint64_t gr30;
	uint64_t gr31;
	/*Out area*/
	struct ia64_pal_retval ret;
};

/* Sal data structure */
struct kvm_sal_call{
	/*In area*/
	uint64_t in0;
	uint64_t in1;
	uint64_t in2;
	uint64_t in3;
	uint64_t in4;
	uint64_t in5;
	uint64_t in6;
	uint64_t in7;
	struct sal_ret_values ret;
};

/*Guest change rr6*/
struct kvm_switch_rr6 {
	uint64_t old_rr;
	uint64_t new_rr;
};

union ia64_ipi_a{
	unsigned long val;
	struct {
		unsigned long rv  : 3;
		unsigned long ir  : 1;
		unsigned long eid : 8;
		unsigned long id  : 8;
		unsigned long ib_base : 44;
	};
};

union ia64_ipi_d {
	unsigned long val;
	struct {
		unsigned long vector : 8;
		unsigned long dm  : 3;
		unsigned long ig  : 53;
	};
};

/*ipi check exit data*/
struct kvm_ipi_data{
	union ia64_ipi_a addr;
	union ia64_ipi_d data;
};

/*global purge data*/
struct kvm_ptc_g {
	unsigned long vaddr;
	unsigned long rr;
	unsigned long ps;
	struct kvm_vcpu *vcpu;
};

/*Exit control data */
struct exit_ctl_data{
	uint32_t exit_reason;
	uint32_t vm_status;
	union {
		struct kvm_mmio_req	ioreq;
		struct kvm_pal_call	pal_data;
		struct kvm_sal_call	sal_data;
		struct kvm_switch_rr6	rr_data;
		struct kvm_ipi_data	ipi_data;
		struct kvm_ptc_g	ptc_g_data;
	} u;
};

union pte_flags {
	unsigned long val;
	struct {
		unsigned long p    :  1; /*0      */
		unsigned long      :  1; /* 1     */
		unsigned long ma   :  3; /* 2-4   */
		unsigned long a    :  1; /* 5     */
		unsigned long d    :  1; /* 6     */
		unsigned long pl   :  2; /* 7-8   */
		unsigned long ar   :  3; /* 9-11  */
		unsigned long ppn  : 38; /* 12-49 */
		unsigned long      :  2; /* 50-51 */
		unsigned long ed   :  1; /* 52    */
	};
};

union ia64_pta {
	unsigned long val;
	struct {
		unsigned long ve : 1;
		unsigned long reserved0 : 1;
		unsigned long size : 6;
		unsigned long vf : 1;
		unsigned long reserved1 : 6;
		unsigned long base : 49;
	};
};

struct thash_cb {
	/* THASH base information */
	struct thash_data	*hash; /* hash table pointer */
	union ia64_pta		pta;
	int           num;
};

struct kvm_vcpu_stat {
};

struct kvm_vcpu_arch {
	int launched;
	int last_exit;
	int last_run_cpu;
	int vmm_tr_slot;
	int vm_tr_slot;
	int sn_rtc_tr_slot;

#define KVM_MP_STATE_RUNNABLE          0
#define KVM_MP_STATE_UNINITIALIZED     1
#define KVM_MP_STATE_INIT_RECEIVED     2
#define KVM_MP_STATE_HALTED            3
	int mp_state;

#define MAX_PTC_G_NUM			3
	int ptc_g_count;
	struct kvm_ptc_g ptc_g_data[MAX_PTC_G_NUM];

	/*halt timer to wake up sleepy vcpus*/
	struct hrtimer hlt_timer;
	long ht_active;

	struct kvm_lapic *apic;    /* kernel irqchip context */
	struct vpd *vpd;

	/* Exit data for vmm_transition*/
	struct exit_ctl_data exit_data;

	cpumask_t cache_coherent_map;

	unsigned long vmm_rr;
	unsigned long host_rr6;
	unsigned long psbits[8];
	unsigned long cr_iipa;
	unsigned long cr_isr;
	unsigned long vsa_base;
	unsigned long dirty_log_lock_pa;
	unsigned long __gp;
	/* TR and TC.  */
	struct thash_data itrs[NITRS];
	struct thash_data dtrs[NDTRS];
	/* Bit is set if there is a tr/tc for the region.  */
	unsigned char itr_regions;
	unsigned char dtr_regions;
	unsigned char tc_regions;
	/* purge all */
	unsigned long ptce_base;
	unsigned long ptce_count[2];
	unsigned long ptce_stride[2];
	/* itc/itm */
	unsigned long last_itc;
	long itc_offset;
	unsigned long itc_check;
	unsigned long timer_check;
	unsigned int timer_pending;
	unsigned int timer_fired;

	unsigned long vrr[8];
	unsigned long ibr[8];
	unsigned long dbr[8];
	unsigned long insvc[4];		/* Interrupt in service.  */
	unsigned long xtp;

	unsigned long metaphysical_rr0; /* from kvm_arch (so is pinned) */
	unsigned long metaphysical_rr4;	/* from kvm_arch (so is pinned) */
	unsigned long metaphysical_saved_rr0; /* from kvm_arch          */
	unsigned long metaphysical_saved_rr4; /* from kvm_arch          */
	unsigned long fp_psr;       /*used for lazy float register */
	unsigned long saved_gp;
	/*for phycial  emulation */
	int mode_flags;
	struct thash_cb vtlb;
	struct thash_cb vhpt;
	char irq_check;
	char irq_new_pending;

	unsigned long opcode;
	unsigned long cause;
	char log_buf[VMM_LOG_LEN];
	union context host;
	union context guest;
};

struct kvm_vm_stat {
	u64 remote_tlb_flush;
};

struct kvm_sal_data {
	unsigned long boot_ip;
	unsigned long boot_gp;
};

struct kvm_arch {
	spinlock_t dirty_log_lock;

	unsigned long	vm_base;
	unsigned long	metaphysical_rr0;
	unsigned long	metaphysical_rr4;
	unsigned long	vmm_init_rr;

	int		is_sn2;

	struct kvm_ioapic *vioapic;
	struct kvm_vm_stat stat;
	struct kvm_sal_data rdv_sal_data;

	struct list_head assigned_dev_head;
	struct iommu_domain *iommu_domain;
	int iommu_flags;
	struct hlist_head irq_ack_notifier_list;

	unsigned long irq_sources_bitmap;
	unsigned long irq_states[KVM_IOAPIC_NUM_PINS];
};

union cpuid3_t {
	u64 value;
	struct {
		u64 number : 8;
		u64 revision : 8;
		u64 model : 8;
		u64 family : 8;
		u64 archrev : 8;
		u64 rv : 24;
	};
};

struct kvm_pt_regs {
	/* The following registers are saved by SAVE_MIN: */
	unsigned long b6;  /* scratch */
	unsigned long b7;  /* scratch */

	unsigned long ar_csd; /* used by cmp8xchg16 (scratch) */
	unsigned long ar_ssd; /* reserved for future use (scratch) */

	unsigned long r8;  /* scratch (return value register 0) */
	unsigned long r9;  /* scratch (return value register 1) */
	unsigned long r10; /* scratch (return value register 2) */
	unsigned long r11; /* scratch (return value register 3) */

	unsigned long cr_ipsr; /* interrupted task's psr */
	unsigned long cr_iip;  /* interrupted task's instruction pointer */
	unsigned long cr_ifs;  /* interrupted task's function state */

	unsigned long ar_unat; /* interrupted task's NaT register (preserved) */
	unsigned long ar_pfs;  /* prev function state  */
	unsigned long ar_rsc;  /* RSE configuration */
	/* The following two are valid only if cr_ipsr.cpl > 0: */
	unsigned long ar_rnat;  /* RSE NaT */
	unsigned long ar_bspstore; /* RSE bspstore */

	unsigned long pr;  /* 64 predicate registers (1 bit each) */
	unsigned long b0;  /* return pointer (bp) */
	unsigned long loadrs;  /* size of dirty partition << 16 */

	unsigned long r1;  /* the gp pointer */
	unsigned long r12; /* interrupted task's memory stack pointer */
	unsigned long r13; /* thread pointer */

	unsigned long ar_fpsr;  /* floating point status (preserved) */
	unsigned long r15;  /* scratch */

	/* The remaining registers are NOT saved for system calls.  */
	unsigned long r14;  /* scratch */
	unsigned long r2;  /* scratch */
	unsigned long r3;  /* scratch */
	unsigned long r16;  /* scratch */
	unsigned long r17;  /* scratch */
	unsigned long r18;  /* scratch */
	unsigned long r19;  /* scratch */
	unsigned long r20;  /* scratch */
	unsigned long r21;  /* scratch */
	unsigned long r22;  /* scratch */
	unsigned long r23;  /* scratch */
	unsigned long r24;  /* scratch */
	unsigned long r25;  /* scratch */
	unsigned long r26;  /* scratch */
	unsigned long r27;  /* scratch */
	unsigned long r28;  /* scratch */
	unsigned long r29;  /* scratch */
	unsigned long r30;  /* scratch */
	unsigned long r31;  /* scratch */
	unsigned long ar_ccv;  /* compare/exchange value (scratch) */

	/*
	 * Floating point registers that the kernel considers scratch:
	 */
	struct ia64_fpreg f6;  /* scratch */
	struct ia64_fpreg f7;  /* scratch */
	struct ia64_fpreg f8;  /* scratch */
	struct ia64_fpreg f9;  /* scratch */
	struct ia64_fpreg f10;  /* scratch */
	struct ia64_fpreg f11;  /* scratch */

	unsigned long r4;  /* preserved */
	unsigned long r5;  /* preserved */
	unsigned long r6;  /* preserved */
	unsigned long r7;  /* preserved */
	unsigned long eml_unat;    /* used for emulating instruction */
	unsigned long pad0;     /* alignment pad */
};

static inline struct kvm_pt_regs *vcpu_regs(struct kvm_vcpu *v)
{
	return (struct kvm_pt_regs *) ((unsigned long) v + KVM_STK_OFFSET) - 1;
}

typedef int kvm_vmm_entry(void);
typedef void kvm_tramp_entry(union context *host, union context *guest);

struct kvm_vmm_info{
	struct module	*module;
	kvm_vmm_entry 	*vmm_entry;
	kvm_tramp_entry *tramp_entry;
	unsigned long 	vmm_ivt;
	unsigned long	patch_mov_ar;
	unsigned long	patch_mov_ar_sn2;
};

int kvm_highest_pending_irq(struct kvm_vcpu *vcpu);
int kvm_emulate_halt(struct kvm_vcpu *vcpu);
int kvm_pal_emul(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run);
void kvm_sal_emul(struct kvm_vcpu *vcpu);

#endif /* __ASSEMBLY__*/

#endif
