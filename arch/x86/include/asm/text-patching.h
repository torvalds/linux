/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TEXT_PATCHING_H
#define _ASM_X86_TEXT_PATCHING_H

#include <linux/types.h>
#include <linux/stddef.h>
#include <asm/ptrace.h>

struct paravirt_patch_site;
#ifdef CONFIG_PARAVIRT
void apply_paravirt(struct paravirt_patch_site *start,
		    struct paravirt_patch_site *end);
#else
static inline void apply_paravirt(struct paravirt_patch_site *start,
				  struct paravirt_patch_site *end)
{}
#define __parainstructions	NULL
#define __parainstructions_end	NULL
#endif

/*
 * Currently, the max observed size in the kernel code is
 * JUMP_LABEL_NOP_SIZE/RELATIVEJUMP_SIZE, which are 5.
 * Raise it if needed.
 */
#define POKE_MAX_OPCODE_SIZE	5

struct text_poke_loc {
	void *detour;
	void *addr;
	size_t len;
	const char opcode[POKE_MAX_OPCODE_SIZE];
};

extern void text_poke_early(void *addr, const void *opcode, size_t len);

/*
 * Clear and restore the kernel write-protection flag on the local CPU.
 * Allows the kernel to edit read-only pages.
 * Side-effect: any interrupt handler running between save and restore will have
 * the ability to write to read-only pages.
 *
 * Warning:
 * Code patching in the UP case is safe if NMIs and MCE handlers are stopped and
 * no thread can be preempted in the instructions being modified (no iret to an
 * invalid instruction possible) or if the instructions are changed from a
 * consistent state to another consistent state atomically.
 * On the local CPU you need to be protected again NMI or MCE handlers seeing an
 * inconsistent instruction while you patch.
 */
extern void *text_poke(void *addr, const void *opcode, size_t len);
extern void *text_poke_kgdb(void *addr, const void *opcode, size_t len);
extern int poke_int3_handler(struct pt_regs *regs);
extern void text_poke_bp(void *addr, const void *opcode, size_t len, void *handler);
extern void text_poke_bp_batch(struct text_poke_loc *tp, unsigned int nr_entries);
extern int after_bootmem;
extern __ro_after_init struct mm_struct *poking_mm;
extern __ro_after_init unsigned long poking_addr;

#ifndef CONFIG_UML_X86
static inline void int3_emulate_jmp(struct pt_regs *regs, unsigned long ip)
{
	regs->ip = ip;
}

#define INT3_INSN_SIZE 1
#define CALL_INSN_SIZE 5

#ifdef CONFIG_X86_64
static inline void int3_emulate_push(struct pt_regs *regs, unsigned long val)
{
	/*
	 * The int3 handler in entry_64.S adds a gap between the
	 * stack where the break point happened, and the saving of
	 * pt_regs. We can extend the original stack because of
	 * this gap. See the idtentry macro's create_gap option.
	 */
	regs->sp -= sizeof(unsigned long);
	*(unsigned long *)regs->sp = val;
}

static inline void int3_emulate_call(struct pt_regs *regs, unsigned long func)
{
	int3_emulate_push(regs, regs->ip - INT3_INSN_SIZE + CALL_INSN_SIZE);
	int3_emulate_jmp(regs, func);
}
#endif /* CONFIG_X86_64 */
#endif /* !CONFIG_UML_X86 */

#endif /* _ASM_X86_TEXT_PATCHING_H */
