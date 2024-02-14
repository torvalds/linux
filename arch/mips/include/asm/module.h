/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_MODULE_H
#define _ASM_MODULE_H

#include <linux/list.h>
#include <linux/elf.h>
#include <asm/extable.h>

struct mod_arch_specific {
	/* Data Bus Error exception tables */
	struct list_head dbe_list;
	const struct exception_table_entry *dbe_start;
	const struct exception_table_entry *dbe_end;
	struct mips_hi16 *r_mips_hi16_list;
};

typedef uint8_t Elf64_Byte;		/* Type for a 8-bit quantity.  */

typedef struct {
	Elf64_Addr r_offset;			/* Address of relocation.  */
	Elf64_Word r_sym;			/* Symbol index.  */
	Elf64_Byte r_ssym;			/* Special symbol.  */
	Elf64_Byte r_type3;			/* Third relocation.  */
	Elf64_Byte r_type2;			/* Second relocation.  */
	Elf64_Byte r_type;			/* First relocation.  */
} Elf64_Mips_Rel;

typedef struct {
	Elf64_Addr r_offset;			/* Address of relocation.  */
	Elf64_Word r_sym;			/* Symbol index.  */
	Elf64_Byte r_ssym;			/* Special symbol.  */
	Elf64_Byte r_type3;			/* Third relocation.  */
	Elf64_Byte r_type2;			/* Second relocation.  */
	Elf64_Byte r_type;			/* First relocation.  */
	Elf64_Sxword r_addend;			/* Addend.  */
} Elf64_Mips_Rela;

#ifdef CONFIG_32BIT
#define Elf_Shdr	Elf32_Shdr
#define Elf_Sym		Elf32_Sym
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Addr	Elf32_Addr
#define Elf_Rel		Elf32_Rel
#define Elf_Rela	Elf32_Rela
#define ELF_R_TYPE(X)	ELF32_R_TYPE(X)
#define ELF_R_SYM(X)	ELF32_R_SYM(X)

#define Elf_Mips_Rel	Elf32_Rel
#define Elf_Mips_Rela	Elf32_Rela

#define ELF_MIPS_R_SYM(rel) ELF32_R_SYM((rel).r_info)
#define ELF_MIPS_R_TYPE(rel) ELF32_R_TYPE((rel).r_info)

#endif

#ifdef CONFIG_64BIT
#define Elf_Shdr	Elf64_Shdr
#define Elf_Sym		Elf64_Sym
#define Elf_Ehdr	Elf64_Ehdr
#define Elf_Addr	Elf64_Addr
#define Elf_Rel		Elf64_Rel
#define Elf_Rela	Elf64_Rela
#define ELF_R_TYPE(X)	ELF64_R_TYPE(X)
#define ELF_R_SYM(X)	ELF64_R_SYM(X)

#define Elf_Mips_Rel	Elf64_Mips_Rel
#define Elf_Mips_Rela	Elf64_Mips_Rela

#define ELF_MIPS_R_SYM(rel) ((rel).r_sym)
#define ELF_MIPS_R_TYPE(rel) ((rel).r_type)

#endif

#ifdef CONFIG_MODULES
/* Given an address, look for it in the exception tables. */
const struct exception_table_entry*search_module_dbetables(unsigned long addr);
#else
/* Given an address, look for it in the exception tables. */
static inline const struct exception_table_entry *
search_module_dbetables(unsigned long addr)
{
	return NULL;
}
#endif

#endif /* _ASM_MODULE_H */
