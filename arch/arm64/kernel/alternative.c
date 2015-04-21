/*
 * alternative runtime patching
 * inspired by the x86 version
 *
 * Copyright (C) 2014 ARM Ltd.
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

#define pr_fmt(fmt) "alternatives: " fmt

#include <linux/init.h>
#include <linux/cpu.h>
#include <asm/cacheflush.h>
#include <asm/alternative.h>
#include <asm/cpufeature.h>
#include <asm/insn.h>
#include <linux/stop_machine.h>

extern struct alt_instr __alt_instructions[], __alt_instructions_end[];

struct alt_region {
	struct alt_instr *begin;
	struct alt_instr *end;
};

/*
 * Decode the imm field of a b/bl instruction, and return the byte
 * offset as a signed value (so it can be used when computing a new
 * branch target).
 */
static s32 get_branch_offset(u32 insn)
{
	s32 imm = aarch64_insn_decode_immediate(AARCH64_INSN_IMM_26, insn);

	/* sign-extend the immediate before turning it into a byte offset */
	return (imm << 6) >> 4;
}

static u32 get_alt_insn(u8 *insnptr, u8 *altinsnptr)
{
	u32 insn;

	aarch64_insn_read(altinsnptr, &insn);

	/* Stop the world on instructions we don't support... */
	BUG_ON(aarch64_insn_is_cbz(insn));
	BUG_ON(aarch64_insn_is_cbnz(insn));
	BUG_ON(aarch64_insn_is_bcond(insn));
	/* ... and there is probably more. */

	if (aarch64_insn_is_b(insn) || aarch64_insn_is_bl(insn)) {
		enum aarch64_insn_branch_type type;
		unsigned long target;

		if (aarch64_insn_is_b(insn))
			type = AARCH64_INSN_BRANCH_NOLINK;
		else
			type = AARCH64_INSN_BRANCH_LINK;

		target = (unsigned long)altinsnptr + get_branch_offset(insn);
		insn = aarch64_insn_gen_branch_imm((unsigned long)insnptr,
						   target, type);
	}

	return insn;
}

static int __apply_alternatives(void *alt_region)
{
	struct alt_instr *alt;
	struct alt_region *region = alt_region;
	u8 *origptr, *replptr;

	for (alt = region->begin; alt < region->end; alt++) {
		u32 insn;
		int i;

		if (!cpus_have_cap(alt->cpufeature))
			continue;

		BUG_ON(alt->alt_len != alt->orig_len);

		pr_info_once("patching kernel code\n");

		origptr = (u8 *)&alt->orig_offset + alt->orig_offset;
		replptr = (u8 *)&alt->alt_offset + alt->alt_offset;

		for (i = 0; i < alt->alt_len; i += sizeof(insn)) {
			insn = get_alt_insn(origptr + i, replptr + i);
			aarch64_insn_write(origptr + i, insn);
		}

		flush_icache_range((uintptr_t)origptr,
				   (uintptr_t)(origptr + alt->alt_len));
	}

	return 0;
}

void apply_alternatives_all(void)
{
	struct alt_region region = {
		.begin	= __alt_instructions,
		.end	= __alt_instructions_end,
	};

	/* better not try code patching on a live SMP system */
	stop_machine(__apply_alternatives, &region, NULL);
}

void apply_alternatives(void *start, size_t length)
{
	struct alt_region region = {
		.begin	= start,
		.end	= start + length,
	};

	__apply_alternatives(&region);
}

void free_alternatives_memory(void)
{
	free_reserved_area(__alt_instructions, __alt_instructions_end,
			   0, "alternatives");
}
