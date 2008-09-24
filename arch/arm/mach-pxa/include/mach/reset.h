#ifndef __ASM_ARCH_RESET_H
#define __ASM_ARCH_RESET_H

#define RESET_STATUS_HARDWARE	(1 << 0)	/* Hardware Reset */
#define RESET_STATUS_WATCHDOG	(1 << 1)	/* Watchdog Reset */
#define RESET_STATUS_LOWPOWER	(1 << 2)	/* Low Power/Sleep Exit */
#define RESET_STATUS_GPIO	(1 << 3)	/* GPIO Reset */
#define RESET_STATUS_ALL	(0xf)

extern unsigned int reset_status;
extern void clear_reset_status(unsigned int mask);

/*
 * register GPIO as reset generator
 */
extern int init_gpio_reset(int gpio);

#endif /* __ASM_ARCH_RESET_H */
