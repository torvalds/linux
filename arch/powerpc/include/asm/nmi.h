/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_NMI_H
#define _ASM_NMI_H

#ifdef CONFIG_PPC_WATCHDOG
long soft_nmi_interrupt(struct pt_regs *regs);
void watchdog_hardlockup_set_timeout_pct(u64 pct);
#else
static inline void watchdog_hardlockup_set_timeout_pct(u64 pct) {}
#endif

extern void hv_nmi_check_nonrecoverable(struct pt_regs *regs);

#endif /* _ASM_NMI_H */
