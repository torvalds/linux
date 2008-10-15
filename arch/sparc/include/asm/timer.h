#ifndef ___ASM_SPARC_TIMER_H
#define ___ASM_SPARC_TIMER_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/timer_64.h>
#else
#include <asm/timer_32.h>
#endif
#endif
