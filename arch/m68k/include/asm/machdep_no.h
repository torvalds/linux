#ifndef _M68KNOMMU_MACHDEP_H
#define _M68KNOMMU_MACHDEP_H

#include <linux/interrupt.h>

/* Hardware clock functions */
extern void hw_timer_init(void);
extern unsigned long hw_timer_offset(void);

extern irqreturn_t arch_timer_interrupt(int irq, void *dummy);

/* Machine dependent time handling */
extern void (*mach_gettod)(int *year, int *mon, int *day, int *hour,
			   int *min, int *sec);
extern int (*mach_set_clock_mmss)(unsigned long);

/* machine dependent power off functions */
extern void (*mach_reset)( void );
extern void (*mach_halt)( void );
extern void (*mach_power_off)( void );

extern void config_BSP(char *command, int len);

extern void do_IRQ(int irq, struct pt_regs *fp);

#endif /* _M68KNOMMU_MACHDEP_H */
