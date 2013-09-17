/*
 * Copyright 2013 Texas Instruments, Inc.
 *	Russ Dill <russ.dill@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/pie.h>
#include <linux/elf.h>

#include <asm/elf.h>

extern char __pie_rel_dyn_start[];
extern char __pie_rel_dyn_end[];
extern char __pie_tail_offset[];

struct arm_pie_tail {
	int count;
	uintptr_t offset[0];
};

int pie_arch_fill_tail(void *tail, void *common_start, void *common_end,
			void *overlay_start, void *code_start, void *code_end)
{
	Elf32_Rel *rel;
	int records;
	int i;
	struct arm_pie_tail *pie_tail = tail;
	int count;

	rel = (Elf32_Rel *) __pie_rel_dyn_start;
	records = (__pie_rel_dyn_end - __pie_rel_dyn_start) /
						sizeof(*rel);

	count = 0;
	for (i = 0; i < records; i++, rel++) {
		void *kern_off;
		if (ELF32_R_TYPE(rel->r_info) != R_ARM_RELATIVE)
			return -ENOEXEC;

		/* Adjust offset to match area in kernel */
		kern_off = common_start + rel->r_offset;

		if (kern_off >= common_start && kern_off < code_end) {
			if (tail)
				pie_tail->offset[count] = rel->r_offset;
			count++;
		} else if (kern_off >= code_start && kern_off < code_end) {
			if (tail)
				pie_tail->offset[count] = rel->r_offset -
						(code_start - overlay_start);
			count++;
		}
	}

	if (tail)
		pie_tail->count = count;

	return count * sizeof(uintptr_t) + sizeof(*pie_tail);
}
EXPORT_SYMBOL_GPL(pie_arch_fill_tail);

/*
 * R_ARM_RELATIVE: B(S) + A
 * B(S) - Addressing origin of the output segment defining the symbol S.
 * A - Addend for the relocation.
 */
int pie_arch_fixup(struct pie_chunk *chunk, void *base, void *tail,
						unsigned long offset)
{
	struct arm_pie_tail *pie_tail = tail;
	int i;

	/* Perform relocation fixups for given offset */
	for (i = 0; i < pie_tail->count; i++)
		*((uintptr_t *) (pie_tail->offset[i] + base)) += offset;

	return 0;
}
EXPORT_SYMBOL_GPL(pie_arch_fixup);
