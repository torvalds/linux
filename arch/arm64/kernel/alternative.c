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

#define __ALT_PTR(a,f)		(u32 *)((void *)&(a)->f + (a)->f)
#define ALT_ORIG_PTR(a)		__ALT_PTR(a, orig_offset)
#define ALT_REPL_PTR(a)		__ALT_PTR(a, alt_offset)

extern struct alt_instr __alt_instructions[], __alt_instructions_end[];

struct alt_region {
	struct alt_instr *begin;
	struct alt_instr *end;
};

/*
 * Check if the target PC is within an alternative block.
 */
static bool branch_insn_requires_update(struct alt_instr *alt, unsigned long pc)
{
	unsigned long replptr;

	if (kernel_text_address(pc))
		return 1;

	replptr = (unsigned long)ALT_REPL_PTR(alt);
	if (pc >= replptr && pc <= (replptr + alt->alt_len))
		return 0;

	/*
	 * Branching into *another* alternate sequence is doomed, and
	 * we're not even trying to fix it up.
	 */
	BUG();
}

static u32 get_alt_insn(struct alt_instr *alt, u32 *insnptr, u32 *altinsnptr)
{
	u32 insn;

	insn = le32_to_cpu(*altinsnptr);

	if (aarch64_insn_is_branch_imm(insn)) {
		s32 offset = aarch64_get_branch_offset(insn);
		unsigned long target;

		target = (unsigned long)altinsnptr + offset;

		/*
		 * If we're branching inside the alternate sequence,
		 * do not rewrite the instruction, as it is already
		 * correct. Otherwise, generate the new instruction.
		 */
		if (branch_insn_requires_update(alt, target)) {
			offset = target - (unsigned long)insnptr;
			insn = aarch64_set_branch_offset(insn, offset);
		}
	}

	return insn;
}

static int __apply_alternatives(void *alt_region)
{
	struct alt_instr *alt;
	struct alt_region *region = alt_region;
	u32 *origptr, *replptr;

	for (alt = region->begin; alt < region->end; alt++) {
		u32 insn;
		int i, nr_inst;

		if (!cpus_have_cap(alt->cpufeature))
			continue;

		BUG_ON(alt->alt_len != alt->orig_len);

		pr_info_once("patching kernel code\n");

		origptr = ALT_ORIG_PTR(alt);
		replptr = ALT_REPL_PTR(alt);
		nr_inst = alt->alt_len / sizeof(insn);

		for (i = 0; i < nr_inst; i++) {
			insn = get_alt_insn(alt, origptr + i, replptr + i);
			*(origptr + i) = cpu_to_le32(insn);
		}

		flush_icache_range((uintptr_t)origptr,
				   (uintptr_t)(origptr + nr_inst));
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
