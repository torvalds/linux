/*
 * linux/arch/arm/mach-clps711x/common.h
 *
 * Common bits.
 */

#define CLPS711X_NR_IRQS	(30)
#define CLPS711X_NR_GPIO	(4 * 8 + 3)
#define CLPS711X_GPIO(prt, bit)	((prt) * 8 + (bit))

struct sys_timer;

extern void clps711x_map_io(void);
extern void clps711x_init_irq(void);
extern struct sys_timer clps711x_timer;
extern void clps711x_restart(char mode, const char *cmd);
