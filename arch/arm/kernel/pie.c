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
#include <asm/pie.h>

extern char __pie_rel_dyn_start[];
extern char __pie_rel_dyn_end[];
extern char __pie_tail_offset[];
extern char __pie_reloc_offset[];

struct arm_pie_tail {
	int count;
	uintptr_t offset[0];
};

int pie_arch_fill_tail(void *tail, void *common_start, void *common_end,
			void *overlay_start, void *code_start, void *code_end,
			void *rel_start, void *rel_end)
{
	Elf32_Rel *rel;
	int records;
	int i;
	struct arm_pie_tail *pie_tail = tail;
	int count;
	void *kern_off;

	rel = (Elf32_Rel *) __pie_rel_dyn_start;
	records = (__pie_rel_dyn_end - __pie_rel_dyn_start) / sizeof(*rel);

	count = 0;
	for (i = 0; i < records; i++, rel++) {
		if (ELF32_R_TYPE(rel->r_info) != R_ARM_RELATIVE)
			break;

		/* Adjust offset to match area in kernel */
		kern_off = common_start + rel->r_offset;

		if (kern_off >= common_start && kern_off < code_end) {
			if (tail)
				pie_tail->offset[count] = rel->r_offset;
			count++;
		}
	}

	rel = (Elf32_Rel *) rel_start;
	records = (rel_end - rel_start) / sizeof(*rel);

	for (i = 0; i < records; i++, rel++) {
		if (ELF32_R_TYPE(rel->r_info) != R_ARM_RELATIVE)
			break;

		/* Adjust offset to match area in kernel */
		kern_off = common_start + rel->r_offset;

		if (kern_off >= common_start && kern_off < code_end) {
			if (tail)
				pie_tail->offset[count] = rel->r_offset;
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
	void *reloc;
	int i;

	/* Perform relocation fixups for given offset */
	for (i = 0; i < pie_tail->count; i++)
		*((uintptr_t *) (pie_tail->offset[i] + base)) += offset;

	/* Store the PIE offset to tail and recol func */
	*kern_to_pie(chunk, (uintptr_t *) __pie_tail_offset) = tail - base;
	reloc = kern_to_pie(chunk,
				(void *) fnptr_to_addr(&__pie___pie_relocate));
	*kern_to_pie(chunk, (uintptr_t *) __pie_reloc_offset) = reloc - base;

	return 0;
}
EXPORT_SYMBOL_GPL(pie_arch_fixup);
