#ifndef _ASM_POWERPC_PGALLOC_H
#define _ASM_POWERPC_PGALLOC_H

#include <linux/mm.h>

#ifdef CONFIG_PPC_BOOK3S
#include <asm/book3s/pgalloc.h>
#else
#include <asm/nohash/pgalloc.h>
#endif

#endif /* _ASM_POWERPC_PGALLOC_H */
