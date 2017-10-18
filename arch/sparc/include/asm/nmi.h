#ifndef __NMI_H
#define __NMI_H

int __init nmi_init(void);
void perfctr_irq(int irq, struct pt_regs *regs);
void nmi_adjust_hz(unsigned int new_hz);

extern atomic_t nmi_active;

void arch_touch_nmi_watchdog(void);
void start_nmi_watchdog(void *unused);
void stop_nmi_watchdog(void *unused);

#endif /* __NMI_H */
