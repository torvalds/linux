#ifndef _ASMARM_SCATTERLIST_H
#define _ASMARM_SCATTERLIST_H

#ifdef CONFIG_ARM_HAS_SG_CHAIN
#define ARCH_HAS_SG_CHAIN
#endif

#include <asm/memory.h>
#include <asm/types.h>
#include <asm-generic/scatterlist.h>

#endif /* _ASMARM_SCATTERLIST_H */
