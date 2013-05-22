#ifndef _ASM_POWERPC_PROCESSOR_H
#define _ASM_POWERPC_PROCESSOR_H

/*
 * Copyright (C) 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/reg.h>

#ifdef CONFIG_VSX
#define TS_FPRWIDTH 2
#else
#define TS_FPRWIDTH 1
#endif

#ifdef CONFIG_PPC64
/* Default SMT priority is set to 3. Use 11- 13bits to save priority. */
#define PPR_PRIORITY 3
#ifdef __ASSEMBLY__
#define INIT_PPR (PPR_PRIORITY << 50)
#else
#define INIT_PPR ((u64)PPR_PRIORITY << 50)
#endif /* __ASSEMBLY__ */
#endif /* CONFIG_PPC64 */

#ifndef __ASSEMBLY__
#include <linux/compiler.h>
#include <linux/cache.h>
#include <asm/ptrace.h>
#include <asm/types.h>
#include <asm/hw_breakpoint.h>

/* We do _not_ want to define new machine types at all, those must die
 * in favor of using the device-tree
 * -- BenH.
 */

/* PREP sub-platform types. Unused */
#define _PREP_Motorola	0x01	/* motorola prep */
#define _PREP_Firm	0x02	/* firmworks prep */
#define _PREP_IBM	0x00	/* ibm prep */
#define _PREP_Bull	0x03	/* bull prep */

/* CHRP sub-platform types. These are arbitrary */
#define _CHRP_Motorola	0x04	/* motorola chrp, the cobra */
#define _CHRP_IBM	0x05	/* IBM chrp, the longtrail and longtrail 2 */
#define _CHRP_Pegasos	0x06	/* Genesi/bplan's Pegasos and Pegasos2 */
#define _CHRP_briq	0x07	/* TotalImpact's briQ */

#if defined(__KERNEL__) && defined(CONFIG_PPC32)

extern int _chrp_type;

#endif /* defined(__KERNEL__) && defined(CONFIG_PPC32) */

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

/* Macros for adjusting thread priority (hardware multi-threading) */
#define HMT_very_low()   asm volatile("or 31,31,31   # very low priority")
#define HMT_low()	 asm volatile("or 1,1,1	     # low priority")
#define HMT_medium_low() asm volatile("or 6,6,6      # medium low priority")
#define HMT_medium()	 asm volatile("or 2,2,2	     # medium priority")
#define HMT_medium_high() asm volatile("or 5,5,5      # medium high priority")
#define HMT_high()	 asm volatile("or 3,3,3	     # high priority")

#ifdef __KERNEL__

struct task_struct;
void start_thread(struct pt_regs *regs, unsigned long fdptr, unsigned long sp);
void release_thread(struct task_struct *);

/* Lazy FPU handling on uni-processor */
extern struct task_struct *last_task_used_math;
extern struct task_struct *last_task_used_altivec;
extern struct task_struct *last_task_used_vsx;
extern struct task_struct *last_task_used_spe;

#ifdef CONFIG_PPC32

#if CONFIG_TASK_SIZE > CONFIG_KERNEL_START
#error User TASK_SIZE overlaps with KERNEL_START address
#endif
#define TASK_SIZE	(CONFIG_TASK_SIZE)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(TASK_SIZE / 8 * 3)
#endif

#ifdef CONFIG_PPC64
/* 64-bit user address space is 46-bits (64TB user VM) */
#define TASK_SIZE_USER64 (0x0000400000000000UL)

/* 
 * 32-bit user address space is 4GB - 1 page 
 * (this 1 page is needed so referencing of 0xFFFFFFFF generates EFAULT
 */
#define TASK_SIZE_USER32 (0x0000000100000000UL - (1*PAGE_SIZE))

#define TASK_SIZE_OF(tsk) (test_tsk_thread_flag(tsk, TIF_32BIT) ? \
		TASK_SIZE_USER32 : TASK_SIZE_USER64)
#define TASK_SIZE	  TASK_SIZE_OF(current)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE_USER32 (PAGE_ALIGN(TASK_SIZE_USER32 / 4))
#define TASK_UNMAPPED_BASE_USER64 (PAGE_ALIGN(TASK_SIZE_USER64 / 4))

#define TASK_UNMAPPED_BASE ((is_32bit_task()) ? \
		TASK_UNMAPPED_BASE_USER32 : TASK_UNMAPPED_BASE_USER64 )
#endif

#ifdef __powerpc64__

#define STACK_TOP_USER64 TASK_SIZE_USER64
#define STACK_TOP_USER32 TASK_SIZE_USER32

#define STACK_TOP (is_32bit_task() ? \
		   STACK_TOP_USER32 : STACK_TOP_USER64)

#define STACK_TOP_MAX STACK_TOP_USER64

#else /* __powerpc64__ */

#define STACK_TOP TASK_SIZE
#define STACK_TOP_MAX	STACK_TOP

#endif /* __powerpc64__ */

typedef struct {
	unsigned long seg;
} mm_segment_t;

#define TS_FPROFFSET 0
#define TS_VSRLOWOFFSET 1
#define TS_FPR(i) fpr[i][TS_FPROFFSET]
#define TS_TRANS_FPR(i) transact_fpr[i][TS_FPROFFSET]

struct thread_struct {
	unsigned long	ksp;		/* Kernel stack pointer */
	unsigned long	ksp_limit;	/* if ksp <= ksp_limit stack overflow */

#ifdef CONFIG_PPC64
	unsigned long	ksp_vsid;
#endif
	struct pt_regs	*regs;		/* Pointer to saved register state */
	mm_segment_t	fs;		/* for get_fs() validation */
#ifdef CONFIG_BOOKE
	/* BookE base exception scratch space; align on cacheline */
	unsigned long	normsave[8] ____cacheline_aligned;
#endif
#ifdef CONFIG_PPC32
	void		*pgdir;		/* root of page-table tree */
#endif
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
	/*
	 * The following help to manage the use of Debug Control Registers
	 * om the BookE platforms.
	 */
	uint32_t	dbcr0;
	uint32_t	dbcr1;
#ifdef CONFIG_BOOKE
	uint32_t	dbcr2;
#endif
	/*
	 * The stored value of the DBSR register will be the value at the
	 * last debug interrupt. This register can only be read from the
	 * user (will never be written to) and has value while helping to
	 * describe the reason for the last debug trap.  Torez
	 */
	uint32_t	dbsr;
	/*
	 * The following will contain addresses used by debug applications
	 * to help trace and trap on particular address locations.
	 * The bits in the Debug Control Registers above help define which
	 * of the following registers will contain valid data and/or addresses.
	 */
	unsigned long	iac1;
	unsigned long	iac2;
#if CONFIG_PPC_ADV_DEBUG_IACS > 2
	unsigned long	iac3;
	unsigned long	iac4;
#endif
	unsigned long	dac1;
	unsigned long	dac2;
#if CONFIG_PPC_ADV_DEBUG_DVCS > 0
	unsigned long	dvc1;
	unsigned long	dvc2;
#endif
#endif
	/* FP and VSX 0-31 register set */
	double		fpr[32][TS_FPRWIDTH];
	struct {

		unsigned int pad;
		unsigned int val;	/* Floating point status */
	} fpscr;
	int		fpexc_mode;	/* floating-point exception mode */
	unsigned int	align_ctl;	/* alignment handling control */
#ifdef CONFIG_PPC64
	unsigned long	start_tb;	/* Start purr when proc switched in */
	unsigned long	accum_tb;	/* Total accumilated purr for process */
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	struct perf_event *ptrace_bps[HBP_NUM];
	/*
	 * Helps identify source of single-step exception and subsequent
	 * hw-breakpoint enablement
	 */
	struct perf_event *last_hit_ubp;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
#endif
	struct arch_hw_breakpoint hw_brk; /* info on the hardware breakpoint */
	unsigned long	trap_nr;	/* last trap # on this thread */
#ifdef CONFIG_ALTIVEC
	/* Complete AltiVec register set */
	vector128	vr[32] __attribute__((aligned(16)));
	/* AltiVec status */
	vector128	vscr __attribute__((aligned(16)));
	unsigned long	vrsave;
	int		used_vr;	/* set if process has used altivec */
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_VSX
	/* VSR status */
	int		used_vsr;	/* set if process has used altivec */
#endif /* CONFIG_VSX */
#ifdef CONFIG_SPE
	unsigned long	evr[32];	/* upper 32-bits of SPE regs */
	u64		acc;		/* Accumulator */
	unsigned long	spefscr;	/* SPE & eFP status */
	int		used_spe;	/* set if process has used spe */
#endif /* CONFIG_SPE */
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	u64		tm_tfhar;	/* Transaction fail handler addr */
	u64		tm_texasr;	/* Transaction exception & summary */
	u64		tm_tfiar;	/* Transaction fail instr address reg */
	unsigned long	tm_orig_msr;	/* Thread's MSR on ctx switch */
	struct pt_regs	ckpt_regs;	/* Checkpointed registers */

	/*
	 * Transactional FP and VSX 0-31 register set.
	 * NOTE: the sense of these is the opposite of the integer ckpt_regs!
	 *
	 * When a transaction is active/signalled/scheduled etc., *regs is the
	 * most recent set of/speculated GPRs with ckpt_regs being the older
	 * checkpointed regs to which we roll back if transaction aborts.
	 *
	 * However, fpr[] is the checkpointed 'base state' of FP regs, and
	 * transact_fpr[] is the new set of transactional values.
	 * VRs work the same way.
	 */
	double		transact_fpr[32][TS_FPRWIDTH];
	struct {
		unsigned int pad;
		unsigned int val;	/* Floating point status */
	} transact_fpscr;
	vector128	transact_vr[32] __attribute__((aligned(16)));
	vector128	transact_vscr __attribute__((aligned(16)));
	unsigned long	transact_vrsave;
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */
#ifdef CONFIG_KVM_BOOK3S_32_HANDLER
	void*		kvm_shadow_vcpu; /* KVM internal data */
#endif /* CONFIG_KVM_BOOK3S_32_HANDLER */
#if defined(CONFIG_KVM) && defined(CONFIG_BOOKE)
	struct kvm_vcpu	*kvm_vcpu;
#endif
#ifdef CONFIG_PPC64
	unsigned long	dscr;
	int		dscr_inherit;
	unsigned long	ppr;	/* used to save/restore SMT priority */
#endif
#ifdef CONFIG_PPC_BOOK3S_64
	unsigned long	tar;
	unsigned long	ebbrr;
	unsigned long	ebbhr;
	unsigned long	bescr;
	unsigned long	siar;
	unsigned long	sdar;
	unsigned long	sier;
	unsigned long	mmcr0;
	unsigned long	mmcr2;
	unsigned long	mmcra;
#endif
};

#define ARCH_MIN_TASKALIGN 16

#define INIT_SP		(sizeof(init_stack) + (unsigned long) &init_stack)
#define INIT_SP_LIMIT \
	(_ALIGN_UP(sizeof(init_thread_info), 16) + (unsigned long) &init_stack)

#ifdef CONFIG_SPE
#define SPEFSCR_INIT .spefscr = SPEFSCR_FINVE | SPEFSCR_FDBZE | SPEFSCR_FUNFE | SPEFSCR_FOVFE,
#else
#define SPEFSCR_INIT
#endif

#ifdef CONFIG_PPC32
#define INIT_THREAD { \
	.ksp = INIT_SP, \
	.ksp_limit = INIT_SP_LIMIT, \
	.fs = KERNEL_DS, \
	.pgdir = swapper_pg_dir, \
	.fpexc_mode = MSR_FE0 | MSR_FE1, \
	SPEFSCR_INIT \
}
#else
#define INIT_THREAD  { \
	.ksp = INIT_SP, \
	.ksp_limit = INIT_SP_LIMIT, \
	.regs = (struct pt_regs *)INIT_SP - 1, /* XXX bogus, I think */ \
	.fs = KERNEL_DS, \
	.fpr = {{0}}, \
	.fpscr = { .val = 0, }, \
	.fpexc_mode = 0, \
	.ppr = INIT_PPR, \
}
#endif

/*
 * Return saved PC of a blocked thread. For now, this is the "user" PC
 */
#define thread_saved_pc(tsk)    \
        ((tsk)->thread.regs? (tsk)->thread.regs->nip: 0)

#define task_pt_regs(tsk)	((struct pt_regs *)(tsk)->thread.regs)

unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)  ((tsk)->thread.regs? (tsk)->thread.regs->nip: 0)
#define KSTK_ESP(tsk)  ((tsk)->thread.regs? (tsk)->thread.regs->gpr[1]: 0)

/* Get/set floating-point exception mode */
#define GET_FPEXC_CTL(tsk, adr) get_fpexc_mode((tsk), (adr))
#define SET_FPEXC_CTL(tsk, val) set_fpexc_mode((tsk), (val))

extern int get_fpexc_mode(struct task_struct *tsk, unsigned long adr);
extern int set_fpexc_mode(struct task_struct *tsk, unsigned int val);

#define GET_ENDIAN(tsk, adr) get_endian((tsk), (adr))
#define SET_ENDIAN(tsk, val) set_endian((tsk), (val))

extern int get_endian(struct task_struct *tsk, unsigned long adr);
extern int set_endian(struct task_struct *tsk, unsigned int val);

#define GET_UNALIGN_CTL(tsk, adr)	get_unalign_ctl((tsk), (adr))
#define SET_UNALIGN_CTL(tsk, val)	set_unalign_ctl((tsk), (val))

extern int get_unalign_ctl(struct task_struct *tsk, unsigned long adr);
extern int set_unalign_ctl(struct task_struct *tsk, unsigned int val);

static inline unsigned int __unpack_fe01(unsigned long msr_bits)
{
	return ((msr_bits & MSR_FE0) >> 10) | ((msr_bits & MSR_FE1) >> 8);
}

static inline unsigned long __pack_fe01(unsigned int fpmode)
{
	return ((fpmode << 10) & MSR_FE0) | ((fpmode << 8) & MSR_FE1);
}

#ifdef CONFIG_PPC64
#define cpu_relax()	do { HMT_low(); HMT_medium(); barrier(); } while (0)
#else
#define cpu_relax()	barrier()
#endif

/* Check that a certain kernel stack pointer is valid in task_struct p */
int validate_sp(unsigned long sp, struct task_struct *p,
                       unsigned long nbytes);

/*
 * Prefetch macros.
 */
#define ARCH_HAS_PREFETCH
#define ARCH_HAS_PREFETCHW
#define ARCH_HAS_SPINLOCK_PREFETCH

static inline void prefetch(const void *x)
{
	if (unlikely(!x))
		return;

	__asm__ __volatile__ ("dcbt 0,%0" : : "r" (x));
}

static inline void prefetchw(const void *x)
{
	if (unlikely(!x))
		return;

	__asm__ __volatile__ ("dcbtst 0,%0" : : "r" (x));
}

#define spin_lock_prefetch(x)	prefetchw(x)

#define HAVE_ARCH_PICK_MMAP_LAYOUT

#ifdef CONFIG_PPC64
static inline unsigned long get_clean_sp(unsigned long sp, int is_32)
{
	if (is_32)
		return sp & 0x0ffffffffUL;
	return sp;
}
#else
static inline unsigned long get_clean_sp(unsigned long sp, int is_32)
{
	return sp;
}
#endif

extern unsigned long cpuidle_disable;
enum idle_boot_override {IDLE_NO_OVERRIDE = 0, IDLE_POWERSAVE_OFF};

extern int powersave_nap;	/* set if nap mode can be used in idle loop */
extern void power7_nap(void);

#ifdef CONFIG_PSERIES_IDLE
extern void update_smt_snooze_delay(int cpu, int residency);
#else
static inline void update_smt_snooze_delay(int cpu, int residency) {}
#endif

extern void flush_instruction_cache(void);
extern void hard_reset_now(void);
extern void poweroff_now(void);
extern int fix_alignment(struct pt_regs *);
extern void cvt_fd(float *from, double *to);
extern void cvt_df(double *from, float *to);
extern void _nmask_and_or_msr(unsigned long nmask, unsigned long or_val);

#ifdef CONFIG_PPC64
/*
 * We handle most unaligned accesses in hardware. On the other hand 
 * unaligned DMA can be very expensive on some ppc64 IO chips (it does
 * powers of 2 writes until it reaches sufficient alignment).
 *
 * Based on this we disable the IP header alignment in network drivers.
 */
#define NET_IP_ALIGN	0
#endif

#endif /* __KERNEL__ */
#endif /* __ASSEMBLY__ */
#endif /* _ASM_POWERPC_PROCESSOR_H */
