/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_NMI_H
#define _ASM_NMI_H

#ifdef CONFIG_PPC_WATCHDOG
extern void arch_touch_nmi_watchdog(void);
#else
static inline void arch_touch_nmi_watchdog(void) {}
#endif

#endif /* _ASM_NMI_H */
