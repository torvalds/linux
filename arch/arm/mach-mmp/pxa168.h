/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_PXA168_H
#define __ASM_MACH_PXA168_H

#include <linux/reboot.h>

extern void pxa168_timer_init(void);
extern void __init pxa168_init_irq(void);
extern void pxa168_restart(enum reboot_mode, const char *);
extern void pxa168_clear_keypad_wakeup(void);

#include <linux/i2c.h>
#include <linux/soc/mmp/cputype.h>
#include <linux/irqchip/mmp.h>

#endif /* __ASM_MACH_PXA168_H */
