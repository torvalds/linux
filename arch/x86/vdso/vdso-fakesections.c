/*
 * Copyright 2014 Andy Lutomirski
 * Subject to the GNU Public License, v.2
 *
 * Hack to keep broken Go programs working.
 *
 * The Go runtime had a couple of bugs: it would read the section table to try
 * to figure out how many dynamic symbols there were (it shouldn't have looked
 * at the section table at all) and, if there were no SHT_SYNDYM section table
 * entry, it would use an uninitialized value for the number of symbols.  As a
 * workaround, we supply a minimal section table.  vdso2c will adjust the
 * in-memory image so that "vdso_fake_sections" becomes the section table.
 *
 * The bug was introduced by:
 * https://code.google.com/p/go/source/detail?r=56ea40aac72b (2012-08-31)
 * and is being addressed in the Go runtime in this issue:
 * https://code.google.com/p/go/issues/detail?id=8197
 */

#ifndef __x86_64__
#error This hack is specific to the 64-bit vDSO
#endif

#include <linux/elf.h>

extern const __visible struct elf64_shdr vdso_fake_sections[];
const __visible struct elf64_shdr vdso_fake_sections[] = {
	{
		.sh_type = SHT_DYNSYM,
		.sh_entsize = sizeof(Elf64_Sym),
	}
};
