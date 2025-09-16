/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2001-2002 Pavel Machek <pavel@suse.cz>
 * Based on code
 * Copyright 2001 Patrick Mochel <mochel@osdl.org>
 */
#ifndef _ASM_X86_SUSPEND_32_H
#define _ASM_X86_SUSPEND_32_H

#include <asm/desc.h>
#include <asm/fpu/api.h>
#include <asm/msr.h>

/* image of the saved processor state */
struct saved_context {
	unsigned long cr0, cr2, cr3, cr4;
	u64 misc_enable;
	struct saved_msrs saved_msrs;
	struct desc_ptr gdt_desc;
	struct desc_ptr idt;
	u16 ldt;
	u16 tss;
	unsigned long tr;
	unsigned long safety;
	unsigned long return_address;
	/*
	 * On x86_32, all segment registers except gs are saved at kernel
	 * entry in pt_regs.
	 */
	u16 gs;
	bool misc_enable_saved;
} __attribute__((packed));

/* routines for saving/restoring kernel state */
extern char core_restore_code[];
extern char restore_registers[];

#endif /* _ASM_X86_SUSPEND_32_H */
