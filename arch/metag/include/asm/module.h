#ifndef _ASM_METAG_MODULE_H
#define _ASM_METAG_MODULE_H

#include <asm-generic/module.h>

struct metag_plt_entry {
	/* Indirect jump instruction sequence. */
	unsigned long tramp[2];
};

struct mod_arch_specific {
	/* Indices of PLT sections within module. */
	unsigned int core_plt_section, init_plt_section;
};

#if defined CONFIG_METAG_META12
#define MODULE_PROC_FAMILY "META 1.2 "
#elif defined CONFIG_METAG_META21
#define MODULE_PROC_FAMILY "META 2.1 "
#else
#define MODULE_PROC_FAMILY ""
#endif

#ifdef CONFIG_4KSTACKS
#define MODULE_STACKSIZE "4KSTACKS "
#else
#define MODULE_STACKSIZE ""
#endif

#define MODULE_ARCH_VERMAGIC MODULE_PROC_FAMILY MODULE_STACKSIZE

#ifdef MODULE
asm(".section .plt,\"ax\",@progbits; .balign 8; .previous");
asm(".section .init.plt,\"ax\",@progbits; .balign 8; .previous");
#endif

#endif /* _ASM_METAG_MODULE_H */
