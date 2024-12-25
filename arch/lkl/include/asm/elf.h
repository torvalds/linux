#ifndef _ASM_LKL_ELF_H
#define _ASM_LKL_ELF_H

#define elf_check_arch(x) 0

#ifdef CONFIG_64BIT
#define ELF_CLASS ELFCLASS64
#else
#define ELF_CLASS ELFCLASS32
#endif

#ifdef CONFIG_MMU
#define ELF_EXEC_PAGESIZE 4096
#define ELF_PLATFORM "i586"
#define ELF_HWCAP 0L
#define ELF_ET_DYN_BASE (TASK_SIZE)
#endif // CONFIG_MMU

#define elf_gregset_t long
#define elf_fpregset_t double
#endif

