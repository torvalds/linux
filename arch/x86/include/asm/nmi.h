#ifndef _ASM_X86_NMI_H
#define _ASM_X86_NMI_H

#include <linux/pm.h>
#include <asm/irq.h>
#include <asm/io.h>

#ifdef CONFIG_X86_LOCAL_APIC

extern void die_nmi(char *str, struct pt_regs *regs, int do_panic);
extern int avail_to_resrv_perfctr_nmi_bit(unsigned int);
extern int reserve_perfctr_nmi(unsigned int);
extern void release_perfctr_nmi(unsigned int);
extern int reserve_evntsel_nmi(unsigned int);
extern void release_evntsel_nmi(unsigned int);

struct ctl_table;
extern int proc_nmi_enabled(struct ctl_table *, int ,
			void __user *, size_t *, loff_t *);
extern int unknown_nmi_panic;

void arch_trigger_all_cpu_backtrace(void);
#define arch_trigger_all_cpu_backtrace arch_trigger_all_cpu_backtrace
#endif

/*
 * Define some priorities for the nmi notifier call chain.
 *
 * Create a local nmi bit that has a higher priority than
 * external nmis, because the local ones are more frequent.
 *
 * Also setup some default high/normal/low settings for
 * subsystems to registers with.  Using 4 bits to seperate
 * the priorities.  This can go alot higher if needed be.
 */

#define NMI_LOCAL_SHIFT		16	/* randomly picked */
#define NMI_LOCAL_BIT		(1ULL << NMI_LOCAL_SHIFT)
#define NMI_HIGH_PRIOR		(1ULL << 8)
#define NMI_NORMAL_PRIOR	(1ULL << 4)
#define NMI_LOW_PRIOR		(1ULL << 0)
#define NMI_LOCAL_HIGH_PRIOR	(NMI_LOCAL_BIT | NMI_HIGH_PRIOR)
#define NMI_LOCAL_NORMAL_PRIOR	(NMI_LOCAL_BIT | NMI_NORMAL_PRIOR)
#define NMI_LOCAL_LOW_PRIOR	(NMI_LOCAL_BIT | NMI_LOW_PRIOR)

void stop_nmi(void);
void restart_nmi(void);

#endif /* _ASM_X86_NMI_H */
