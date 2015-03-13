#ifndef __BACKPORT_ASM_GENERIC_BARRIER_H
#define __BACKPORT_ASM_GENERIC_BARRIER_H
#include_next <asm-generic/barrier.h>

#ifndef dma_rmb
#define dma_rmb()	rmb()
#endif

#endif /* __BACKPORT_ASM_GENERIC_BARRIER_H */
