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
#include <linux/stop_machine.h>

extern struct alt_instr __alt_instructions[], __alt_instructions_end[];

static int __apply_alternatives(void *dummy)
{
	struct alt_instr *alt;
	u8 *origptr, *replptr;

	for (alt = __alt_instructions; alt < __alt_instructions_end; alt++) {
		if (!cpus_have_cap(alt->cpufeature))
			continue;

		BUG_ON(alt->alt_len > alt->orig_len);

		pr_info_once("patching kernel code\n");

		origptr = (u8 *)&alt->orig_offset + alt->orig_offset;
		replptr = (u8 *)&alt->alt_offset + alt->alt_offset;
		memcpy(origptr, replptr, alt->alt_len);
		flush_icache_range((uintptr_t)origptr,
				   (uintptr_t)(origptr + alt->alt_len));
	}

	return 0;
}

void apply_alternatives(void)
{
	/* better not try code patching on a live SMP system */
	stop_machine(__apply_alternatives, NULL, NULL);
}

void free_alternatives_memory(void)
{
	free_reserved_area(__alt_instructions, __alt_instructions_end,
			   0, "alternatives");
}
