/* MN10300 Kernel Probes support
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by Mark Salter (msalter@redhat.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public Licence as published by
 * the Free Software Foundation; either version 2 of the Licence, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public Licence
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */
#ifndef _ASM_KPROBES_H
#define _ASM_KPROBES_H

#include <asm-generic/kprobes.h>

#define BREAKPOINT_INSTRUCTION	0xff

#ifdef CONFIG_KPROBES
#include <linux/types.h>
#include <linux/ptrace.h>

struct kprobe;

typedef unsigned char kprobe_opcode_t;
#define MAX_INSN_SIZE 8
#define MAX_STACK_SIZE 128

/* Architecture specific copy of original instruction */
struct arch_specific_insn {
	/*  copy of original instruction
	 */
	kprobe_opcode_t insn[MAX_INSN_SIZE];
};

extern const int kretprobe_blacklist_size;

extern int kprobe_exceptions_notify(struct notifier_block *self,
				    unsigned long val, void *data);

#define flush_insn_slot(p)  do {} while (0)

extern void arch_remove_kprobe(struct kprobe *p);

#endif /* CONFIG_KPROBES */
#endif /* _ASM_KPROBES_H */
