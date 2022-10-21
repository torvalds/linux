/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Sifive.
 */

#ifndef __ASM_ALTERNATIVE_H
#define __ASM_ALTERNATIVE_H

#define ERRATA_STRING_LENGTH_MAX 32

#include <asm/alternative-macros.h>

#ifndef __ASSEMBLY__

#ifdef CONFIG_RISCV_ALTERNATIVE

#include <linux/init.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <asm/hwcap.h>

#define RISCV_ALTERNATIVES_BOOT		0 /* alternatives applied during regular boot */
#define RISCV_ALTERNATIVES_MODULE	1 /* alternatives applied during module-init */
#define RISCV_ALTERNATIVES_EARLY_BOOT	2 /* alternatives applied before mmu start */

void __init apply_boot_alternatives(void);
void __init apply_early_boot_alternatives(void);
void apply_module_alternatives(void *start, size_t length);

struct alt_entry {
	void *old_ptr;		 /* address of original instruciton or data  */
	void *alt_ptr;		 /* address of replacement instruction or data */
	unsigned long vendor_id; /* cpu vendor id */
	unsigned long alt_len;   /* The replacement size */
	unsigned int errata_id;  /* The errata id */
} __packed;

struct errata_checkfunc_id {
	unsigned long vendor_id;
	bool (*func)(struct alt_entry *alt);
};

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
