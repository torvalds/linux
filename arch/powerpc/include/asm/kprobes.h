#ifndef _ASM_POWERPC_KPROBES_H
#define _ASM_POWERPC_KPROBES_H
#ifdef __KERNEL__
/*
 *  Kernel Probes (KProbes)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2002, 2004
 *
 * 2002-Oct	Created by Vamsi Krishna S <vamsi_krishna@in.ibm.com> Kernel
 *		Probes initial implementation ( includes suggestions from
 *		Rusty Russell).
 * 2004-Nov	Modified for PPC64 by Ananth N Mavinakayanahalli
 *		<ananth@in.ibm.com>
 */
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>
#include <asm/probes.h>
#include <asm/code-patching.h>

#define  __ARCH_WANT_KPROBES_INSN_SLOT

struct pt_regs;
struct kprobe;

typedef ppc_opcode_t kprobe_opcode_t;
#define MAX_INSN_SIZE 1

#ifdef CONFIG_PPC64
#if defined(_CALL_ELF) && _CALL_ELF == 2
/* PPC64 ABIv2 needs local entry point */
#define kprobe_lookup_name(name, addr)					\
{									\
	addr = (kprobe_opcode_t *)kallsyms_lookup_name(name);		\
	if (addr)							\
		addr = (kprobe_opcode_t *)ppc_function_entry(addr);	\
}
#else
/*
 * 64bit powerpc ABIv1 uses function descriptors:
 * - Check for the dot variant of the symbol first.
 * - If that fails, try looking up the symbol provided.
 *
 * This ensures we always get to the actual symbol and not the descriptor.
 * Also handle <module:symbol> format.
 */
#define kprobe_lookup_name(name, addr)					\
{									\
	char dot_name[MODULE_NAME_LEN + 1 + KSYM_NAME_LEN];		\
	char *modsym;							\
	bool dot_appended = false;					\
	if ((modsym = strchr(name, ':')) != NULL) {			\
		modsym++;						\
		if (*modsym != '\0' && *modsym != '.') {		\
			/* Convert to <module:.symbol> */		\
			strncpy(dot_name, name, modsym - name);		\
			dot_name[modsym - name] = '.';			\
			dot_name[modsym - name + 1] = '\0';		\
			strncat(dot_name, modsym,			\
				sizeof(dot_name) - (modsym - name) - 2);\
			dot_appended = true;				\
		} else {						\
			dot_name[0] = '\0';				\
			strncat(dot_name, name, sizeof(dot_name) - 1);	\
		}							\
	} else if (name[0] != '.') {					\
		dot_name[0] = '.';					\
		dot_name[1] = '\0';					\
		strncat(dot_name, name, KSYM_NAME_LEN - 2);		\
		dot_appended = true;					\
	} else {							\
		dot_name[0] = '\0';					\
		strncat(dot_name, name, KSYM_NAME_LEN - 1);		\
	}								\
	addr = (kprobe_opcode_t *)kallsyms_lookup_name(dot_name);	\
	if (!addr && dot_appended) {					\
		/* Let's try the original non-dot symbol lookup	*/	\
		addr = (kprobe_opcode_t *)kallsyms_lookup_name(name);	\
	}								\
}
#endif /* defined(_CALL_ELF) && _CALL_ELF == 2 */
#endif /* CONFIG_PPC64 */

#define flush_insn_slot(p)	do { } while (0)
#define kretprobe_blacklist_size 0

void kretprobe_trampoline(void);
extern void arch_remove_kprobe(struct kprobe *p);

/* Architecture specific copy of original instruction */
struct arch_specific_insn {
	/* copy of original instruction */
	kprobe_opcode_t *insn;
	/*
	 * Set in kprobes code, initially to 0. If the instruction can be
	 * eumulated, this is set to 1, if not, to -1.
	 */
	int boostable;
};

struct prev_kprobe {
	struct kprobe *kp;
	unsigned long status;
	unsigned long saved_msr;
};

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned long kprobe_status;
	unsigned long kprobe_saved_msr;
	struct pt_regs jprobe_saved_regs;
	struct prev_kprobe prev_kprobe;
};

extern int kprobe_exceptions_notify(struct notifier_block *self,
					unsigned long val, void *data);
extern int kprobe_fault_handler(struct pt_regs *regs, int trapnr);
#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_KPROBES_H */
