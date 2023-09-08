/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Sifive.
 */

#ifndef __ASM_ALTERNATIVE_H
#define __ASM_ALTERNATIVE_H

#include <asm/alternative-macros.h>

#ifndef __ASSEMBLY__

#ifdef CONFIG_RISCV_ALTERNATIVE

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <asm/hwcap.h>

#define PATCH_ID_CPUFEATURE_ID(p)		lower_16_bits(p)
#define PATCH_ID_CPUFEATURE_VALUE(p)		upper_16_bits(p)

#define RISCV_ALTERNATIVES_BOOT		0 /* alternatives applied during regular boot */
#define RISCV_ALTERNATIVES_MODULE	1 /* alternatives applied during module-init */
#define RISCV_ALTERNATIVES_EARLY_BOOT	2 /* alternatives applied before mmu start */

/* add the relative offset to the address of the offset to get the absolute address */
#define __ALT_PTR(a, f)			((void *)&(a)->f + (a)->f)
#define ALT_OLD_PTR(a)			__ALT_PTR(a, old_offset)
#define ALT_ALT_PTR(a)			__ALT_PTR(a, alt_offset)

void __init apply_boot_alternatives(void);
void __init apply_early_boot_alternatives(void);
void apply_module_alternatives(void *start, size_t length);

void riscv_alternative_fix_offsets(void *alt_ptr, unsigned int len,
				   int patch_offset);

struct alt_entry {
	s32 old_offset;		/* offset relative to original instruction or data  */
	s32 alt_offset;		/* offset relative to replacement instruction or data */
	u16 vendor_id;		/* CPU vendor ID */
	u16 alt_len;		/* The replacement size */
	u32 patch_id;		/* The patch ID (erratum ID or cpufeature ID) */
};

void andes_errata_patch_func(struct alt_entry *begin, struct alt_entry *end,
			     unsigned long archid, unsigned long impid,
			     unsigned int stage);
void sifive_errata_patch_func(struct alt_entry *begin, struct alt_entry *end,
			      unsigned long archid, unsigned long impid,
			      unsigned int stage);
void thead_errata_patch_func(struct alt_entry *begin, struct alt_entry *end,
			     unsigned long archid, unsigned long impid,
			     unsigned int stage);

void riscv_cpufeature_patch_func(struct alt_entry *begin, struct alt_entry *end,
				 unsigned int stage);

#else /* CONFIG_RISCV_ALTERNATIVE */

static inline void apply_boot_alternatives(void) { }
static inline void apply_early_boot_alternatives(void) { }
static inline void apply_module_alternatives(void *start, size_t length) { }

#endif /* CONFIG_RISCV_ALTERNATIVE */

#endif
#endif
