#ifndef _ASM_POWERPC_BOOK3S_PGTABLE_H
#define _ASM_POWERPC_BOOK3S_PGTABLE_H

#ifdef CONFIG_PPC64
#include <asm/book3s/64/pgtable.h>
#else
#include <asm/book3s/32/pgtable.h>
#endif

#define FIRST_USER_ADDRESS	0UL
#endif
