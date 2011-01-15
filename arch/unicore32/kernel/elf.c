/*
 * linux/arch/unicore32/kernel/elf.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/personality.h>
#include <linux/binfmts.h>
#include <linux/elf.h>

int elf_check_arch(const struct elf32_hdr *x)
{
	/* Make sure it's an UniCore executable */
	if (x->e_machine != EM_UNICORE)
		return 0;

	/* Make sure the entry address is reasonable */
	if (x->e_entry & 3)
		return 0;

	return 1;
}
EXPORT_SYMBOL(elf_check_arch);

void elf_set_personality(const struct elf32_hdr *x)
{
	unsigned int personality = PER_LINUX;

	set_personality(personality);
}
EXPORT_SYMBOL(elf_set_personality);
