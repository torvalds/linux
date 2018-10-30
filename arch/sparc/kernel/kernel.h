/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPARC_KERNEL_H
#define __SPARC_KERNEL_H

#include <linux/interrupt.h>
#include <linux/ftrace.h>

#include <asm/traps.h>
#include <asm/head.h>
#include <asm/io.h>

/* cpu.c */
extern const char *sparc_pmu_type;
extern unsigned int fsr_storage;
extern int ncpus_probed;

#ifdef CONFIG_SPARC64
/* setup_64.c */
struct seq_file;
void cpucap_info(struct seq_file *);

static inline unsigned long kimage_addr_to_ra(const void *p)
{
	unsigned long val = (unsigned long) p;

	return kern_base + (val - KERNBASE);
}

/* sys_sparc_64.c */
asmlinkage long sys_kern_features(void);

/* unaligned_64.c */
asmlinkage void kernel_unaligned_trap(struct pt_regs *regs, unsigned int insn);
int handle_popc(u32 insn, struct pt_regs *regs);
void handle_lddfmna(struct pt_regs *regs, unsigned long sfar, unsigned long sfsr);
void handle_stdfmna(struct pt_regs *regs, unsigned long sfar, unsigned long sfsr);

/* smp_64.c */
void __irq_entry smp_call_function_client(int irq, struct pt_regs *regs);
void __irq_entry smp_call_function_single_client(int irq, struct pt_regs *regs);
void __irq_entry smp_penguin_jailcell(int irq, struct pt_regs *regs);
void __irq_entry smp_receive_signal_client(int irq, struct pt_regs *regs);

/* kgdb_64.c */
void __irq_entry smp_kgdb_capture_client(int irq, struct pt_regs *regs);

/* pci.c */
int pci64_dma_supported(struct pci_dev *pdev, u64 device_mask);

/* signal32.c */
void do_sigreturn32(struct pt_regs *regs);
asmlinkage void do_rt_sigreturn32(struct pt_regs *regs);
void do_signal32(struct pt_regs * regs);
asmlinkage int do_sys32_sigstack(u32 u_ssptr, u32 u_ossptr, unsigned long sp);

/* time_64.c */
void __init time_init_early(void);

/* compat_audit.c */
extern unsigned int sparc32_dir_class[];
extern unsigned int sparc32_chattr_class[];
extern unsigned int sparc32_write_class[];
extern unsigned int sparc32_read_class[];
extern unsigned int sparc32_signal_class[];
int sparc32_classify_syscall(unsigned int syscall);
#endif

#ifdef CONFIG_SPARC32
/* setup_32.c */
struct linux_romvec;
void sparc32_start_kernel(struct linux_romvec *rp);

/* cpu.c */
void cpu_probe(void);

/* traps_32.c */
void handle_hw_divzero(struct pt_regs *regs, unsigned long pc,
                       unsigned long npc, unsigned long psr);
/* irq_32.c */
extern struct irqaction static_irqaction[];
extern int static_irq_count;
extern spinlock_t irq_action_lock;

void unexpected_irq(int irq, void *dev_id, struct pt_regs * regs);
void init_IRQ(void);

/* sun4m_irq.c */
void sun4m_init_IRQ(void);
void sun4m_unmask_profile_irq(void);
void sun4m_clear_profile_irq(int cpu);

/* sun4m_smp.c */
void sun4m_cpu_pre_starting(void *arg);
void sun4m_cpu_pre_online(void *arg);
void __init smp4m_boot_cpus(void);
int smp4m_boot_one_cpu(int i, struct task_struct *idle);
void __init smp4m_smp_done(void);
void smp4m_cross_call_irq(void);
void smp4m_percpu_timer_interrupt(struct pt_regs *regs);

/* sun4d_irq.c */
extern spinlock_t sun4d_imsk_lock;

void sun4d_init_IRQ(void);
int sun4d_request_irq(unsigned int irq,
                      irq_handler_t handler,
                      unsigned long irqflags,
                      const char *devname, void *dev_id);
int show_sun4d_interrupts(struct seq_file *, void *);
void sun4d_distribute_irqs(void);
void sun4d_free_irq(unsigned int irq, void *dev_id);

/* sun4d_smp.c */
void sun4d_cpu_pre_starting(void *arg);
void sun4d_cpu_pre_online(void *arg);
void __init smp4d_boot_cpus(void);
int smp4d_boot_one_cpu(int i, struct task_struct *idle);
void __init smp4d_smp_done(void);
void smp4d_cross_call_irq(void);
void smp4d_percpu_timer_interrupt(struct pt_regs *regs);

/* leon_smp.c */
void leon_cpu_pre_starting(void *arg);
void leon_cpu_pre_online(void *arg);
void leonsmp_ipi_interrupt(void);
void leon_cross_call_irq(void);

/* head_32.S */
extern unsigned int t_nmi[];
extern unsigned int linux_trap_ipi15_sun4d[];
extern unsigned int linux_trap_ipi15_sun4m[];

extern struct tt_entry trapbase;
extern struct tt_entry trapbase_cpu1;
extern struct tt_entry trapbase_cpu2;
extern struct tt_entry trapbase_cpu3;

extern char cputypval[];

/* entry.S */
extern unsigned long lvl14_save[4];
extern unsigned int real_irq_entry[];
extern unsigned int smp4d_ticker[];
extern unsigned int patchme_maybe_smp_msg[];

void floppy_hardint(void);

/* trampoline_32.S */
extern unsigned long sun4m_cpu_startup;
extern unsigned long sun4d_cpu_startup;

/* process_32.c */
asmlinkage int sparc_do_fork(unsigned long clone_flags,
                             unsigned long stack_start,
                             struct pt_regs *regs,
                             unsigned long stack_size);

/* signal_32.c */
asmlinkage void do_sigreturn(struct pt_regs *regs);
asmlinkage void do_rt_sigreturn(struct pt_regs *regs);
void do_notify_resume(struct pt_regs *regs, unsigned long orig_i0,
                      unsigned long thread_info_flags);
asmlinkage int do_sys_sigstack(struct sigstack __user *ssptr,
                               struct sigstack __user *ossptr,
                               unsigned long sp);

/* ptrace_32.c */
asmlinkage int syscall_trace(struct pt_regs *regs, int syscall_exit_p);

/* unaligned_32.c */
asmlinkage void kernel_unaligned_trap(struct pt_regs *regs, unsigned int insn);
asmlinkage void user_unaligned_trap(struct pt_regs *regs, unsigned int insn);

/* windows.c */
void try_to_clear_window_buffer(struct pt_regs *regs, int who);

/* auxio_32.c */
void __init auxio_probe(void);
void __init auxio_power_probe(void);

/* pcic.c */
extern void __iomem *pcic_regs;
void pcic_nmi(unsigned int pend, struct pt_regs *regs);

/* time_32.c */
void __init time_init(void);

#else /* CONFIG_SPARC32 */
#endif /* CONFIG_SPARC32 */
#endif /* !(__SPARC_KERNEL_H) */
