/*
* This file is subject to the terms and conditions of the GNU General Public
* License.  See the file "COPYING" in the main directory of this archive
* for more details.
*
* Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
* Authors: Sanjay Lal <sanjayl@kymasys.com>
*/

#ifndef __MIPS_KVM_HOST_H__
#define __MIPS_KVM_HOST_H__

#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/kvm.h>
#include <linux/kvm_types.h>
#include <linux/threads.h>
#include <linux/spinlock.h>

#include <asm/inst.h>
#include <asm/mipsregs.h>

/* MIPS KVM register ids */
#define MIPS_CP0_32(_R, _S)					\
	(KVM_REG_MIPS_CP0 | KVM_REG_SIZE_U32 | (8 * (_R) + (_S)))

#define MIPS_CP0_64(_R, _S)					\
	(KVM_REG_MIPS_CP0 | KVM_REG_SIZE_U64 | (8 * (_R) + (_S)))

#define KVM_REG_MIPS_CP0_INDEX		MIPS_CP0_32(0, 0)
#define KVM_REG_MIPS_CP0_ENTRYLO0	MIPS_CP0_64(2, 0)
#define KVM_REG_MIPS_CP0_ENTRYLO1	MIPS_CP0_64(3, 0)
#define KVM_REG_MIPS_CP0_CONTEXT	MIPS_CP0_64(4, 0)
#define KVM_REG_MIPS_CP0_USERLOCAL	MIPS_CP0_64(4, 2)
#define KVM_REG_MIPS_CP0_PAGEMASK	MIPS_CP0_32(5, 0)
#define KVM_REG_MIPS_CP0_PAGEGRAIN	MIPS_CP0_32(5, 1)
#define KVM_REG_MIPS_CP0_WIRED		MIPS_CP0_32(6, 0)
#define KVM_REG_MIPS_CP0_HWRENA		MIPS_CP0_32(7, 0)
#define KVM_REG_MIPS_CP0_BADVADDR	MIPS_CP0_64(8, 0)
#define KVM_REG_MIPS_CP0_COUNT		MIPS_CP0_32(9, 0)
#define KVM_REG_MIPS_CP0_ENTRYHI	MIPS_CP0_64(10, 0)
#define KVM_REG_MIPS_CP0_COMPARE	MIPS_CP0_32(11, 0)
#define KVM_REG_MIPS_CP0_STATUS		MIPS_CP0_32(12, 0)
#define KVM_REG_MIPS_CP0_INTCTL		MIPS_CP0_32(12, 1)
#define KVM_REG_MIPS_CP0_CAUSE		MIPS_CP0_32(13, 0)
#define KVM_REG_MIPS_CP0_EPC		MIPS_CP0_64(14, 0)
#define KVM_REG_MIPS_CP0_PRID		MIPS_CP0_32(15, 0)
#define KVM_REG_MIPS_CP0_EBASE		MIPS_CP0_64(15, 1)
#define KVM_REG_MIPS_CP0_CONFIG		MIPS_CP0_32(16, 0)
#define KVM_REG_MIPS_CP0_CONFIG1	MIPS_CP0_32(16, 1)
#define KVM_REG_MIPS_CP0_CONFIG2	MIPS_CP0_32(16, 2)
#define KVM_REG_MIPS_CP0_CONFIG3	MIPS_CP0_32(16, 3)
#define KVM_REG_MIPS_CP0_CONFIG4	MIPS_CP0_32(16, 4)
#define KVM_REG_MIPS_CP0_CONFIG5	MIPS_CP0_32(16, 5)
#define KVM_REG_MIPS_CP0_CONFIG7	MIPS_CP0_32(16, 7)
#define KVM_REG_MIPS_CP0_XCONTEXT	MIPS_CP0_64(20, 0)
#define KVM_REG_MIPS_CP0_ERROREPC	MIPS_CP0_64(30, 0)
#define KVM_REG_MIPS_CP0_KSCRATCH1	MIPS_CP0_64(31, 2)
#define KVM_REG_MIPS_CP0_KSCRATCH2	MIPS_CP0_64(31, 3)
#define KVM_REG_MIPS_CP0_KSCRATCH3	MIPS_CP0_64(31, 4)
#define KVM_REG_MIPS_CP0_KSCRATCH4	MIPS_CP0_64(31, 5)
#define KVM_REG_MIPS_CP0_KSCRATCH5	MIPS_CP0_64(31, 6)
#define KVM_REG_MIPS_CP0_KSCRATCH6	MIPS_CP0_64(31, 7)


#define KVM_MAX_VCPUS		8
#define KVM_USER_MEM_SLOTS	8
/* memory slots that does not exposed to userspace */
#define KVM_PRIVATE_MEM_SLOTS	0

#define KVM_COALESCED_MMIO_PAGE_OFFSET 1
#define KVM_HALT_POLL_NS_DEFAULT 500000



/*
 * Special address that contains the comm page, used for reducing # of traps
 * This needs to be within 32Kb of 0x0 (so the zero register can be used), but
 * preferably not at 0x0 so that most kernel NULL pointer dereferences can be
 * caught.
 */
#define KVM_GUEST_COMMPAGE_ADDR		((PAGE_SIZE > 0x8000) ?	0 : \
					 (0x8000 - PAGE_SIZE))

#define KVM_GUEST_KERNEL_MODE(vcpu)	((kvm_read_c0_guest_status(vcpu->arch.cop0) & (ST0_EXL | ST0_ERL)) || \
					((kvm_read_c0_guest_status(vcpu->arch.cop0) & KSU_USER) == 0))

#define KVM_GUEST_KUSEG			0x00000000UL
#define KVM_GUEST_KSEG0			0x40000000UL
#define KVM_GUEST_KSEG1			0x40000000UL
#define KVM_GUEST_KSEG23		0x60000000UL
#define KVM_GUEST_KSEGX(a)		((_ACAST32_(a)) & 0xe0000000)
#define KVM_GUEST_CPHYSADDR(a)		((_ACAST32_(a)) & 0x1fffffff)

#define KVM_GUEST_CKSEG0ADDR(a)		(KVM_GUEST_CPHYSADDR(a) | KVM_GUEST_KSEG0)
#define KVM_GUEST_CKSEG1ADDR(a)		(KVM_GUEST_CPHYSADDR(a) | KVM_GUEST_KSEG1)
#define KVM_GUEST_CKSEG23ADDR(a)	(KVM_GUEST_CPHYSADDR(a) | KVM_GUEST_KSEG23)

/*
 * Map an address to a certain kernel segment
 */
#define KVM_GUEST_KSEG0ADDR(a)		(KVM_GUEST_CPHYSADDR(a) | KVM_GUEST_KSEG0)
#define KVM_GUEST_KSEG1ADDR(a)		(KVM_GUEST_CPHYSADDR(a) | KVM_GUEST_KSEG1)
#define KVM_GUEST_KSEG23ADDR(a)		(KVM_GUEST_CPHYSADDR(a) | KVM_GUEST_KSEG23)

#define KVM_INVALID_PAGE		0xdeadbeef
#define KVM_INVALID_ADDR		0xdeadbeef

/*
 * EVA has overlapping user & kernel address spaces, so user VAs may be >
 * PAGE_OFFSET. For this reason we can't use the default KVM_HVA_ERR_BAD of
 * PAGE_OFFSET.
 */

#define KVM_HVA_ERR_BAD			(-1UL)
#define KVM_HVA_ERR_RO_BAD		(-2UL)

static inline bool kvm_is_error_hva(unsigned long addr)
{
	return IS_ERR_VALUE(addr);
}

struct kvm_vm_stat {
	ulong remote_tlb_flush;
};

struct kvm_vcpu_stat {
	u64 wait_exits;
	u64 cache_exits;
	u64 signal_exits;
	u64 int_exits;
	u64 cop_unusable_exits;
	u64 tlbmod_exits;
	u64 tlbmiss_ld_exits;
	u64 tlbmiss_st_exits;
	u64 addrerr_st_exits;
	u64 addrerr_ld_exits;
	u64 syscall_exits;
	u64 resvd_inst_exits;
	u64 break_inst_exits;
	u64 trap_inst_exits;
	u64 msa_fpe_exits;
	u64 fpe_exits;
	u64 msa_disabled_exits;
	u64 flush_dcache_exits;
	u64 halt_successful_poll;
	u64 halt_attempted_poll;
	u64 halt_poll_invalid;
	u64 halt_wakeup;
};

struct kvm_arch_memory_slot {
};

struct kvm_arch {
	/* Guest physical mm */
	struct mm_struct gpa_mm;
};

#define N_MIPS_COPROC_REGS	32
#define N_MIPS_COPROC_SEL	8

struct mips_coproc {
	unsigned long reg[N_MIPS_COPROC_REGS][N_MIPS_COPROC_SEL];
#ifdef CONFIG_KVM_MIPS_DEBUG_COP0_COUNTERS
	unsigned long stat[N_MIPS_COPROC_REGS][N_MIPS_COPROC_SEL];
#endif
};

/*
 * Coprocessor 0 register names
 */
#define MIPS_CP0_TLB_INDEX	0
#define MIPS_CP0_TLB_RANDOM	1
#define MIPS_CP0_TLB_LOW	2
#define MIPS_CP0_TLB_LO0	2
#define MIPS_CP0_TLB_LO1	3
#define MIPS_CP0_TLB_CONTEXT	4
#define MIPS_CP0_TLB_PG_MASK	5
#define MIPS_CP0_TLB_WIRED	6
#define MIPS_CP0_HWRENA		7
#define MIPS_CP0_BAD_VADDR	8
#define MIPS_CP0_COUNT		9
#define MIPS_CP0_TLB_HI		10
#define MIPS_CP0_COMPARE	11
#define MIPS_CP0_STATUS		12
#define MIPS_CP0_CAUSE		13
#define MIPS_CP0_EXC_PC		14
#define MIPS_CP0_PRID		15
#define MIPS_CP0_CONFIG		16
#define MIPS_CP0_LLADDR		17
#define MIPS_CP0_WATCH_LO	18
#define MIPS_CP0_WATCH_HI	19
#define MIPS_CP0_TLB_XCONTEXT	20
#define MIPS_CP0_ECC		26
#define MIPS_CP0_CACHE_ERR	27
#define MIPS_CP0_TAG_LO		28
#define MIPS_CP0_TAG_HI		29
#define MIPS_CP0_ERROR_PC	30
#define MIPS_CP0_DEBUG		23
#define MIPS_CP0_DEPC		24
#define MIPS_CP0_PERFCNT	25
#define MIPS_CP0_ERRCTL		26
#define MIPS_CP0_DATA_LO	28
#define MIPS_CP0_DATA_HI	29
#define MIPS_CP0_DESAVE		31

#define MIPS_CP0_CONFIG_SEL	0
#define MIPS_CP0_CONFIG1_SEL	1
#define MIPS_CP0_CONFIG2_SEL	2
#define MIPS_CP0_CONFIG3_SEL	3
#define MIPS_CP0_CONFIG4_SEL	4
#define MIPS_CP0_CONFIG5_SEL	5

/* Resume Flags */
#define RESUME_FLAG_DR		(1<<0)	/* Reload guest nonvolatile state? */
#define RESUME_FLAG_HOST	(1<<1)	/* Resume host? */

#define RESUME_GUEST		0
#define RESUME_GUEST_DR		RESUME_FLAG_DR
#define RESUME_HOST		RESUME_FLAG_HOST

enum emulation_result {
	EMULATE_DONE,		/* no further processing */
	EMULATE_DO_MMIO,	/* kvm_run filled with MMIO request */
	EMULATE_FAIL,		/* can't emulate this instruction */
	EMULATE_WAIT,		/* WAIT instruction */
	EMULATE_PRIV_FAIL,
	EMULATE_EXCEPT,		/* A guest exception has been generated */
};

#define mips3_paddr_to_tlbpfn(x) \
	(((unsigned long)(x) >> MIPS3_PG_SHIFT) & MIPS3_PG_FRAME)
#define mips3_tlbpfn_to_paddr(x) \
	((unsigned long)((x) & MIPS3_PG_FRAME) << MIPS3_PG_SHIFT)

#define MIPS3_PG_SHIFT		6
#define MIPS3_PG_FRAME		0x3fffffc0

#define VPN2_MASK		0xffffe000
#define KVM_ENTRYHI_ASID	MIPS_ENTRYHI_ASID
#define TLB_IS_GLOBAL(x)	((x).tlb_lo[0] & (x).tlb_lo[1] & ENTRYLO_G)
#define TLB_VPN2(x)		((x).tlb_hi & VPN2_MASK)
#define TLB_ASID(x)		((x).tlb_hi & KVM_ENTRYHI_ASID)
#define TLB_LO_IDX(x, va)	(((va) >> PAGE_SHIFT) & 1)
#define TLB_IS_VALID(x, va)	((x).tlb_lo[TLB_LO_IDX(x, va)] & ENTRYLO_V)
#define TLB_IS_DIRTY(x, va)	((x).tlb_lo[TLB_LO_IDX(x, va)] & ENTRYLO_D)
#define TLB_HI_VPN2_HIT(x, y)	((TLB_VPN2(x) & ~(x).tlb_mask) ==	\
				 ((y) & VPN2_MASK & ~(x).tlb_mask))
#define TLB_HI_ASID_HIT(x, y)	(TLB_IS_GLOBAL(x) ||			\
				 TLB_ASID(x) == ((y) & KVM_ENTRYHI_ASID))

struct kvm_mips_tlb {
	long tlb_mask;
	long tlb_hi;
	long tlb_lo[2];
};

#define KVM_NR_MEM_OBJS     4

/*
 * We don't want allocation failures within the mmu code, so we preallocate
 * enough memory for a single page fault in a cache.
 */
struct kvm_mmu_memory_cache {
	int nobjs;
	void *objects[KVM_NR_MEM_OBJS];
};

#define KVM_MIPS_AUX_FPU	0x1
#define KVM_MIPS_AUX_MSA	0x2

#define KVM_MIPS_GUEST_TLB_SIZE	64
struct kvm_vcpu_arch {
	void *guest_ebase;
	int (*vcpu_run)(struct kvm_run *run, struct kvm_vcpu *vcpu);
	unsigned long host_stack;
	unsigned long host_gp;

	/* Host CP0 registers used when handling exits from guest */
	unsigned long host_cp0_badvaddr;
	unsigned long host_cp0_epc;
	u32 host_cp0_cause;
	u32 host_cp0_badinstr;
	u32 host_cp0_badinstrp;

	/* GPRS */
	unsigned long gprs[32];
	unsigned long hi;
	unsigned long lo;
	unsigned long pc;

	/* FPU State */
	struct mips_fpu_struct fpu;
	/* Which auxiliary state is loaded (KVM_MIPS_AUX_*) */
	unsigned int aux_inuse;

	/* COP0 State */
	struct mips_coproc *cop0;

	/* Host KSEG0 address of the EI/DI offset */
	void *kseg0_commpage;

	/* Resume PC after MMIO completion */
	unsigned long io_pc;
	/* GPR used as IO source/target */
	u32 io_gpr;

	struct hrtimer comparecount_timer;
	/* Count timer control KVM register */
	u32 count_ctl;
	/* Count bias from the raw time */
	u32 count_bias;
	/* Frequency of timer in Hz */
	u32 count_hz;
	/* Dynamic nanosecond bias (multiple of count_period) to avoid overflow */
	s64 count_dyn_bias;
	/* Resume time */
	ktime_t count_resume;
	/* Period of timer tick in ns */
	u64 count_period;

	/* Bitmask of exceptions that are pending */
	unsigned long pending_exceptions;

	/* Bitmask of pending exceptions to be cleared */
	unsigned long pending_exceptions_clr;

	/* S/W Based TLB for guest */
	struct kvm_mips_tlb guest_tlb[KVM_MIPS_GUEST_TLB_SIZE];

	/* Guest kernel/user [partial] mm */
	struct mm_struct guest_kernel_mm, guest_user_mm;

	/* Guest ASID of last user mode execution */
	unsigned int last_user_gasid;

	/* Cache some mmu pages needed inside spinlock regions */
	struct kvm_mmu_memory_cache mmu_page_cache;

	int last_sched_cpu;

	/* WAIT executed */
	int wait;

	u8 fpu_enabled;
	u8 msa_enabled;
};


#define kvm_read_c0_guest_index(cop0)		(cop0->reg[MIPS_CP0_TLB_INDEX][0])
#define kvm_write_c0_guest_index(cop0, val)	(cop0->reg[MIPS_CP0_TLB_INDEX][0] = val)
#define kvm_read_c0_guest_entrylo0(cop0)	(cop0->reg[MIPS_CP0_TLB_LO0][0])
#define kvm_write_c0_guest_entrylo0(cop0, val)	(cop0->reg[MIPS_CP0_TLB_LO0][0] = (val))
#define kvm_read_c0_guest_entrylo1(cop0)	(cop0->reg[MIPS_CP0_TLB_LO1][0])
#define kvm_write_c0_guest_entrylo1(cop0, val)	(cop0->reg[MIPS_CP0_TLB_LO1][0] = (val))
#define kvm_read_c0_guest_context(cop0)		(cop0->reg[MIPS_CP0_TLB_CONTEXT][0])
#define kvm_write_c0_guest_context(cop0, val)	(cop0->reg[MIPS_CP0_TLB_CONTEXT][0] = (val))
#define kvm_read_c0_guest_userlocal(cop0)	(cop0->reg[MIPS_CP0_TLB_CONTEXT][2])
#define kvm_write_c0_guest_userlocal(cop0, val)	(cop0->reg[MIPS_CP0_TLB_CONTEXT][2] = (val))
#define kvm_read_c0_guest_pagemask(cop0)	(cop0->reg[MIPS_CP0_TLB_PG_MASK][0])
#define kvm_write_c0_guest_pagemask(cop0, val)	(cop0->reg[MIPS_CP0_TLB_PG_MASK][0] = (val))
#define kvm_read_c0_guest_wired(cop0)		(cop0->reg[MIPS_CP0_TLB_WIRED][0])
#define kvm_write_c0_guest_wired(cop0, val)	(cop0->reg[MIPS_CP0_TLB_WIRED][0] = (val))
#define kvm_read_c0_guest_hwrena(cop0)		(cop0->reg[MIPS_CP0_HWRENA][0])
#define kvm_write_c0_guest_hwrena(cop0, val)	(cop0->reg[MIPS_CP0_HWRENA][0] = (val))
#define kvm_read_c0_guest_badvaddr(cop0)	(cop0->reg[MIPS_CP0_BAD_VADDR][0])
#define kvm_write_c0_guest_badvaddr(cop0, val)	(cop0->reg[MIPS_CP0_BAD_VADDR][0] = (val))
#define kvm_read_c0_guest_count(cop0)		(cop0->reg[MIPS_CP0_COUNT][0])
#define kvm_write_c0_guest_count(cop0, val)	(cop0->reg[MIPS_CP0_COUNT][0] = (val))
#define kvm_read_c0_guest_entryhi(cop0)		(cop0->reg[MIPS_CP0_TLB_HI][0])
#define kvm_write_c0_guest_entryhi(cop0, val)	(cop0->reg[MIPS_CP0_TLB_HI][0] = (val))
#define kvm_read_c0_guest_compare(cop0)		(cop0->reg[MIPS_CP0_COMPARE][0])
#define kvm_write_c0_guest_compare(cop0, val)	(cop0->reg[MIPS_CP0_COMPARE][0] = (val))
#define kvm_read_c0_guest_status(cop0)		(cop0->reg[MIPS_CP0_STATUS][0])
#define kvm_write_c0_guest_status(cop0, val)	(cop0->reg[MIPS_CP0_STATUS][0] = (val))
#define kvm_read_c0_guest_intctl(cop0)		(cop0->reg[MIPS_CP0_STATUS][1])
#define kvm_write_c0_guest_intctl(cop0, val)	(cop0->reg[MIPS_CP0_STATUS][1] = (val))
#define kvm_read_c0_guest_cause(cop0)		(cop0->reg[MIPS_CP0_CAUSE][0])
#define kvm_write_c0_guest_cause(cop0, val)	(cop0->reg[MIPS_CP0_CAUSE][0] = (val))
#define kvm_read_c0_guest_epc(cop0)		(cop0->reg[MIPS_CP0_EXC_PC][0])
#define kvm_write_c0_guest_epc(cop0, val)	(cop0->reg[MIPS_CP0_EXC_PC][0] = (val))
#define kvm_read_c0_guest_prid(cop0)		(cop0->reg[MIPS_CP0_PRID][0])
#define kvm_write_c0_guest_prid(cop0, val)	(cop0->reg[MIPS_CP0_PRID][0] = (val))
#define kvm_read_c0_guest_ebase(cop0)		(cop0->reg[MIPS_CP0_PRID][1])
#define kvm_write_c0_guest_ebase(cop0, val)	(cop0->reg[MIPS_CP0_PRID][1] = (val))
#define kvm_read_c0_guest_config(cop0)		(cop0->reg[MIPS_CP0_CONFIG][0])
#define kvm_read_c0_guest_config1(cop0)		(cop0->reg[MIPS_CP0_CONFIG][1])
#define kvm_read_c0_guest_config2(cop0)		(cop0->reg[MIPS_CP0_CONFIG][2])
#define kvm_read_c0_guest_config3(cop0)		(cop0->reg[MIPS_CP0_CONFIG][3])
#define kvm_read_c0_guest_config4(cop0)		(cop0->reg[MIPS_CP0_CONFIG][4])
#define kvm_read_c0_guest_config5(cop0)		(cop0->reg[MIPS_CP0_CONFIG][5])
#define kvm_read_c0_guest_config7(cop0)		(cop0->reg[MIPS_CP0_CONFIG][7])
#define kvm_write_c0_guest_config(cop0, val)	(cop0->reg[MIPS_CP0_CONFIG][0] = (val))
#define kvm_write_c0_guest_config1(cop0, val)	(cop0->reg[MIPS_CP0_CONFIG][1] = (val))
#define kvm_write_c0_guest_config2(cop0, val)	(cop0->reg[MIPS_CP0_CONFIG][2] = (val))
#define kvm_write_c0_guest_config3(cop0, val)	(cop0->reg[MIPS_CP0_CONFIG][3] = (val))
#define kvm_write_c0_guest_config4(cop0, val)	(cop0->reg[MIPS_CP0_CONFIG][4] = (val))
#define kvm_write_c0_guest_config5(cop0, val)	(cop0->reg[MIPS_CP0_CONFIG][5] = (val))
#define kvm_write_c0_guest_config7(cop0, val)	(cop0->reg[MIPS_CP0_CONFIG][7] = (val))
#define kvm_read_c0_guest_errorepc(cop0)	(cop0->reg[MIPS_CP0_ERROR_PC][0])
#define kvm_write_c0_guest_errorepc(cop0, val)	(cop0->reg[MIPS_CP0_ERROR_PC][0] = (val))
#define kvm_read_c0_guest_kscratch1(cop0)	(cop0->reg[MIPS_CP0_DESAVE][2])
#define kvm_read_c0_guest_kscratch2(cop0)	(cop0->reg[MIPS_CP0_DESAVE][3])
#define kvm_read_c0_guest_kscratch3(cop0)	(cop0->reg[MIPS_CP0_DESAVE][4])
#define kvm_read_c0_guest_kscratch4(cop0)	(cop0->reg[MIPS_CP0_DESAVE][5])
#define kvm_read_c0_guest_kscratch5(cop0)	(cop0->reg[MIPS_CP0_DESAVE][6])
#define kvm_read_c0_guest_kscratch6(cop0)	(cop0->reg[MIPS_CP0_DESAVE][7])
#define kvm_write_c0_guest_kscratch1(cop0, val)	(cop0->reg[MIPS_CP0_DESAVE][2] = (val))
#define kvm_write_c0_guest_kscratch2(cop0, val)	(cop0->reg[MIPS_CP0_DESAVE][3] = (val))
#define kvm_write_c0_guest_kscratch3(cop0, val)	(cop0->reg[MIPS_CP0_DESAVE][4] = (val))
#define kvm_write_c0_guest_kscratch4(cop0, val)	(cop0->reg[MIPS_CP0_DESAVE][5] = (val))
#define kvm_write_c0_guest_kscratch5(cop0, val)	(cop0->reg[MIPS_CP0_DESAVE][6] = (val))
#define kvm_write_c0_guest_kscratch6(cop0, val)	(cop0->reg[MIPS_CP0_DESAVE][7] = (val))

/*
 * Some of the guest registers may be modified asynchronously (e.g. from a
 * hrtimer callback in hard irq context) and therefore need stronger atomicity
 * guarantees than other registers.
 */

static inline void _kvm_atomic_set_c0_guest_reg(unsigned long *reg,
						unsigned long val)
{
	unsigned long temp;
	do {
		__asm__ __volatile__(
		"	.set	"MIPS_ISA_ARCH_LEVEL"		\n"
		"	" __LL "%0, %1				\n"
		"	or	%0, %2				\n"
		"	" __SC	"%0, %1				\n"
		"	.set	mips0				\n"
		: "=&r" (temp), "+m" (*reg)
		: "r" (val));
	} while (unlikely(!temp));
}

static inline void _kvm_atomic_clear_c0_guest_reg(unsigned long *reg,
						  unsigned long val)
{
	unsigned long temp;
	do {
		__asm__ __volatile__(
		"	.set	"MIPS_ISA_ARCH_LEVEL"		\n"
		"	" __LL "%0, %1				\n"
		"	and	%0, %2				\n"
		"	" __SC	"%0, %1				\n"
		"	.set	mips0				\n"
		: "=&r" (temp), "+m" (*reg)
		: "r" (~val));
	} while (unlikely(!temp));
}

static inline void _kvm_atomic_change_c0_guest_reg(unsigned long *reg,
						   unsigned long change,
						   unsigned long val)
{
	unsigned long temp;
	do {
		__asm__ __volatile__(
		"	.set	"MIPS_ISA_ARCH_LEVEL"		\n"
		"	" __LL "%0, %1				\n"
		"	and	%0, %2				\n"
		"	or	%0, %3				\n"
		"	" __SC	"%0, %1				\n"
		"	.set	mips0				\n"
		: "=&r" (temp), "+m" (*reg)
		: "r" (~change), "r" (val & change));
	} while (unlikely(!temp));
}

#define kvm_set_c0_guest_status(cop0, val)	(cop0->reg[MIPS_CP0_STATUS][0] |= (val))
#define kvm_clear_c0_guest_status(cop0, val)	(cop0->reg[MIPS_CP0_STATUS][0] &= ~(val))

/* Cause can be modified asynchronously from hardirq hrtimer callback */
#define kvm_set_c0_guest_cause(cop0, val)				\
	_kvm_atomic_set_c0_guest_reg(&cop0->reg[MIPS_CP0_CAUSE][0], val)
#define kvm_clear_c0_guest_cause(cop0, val)				\
	_kvm_atomic_clear_c0_guest_reg(&cop0->reg[MIPS_CP0_CAUSE][0], val)
#define kvm_change_c0_guest_cause(cop0, change, val)			\
	_kvm_atomic_change_c0_guest_reg(&cop0->reg[MIPS_CP0_CAUSE][0],	\
					change, val)

#define kvm_set_c0_guest_ebase(cop0, val)	(cop0->reg[MIPS_CP0_PRID][1] |= (val))
#define kvm_clear_c0_guest_ebase(cop0, val)	(cop0->reg[MIPS_CP0_PRID][1] &= ~(val))
#define kvm_change_c0_guest_ebase(cop0, change, val)			\
{									\
	kvm_clear_c0_guest_ebase(cop0, change);				\
	kvm_set_c0_guest_ebase(cop0, ((val) & (change)));		\
}

/* Helpers */

static inline bool kvm_mips_guest_can_have_fpu(struct kvm_vcpu_arch *vcpu)
{
	return (!__builtin_constant_p(raw_cpu_has_fpu) || raw_cpu_has_fpu) &&
		vcpu->fpu_enabled;
}

static inline bool kvm_mips_guest_has_fpu(struct kvm_vcpu_arch *vcpu)
{
	return kvm_mips_guest_can_have_fpu(vcpu) &&
		kvm_read_c0_guest_config1(vcpu->cop0) & MIPS_CONF1_FP;
}

static inline bool kvm_mips_guest_can_have_msa(struct kvm_vcpu_arch *vcpu)
{
	return (!__builtin_constant_p(cpu_has_msa) || cpu_has_msa) &&
		vcpu->msa_enabled;
}

static inline bool kvm_mips_guest_has_msa(struct kvm_vcpu_arch *vcpu)
{
	return kvm_mips_guest_can_have_msa(vcpu) &&
		kvm_read_c0_guest_config3(vcpu->cop0) & MIPS_CONF3_MSA;
}

struct kvm_mips_callbacks {
	int (*handle_cop_unusable)(struct kvm_vcpu *vcpu);
	int (*handle_tlb_mod)(struct kvm_vcpu *vcpu);
	int (*handle_tlb_ld_miss)(struct kvm_vcpu *vcpu);
	int (*handle_tlb_st_miss)(struct kvm_vcpu *vcpu);
	int (*handle_addr_err_st)(struct kvm_vcpu *vcpu);
	int (*handle_addr_err_ld)(struct kvm_vcpu *vcpu);
	int (*handle_syscall)(struct kvm_vcpu *vcpu);
	int (*handle_res_inst)(struct kvm_vcpu *vcpu);
	int (*handle_break)(struct kvm_vcpu *vcpu);
	int (*handle_trap)(struct kvm_vcpu *vcpu);
	int (*handle_msa_fpe)(struct kvm_vcpu *vcpu);
	int (*handle_fpe)(struct kvm_vcpu *vcpu);
	int (*handle_msa_disabled)(struct kvm_vcpu *vcpu);
	int (*vcpu_init)(struct kvm_vcpu *vcpu);
	void (*vcpu_uninit)(struct kvm_vcpu *vcpu);
	int (*vcpu_setup)(struct kvm_vcpu *vcpu);
	void (*flush_shadow_all)(struct kvm *kvm);
	/*
	 * Must take care of flushing any cached GPA PTEs (e.g. guest entries in
	 * VZ root TLB, or T&E GVA page tables and corresponding root TLB
	 * mappings).
	 */
	void (*flush_shadow_memslot)(struct kvm *kvm,
				     const struct kvm_memory_slot *slot);
	gpa_t (*gva_to_gpa)(gva_t gva);
	void (*queue_timer_int)(struct kvm_vcpu *vcpu);
	void (*dequeue_timer_int)(struct kvm_vcpu *vcpu);
	void (*queue_io_int)(struct kvm_vcpu *vcpu,
			     struct kvm_mips_interrupt *irq);
	void (*dequeue_io_int)(struct kvm_vcpu *vcpu,
			       struct kvm_mips_interrupt *irq);
	int (*irq_deliver)(struct kvm_vcpu *vcpu, unsigned int priority,
			   u32 cause);
	int (*irq_clear)(struct kvm_vcpu *vcpu, unsigned int priority,
			 u32 cause);
	unsigned long (*num_regs)(struct kvm_vcpu *vcpu);
	int (*copy_reg_indices)(struct kvm_vcpu *vcpu, u64 __user *indices);
	int (*get_one_reg)(struct kvm_vcpu *vcpu,
			   const struct kvm_one_reg *reg, s64 *v);
	int (*set_one_reg)(struct kvm_vcpu *vcpu,
			   const struct kvm_one_reg *reg, s64 v);
	int (*vcpu_load)(struct kvm_vcpu *vcpu, int cpu);
	int (*vcpu_put)(struct kvm_vcpu *vcpu, int cpu);
	int (*vcpu_run)(struct kvm_run *run, struct kvm_vcpu *vcpu);
	void (*vcpu_reenter)(struct kvm_run *run, struct kvm_vcpu *vcpu);
};
extern struct kvm_mips_callbacks *kvm_mips_callbacks;
int kvm_mips_emulation_init(struct kvm_mips_callbacks **install_callbacks);

/* Debug: dump vcpu state */
int kvm_arch_vcpu_dump_regs(struct kvm_vcpu *vcpu);

extern int kvm_mips_handle_exit(struct kvm_run *run, struct kvm_vcpu *vcpu);

/* Building of entry/exception code */
int kvm_mips_entry_setup(void);
void *kvm_mips_build_vcpu_run(void *addr);
void *kvm_mips_build_tlb_refill_exception(void *addr, void *handler);
void *kvm_mips_build_exception(void *addr, void *handler);
void *kvm_mips_build_exit(void *addr);

/* FPU/MSA context management */
void __kvm_save_fpu(struct kvm_vcpu_arch *vcpu);
void __kvm_restore_fpu(struct kvm_vcpu_arch *vcpu);
void __kvm_restore_fcsr(struct kvm_vcpu_arch *vcpu);
void __kvm_save_msa(struct kvm_vcpu_arch *vcpu);
void __kvm_restore_msa(struct kvm_vcpu_arch *vcpu);
void __kvm_restore_msa_upper(struct kvm_vcpu_arch *vcpu);
void __kvm_restore_msacsr(struct kvm_vcpu_arch *vcpu);
void kvm_own_fpu(struct kvm_vcpu *vcpu);
void kvm_own_msa(struct kvm_vcpu *vcpu);
void kvm_drop_fpu(struct kvm_vcpu *vcpu);
void kvm_lose_fpu(struct kvm_vcpu *vcpu);

/* TLB handling */
u32 kvm_get_kernel_asid(struct kvm_vcpu *vcpu);

u32 kvm_get_user_asid(struct kvm_vcpu *vcpu);

u32 kvm_get_commpage_asid (struct kvm_vcpu *vcpu);

extern int kvm_mips_handle_kseg0_tlb_fault(unsigned long badbaddr,
					   struct kvm_vcpu *vcpu,
					   bool write_fault);

extern int kvm_mips_handle_commpage_tlb_fault(unsigned long badvaddr,
					      struct kvm_vcpu *vcpu);

extern int kvm_mips_handle_mapped_seg_tlb_fault(struct kvm_vcpu *vcpu,
						struct kvm_mips_tlb *tlb,
						unsigned long gva,
						bool write_fault);

extern enum emulation_result kvm_mips_handle_tlbmiss(u32 cause,
						     u32 *opc,
						     struct kvm_run *run,
						     struct kvm_vcpu *vcpu,
						     bool write_fault);

extern void kvm_mips_dump_host_tlbs(void);
extern void kvm_mips_dump_guest_tlbs(struct kvm_vcpu *vcpu);
extern int kvm_mips_host_tlb_inv(struct kvm_vcpu *vcpu, unsigned long entryhi,
				 bool user, bool kernel);

extern int kvm_mips_guest_tlb_lookup(struct kvm_vcpu *vcpu,
				     unsigned long entryhi);

void kvm_mips_suspend_mm(int cpu);
void kvm_mips_resume_mm(int cpu);

/* MMU handling */

/**
 * enum kvm_mips_flush - Types of MMU flushes.
 * @KMF_USER:	Flush guest user virtual memory mappings.
 *		Guest USeg only.
 * @KMF_KERN:	Flush guest kernel virtual memory mappings.
 *		Guest USeg and KSeg2/3.
 * @KMF_GPA:	Flush guest physical memory mappings.
 *		Also includes KSeg0 if KMF_KERN is set.
 */
enum kvm_mips_flush {
	KMF_USER	= 0x0,
	KMF_KERN	= 0x1,
	KMF_GPA		= 0x2,
};
void kvm_mips_flush_gva_pt(pgd_t *pgd, enum kvm_mips_flush flags);
bool kvm_mips_flush_gpa_pt(struct kvm *kvm, gfn_t start_gfn, gfn_t end_gfn);
int kvm_mips_mkclean_gpa_pt(struct kvm *kvm, gfn_t start_gfn, gfn_t end_gfn);
pgd_t *kvm_pgd_alloc(void);
void kvm_mmu_free_memory_caches(struct kvm_vcpu *vcpu);
void kvm_trap_emul_invalidate_gva(struct kvm_vcpu *vcpu, unsigned long addr,
				  bool user);
void kvm_trap_emul_gva_lockless_begin(struct kvm_vcpu *vcpu);
void kvm_trap_emul_gva_lockless_end(struct kvm_vcpu *vcpu);

enum kvm_mips_fault_result {
	KVM_MIPS_MAPPED = 0,
	KVM_MIPS_GVA,
	KVM_MIPS_GPA,
	KVM_MIPS_TLB,
	KVM_MIPS_TLBINV,
	KVM_MIPS_TLBMOD,
};
enum kvm_mips_fault_result kvm_trap_emul_gva_fault(struct kvm_vcpu *vcpu,
						   unsigned long gva,
						   bool write);

#define KVM_ARCH_WANT_MMU_NOTIFIER
int kvm_unmap_hva(struct kvm *kvm, unsigned long hva);
int kvm_unmap_hva_range(struct kvm *kvm,
			unsigned long start, unsigned long end);
void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte);
int kvm_age_hva(struct kvm *kvm, unsigned long start, unsigned long end);
int kvm_test_age_hva(struct kvm *kvm, unsigned long hva);

static inline void kvm_arch_mmu_notifier_invalidate_page(struct kvm *kvm,
							 unsigned long address)
{
}

/* Emulation */
int kvm_get_inst(u32 *opc, struct kvm_vcpu *vcpu, u32 *out);
enum emulation_result update_pc(struct kvm_vcpu *vcpu, u32 cause);
int kvm_get_badinstr(u32 *opc, struct kvm_vcpu *vcpu, u32 *out);
int kvm_get_badinstrp(u32 *opc, struct kvm_vcpu *vcpu, u32 *out);

/**
 * kvm_is_ifetch_fault() - Find whether a TLBL exception is due to ifetch fault.
 * @vcpu:	Virtual CPU.
 *
 * Returns:	Whether the TLBL exception was likely due to an instruction
 *		fetch fault rather than a data load fault.
 */
static inline bool kvm_is_ifetch_fault(struct kvm_vcpu_arch *vcpu)
{
	unsigned long badvaddr = vcpu->host_cp0_badvaddr;
	unsigned long epc = msk_isa16_mode(vcpu->pc);
	u32 cause = vcpu->host_cp0_cause;

	if (epc == badvaddr)
		return true;

	/*
	 * Branches may be 32-bit or 16-bit instructions.
	 * This isn't exact, but we don't really support MIPS16 or microMIPS yet
	 * in KVM anyway.
	 */
	if ((cause & CAUSEF_BD) && badvaddr - epc <= 4)
		return true;

	return false;
}

extern enum emulation_result kvm_mips_emulate_inst(u32 cause,
						   u32 *opc,
						   struct kvm_run *run,
						   struct kvm_vcpu *vcpu);

long kvm_mips_guest_exception_base(struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_emulate_syscall(u32 cause,
						      u32 *opc,
						      struct kvm_run *run,
						      struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_emulate_tlbmiss_ld(u32 cause,
							 u32 *opc,
							 struct kvm_run *run,
							 struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_emulate_tlbinv_ld(u32 cause,
							u32 *opc,
							struct kvm_run *run,
							struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_emulate_tlbmiss_st(u32 cause,
							 u32 *opc,
							 struct kvm_run *run,
							 struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_emulate_tlbinv_st(u32 cause,
							u32 *opc,
							struct kvm_run *run,
							struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_emulate_tlbmod(u32 cause,
						     u32 *opc,
						     struct kvm_run *run,
						     struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_emulate_fpu_exc(u32 cause,
						      u32 *opc,
						      struct kvm_run *run,
						      struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_handle_ri(u32 cause,
						u32 *opc,
						struct kvm_run *run,
						struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_emulate_ri_exc(u32 cause,
						     u32 *opc,
						     struct kvm_run *run,
						     struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_emulate_bp_exc(u32 cause,
						     u32 *opc,
						     struct kvm_run *run,
						     struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_emulate_trap_exc(u32 cause,
						       u32 *opc,
						       struct kvm_run *run,
						       struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_emulate_msafpe_exc(u32 cause,
							 u32 *opc,
							 struct kvm_run *run,
							 struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_emulate_fpe_exc(u32 cause,
						      u32 *opc,
						      struct kvm_run *run,
						      struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_emulate_msadis_exc(u32 cause,
							 u32 *opc,
							 struct kvm_run *run,
							 struct kvm_vcpu *vcpu);

extern enum emulation_result kvm_mips_complete_mmio_load(struct kvm_vcpu *vcpu,
							 struct kvm_run *run);

u32 kvm_mips_read_count(struct kvm_vcpu *vcpu);
void kvm_mips_write_count(struct kvm_vcpu *vcpu, u32 count);
void kvm_mips_write_compare(struct kvm_vcpu *vcpu, u32 compare, bool ack);
void kvm_mips_init_count(struct kvm_vcpu *vcpu);
int kvm_mips_set_count_ctl(struct kvm_vcpu *vcpu, s64 count_ctl);
int kvm_mips_set_count_resume(struct kvm_vcpu *vcpu, s64 count_resume);
int kvm_mips_set_count_hz(struct kvm_vcpu *vcpu, s64 count_hz);
void kvm_mips_count_enable_cause(struct kvm_vcpu *vcpu);
void kvm_mips_count_disable_cause(struct kvm_vcpu *vcpu);
enum hrtimer_restart kvm_mips_count_timeout(struct kvm_vcpu *vcpu);

enum emulation_result kvm_mips_check_privilege(u32 cause,
					       u32 *opc,
					       struct kvm_run *run,
					       struct kvm_vcpu *vcpu);

enum emulation_result kvm_mips_emulate_cache(union mips_instruction inst,
					     u32 *opc,
					     u32 cause,
					     struct kvm_run *run,
					     struct kvm_vcpu *vcpu);
enum emulation_result kvm_mips_emulate_CP0(union mips_instruction inst,
					   u32 *opc,
					   u32 cause,
					   struct kvm_run *run,
					   struct kvm_vcpu *vcpu);
enum emulation_result kvm_mips_emulate_store(union mips_instruction inst,
					     u32 cause,
					     struct kvm_run *run,
					     struct kvm_vcpu *vcpu);
enum emulation_result kvm_mips_emulate_load(union mips_instruction inst,
					    u32 cause,
					    struct kvm_run *run,
					    struct kvm_vcpu *vcpu);

unsigned int kvm_mips_config1_wrmask(struct kvm_vcpu *vcpu);
unsigned int kvm_mips_config3_wrmask(struct kvm_vcpu *vcpu);
unsigned int kvm_mips_config4_wrmask(struct kvm_vcpu *vcpu);
unsigned int kvm_mips_config5_wrmask(struct kvm_vcpu *vcpu);

/* Dynamic binary translation */
extern int kvm_mips_trans_cache_index(union mips_instruction inst,
				      u32 *opc, struct kvm_vcpu *vcpu);
extern int kvm_mips_trans_cache_va(union mips_instruction inst, u32 *opc,
				   struct kvm_vcpu *vcpu);
extern int kvm_mips_trans_mfc0(union mips_instruction inst, u32 *opc,
			       struct kvm_vcpu *vcpu);
extern int kvm_mips_trans_mtc0(union mips_instruction inst, u32 *opc,
			       struct kvm_vcpu *vcpu);

/* Misc */
extern void kvm_mips_dump_stats(struct kvm_vcpu *vcpu);
extern unsigned long kvm_mips_get_ramsize(struct kvm *kvm);

static inline void kvm_arch_hardware_disable(void) {}
static inline void kvm_arch_hardware_unsetup(void) {}
static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_free_memslot(struct kvm *kvm,
		struct kvm_memory_slot *free, struct kvm_memory_slot *dont) {}
static inline void kvm_arch_memslots_updated(struct kvm *kvm, struct kvm_memslots *slots) {}
static inline void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu) {}
static inline void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_vcpu_block_finish(struct kvm_vcpu *vcpu) {}

#endif /* __MIPS_KVM_HOST_H__ */
