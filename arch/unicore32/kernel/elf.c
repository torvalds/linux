// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/unicore32/kernel/elf.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
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
