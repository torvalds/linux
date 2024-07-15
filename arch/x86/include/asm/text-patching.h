/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TEXT_PATCHING_H
#define _ASM_X86_TEXT_PATCHING_H

#include <linux/types.h>
#include <linux/stddef.h>
#include <asm/ptrace.h>

/*
 * Currently, the max observed size in the kernel code is
 * JUMP_LABEL_NOP_SIZE/RELATIVEJUMP_SIZE, which are 5.
 * Raise it if needed.
 */
#define POKE_MAX_OPCODE_SIZE	5

extern void text_poke_early(void *addr, const void *opcode, size_t len);

extern void apply_relocation(u8 *buf, size_t len, u8 *dest, u8 *src, size_t src_len);

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
 * On the local CPU you need to be protected against NMI or MCE handlers seeing
 * an inconsistent instruction while you patch.
 */
extern void *text_poke(void *addr, const void *opcode, size_t len);
extern void text_poke_sync(void);
extern void *text_poke_kgdb(void *addr, const void *opcode, size_t len);
extern void *text_poke_copy(void *addr, const void *opcode, size_t len);
extern void *text_poke_copy_locked(void *addr, const void *opcode, size_t len, bool core_ok);
extern void *text_poke_set(void *addr, int c, size_t len);
extern int poke_int3_handler(struct pt_regs *regs);
extern void text_poke_bp(void *addr, const void *opcode, size_t len, const void *emulate);

extern void text_poke_queue(void *addr, const void *opcode, size_t len, const void *emulate);
extern void text_poke_finish(void);

#define INT3_INSN_SIZE		1
#define INT3_INSN_OPCODE	0xCC

#define RET_INSN_SIZE		1
#define RET_INSN_OPCODE		0xC3

#define CALL_INSN_SIZE		5
#define CALL_INSN_OPCODE	0xE8

#define JMP32_INSN_SIZE		5
#define JMP32_INSN_OPCODE	0xE9

#define JMP8_INSN_SIZE		2
#define JMP8_INSN_OPCODE	0xEB

#define DISP32_SIZE		4

static __always_inline int text_opcode_size(u8 opcode)
{
	int size = 0;

#define __CASE(insn)	\
	case insn##_INSN_OPCODE: size = insn##_INSN_SIZE; break

	switch(opcode) {
	__CASE(INT3);
	__CASE(RET);
	__CASE(CALL);
	__CASE(JMP32);
	__CASE(JMP8);
	}

#undef __CASE

	return size;
}

union text_poke_insn {
	u8 text[POKE_MAX_OPCODE_SIZE];
	struct {
		u8 opcode;
		s32 disp;
	} __attribute__((packed));
};

static __always_inline
void __text_gen_insn(void *buf, u8 opcode, const void *addr, const void *dest, int size)
{
	union text_poke_insn *insn = buf;

	BUG_ON(size < text_opcode_size(opcode));

	/*
	 * Hide the addresses to avoid the compiler folding in constants when
	 * referencing code, these can mess up annotations like
	 * ANNOTATE_NOENDBR.
	 */
	OPTIMIZER_HIDE_VAR(insn);
	OPTIMIZER_HIDE_VAR(addr);
	OPTIMIZER_HIDE_VAR(dest);

	insn->opcode = opcode;

	if (size > 1) {
		insn->disp = (long)dest - (long)(addr + size);
		if (size == 2) {
			/*
			 * Ensure that for JMP8 the displacement
			 * actually fits the signed byte.
			 */
			BUG_ON((insn->disp >> 31) != (insn->disp >> 7));
		}
	}
}

static __always_inline
void *text_gen_insn(u8 opcode, const void *addr, const void *dest)
{
	static union text_poke_insn insn; /* per instance */
	__text_gen_insn(&insn, opcode, addr, dest, text_opcode_size(opcode));
	return &insn.text;
}

extern int after_bootmem;
extern __ro_after_init struct mm_struct *poking_mm;
extern __ro_after_init unsigned long poking_addr;

#ifndef CONFIG_UML_X86
static __always_inline
void int3_emulate_jmp(struct pt_regs *regs, unsigned long ip)
{
	regs->ip = ip;
}

static __always_inline
void int3_emulate_push(struct pt_regs *regs, unsigned long val)
{
	/*
	 * The int3 handler in entry_64.S adds a gap between the
	 * stack where the break point happened, and the saving of
	 * pt_regs. We can extend the original stack because of
	 * this gap. See the idtentry macro's create_gap option.
	 *
	 * Similarly entry_32.S will have a gap on the stack for (any) hardware
	 * exception and pt_regs; see FIXUP_FRAME.
	 */
	regs->sp -= sizeof(unsigned long);
	*(unsigned long *)regs->sp = val;
}

static __always_inline
unsigned long int3_emulate_pop(struct pt_regs *regs)
{
	unsigned long val = *(unsigned long *)regs->sp;
	regs->sp += sizeof(unsigned long);
	return val;
}

static __always_inline
void int3_emulate_call(struct pt_regs *regs, unsigned long func)
{
	int3_emulate_push(regs, regs->ip - INT3_INSN_SIZE + CALL_INSN_SIZE);
	int3_emulate_jmp(regs, func);
}

static __always_inline
void int3_emulate_ret(struct pt_regs *regs)
{
	unsigned long ip = int3_emulate_pop(regs);
	int3_emulate_jmp(regs, ip);
}

static __always_inline
void int3_emulate_jcc(struct pt_regs *regs, u8 cc, unsigned long ip, unsigned long disp)
{
	static const unsigned long jcc_mask[6] = {
		[0] = X86_EFLAGS_OF,
		[1] = X86_EFLAGS_CF,
		[2] = X86_EFLAGS_ZF,
		[3] = X86_EFLAGS_CF | X86_EFLAGS_ZF,
		[4] = X86_EFLAGS_SF,
		[5] = X86_EFLAGS_PF,
	};

	bool invert = cc & 1;
	bool match;

	if (cc < 0xc) {
		match = regs->flags & jcc_mask[cc >> 1];
	} else {
		match = ((regs->flags & X86_EFLAGS_SF) >> X86_EFLAGS_SF_BIT) ^
			((regs->flags & X86_EFLAGS_OF) >> X86_EFLAGS_OF_BIT);
		if (cc >= 0xe)
			match = match || (regs->flags & X86_EFLAGS_ZF);
	}

	if ((match && !invert) || (!match && invert))
		ip += disp;

	int3_emulate_jmp(regs, ip);
}

#endif /* !CONFIG_UML_X86 */

#endif /* _ASM_X86_TEXT_PATCHING_H */
