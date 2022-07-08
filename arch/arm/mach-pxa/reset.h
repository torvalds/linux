/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARCH_RESET_H
#define __ASM_ARCH_RESET_H

#define RESET_STATUS_HARDWARE	(1 << 0)	/* Hardware Reset */
#define RESET_STATUS_WATCHDOG	(1 << 1)	/* Watchdog Reset */
#define RESET_STATUS_LOWPOWER	(1 << 2)	/* Low Power/Sleep Exit */
#define RESET_STATUS_GPIO	(1 << 3)	/* GPIO Reset */
#define RESET_STATUS_ALL	(0xf)

extern void clear_reset_status(unsigned int mask);
extern void pxa_register_wdt(unsigned int reset_status);

/**
 * init_gpio_reset() - register GPIO as reset generator
 * @gpio: gpio nr
 * @output: set gpio as output instead of input during normal work
 * @level: output level
 */
extern int init_gpio_reset(int gpio, int output, int level);

#endif /* __ASM_ARCH_RESET_H */
