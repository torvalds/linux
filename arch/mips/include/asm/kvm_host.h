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

#include <linux/cpumask.h>
#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/kvm.h>
#include <linux/kvm_types.h>
#include <linux/threads.h>
#include <linux/spinlock.h>

#include <asm/asm.h>
#include <asm/inst.h>
#include <asm/mipsregs.h>

#include <kvm/iodev.h>

/* MIPS KVM register ids */
#define MIPS_CP0_32(_R, _S)					\
	(KVM_REG_MIPS_CP0 | KVM_REG_SIZE_U32 | (8 * (_R) + (_S)))

#define MIPS_CP0_64(_R, _S)					\
	(KVM_REG_MIPS_CP0 | KVM_REG_SIZE_U64 | (8 * (_R) + (_S)))

#define KVM_REG_MIPS_CP0_INDEX		MIPS_CP0_32(0, 0)
#define KVM_REG_MIPS_CP0_ENTRYLO0	MIPS_CP0_64(2, 0)
#define KVM_REG_MIPS_CP0_ENTRYLO1	MIPS_CP0_64(3, 0)
#define KVM_REG_MIPS_CP0_CONTEXT	MIPS_CP0_64(4, 0)
#define KVM_REG_MIPS_CP0_CONTEXTCONFIG	MIPS_CP0_32(4, 1)
#define KVM_REG_MIPS_CP0_USERLOCAL	MIPS_CP0_64(4, 2)
#define KVM_REG_MIPS_CP0_XCONTEXTCONFIG	MIPS_CP0_64(4, 3)
#define KVM_REG_MIPS_CP0_PAGEMASK	MIPS_CP0_32(5, 0)
#define KVM_REG_MIPS_CP0_PAGEGRAIN	MIPS_CP0_32(5, 1)
#define KVM_REG_MIPS_CP0_SEGCTL0	MIPS_CP0_64(5, 2)
#define KVM_REG_MIPS_CP0_SEGCTL1	MIPS_CP0_64(5, 3)
#define KVM_REG_MIPS_CP0_SEGCTL2	MIPS_CP0_64(5, 4)
#define KVM_REG_MIPS_CP0_PWBASE		MIPS_CP0_64(5, 5)
#define KVM_REG_MIPS_CP0_PWFIELD	MIPS_CP0_64(5, 6)
#define KVM_REG_MIPS_CP0_PWSIZE		MIPS_CP0_64(5, 7)
#define KVM_REG_MIPS_CP0_WIRED		MIPS_CP0_32(6, 0)
#define KVM_REG_MIPS_CP0_PWCTL		MIPS_CP0_32(6, 6)
#define KVM_REG_MIPS_CP0_HWRENA		MIPS_CP0_32(7, 0)
#define KVM_REG_MIPS_CP0_BADVADDR	MIPS_CP0_64(8, 0)
#define KVM_REG_MIPS_CP0_BADINSTR	MIPS_CP0_32(8, 1)
#define KVM_REG_MIPS_CP0_BADINSTRP	MIPS_CP0_32(8, 2)
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
#define KVM_REG_MIPS_CP0_CONFIG6	MIPS_CP0_32(16, 6)
#define KVM_REG_MIPS_CP0_CONFIG7	MIPS_CP0_32(16, 7)
#define KVM_REG_MIPS_CP0_MAARI		MIPS_CP0_64(17, 2)
#define KVM_REG_MIPS_CP0_XCONTEXT	MIPS_CP0_64(20, 0)
#define KVM_REG_MIPS_CP0_DIAG		MIPS_CP0_32(22, 0)
#define KVM_REG_MIPS_CP0_ERROREPC	MIPS_CP0_64(30, 0)
#define KVM_REG_MIPS_CP0_KSCRATCH1	MIPS_CP0_64(31, 2)
#define KVM_REG_MIPS_CP0_KSCRATCH2	MIPS_CP0_64(31, 3)
#define KVM_REG_MIPS_CP0_KSCRATCH3	MIPS_CP0_64(31, 4)
#define KVM_REG_MIPS_CP0_KSCRATCH4	MIPS_CP0_64(31, 5)
#define KVM_REG_MIPS_CP0_KSCRATCH5	MIPS_CP0_64(31, 6)
#define KVM_REG_MIPS_CP0_KSCRATCH6	MIPS_CP0_64(31, 7)


#define KVM_MAX_VCPUS		16

#define KVM_HALT_POLL_NS_DEFAULT 500000

extern unsigned long GUESTID_MASK;
extern unsigned long GUESTID_FIRST_VERSION;
extern unsigned long GUESTID_VERSION_MASK;

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
	struct kvm_vm_stat_generic generic;
};

struct kvm_vcpu_stat {
	struct kvm_vcpu_stat_generic generic;
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
	u64 vz_gpsi_exits;
	u64 vz_gsfc_exits;
	u64 vz_hc_exits;
	u64 vz_grr_exits;
	u64 vz_gva_exits;
	u64 vz_ghfc_exits;
	u64 vz_gpa_exits;
	u64 vz_resvd_exits;
#ifdef CONFIG_CPU_LOONGSON64
	u64 vz_cpucfg_exits;
#endif
};

struct kvm_arch_memory_slot {
};

#ifdef CONFIG_CPU_LOONGSON64
struct ipi_state {
	uint32_t status;
	uint32_t en;
	uint32_t set;
	uint32_t clear;
	uint64_t buf[4];
};

struct loongson_kvm_ipi;

struct ipi_io_device {
	int node_id;
	struct loongson_kvm_ipi *ipi;
	struct kvm_io_device device;
};

struct loongson_kvm_ipi {
	spinlock_t lock;
	struct kvm *kvm;
	struct ipi_state ipistate[16];
	struct ipi_io_device dev_ipi[4];
};
#endif

struct kvm_arch {
	/* Guest physical mm */
	struct mm_struct gpa_mm;
	/* Mask of CPUs needing GPA ASID flush */
	cpumask_t asid_flush_mask;
#ifdef CONFIG_CPU_LOONGSON64
	struct loongson_kvm_ipi ipi;
#endif
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
#define MIPS_CP0_DIAG		22
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

#define MIPS_CP0_GUESTCTL2	10
#define MIPS_CP0_GUESTCTL2_SEL	5
#define MIPS_CP0_GTOFFSET	12
#define MIPS_CP0_GTOFFSET_SEL	7

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
	EMULATE_HYPERCALL,	/* HYPCALL instruction */
};

#if defined(CONFIG_64BIT)
#define VPN2_MASK		GENMASK(cpu_vmbits - 1, 13)
#else
#define VPN2_MASK		0xffffe000
#endif
#define KVM_ENTRYHI_ASID	cpu_asid_mask(&boot_cpu_data)
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

#define KVM_MIPS_AUX_FPU	0x1
#define KVM_MIPS_AUX_MSA	0x2

struct kvm_vcpu_arch {
	void *guest_ebase;
	int (*vcpu_run)(struct kvm_vcpu *vcpu);

	/* Host registers preserved across guest mode execution */
	unsigned long host_stack;
	unsigned long host_gp;
	unsigned long host_pgd;
	unsigned long host_entryhi;

	/* Host CP0 registers used when handling exits from guest */
	unsigned long host_cp0_badvaddr;
	unsigned long host_cp0_epc;
	u32 host_cp0_cause;
	u32 host_cp0_guestctl0;
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

	/* Cache some mmu pages needed inside spinlock regions */
	struct kvm_mmu_memory_cache mmu_page_cache;

	/* vcpu's vzguestid is different on each host cpu in an smp system */
	u32 vzguestid[NR_CPUS];

	/* wired guest TLB entries */
	struct kvm_mips_tlb *wired_tlb;
	unsigned int wired_tlb_limit;
	unsigned int wired_tlb_used;

	/* emulated guest MAAR registers */
	unsigned long maar[6];

	/* Last CPU the VCPU state was loaded on */
	int last_sched_cpu;
	/* Last CPU the VCPU actually executed guest code on */
	int last_exec_cpu;

	/* WAIT executed */
	int wait;

	u8 fpu_enabled;
	u8 msa_enabled;
};

static inline void _kvm_atomic_set_c0_guest_reg(unsigned long *reg,
						unsigned long val)
{
	unsigned long temp;
	do {
		__asm__ __volatile__(
		"	.set	push				\n"
		"	.set	"MIPS_ISA_ARCH_LEVEL"		\n"
		"	"__stringify(LONG_LL)	" %0, %1	\n"
		"	or	%0, %2				\n"
		"	"__stringify(LONG_SC)	" %0, %1	\n"
		"	.set	pop				\n"
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
		"	.set	push				\n"
		"	.set	"MIPS_ISA_ARCH_LEVEL"		\n"
		"	"__stringify(LONG_LL)	" %0, %1	\n"
		"	and	%0, %2				\n"
		"	"__stringify(LONG_SC)	" %0, %1	\n"
		"	.set	pop				\n"
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
		"	.set	push				\n"
		"	.set	"MIPS_ISA_ARCH_LEVEL"		\n"
		"	"__stringify(LONG_LL)	" %0, %1	\n"
		"	and	%0, %2				\n"
		"	or	%0, %3				\n"
		"	"__stringify(LONG_SC)	" %0, %1	\n"
		"	.set	pop				\n"
		: "=&r" (temp), "+m" (*reg)
		: "r" (~change), "r" (val & change));
	} while (unlikely(!temp));
}

/* Guest register types, used in accessor build below */
#define __KVMT32	u32
#define __KVMTl	unsigned long

/*
 * __BUILD_KVM_$ops_SAVED(): kvm_$op_sw_gc0_$reg()
 * These operate on the saved guest C0 state in RAM.
 */

/* Generate saved context simple accessors */
#define __BUILD_KVM_RW_SAVED(name, type, _reg, sel)			\
static inline __KVMT##type kvm_read_sw_gc0_##name(struct mips_coproc *cop0) \
{									\
	return cop0->reg[(_reg)][(sel)];				\
}									\
static inline void kvm_write_sw_gc0_##name(struct mips_coproc *cop0,	\
					   __KVMT##type val)		\
{									\
	cop0->reg[(_reg)][(sel)] = val;					\
}

/* Generate saved context bitwise modifiers */
#define __BUILD_KVM_SET_SAVED(name, type, _reg, sel)			\
static inline void kvm_set_sw_gc0_##name(struct mips_coproc *cop0,	\
					 __KVMT##type val)		\
{									\
	cop0->reg[(_reg)][(sel)] |= val;				\
}									\
static inline void kvm_clear_sw_gc0_##name(struct mips_coproc *cop0,	\
					   __KVMT##type val)		\
{									\
	cop0->reg[(_reg)][(sel)] &= ~val;				\
}									\
static inline void kvm_change_sw_gc0_##name(struct mips_coproc *cop0,	\
					    __KVMT##type mask,		\
					    __KVMT##type val)		\
{									\
	unsigned long _mask = mask;					\
	cop0->reg[(_reg)][(sel)] &= ~_mask;				\
	cop0->reg[(_reg)][(sel)] |= val & _mask;			\
}

/* Generate saved context atomic bitwise modifiers */
#define __BUILD_KVM_ATOMIC_SAVED(name, type, _reg, sel)			\
static inline void kvm_set_sw_gc0_##name(struct mips_coproc *cop0,	\
					 __KVMT##type val)		\
{									\
	_kvm_atomic_set_c0_guest_reg(&cop0->reg[(_reg)][(sel)], val);	\
}									\
static inline void kvm_clear_sw_gc0_##name(struct mips_coproc *cop0,	\
					   __KVMT##type val)		\
{									\
	_kvm_atomic_clear_c0_guest_reg(&cop0->reg[(_reg)][(sel)], val);	\
}									\
static inline void kvm_change_sw_gc0_##name(struct mips_coproc *cop0,	\
					    __KVMT##type mask,		\
					    __KVMT##type val)		\
{									\
	_kvm_atomic_change_c0_guest_reg(&cop0->reg[(_reg)][(sel)], mask, \
					val);				\
}

/*
 * __BUILD_KVM_$ops_VZ(): kvm_$op_vz_gc0_$reg()
 * These operate on the VZ guest C0 context in hardware.
 */

/* Generate VZ guest context simple accessors */
#define __BUILD_KVM_RW_VZ(name, type, _reg, sel)			\
static inline __KVMT##type kvm_read_vz_gc0_##name(struct mips_coproc *cop0) \
{									\
	return read_gc0_##name();					\
}									\
static inline void kvm_write_vz_gc0_##name(struct mips_coproc *cop0,	\
					   __KVMT##type val)		\
{									\
	write_gc0_##name(val);						\
}

/* Generate VZ guest context bitwise modifiers */
#define __BUILD_KVM_SET_VZ(name, type, _reg, sel)			\
static inline void kvm_set_vz_gc0_##name(struct mips_coproc *cop0,	\
					 __KVMT##type val)		\
{									\
	set_gc0_##name(val);						\
}									\
static inline void kvm_clear_vz_gc0_##name(struct mips_coproc *cop0,	\
					   __KVMT##type val)		\
{									\
	clear_gc0_##name(val);						\
}									\
static inline void kvm_change_vz_gc0_##name(struct mips_coproc *cop0,	\
					    __KVMT##type mask,		\
					    __KVMT##type val)		\
{									\
	change_gc0_##name(mask, val);					\
}

/* Generate VZ guest context save/restore to/from saved context */
#define __BUILD_KVM_SAVE_VZ(name, _reg, sel)			\
static inline void kvm_restore_gc0_##name(struct mips_coproc *cop0)	\
{									\
	write_gc0_##name(cop0->reg[(_reg)][(sel)]);			\
}									\
static inline void kvm_save_gc0_##name(struct mips_coproc *cop0)	\
{									\
	cop0->reg[(_reg)][(sel)] = read_gc0_##name();			\
}

/*
 * __BUILD_KVM_$ops_WRAP(): kvm_$op_$name1() -> kvm_$op_$name2()
 * These wrap a set of operations to provide them with a different name.
 */

/* Generate simple accessor wrapper */
#define __BUILD_KVM_RW_WRAP(name1, name2, type)				\
static inline __KVMT##type kvm_read_##name1(struct mips_coproc *cop0)	\
{									\
	return kvm_read_##name2(cop0);					\
}									\
static inline void kvm_write_##name1(struct mips_coproc *cop0,		\
				     __KVMT##type val)			\
{									\
	kvm_write_##name2(cop0, val);					\
}

/* Generate bitwise modifier wrapper */
#define __BUILD_KVM_SET_WRAP(name1, name2, type)			\
static inline void kvm_set_##name1(struct mips_coproc *cop0,		\
				   __KVMT##type val)			\
{									\
	kvm_set_##name2(cop0, val);					\
}									\
static inline void kvm_clear_##name1(struct mips_coproc *cop0,		\
				     __KVMT##type val)			\
{									\
	kvm_clear_##name2(cop0, val);					\
}									\
static inline void kvm_change_##name1(struct mips_coproc *cop0,		\
				      __KVMT##type mask,		\
				      __KVMT##type val)			\
{									\
	kvm_change_##name2(cop0, mask, val);				\
}

/*
 * __BUILD_KVM_$ops_SW(): kvm_$op_c0_guest_$reg() -> kvm_$op_sw_gc0_$reg()
 * These generate accessors operating on the saved context in RAM, and wrap them
 * with the common guest C0 accessors (for use by common emulation code).
 */

#define __BUILD_KVM_RW_SW(name, type, _reg, sel)			\
	__BUILD_KVM_RW_SAVED(name, type, _reg, sel)			\
	__BUILD_KVM_RW_WRAP(c0_guest_##name, sw_gc0_##name, type)

#define __BUILD_KVM_SET_SW(name, type, _reg, sel)			\
	__BUILD_KVM_SET_SAVED(name, type, _reg, sel)			\
	__BUILD_KVM_SET_WRAP(c0_guest_##name, sw_gc0_##name, type)

#define __BUILD_KVM_ATOMIC_SW(name, type, _reg, sel)			\
	__BUILD_KVM_ATOMIC_SAVED(name, type, _reg, sel)			\
	__BUILD_KVM_SET_WRAP(c0_guest_##name, sw_gc0_##name, type)

/*
 * VZ (hardware assisted virtualisation)
 * These macros use the active guest state in VZ mode (hardware registers),
 */

/*
 * __BUILD_KVM_$ops_HW(): kvm_$op_c0_guest_$reg() -> kvm_$op_vz_gc0_$reg()
 * These generate accessors operating on the VZ guest context in hardware, and
 * wrap them with the common guest C0 accessors (for use by common emulation
 * code).
 *
 * Accessors operating on the saved context in RAM are also generated to allow
 * convenient explicit saving and restoring of the state.
 */

#define __BUILD_KVM_RW_HW(name, type, _reg, sel)			\
	__BUILD_KVM_RW_SAVED(name, type, _reg, sel)			\
	__BUILD_KVM_RW_VZ(name, type, _reg, sel)			\
	__BUILD_KVM_RW_WRAP(c0_guest_##name, vz_gc0_##name, type)	\
	__BUILD_KVM_SAVE_VZ(name, _reg, sel)

#define __BUILD_KVM_SET_HW(name, type, _reg, sel)			\
	__BUILD_KVM_SET_SAVED(name, type, _reg, sel)			\
	__BUILD_KVM_SET_VZ(name, type, _reg, sel)			\
	__BUILD_KVM_SET_WRAP(c0_guest_##name, vz_gc0_##name, type)

/*
 * We can't do atomic modifications of COP0 state if hardware can modify it.
 * Races must be handled explicitly.
 */
#define __BUILD_KVM_ATOMIC_HW	__BUILD_KVM_SET_HW

/*
 * Define accessors for CP0 registers that are accessible to the guest. These
 * are primarily used by common emulation code, which may need to access the
 * registers differently depending on the implementation.
 *
 *    fns_hw/sw    name     type    reg num         select
 */
__BUILD_KVM_RW_HW(index,          32, MIPS_CP0_TLB_INDEX,    0)
__BUILD_KVM_RW_HW(entrylo0,       l,  MIPS_CP0_TLB_LO0,      0)
__BUILD_KVM_RW_HW(entrylo1,       l,  MIPS_CP0_TLB_LO1,      0)
__BUILD_KVM_RW_HW(context,        l,  MIPS_CP0_TLB_CONTEXT,  0)
__BUILD_KVM_RW_HW(contextconfig,  32, MIPS_CP0_TLB_CONTEXT,  1)
__BUILD_KVM_RW_HW(userlocal,      l,  MIPS_CP0_TLB_CONTEXT,  2)
__BUILD_KVM_RW_HW(xcontextconfig, l,  MIPS_CP0_TLB_CONTEXT,  3)
__BUILD_KVM_RW_HW(pagemask,       l,  MIPS_CP0_TLB_PG_MASK,  0)
__BUILD_KVM_RW_HW(pagegrain,      32, MIPS_CP0_TLB_PG_MASK,  1)
__BUILD_KVM_RW_HW(segctl0,        l,  MIPS_CP0_TLB_PG_MASK,  2)
__BUILD_KVM_RW_HW(segctl1,        l,  MIPS_CP0_TLB_PG_MASK,  3)
__BUILD_KVM_RW_HW(segctl2,        l,  MIPS_CP0_TLB_PG_MASK,  4)
__BUILD_KVM_RW_HW(pwbase,         l,  MIPS_CP0_TLB_PG_MASK,  5)
__BUILD_KVM_RW_HW(pwfield,        l,  MIPS_CP0_TLB_PG_MASK,  6)
__BUILD_KVM_RW_HW(pwsize,         l,  MIPS_CP0_TLB_PG_MASK,  7)
__BUILD_KVM_RW_HW(wired,          32, MIPS_CP0_TLB_WIRED,    0)
__BUILD_KVM_RW_HW(pwctl,          32, MIPS_CP0_TLB_WIRED,    6)
__BUILD_KVM_RW_HW(hwrena,         32, MIPS_CP0_HWRENA,       0)
__BUILD_KVM_RW_HW(badvaddr,       l,  MIPS_CP0_BAD_VADDR,    0)
__BUILD_KVM_RW_HW(badinstr,       32, MIPS_CP0_BAD_VADDR,    1)
__BUILD_KVM_RW_HW(badinstrp,      32, MIPS_CP0_BAD_VADDR,    2)
__BUILD_KVM_RW_SW(count,          32, MIPS_CP0_COUNT,        0)
__BUILD_KVM_RW_HW(entryhi,        l,  MIPS_CP0_TLB_HI,       0)
__BUILD_KVM_RW_HW(compare,        32, MIPS_CP0_COMPARE,      0)
__BUILD_KVM_RW_HW(status,         32, MIPS_CP0_STATUS,       0)
__BUILD_KVM_RW_HW(intctl,         32, MIPS_CP0_STATUS,       1)
__BUILD_KVM_RW_HW(cause,          32, MIPS_CP0_CAUSE,        0)
__BUILD_KVM_RW_HW(epc,            l,  MIPS_CP0_EXC_PC,       0)
__BUILD_KVM_RW_SW(prid,           32, MIPS_CP0_PRID,         0)
__BUILD_KVM_RW_HW(ebase,          l,  MIPS_CP0_PRID,         1)
__BUILD_KVM_RW_HW(config,         32, MIPS_CP0_CONFIG,       0)
__BUILD_KVM_RW_HW(config1,        32, MIPS_CP0_CONFIG,       1)
__BUILD_KVM_RW_HW(config2,        32, MIPS_CP0_CONFIG,       2)
__BUILD_KVM_RW_HW(config3,        32, MIPS_CP0_CONFIG,       3)
__BUILD_KVM_RW_HW(config4,        32, MIPS_CP0_CONFIG,       4)
__BUILD_KVM_RW_HW(config5,        32, MIPS_CP0_CONFIG,       5)
__BUILD_KVM_RW_HW(config6,        32, MIPS_CP0_CONFIG,       6)
__BUILD_KVM_RW_HW(config7,        32, MIPS_CP0_CONFIG,       7)
__BUILD_KVM_RW_SW(maari,          l,  MIPS_CP0_LLADDR,       2)
__BUILD_KVM_RW_HW(xcontext,       l,  MIPS_CP0_TLB_XCONTEXT, 0)
__BUILD_KVM_RW_HW(errorepc,       l,  MIPS_CP0_ERROR_PC,     0)
__BUILD_KVM_RW_HW(kscratch1,      l,  MIPS_CP0_DESAVE,       2)
__BUILD_KVM_RW_HW(kscratch2,      l,  MIPS_CP0_DESAVE,       3)
__BUILD_KVM_RW_HW(kscratch3,      l,  MIPS_CP0_DESAVE,       4)
__BUILD_KVM_RW_HW(kscratch4,      l,  MIPS_CP0_DESAVE,       5)
__BUILD_KVM_RW_HW(kscratch5,      l,  MIPS_CP0_DESAVE,       6)
__BUILD_KVM_RW_HW(kscratch6,      l,  MIPS_CP0_DESAVE,       7)

/* Bitwise operations (on HW state) */
__BUILD_KVM_SET_HW(status,        32, MIPS_CP0_STATUS,       0)
/* Cause can be modified asynchronously from hardirq hrtimer callback */
__BUILD_KVM_ATOMIC_HW(cause,      32, MIPS_CP0_CAUSE,        0)
__BUILD_KVM_SET_HW(ebase,         l,  MIPS_CP0_PRID,         1)

/* Bitwise operations (on saved state) */
__BUILD_KVM_SET_SAVED(config,     32, MIPS_CP0_CONFIG,       0)
__BUILD_KVM_SET_SAVED(config1,    32, MIPS_CP0_CONFIG,       1)
__BUILD_KVM_SET_SAVED(config2,    32, MIPS_CP0_CONFIG,       2)
__BUILD_KVM_SET_SAVED(config3,    32, MIPS_CP0_CONFIG,       3)
__BUILD_KVM_SET_SAVED(config4,    32, MIPS_CP0_CONFIG,       4)
__BUILD_KVM_SET_SAVED(config5,    32, MIPS_CP0_CONFIG,       5)

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
	int (*handle_guest_exit)(struct kvm_vcpu *vcpu);
	int (*hardware_enable)(void);
	void (*hardware_disable)(void);
	int (*check_extension)(struct kvm *kvm, long ext);
	int (*vcpu_init)(struct kvm_vcpu *vcpu);
	void (*vcpu_uninit)(struct kvm_vcpu *vcpu);
	int (*vcpu_setup)(struct kvm_vcpu *vcpu);
	void (*prepare_flush_shadow)(struct kvm *kvm);
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
	int (*vcpu_run)(struct kvm_vcpu *vcpu);
	void (*vcpu_reenter)(struct kvm_vcpu *vcpu);
};
extern struct kvm_mips_callbacks *kvm_mips_callbacks;
int kvm_mips_emulation_init(void);

/* Debug: dump vcpu state */
int kvm_arch_vcpu_dump_regs(struct kvm_vcpu *vcpu);

extern int kvm_mips_handle_exit(struct kvm_vcpu *vcpu);

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
int kvm_mips_handle_vz_root_tlb_fault(unsigned long badvaddr,
				      struct kvm_vcpu *vcpu, bool write_fault);

int kvm_vz_host_tlb_inv(struct kvm_vcpu *vcpu, unsigned long entryhi);
int kvm_vz_guest_tlb_lookup(struct kvm_vcpu *vcpu, unsigned long gva,
			    unsigned long *gpa);
void kvm_vz_local_flush_roottlb_all_guests(void);
void kvm_vz_local_flush_guesttlb_all(void);
void kvm_vz_save_guesttlb(struct kvm_mips_tlb *buf, unsigned int index,
			  unsigned int count);
void kvm_vz_load_guesttlb(const struct kvm_mips_tlb *buf, unsigned int index,
			  unsigned int count);
#ifdef CONFIG_CPU_LOONGSON64
void kvm_loongson_clear_guest_vtlb(void);
void kvm_loongson_clear_guest_ftlb(void);
#endif

/* MMU handling */

bool kvm_mips_flush_gpa_pt(struct kvm *kvm, gfn_t start_gfn, gfn_t end_gfn);
int kvm_mips_mkclean_gpa_pt(struct kvm *kvm, gfn_t start_gfn, gfn_t end_gfn);
pgd_t *kvm_pgd_alloc(void);
void kvm_mmu_free_memory_caches(struct kvm_vcpu *vcpu);

#define KVM_ARCH_WANT_MMU_NOTIFIER

/* Emulation */
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

extern enum emulation_result kvm_mips_complete_mmio_load(struct kvm_vcpu *vcpu);

u32 kvm_mips_read_count(struct kvm_vcpu *vcpu);
void kvm_mips_write_count(struct kvm_vcpu *vcpu, u32 count);
void kvm_mips_write_compare(struct kvm_vcpu *vcpu, u32 compare, bool ack);
void kvm_mips_init_count(struct kvm_vcpu *vcpu, unsigned long count_hz);
int kvm_mips_set_count_ctl(struct kvm_vcpu *vcpu, s64 count_ctl);
int kvm_mips_set_count_resume(struct kvm_vcpu *vcpu, s64 count_resume);
int kvm_mips_set_count_hz(struct kvm_vcpu *vcpu, s64 count_hz);
void kvm_mips_count_enable_cause(struct kvm_vcpu *vcpu);
void kvm_mips_count_disable_cause(struct kvm_vcpu *vcpu);
enum hrtimer_restart kvm_mips_count_timeout(struct kvm_vcpu *vcpu);

/* fairly internal functions requiring some care to use */
int kvm_mips_count_disabled(struct kvm_vcpu *vcpu);
ktime_t kvm_mips_freeze_hrtimer(struct kvm_vcpu *vcpu, u32 *count);
int kvm_mips_restore_hrtimer(struct kvm_vcpu *vcpu, ktime_t before,
			     u32 count, int min_drift);

void kvm_vz_acquire_htimer(struct kvm_vcpu *vcpu);
void kvm_vz_lose_htimer(struct kvm_vcpu *vcpu);

enum emulation_result kvm_mips_emulate_store(union mips_instruction inst,
					     u32 cause,
					     struct kvm_vcpu *vcpu);
enum emulation_result kvm_mips_emulate_load(union mips_instruction inst,
					    u32 cause,
					    struct kvm_vcpu *vcpu);

/* COP0 */
enum emulation_result kvm_mips_emul_wait(struct kvm_vcpu *vcpu);

/* Hypercalls (hypcall.c) */

enum emulation_result kvm_mips_emul_hypcall(struct kvm_vcpu *vcpu,
					    union mips_instruction inst);
int kvm_mips_handle_hypcall(struct kvm_vcpu *vcpu);

/* Misc */
extern void kvm_mips_dump_stats(struct kvm_vcpu *vcpu);
extern unsigned long kvm_mips_get_ramsize(struct kvm *kvm);
extern int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu,
			     struct kvm_mips_interrupt *irq);

static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_free_memslot(struct kvm *kvm,
					 struct kvm_memory_slot *slot) {}
static inline void kvm_arch_memslots_updated(struct kvm *kvm, u64 gen) {}
static inline void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu) {}
static inline void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu) {}

#define __KVM_HAVE_ARCH_FLUSH_REMOTE_TLB
int kvm_arch_flush_remote_tlb(struct kvm *kvm);

#endif /* __MIPS_KVM_HOST_H__ */
