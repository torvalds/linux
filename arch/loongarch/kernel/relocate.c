// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Kernel relocation at boot time
 *
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/panic_notifier.h>
#include <asm/inst.h>
#include <asm/sections.h>
#include <asm/setup.h>

#define RELOCATED(x) ((void *)((long)x + reloc_offset))

static unsigned long reloc_offset;

static inline void __init relocate_relative(void)
{
	Elf64_Rela *rela, *rela_end;
	rela = (Elf64_Rela *)&__rela_dyn_begin;
	rela_end = (Elf64_Rela *)&__rela_dyn_end;

	for ( ; rela < rela_end; rela++) {
		Elf64_Addr addr = rela->r_offset;
		Elf64_Addr relocated_addr = rela->r_addend;

		if (rela->r_info != R_LARCH_RELATIVE)
			continue;

		if (relocated_addr >= VMLINUX_LOAD_ADDRESS)
			relocated_addr = (Elf64_Addr)RELOCATED(relocated_addr);

		*(Elf64_Addr *)RELOCATED(addr) = relocated_addr;
	}
}

static inline void __init relocate_absolute(void)
{
	void *begin, *end;
	struct rela_la_abs *p;

	begin = &__la_abs_begin;
	end   = &__la_abs_end;

	for (p = begin; (void *)p < end; p++) {
		long v = p->symvalue;
		uint32_t lu12iw, ori, lu32id, lu52id;
		union loongarch_instruction *insn = (void *)p - p->offset;

		lu12iw = (v >> 12) & 0xfffff;
		ori    = v & 0xfff;
		lu32id = (v >> 32) & 0xfffff;
		lu52id = v >> 52;

		insn[0].reg1i20_format.immediate = lu12iw;
		insn[1].reg2i12_format.immediate = ori;
		insn[2].reg1i20_format.immediate = lu32id;
		insn[3].reg2i12_format.immediate = lu52id;
	}
}

void __init relocate_kernel(void)
{
	reloc_offset = (unsigned long)_text - VMLINUX_LOAD_ADDRESS;

	if (reloc_offset)
		relocate_relative();

	relocate_absolute();
}

/*
 * Show relocation information on panic.
 */
static void show_kernel_relocation(const char *level)
{
	if (reloc_offset > 0) {
		printk(level);
		pr_cont("Kernel relocated by 0x%lx\n", reloc_offset);
		pr_cont(" .text @ 0x%px\n", _text);
		pr_cont(" .data @ 0x%px\n", _sdata);
		pr_cont(" .bss  @ 0x%px\n", __bss_start);
	}
}

static int kernel_location_notifier_fn(struct notifier_block *self,
				       unsigned long v, void *p)
{
	show_kernel_relocation(KERN_EMERG);
	return NOTIFY_DONE;
}

static struct notifier_block kernel_location_notifier = {
	.notifier_call = kernel_location_notifier_fn
};

static int __init register_kernel_offset_dumper(void)
{
	atomic_notifier_chain_register(&panic_notifier_list,
				       &kernel_location_notifier);
	return 0;
}

arch_initcall(register_kernel_offset_dumper);
