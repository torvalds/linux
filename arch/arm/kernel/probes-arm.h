/*
 * arch/arm/kernel/probes-arm.h
 *
 * Copyright 2013 Linaro Ltd.
 * Written by: David A. Long
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef _ARM_KERNEL_PROBES_ARM_H
#define  _ARM_KERNEL_PROBES_ARM_H

void __kprobes simulate_bbl(struct kprobe *p, struct pt_regs *regs);
void __kprobes simulate_blx1(struct kprobe *p, struct pt_regs *regs);
void __kprobes simulate_blx2bx(struct kprobe *p, struct pt_regs *regs);
void __kprobes simulate_mrs(struct kprobe *p, struct pt_regs *regs);
void __kprobes simulate_mov_ipsp(struct kprobe *p, struct pt_regs *regs);

void __kprobes emulate_ldrdstrd(struct kprobe *p, struct pt_regs *regs);
void __kprobes emulate_ldr(struct kprobe *p, struct pt_regs *regs);
void __kprobes emulate_str(struct kprobe *p, struct pt_regs *regs);
void __kprobes emulate_rd12rn16rm0rs8_rwflags(struct kprobe *p,
	struct pt_regs *regs);
void __kprobes emulate_rd12rn16rm0_rwflags_nopc(struct kprobe *p,
	struct pt_regs *regs);
void __kprobes emulate_rd16rn12rm0rs8_rwflags_nopc(struct kprobe *p,
	struct pt_regs *regs);
void __kprobes emulate_rd12rm0_noflags_nopc(struct kprobe *p,
	struct pt_regs *regs);
void __kprobes emulate_rdlo12rdhi16rn0rm8_rwflags_nopc(struct kprobe *p,
	struct pt_regs *regs);

#endif
