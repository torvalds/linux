/*
 * This control block defines the PACA which defines the processor
 * specific data for each logical processor on the system.
 * There are some pointers defined that are utilized by PLIC.
 *
 * C 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_POWERPC_PACA_H
#define _ASM_POWERPC_PACA_H
#ifdef __KERNEL__

#ifdef CONFIG_PPC64

#include <linux/init.h>
#include <asm/types.h>
#include <asm/lppaca.h>
#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/exception-64e.h>
#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
#include <asm/kvm_book3s_asm.h>
#endif

register struct paca_struct *local_paca asm("r13");

#if defined(CONFIG_DEBUG_PREEMPT) && defined(CONFIG_SMP)
extern unsigned int debug_smp_processor_id(void); /* from linux/smp.h */
/*
 * Add standard checks that preemption cannot occur when using get_paca():
 * otherwise the paca_struct it points to may be the wrong one just after.
 */
#define get_paca()	((void) debug_smp_processor_id(), local_paca)
#else
#define get_paca()	local_paca
#endif

#define get_lppaca()	(get_paca()->lppaca_ptr)
#define get_slb_shadow()	(get_paca()->slb_shadow_ptr)

struct task_struct;
struct opal_machine_check_event;

/*
 * Defines the layout of the paca.
 *
 * This structure is not directly accessed by firmware or the service
 * processor.
 */
struct paca_struct {
#ifdef CONFIG_PPC_BOOK3S
	/*
	 * Because hw_cpu_id, unlike other paca fields, is accessed
	 * routinely from other CPUs (from the IRQ code), we stick to
	 * read-only (after boot) fields in the first cacheline to
	 * avoid cacheline bouncing.
	 */

	struct lppaca *lppaca_ptr;	/* Pointer to LpPaca for PLIC */
#endif /* CONFIG_PPC_BOOK3S */
	/*
	 * MAGIC: the spinlock functions in arch/powerpc/lib/locks.c 
	 * load lock_token and paca_index with a single lwz
	 * instruction.  They must travel together and be properly
	 * aligned.
	 */
	u16 lock_token;			/* Constant 0x8000, used in locks */
	u16 paca_index;			/* Logical processor number */

	u64 kernel_toc;			/* Kernel TOC address */
	u64 kernelbase;			/* Base address of kernel */
	u64 kernel_msr;			/* MSR while running in kernel */
#ifdef CONFIG_PPC_STD_MMU_64
	u64 stab_real;			/* Absolute address of segment table */
	u64 stab_addr;			/* Virtual address of segment table */
#endif /* CONFIG_PPC_STD_MMU_64 */
	void *emergency_sp;		/* pointer to emergency stack */
	u64 data_offset;		/* per cpu data offset */
	s16 hw_cpu_id;			/* Physical processor number */
	u8 cpu_start;			/* At startup, processor spins until */
					/* this becomes non-zero. */
	u8 kexec_state;		/* set when kexec down has irqs off */
#ifdef CONFIG_PPC_STD_MMU_64
	struct slb_shadow *slb_shadow_ptr;
	struct dtl_entry *dispatch_log;
	struct dtl_entry *dispatch_log_end;

	/*
	 * Now, starting in cacheline 2, the exception save areas
	 */
	/* used for most interrupts/exceptions */
	u64 exgen[11] __attribute__((aligned(0x80)));
	u64 exmc[11];		/* used for machine checks */
	u64 exslb[11];		/* used for SLB/segment table misses
 				 * on the linear mapping */
	/* SLB related definitions */
	u16 vmalloc_sllp;
	u16 slb_cache_ptr;
	u16 slb_cache[SLB_CACHE_ENTRIES];
#endif /* CONFIG_PPC_STD_MMU_64 */

#ifdef CONFIG_PPC_BOOK3E
	u64 exgen[8] __attribute__((aligned(0x80)));
	/* Keep pgd in the same cacheline as the start of extlb */
	pgd_t *pgd __attribute__((aligned(0x80))); /* Current PGD */
	pgd_t *kernel_pgd;		/* Kernel PGD */
	/* We can have up to 3 levels of reentrancy in the TLB miss handler */
	u64 extlb[3][EX_TLB_SIZE / sizeof(u64)];
	u64 exmc[8];		/* used for machine checks */
	u64 excrit[8];		/* used for crit interrupts */
	u64 exdbg[8];		/* used for debug interrupts */

	/* Kernel stack pointers for use by special exceptions */
	void *mc_kstack;
	void *crit_kstack;
	void *dbg_kstack;
#endif /* CONFIG_PPC_BOOK3E */

	mm_context_t context;

	/*
	 * then miscellaneous read-write fields
	 */
	struct task_struct *__current;	/* Pointer to current */
	u64 kstack;			/* Saved Kernel stack addr */
	u64 stab_rr;			/* stab/slb round-robin counter */
	u64 saved_r1;			/* r1 save for RTAS calls or PM */
	u64 saved_msr;			/* MSR saved here by enter_rtas */
	u16 trap_save;			/* Used when bad stack is encountered */
	u8 soft_enabled;		/* irq soft-enable flag */
	u8 hard_enabled;		/* set if irqs are enabled in MSR */
	u8 io_sync;			/* writel() needs spin_unlock sync */
	u8 irq_work_pending;		/* IRQ_WORK interrupt while soft-disable */
	u8 nap_state_lost;		/* NV GPR values lost in power7_idle */

#ifdef CONFIG_PPC_POWERNV
	/* Pointer to OPAL machine check event structure set by the
	 * early exception handler for use by high level C handler
	 */
	struct opal_machine_check_event *opal_mc_evt;
#endif

	/* Stuff for accurate time accounting */
	u64 user_time;			/* accumulated usermode TB ticks */
	u64 system_time;		/* accumulated system TB ticks */
	u64 user_time_scaled;		/* accumulated usermode SPURR ticks */
	u64 starttime;			/* TB value snapshot */
	u64 starttime_user;		/* TB value on exit to usermode */
	u64 startspurr;			/* SPURR value snapshot */
	u64 utime_sspurr;		/* ->user_time when ->startspurr set */
	u64 stolen_time;		/* TB ticks taken by hypervisor */
	u64 dtl_ridx;			/* read index in dispatch log */
	struct dtl_entry *dtl_curr;	/* pointer corresponding to dtl_ridx */

#ifdef CONFIG_KVM_BOOK3S_HANDLER
#ifdef CONFIG_KVM_BOOK3S_PR
	/* We use this to store guest state in */
	struct kvmppc_book3s_shadow_vcpu shadow_vcpu;
#endif
	struct kvmppc_host_state kvm_hstate;
#endif
};

extern struct paca_struct *paca;
extern __initdata struct paca_struct boot_paca;
extern void initialise_paca(struct paca_struct *new_paca, int cpu);
extern void setup_paca(struct paca_struct *new_paca);
extern void allocate_pacas(void);
extern void free_unused_pacas(void);

#else /* CONFIG_PPC64 */

static inline void allocate_pacas(void) { };
static inline void free_unused_pacas(void) { };

#endif /* CONFIG_PPC64 */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_PACA_H */
