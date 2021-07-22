/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Sifive.
 */

#ifndef __ASM_ALTERNATIVE_H
#define __ASM_ALTERNATIVE_H

#define ERRATA_STRING_LENGTH_MAX 32

#include <asm/alternative-macros.h>

#ifndef __ASSEMBLY__

#include <linux/init.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <asm/hwcap.h>

void __init apply_boot_alternatives(void);

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
			      unsigned long archid, unsigned long impid);

#endif
#endif
