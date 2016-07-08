/*
 * arch/arm64/kernel/probes/decode-insn.h
 *
 * Copyright (C) 2013 Linaro Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _ARM_KERNEL_KPROBES_ARM64_H
#define _ARM_KERNEL_KPROBES_ARM64_H

/*
 * ARM strongly recommends a limit of 128 bytes between LoadExcl and
 * StoreExcl instructions in a single thread of execution. So keep the
 * max atomic context size as 32.
 */
#define MAX_ATOMIC_CONTEXT_SIZE	(128 / sizeof(kprobe_opcode_t))

enum kprobe_insn {
	INSN_REJECTED,
	INSN_GOOD_NO_SLOT,
	INSN_GOOD,
};

enum kprobe_insn __kprobes
arm_kprobe_decode_insn(kprobe_opcode_t *addr, struct arch_specific_insn *asi);

#endif /* _ARM_KERNEL_KPROBES_ARM64_H */
