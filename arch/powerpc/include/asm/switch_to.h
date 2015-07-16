/*
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _ASM_POWERPC_SWITCH_TO_H
#define _ASM_POWERPC_SWITCH_TO_H

struct thread_struct;
struct task_struct;
struct pt_regs;

extern struct task_struct *__switch_to(struct task_struct *,
	struct task_struct *);
#define switch_to(prev, next, last)	((last) = __switch_to((prev), (next)))

struct thread_struct;
extern struct task_struct *_switch(struct thread_struct *prev,
				   struct thread_struct *next);
#ifdef CONFIG_PPC_BOOK3S_64
static inline void save_early_sprs(struct thread_struct *prev)
{
	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		prev->tar = mfspr(SPRN_TAR);
	if (cpu_has_feature(CPU_FTR_DSCR))
		prev->dscr = mfspr(SPRN_DSCR);
}
#else
static inline void save_early_sprs(struct thread_struct *prev) {}
#endif

extern void enable_kernel_fp(void);
extern void enable_kernel_altivec(void);
extern int emulate_altivec(struct pt_regs *);
extern void __giveup_vsx(struct task_struct *);
extern void giveup_vsx(struct task_struct *);
extern void enable_kernel_spe(void);
extern void giveup_spe(struct task_struct *);
extern void load_up_spe(struct task_struct *);
extern void switch_booke_debug_regs(struct debug_reg *new_debug);

#ifndef CONFIG_SMP
extern void discard_lazy_cpu_state(void);
#else
static inline void discard_lazy_cpu_state(void)
{
}
#endif

#ifdef CONFIG_PPC_FPU
extern void flush_fp_to_thread(struct task_struct *);
extern void giveup_fpu(struct task_struct *);
#else
static inline void flush_fp_to_thread(struct task_struct *t) { }
static inline void giveup_fpu(struct task_struct *t) { }
#endif

#ifdef CONFIG_ALTIVEC
extern void flush_altivec_to_thread(struct task_struct *);
extern void giveup_altivec(struct task_struct *);
extern void giveup_altivec_notask(void);
#else
static inline void flush_altivec_to_thread(struct task_struct *t)
{
}
static inline void giveup_altivec(struct task_struct *t)
{
}
#endif

#ifdef CONFIG_VSX
extern void flush_vsx_to_thread(struct task_struct *);
#else
static inline void flush_vsx_to_thread(struct task_struct *t)
{
}
#endif

#ifdef CONFIG_SPE
extern void flush_spe_to_thread(struct task_struct *);
#else
static inline void flush_spe_to_thread(struct task_struct *t)
{
}
#endif

static inline void clear_task_ebb(struct task_struct *t)
{
#ifdef CONFIG_PPC_BOOK3S_64
    /* EBB perf events are not inherited, so clear all EBB state. */
    t->thread.ebbrr = 0;
    t->thread.ebbhr = 0;
    t->thread.bescr = 0;
    t->thread.mmcr2 = 0;
    t->thread.mmcr0 = 0;
    t->thread.siar = 0;
    t->thread.sdar = 0;
    t->thread.sier = 0;
    t->thread.used_ebb = 0;
#endif
}

#endif /* _ASM_POWERPC_SWITCH_TO_H */
