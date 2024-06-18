/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __S390_RETHOOK_H
#define __S390_RETHOOK_H

unsigned long arch_rethook_trampoline_callback(struct pt_regs *regs);

#endif
