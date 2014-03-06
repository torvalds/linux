#ifndef _M68K_BARRIER_H
#define _M68K_BARRIER_H

#define nop()		do { asm volatile ("nop"); barrier(); } while (0)

#include <asm-generic/barrier.h>

#endif /* _M68K_BARRIER_H */
