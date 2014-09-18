/*
 * arch/arm/kernel/kprobes.h
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 *
 * Some contents moved here from arch/arm/include/asm/kprobes.h which is
 * Copyright (C) 2006, 2007 Motorola Inc.
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

#ifndef _ARM_KERNEL_KPROBES_H
#define _ARM_KERNEL_KPROBES_H

#include "probes.h"

/*
 * These undefined instructions must be unique and
 * reserved solely for kprobes' use.
 */
#define KPROBE_ARM_BREAKPOINT_INSTRUCTION	0x07f001f8
#define KPROBE_THUMB16_BREAKPOINT_INSTRUCTION	0xde18
#define KPROBE_THUMB32_BREAKPOINT_INSTRUCTION	0xf7f0a018

enum probes_insn __kprobes
kprobe_decode_ldmstm(kprobe_opcode_t insn, struct arch_probes_insn *asi,
		const struct decode_header *h);

typedef enum probes_insn (kprobe_decode_insn_t)(probes_opcode_t,
						struct arch_probes_insn *,
						bool,
						const union decode_action *);

#ifdef CONFIG_THUMB2_KERNEL

extern const union decode_action kprobes_t32_actions[];
extern const union decode_action kprobes_t16_actions[];

#else /* !CONFIG_THUMB2_KERNEL */

extern const union decode_action kprobes_arm_actions[];

#endif

#endif /* _ARM_KERNEL_KPROBES_H */
