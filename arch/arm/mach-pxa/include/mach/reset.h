#ifndef __ASM_ARCH_RESET_H
#define __ASM_ARCH_RESET_H

#define RESET_STATUS_HARDWARE	(1 << 0)	/* Hardware Reset */
#define RESET_STATUS_WATCHDOG	(1 << 1)	/* Watchdog Reset */
#define RESET_STATUS_LOWPOWER	(1 << 2)	/* Low Power/Sleep Exit */
#define RESET_STATUS_GPIO	(1 << 3)	/* GPIO Reset */
#define RESET_STATUS_ALL	(0xf)

extern unsigned int reset_status;
extern void clear_reset_status(unsigned int mask);

/**
 * init_gpio_reset() - register GPIO as reset generator
 * @gpio: gpio nr
 * @output: set gpio as out/low instead of input during normal work
 */
extern int init_gpio_reset(int gpio, int output);

#endif /* __ASM_ARCH_RESET_H */
