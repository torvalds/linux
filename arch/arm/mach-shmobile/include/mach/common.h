#ifndef __ARCH_MACH_COMMON_H
#define __ARCH_MACH_COMMON_H

extern struct sys_timer shmobile_timer;
extern void shmobile_setup_console(void);
struct clk;
extern int clk_init(void);
extern void shmobile_handle_irq_intc(struct pt_regs *);

extern void sh7367_init_irq(void);
extern void sh7367_add_early_devices(void);
extern void sh7367_add_standard_devices(void);
extern void sh7367_clock_init(void);
extern void sh7367_pinmux_init(void);
extern struct clk sh7367_extalb1_clk;
extern struct clk sh7367_extal2_clk;

extern void sh7377_init_irq(void);
extern void sh7377_add_early_devices(void);
extern void sh7377_add_standard_devices(void);
extern void sh7377_clock_init(void);
extern void sh7377_pinmux_init(void);
extern struct clk sh7377_extalc1_clk;
extern struct clk sh7377_extal2_clk;

extern void sh7372_init_irq(void);
extern void sh7372_add_early_devices(void);
extern void sh7372_add_standard_devices(void);
extern void sh7372_clock_init(void);
extern void sh7372_pinmux_init(void);
extern struct clk sh7372_extal1_clk;
extern struct clk sh7372_extal2_clk;

extern void sh73a0_init_irq(void);
extern void sh73a0_add_early_devices(void);
extern void sh73a0_add_standard_devices(void);
extern void sh73a0_clock_init(void);
extern void sh73a0_pinmux_init(void);
extern struct clk sh73a0_extal1_clk;
extern struct clk sh73a0_extal2_clk;

#endif /* __ARCH_MACH_COMMON_H */
