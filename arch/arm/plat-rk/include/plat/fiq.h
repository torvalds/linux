#ifndef __PLAT_FIQ_H
#define __PLAT_FIQ_H

/* enable/disable an interrupt that is an FIQ (safe from FIQ context?) */
void rk_fiq_enable(int n);
void rk_fiq_disable(int n);
void rk_irq_setpending(int irq);
void rk_irq_clearpending(int irq);
void rk_fiq_init(void);

#endif
