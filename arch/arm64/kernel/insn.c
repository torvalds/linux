/*
 * Copyright (C) 2013 Huawei Ltd.
 * Author: Jiang Liu <liuj97@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <asm/insn.h>

static int aarch64_insn_encoding_class[] = {
	AARCH64_INSN_CLS_UNKNOWN,
	AARCH64_INSN_CLS_UNKNOWN,
	AARCH64_INSN_CLS_UNKNOWN,
	AARCH64_INSN_CLS_UNKNOWN,
	AARCH64_INSN_CLS_LDST,
	AARCH64_INSN_CLS_DP_REG,
	AARCH64_INSN_CLS_LDST,
	AARCH64_INSN_CLS_DP_FPSIMD,
	AARCH64_INSN_CLS_DP_IMM,
	AARCH64_INSN_CLS_DP_IMM,
	AARCH64_INSN_CLS_BR_SYS,
	AARCH64_INSN_CLS_BR_SYS,
	AARCH64_INSN_CLS_LDST,
	AARCH64_INSN_CLS_DP_REG,
	AARCH64_INSN_CLS_LDST,
	AARCH64_INSN_CLS_DP_FPSIMD,
};

enum aarch64_insn_encoding_class __kprobes aarch64_get_insn_class(u32 insn)
{
	return aarch64_insn_encoding_class[(insn >> 25) & 0xf];
}

/* NOP is an alias of HINT */
bool __kprobes aarch64_insn_is_nop(u32 insn)
{
	if (!aarch64_insn_is_hint(insn))
		return false;

	switch (insn & 0xFE0) {
	case AARCH64_INSN_HINT_YIELD:
	case AARCH64_INSN_HINT_WFE:
	case AARCH64_INSN_HINT_WFI:
	case AARCH64_INSN_HINT_SEV:
	case AARCH64_INSN_HINT_SEVL:
		return false;
	default:
		return true;
	}
}

static bool __kprobes __aarch64_insn_hotpatch_safe(u32 insn)
{
	if (aarch64_get_insn_class(insn) != AARCH64_INSN_CLS_BR_SYS)
		return false;

	return	aarch64_insn_is_b(insn) ||
		aarch64_insn_is_bl(insn) ||
		aarch64_insn_is_svc(insn) ||
		aarch64_insn_is_hvc(insn) ||
		aarch64_insn_is_smc(insn) ||
		aarch64_insn_is_brk(insn) ||
		aarch64_insn_is_nop(insn);
}

/*
 * ARM Architecture Reference Manual for ARMv8 Profile-A, Issue A.a
 * Section B2.6.5 "Concurrent modification and execution of instructions":
 * Concurrent modification and execution of instructions can lead to the
 * resulting instruction performing any behavior that can be achieved by
 * executing any sequence of instructions that can be executed from the
 * same Exception level, except where the instruction before modification
 * and the instruction after modification is a B, BL, NOP, BKPT, SVC, HVC,
 * or SMC instruction.
 */
bool __kprobes aarch64_insn_hotpatch_safe(u32 old_insn, u32 new_insn)
{
	return __aarch64_insn_hotpatch_safe(old_insn) &&
	       __aarch64_insn_hotpatch_safe(new_insn);
}
