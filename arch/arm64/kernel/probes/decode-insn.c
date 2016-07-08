/*
 * arch/arm64/kernel/probes/decode-insn.c
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

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <asm/kprobes.h>
#include <asm/insn.h>
#include <asm/sections.h>

#include "decode-insn.h"

static bool __kprobes aarch64_insn_is_steppable(u32 insn)
{
	/*
	 * Branch instructions will write a new value into the PC which is
	 * likely to be relative to the XOL address and therefore invalid.
	 * Deliberate generation of an exception during stepping is also not
	 * currently safe. Lastly, MSR instructions can do any number of nasty
	 * things we can't handle during single-stepping.
	 */
	if (aarch64_get_insn_class(insn) == AARCH64_INSN_CLS_BR_SYS) {
		if (aarch64_insn_is_branch(insn) ||
		    aarch64_insn_is_msr_imm(insn) ||
		    aarch64_insn_is_msr_reg(insn) ||
		    aarch64_insn_is_exception(insn) ||
		    aarch64_insn_is_eret(insn))
			return false;

		/*
		 * The MRS instruction may not return a correct value when
		 * executing in the single-stepping environment. We do make one
		 * exception, for reading the DAIF bits.
		 */
		if (aarch64_insn_is_mrs(insn))
			return aarch64_insn_extract_system_reg(insn)
			     != AARCH64_INSN_SPCLREG_DAIF;

		/*
		 * The HINT instruction is is problematic when single-stepping,
		 * except for the NOP case.
		 */
		if (aarch64_insn_is_hint(insn))
			return aarch64_insn_is_nop(insn);

		return true;
	}

	/*
	 * Instructions which load PC relative literals are not going to work
	 * when executed from an XOL slot. Instructions doing an exclusive
	 * load/store are not going to complete successfully when single-step
	 * exception handling happens in the middle of the sequence.
	 */
	if (aarch64_insn_uses_literal(insn) ||
	    aarch64_insn_is_exclusive(insn))
		return false;

	return true;
}

/* Return:
 *   INSN_REJECTED     If instruction is one not allowed to kprobe,
 *   INSN_GOOD         If instruction is supported and uses instruction slot,
 */
static enum kprobe_insn __kprobes
arm_probe_decode_insn(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	/*
	 * Instructions reading or modifying the PC won't work from the XOL
	 * slot.
	 */
	if (aarch64_insn_is_steppable(insn))
		return INSN_GOOD;
	else
		return INSN_REJECTED;
}

static bool __kprobes
is_probed_address_atomic(kprobe_opcode_t *scan_start, kprobe_opcode_t *scan_end)
{
	while (scan_start > scan_end) {
		/*
		 * atomic region starts from exclusive load and ends with
		 * exclusive store.
		 */
		if (aarch64_insn_is_store_ex(le32_to_cpu(*scan_start)))
			return false;
		else if (aarch64_insn_is_load_ex(le32_to_cpu(*scan_start)))
			return true;
		scan_start--;
	}

	return false;
}

enum kprobe_insn __kprobes
arm_kprobe_decode_insn(kprobe_opcode_t *addr, struct arch_specific_insn *asi)
{
	enum kprobe_insn decoded;
	kprobe_opcode_t insn = le32_to_cpu(*addr);
	kprobe_opcode_t *scan_start = addr - 1;
	kprobe_opcode_t *scan_end = addr - MAX_ATOMIC_CONTEXT_SIZE;
#if defined(CONFIG_MODULES) && defined(MODULES_VADDR)
	struct module *mod;
#endif

	if (addr >= (kprobe_opcode_t *)_text &&
	    scan_end < (kprobe_opcode_t *)_text)
		scan_end = (kprobe_opcode_t *)_text;
#if defined(CONFIG_MODULES) && defined(MODULES_VADDR)
	else {
		preempt_disable();
		mod = __module_address((unsigned long)addr);
		if (mod && within_module_init((unsigned long)addr, mod) &&
			!within_module_init((unsigned long)scan_end, mod))
			scan_end = (kprobe_opcode_t *)mod->init_layout.base;
		else if (mod && within_module_core((unsigned long)addr, mod) &&
			!within_module_core((unsigned long)scan_end, mod))
			scan_end = (kprobe_opcode_t *)mod->core_layout.base;
		preempt_enable();
	}
#endif
	decoded = arm_probe_decode_insn(insn, asi);

	if (decoded == INSN_REJECTED ||
			is_probed_address_atomic(scan_start, scan_end))
		return INSN_REJECTED;

	return decoded;
}
