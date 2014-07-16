#ifndef _ASM_CMP_H
#define _ASM_CMP_H

/*
 * Definitions for CMP multitasking on MIPS cores
 */
struct task_struct;

extern void cmp_smp_setup(void);
extern void cmp_smp_finish(void);
extern void cmp_boot_secondary(int cpu, struct task_struct *t);
extern void cmp_init_secondary(void);
extern void cmp_prepare_cpus(unsigned int max_cpus);

/* This is platform specific */
extern void cmp_send_ipi(int cpu, unsigned int action);
#endif /*  _ASM_CMP_H */
