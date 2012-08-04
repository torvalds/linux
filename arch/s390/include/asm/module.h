#ifndef _ASM_S390_MODULE_H
#define _ASM_S390_MODULE_H
/*
 * This file contains the s390 architecture specific module code.
 */

struct mod_arch_syminfo
{
	unsigned long got_offset;
	unsigned long plt_offset;
	int got_initialized;
	int plt_initialized;
};

struct mod_arch_specific
{
	/* Starting offset of got in the module core memory. */
	unsigned long got_offset;
	/* Starting offset of plt in the module core memory. */
	unsigned long plt_offset;
	/* Size of the got. */
	unsigned long got_size;
	/* Size of the plt. */
	unsigned long plt_size;
	/* Number of symbols in syminfo. */
	int nsyms;
	/* Additional symbol information (got and plt offsets). */
	struct mod_arch_syminfo *syminfo;
};

#ifdef CONFIG_64BIT
#define ElfW(x) Elf64_ ## x
#define ELFW(x) ELF64_ ## x
#else
#define ElfW(x) Elf32_ ## x
#define ELFW(x) ELF32_ ## x
#endif

#define Elf_Addr ElfW(Addr)
#define Elf_Rela ElfW(Rela)
#define Elf_Shdr ElfW(Shdr)
#define Elf_Sym ElfW(Sym)
#define Elf_Ehdr ElfW(Ehdr)
#define ELF_R_SYM ELFW(R_SYM)
#define ELF_R_TYPE ELFW(R_TYPE)
#endif /* _ASM_S390_MODULE_H */
