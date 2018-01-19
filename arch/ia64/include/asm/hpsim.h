/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASMIA64_HPSIM_H
#define _ASMIA64_HPSIM_H

#ifndef CONFIG_HP_SIMSERIAL_CONSOLE
static inline int simcons_register(void) { return 1; }
#else
int simcons_register(void);
#endif

struct tty_driver;
extern struct tty_driver *hp_simserial_driver;

extern int hpsim_get_irq(int intr);
void ia64_ctl_trace(long on);

#endif
