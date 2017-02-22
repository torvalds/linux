#ifndef _ASM_LKL_CPU_H
#define _ASM_LKL_CPU_H

int lkl_cpu_get(void);
void lkl_cpu_put(void);
int lkl_cpu_try_run_irq(int irq);
int lkl_cpu_init(void);
void lkl_cpu_shutdown(void);
void lkl_cpu_wait_shutdown(void);
void lkl_cpu_wakeup_idle(void);
void lkl_cpu_change_owner(lkl_thread_t owner);
void lkl_cpu_set_irqs_pending(void);
void lkl_idle_tail_schedule(void);
int lkl_cpu_idle_pending(void);
extern void do_idle(void);

#endif /* _ASM_LKL_CPU_H */
