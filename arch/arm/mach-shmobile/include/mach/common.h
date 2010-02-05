#ifndef __ARCH_MACH_COMMON_H
#define __ARCH_MACH_COMMON_H

extern struct sys_timer shmobile_timer;

extern void sh7367_init_irq(void);
extern void sh7367_add_early_devices(void);
extern void sh7367_add_standard_devices(void);
extern void sh7367_clock_init(void);

extern void sh7377_init_irq(void);
extern void sh7377_add_early_devices(void);
extern void sh7377_add_standard_devices(void);

#endif /* __ARCH_MACH_COMMON_H */
