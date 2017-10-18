#ifndef _ASM_LKL_ELF_H
#define _ASM_LKL_ELF_H

#define elf_check_arch(x) 0

#ifdef CONFIG_64BIT
#define ELF_CLASS ELFCLASS64
#else
#define ELF_CLASS ELFCLASS32
#endif

#define elf_gregset_t long
#define elf_fpregset_t double
#endif

