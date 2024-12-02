// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm64/kernel/probes/decode-insn.c
 *
 * Copyright (C) 2013 Linaro Limited.
 */

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <asm/insn.h>
#include <asm/sections.h>

#include "decode-insn.h"
#include "simulate-insn.h"

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
		    aarch64_insn_is_eret(insn) ||
		    aarch64_insn_is_eret_auth(insn))
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
		 * The HINT instruction is steppable only if it is in whitelist
		 * and the rest of other such instructions are blocked for
		 * single stepping as they may cause exception or other
		 * unintended behaviour.
		 */
		if (aarch64_insn_is_hint(insn))
			return aarch64_insn_is_steppable_hint(insn);

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
 *   INSN_GOOD_NO_SLOT If instruction is supported but doesn't use its slot.
 */
enum probe_insn __kprobes
arm_probe_decode_insn(probe_opcode_t insn, struct arch_probe_insn *api)
{
	/*
	 * Instructions reading or modifying the PC won't work from the XOL
	 * slot.
	 */
	if (aarch64_insn_is_steppable(insn))
		return INSN_GOOD;

	if (aarch64_insn_is_bcond(insn)) {
		api->handler = simulate_b_cond;
	} else if (aarch64_insn_is_cbz(insn) ||
	    aarch64_insn_is_cbnz(insn)) {
		api->handler = simulate_cbz_cbnz;
	} else if (aarch64_insn_is_tbz(insn) ||
	    aarch64_insn_is_tbnz(insn)) {
		api->handler = simulate_tbz_tbnz;
	} else if (aarch64_insn_is_adr_adrp(insn)) {
		api->handler = simulate_adr_adrp;
	} else if (aarch64_insn_is_b(insn) ||
	    aarch64_insn_is_bl(insn)) {
		api->handler = simulate_b_bl;
	} else if (aarch64_insn_is_br(insn) ||
	    aarch64_insn_is_blr(insn) ||
	    aarch64_insn_is_ret(insn)) {
		api->handler = simulate_br_blr_ret;
	} else {
		/*
		 * Instruction cannot be stepped out-of-line and we don't
		 * (yet) simulate it.
		 */
		return INSN_REJECTED;
	}

	return INSN_GOOD_NO_SLOT;
}

#ifdef CONFIG_KPROBES
static bool __kprobes
is_probed_address_atomic(kprobe_opcode_t *scan_start, kprobe_opcode_t *scan_end)
{
	while (scan_start >= scan_end) {
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

enum probe_insn __kprobes
arm_kprobe_decode_insn(kprobe_opcode_t *addr, struct arch_specific_insn *asi)
{
	enum probe_insn decoded;
	probe_opcode_t insn = le32_to_cpu(*addr);
	probe_opcode_t *scan_end = NULL;
	unsigned long size = 0, offset = 0;
	struct arch_probe_insn *api = &asi->api;

	if (aarch64_insn_is_ldr_lit(insn)) {
		api->handler = simulate_ldr_literal;
		decoded = INSN_GOOD_NO_SLOT;
	} else if (aarch64_insn_is_ldrsw_lit(insn)) {
		api->handler = simulate_ldrsw_literal;
		decoded = INSN_GOOD_NO_SLOT;
	} else {
		decoded = arm_probe_decode_insn(insn, &asi->api);
	}

	/*
	 * If there's a symbol defined in front of and near enough to
	 * the probe address assume it is the entry point to this
	 * code and use it to further limit how far back we search
	 * when determining if we're in an atomic sequence. If we could
	 * not find any symbol skip the atomic test altogether as we
	 * could otherwise end up searching irrelevant text/literals.
	 * KPROBES depends on KALLSYMS so this last case should never
	 * happen.
	 */
	if (kallsyms_lookup_size_offset((unsigned long) addr, &size, &offset)) {
		if (offset < (MAX_ATOMIC_CONTEXT_SIZE*sizeof(kprobe_opcode_t)))
			scan_end = addr - (offset / sizeof(kprobe_opcode_t));
		else
			scan_end = addr - MAX_ATOMIC_CONTEXT_SIZE;
	}

	if (decoded != INSN_REJECTED && scan_end)
		if (is_probed_address_atomic(addr - 1, scan_end))
			return INSN_REJECTED;

	return decoded;
}
#endif
