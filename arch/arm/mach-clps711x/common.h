/*
 * linux/arch/arm/mach-clps711x/common.h
 *
 * Common bits.
 */

struct sys_timer;

extern void clps711x_map_io(void);
extern void clps711x_init_irq(void);
extern struct sys_timer clps711x_timer;
