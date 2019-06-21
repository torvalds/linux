/*
 *  Kernel Probes (KProbes)
 *  include/asm-mips/kprobes.h
 *
 *  Copyright 2006 Sony Corp.
 *  Copyright 2010 Cavium Networks
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _ASM_KPROBES_H
#define _ASM_KPROBES_H

#include <asm-generic/kprobes.h>

#ifdef CONFIG_KPROBES
#include <linux/ptrace.h>
#include <linux/types.h>

#include <asm/cacheflush.h>
#include <asm/kdebug.h>
#include <asm/inst.h>

#define	 __ARCH_WANT_KPROBES_INSN_SLOT

struct kprobe;
struct pt_regs;

typedef union mips_instruction kprobe_opcode_t;

#define MAX_INSN_SIZE 2

#define flush_insn_slot(p)						\
do {									\
	if (p->addr)							\
		flush_icache_range((unsigned long)p->addr,		\
			   (unsigned long)p->addr +			\
			   (MAX_INSN_SIZE * sizeof(kprobe_opcode_t)));	\
} while (0)


#define kretprobe_blacklist_size 0

void arch_remove_kprobe(struct kprobe *p);
int kprobe_fault_handler(struct pt_regs *regs, int trapnr);

/* Architecture specific copy of original instruction*/
struct arch_specific_insn {
	/* copy of the original instruction */
	kprobe_opcode_t *insn;
};

struct prev_kprobe {
	struct kprobe *kp;
	unsigned long status;
	unsigned long old_SR;
	unsigned long saved_SR;
	unsigned long saved_epc;
};

#define SKIP_DELAYSLOT 0x0001

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned long kprobe_status;
	unsigned long kprobe_old_SR;
	unsigned long kprobe_saved_SR;
	unsigned long kprobe_saved_epc;
	/* Per-thread fields, used while emulating branches */
	unsigned long flags;
	unsigned long target_epc;
	struct prev_kprobe prev_kprobe;
};

extern int kprobe_exceptions_notify(struct notifier_block *self,
				    unsigned long val, void *data);

#endif /* CONFIG_KPROBES */
#endif /* _ASM_KPROBES_H */
