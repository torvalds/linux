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

void stop_nmi(void);
void restart_nmi(void);

#endif /* _ASM_X86_NMI_H */
